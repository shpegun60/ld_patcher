#include "workflowservice.h"

#include "catalogloader.h"
#include "sourcepackage.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSet>
#include <algorithm>

namespace {

bool isCancelled(const WorkflowService::CancelToken &cancelToken)
{
    return cancelToken && cancelToken->load();
}

void reportProgress(const WorkflowService::StepProgressCallback &progressCallback,
                    int percent,
                    const QString &message)
{
    if (progressCallback) {
        progressCallback(percent, message);
    }
}

void emitLog(const WorkflowService::LogCallback &logCallback, const QString &line)
{
    if (logCallback && !line.trimmed().isEmpty()) {
        logCallback(line);
    }
}

void emitProcessLogLine(const WorkflowService::LogCallback &logCallback, const QString &line)
{
    if (logCallback) {
        logCallback(line);
    }
}

bool parseProgressMarker(const QString &line, int *percent, QString *message)
{
    static const QRegularExpression markerRegex(
        QStringLiteral("^\\s*LDPATCHER_PROGRESS\\s+(\\d{1,3})(?:\\s+(.*))?\\s*$"));
    const QRegularExpressionMatch match = markerRegex.match(line);
    if (!match.hasMatch()) {
        return false;
    }

    if (percent) {
        *percent = qBound(0, match.captured(1).toInt(), 100);
    }
    if (message) {
        *message = match.captured(2).trimmed();
    }
    return true;
}

QString workspaceRootFromCatalog(const CatalogData &catalog)
{
    const QFileInfo rootInfo(catalog.rootDir);
    if (rootInfo.fileName().compare(QStringLiteral("ld_patcher"), Qt::CaseInsensitive) == 0) {
        return rootInfo.dir().absolutePath();
    }
    return rootInfo.absoluteFilePath();
}

QString toMsysPath(const QString &path)
{
    if (path.isEmpty()) {
        return path;
    }

    const QString normalized = QDir::fromNativeSeparators(QDir::cleanPath(path));
    const QRegularExpression drivePattern(QStringLiteral("^([A-Za-z]):(/.*)?$"));
    const QRegularExpressionMatch match = drivePattern.match(normalized);
    if (!match.hasMatch()) {
        return normalized;
    }

    const QString drive = match.captured(1).toLower();
    const QString tail = match.captured(2);
    return QStringLiteral("/%1%2").arg(drive, tail);
}

QString shellQuoteBash(const QString &value)
{
    if (value == QStringLiteral("&&") || value == QStringLiteral("||")
        || value == QStringLiteral(";") || value == QStringLiteral("|")) {
        return value;
    }

    QString escaped = value;
    escaped.replace(QStringLiteral("'"), QStringLiteral("'\"'\"'"));
    return QStringLiteral("'%1'").arg(escaped);
}

QString wrapMsys2BashCommand(const QString &command)
{
    return QStringLiteral("export PATH=/mingw64/bin:/usr/bin:$PATH; %1").arg(command);
}

QString wrapMsys2BashCommandInDirectory(const QString &workingDirectory, const QString &command)
{
    const QString msysWorkingDirectory = toMsysPath(workingDirectory);
    const QString cdPrefix = msysWorkingDirectory.trimmed().isEmpty()
                                 ? QString()
                                 : QStringLiteral("cd %1 && ").arg(shellQuoteBash(msysWorkingDirectory));
    return wrapMsys2BashCommand(QStringLiteral("%1%2").arg(cdPrefix, command));
}

QStringList splitOutputLines(const QByteArray &bytes)
{
    QString text = QString::fromLocal8Bit(bytes);
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

struct ProcessStreamLogState
{
    QString pendingLine;
    bool previousChunkEndedWithCarriageReturn = false;
};

void appendProcessChunkToLogs(const QByteArray &chunk,
                              QString *aggregate,
                              ProcessStreamLogState *state,
                              const WorkflowService::LogCallback &logCallback)
{
    if (chunk.isEmpty()) {
        return;
    }

    QString text = QString::fromLocal8Bit(chunk);
    if (aggregate) {
        aggregate->append(text);
    }
    if (!state) {
        return;
    }

    if (state->previousChunkEndedWithCarriageReturn && !text.isEmpty() && text.front() == QLatin1Char('\n')) {
        text.remove(0, 1);
    }
    state->previousChunkEndedWithCarriageReturn = false;

    for (QChar character : text) {
        if (character == QLatin1Char('\r') || character == QLatin1Char('\n')) {
            emitProcessLogLine(logCallback, state->pendingLine);
            state->pendingLine.clear();
            state->previousChunkEndedWithCarriageReturn = (character == QLatin1Char('\r'));
        } else {
            state->pendingLine.append(character);
            state->previousChunkEndedWithCarriageReturn = false;
        }
    }
}

void flushProcessLogRemainder(ProcessStreamLogState *state,
                              const WorkflowService::LogCallback &logCallback)
{
    if (!state) {
        return;
    }

    if (!state->pendingLine.isEmpty()) {
        emitProcessLogLine(logCallback, state->pendingLine);
        state->pendingLine.clear();
    }
    state->previousChunkEndedWithCarriageReturn = false;
}

QString absoluteCleanPath(const QString &path)
{
    if (path.trimmed().isEmpty()) {
        return {};
    }
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString comparablePath(const QString &path)
{
    QString normalized = QDir::fromNativeSeparators(absoluteCleanPath(path));
#ifdef Q_OS_WIN
    normalized = normalized.toLower();
#endif
    return normalized;
}

bool pathEquals(const QString &left, const QString &right)
{
    return comparablePath(left) == comparablePath(right);
}

bool isPathWithin(const QString &childPath, const QString &parentPath)
{
    const QString child = comparablePath(childPath);
    const QString parent = comparablePath(parentPath);
    if (child.isEmpty() || parent.isEmpty() || child == parent) {
        return false;
    }

    return child.startsWith(parent + QLatin1Char('/'));
}

bool pathsOverlap(const QString &left, const QString &right)
{
    return pathEquals(left, right)
           || isPathWithin(left, right)
           || isPathWithin(right, left);
}

bool isUnsafeRootPath(const QString &path)
{
    const QString normalized = comparablePath(path);
    if (normalized.isEmpty()) {
        return true;
    }
    if (normalized == QStringLiteral("/")) {
        return true;
    }
    static const QRegularExpression driveRootRegex(QStringLiteral("^[a-z]:/$"));
    return driveRootRegex.match(normalized).hasMatch();
}

bool resolvePathUnderRoot(const QString &rootPath,
                          const QString &relativePath,
                          const QString &label,
                          QString *resolvedPath,
                          QString *errorMessage)
{
    const QString cleanRoot = absoluteCleanPath(rootPath);
    if (cleanRoot.isEmpty() || !QFileInfo(cleanRoot).isDir()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("%1 root is not available: %2").arg(label, rootPath);
        }
        return false;
    }

    const QString candidate = absoluteCleanPath(QDir(cleanRoot).filePath(relativePath));
    if (!pathEquals(candidate, cleanRoot) && !isPathWithin(candidate, cleanRoot)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("%1 escapes its root: %2").arg(label, relativePath);
        }
        return false;
    }

    if (resolvedPath) {
        *resolvedPath = candidate;
    }
    return true;
}

bool resolveChildDirectoryPath(const QString &parentDir,
                               const QString &directoryName,
                               QString *resolvedPath,
                               QString *errorMessage)
{
    const QString cleanParent = absoluteCleanPath(parentDir);
    const QString cleanName = directoryName.trimmed();
    if (cleanParent.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Destination parent directory is empty.");
        }
        return false;
    }
    if (cleanName.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Destination directory name is empty.");
        }
        return false;
    }
    if (cleanName.contains(QLatin1Char('/')) || cleanName.contains(QLatin1Char('\\'))
        || cleanName == QStringLiteral(".") || cleanName == QStringLiteral("..")) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "Destination directory name must be a single folder name without path separators: %1")
                .arg(directoryName);
        }
        return false;
    }

    const QString candidate = absoluteCleanPath(QDir(cleanParent).filePath(cleanName));
    if (!isPathWithin(candidate, cleanParent)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Destination directory escapes its parent: %1")
                                .arg(candidate);
        }
        return false;
    }

    if (resolvedPath) {
        *resolvedPath = candidate;
    }
    return true;
}

bool validateRecursiveDeleteTarget(const QString &targetPath,
                                   const QString &explicitParentPath,
                                   const QString &label,
                                   QString *errorMessage)
{
    const QString cleanTarget = absoluteCleanPath(targetPath);
    const QString cleanParent = absoluteCleanPath(explicitParentPath);
    if (cleanTarget.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("%1 delete target is empty.").arg(label);
        }
        return false;
    }
    if (cleanParent.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("%1 delete parent is empty.").arg(label);
        }
        return false;
    }
    if (isUnsafeRootPath(cleanTarget) || isUnsafeRootPath(cleanParent)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("%1 delete target is too broad: %2")
                                .arg(label, cleanTarget);
        }
        return false;
    }
    if (!isPathWithin(cleanTarget, cleanParent)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("%1 delete target is not contained by its explicit parent: %2")
                                .arg(label, cleanTarget);
        }
        return false;
    }
    return true;
}

bool validateManagedBuildLayout(const QString &workingRootPath,
                                const QHash<QString, QString> &windowsVariables,
                                QString *errorMessage)
{
    const QString sourceRoot = absoluteCleanPath(workingRootPath);
    const QString buildRoot = absoluteCleanPath(windowsVariables.value(QStringLiteral("build_root")));
    const QStringList managedPaths = {
        windowsVariables.value(QStringLiteral("build_dir")),
        windowsVariables.value(QStringLiteral("install_dir")),
        windowsVariables.value(QStringLiteral("drop_dir")),
        windowsVariables.value(QStringLiteral("package_dir"))
    };

    if (sourceRoot.isEmpty() || buildRoot.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Build layout could not be resolved to absolute paths.");
        }
        return false;
    }
    if (!QFileInfo(sourceRoot).isDir()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Working source root does not exist: %1").arg(sourceRoot);
        }
        return false;
    }
    if (isUnsafeRootPath(buildRoot)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Build root is too broad to manage safely: %1").arg(buildRoot);
        }
        return false;
    }
    if (pathEquals(sourceRoot, buildRoot)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Build root must not be the same directory as the source root: %1")
                                .arg(buildRoot);
        }
        return false;
    }

    QStringList seenPaths;
    for (const QString &path : managedPaths) {
        const QString cleanPath = absoluteCleanPath(path);
        if (cleanPath.isEmpty()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("A managed build path is empty.");
            }
            return false;
        }
        if (isUnsafeRootPath(cleanPath)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Managed build path is too broad: %1").arg(cleanPath);
            }
            return false;
        }
        if (!isPathWithin(cleanPath, buildRoot)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Managed build path escapes the build root: %1").arg(cleanPath);
            }
            return false;
        }
        if (seenPaths.contains(comparablePath(cleanPath))) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Managed build paths must be distinct: %1").arg(cleanPath);
            }
            return false;
        }
        seenPaths.append(comparablePath(cleanPath));
    }

    return true;
}

QStringList candidatePatchPackageRoots(const CatalogData &catalog, const PatchRecipeData &recipe)
{
    const QString recipeFullPath = QDir(catalog.rootDir)
                                       .filePath(QStringLiteral("catalog/%1").arg(recipe.filePath));
    const QString recipeDir = QFileInfo(recipeFullPath).absolutePath();

    QStringList hints = recipe.patchPackageRootHints;
    if (hints.isEmpty() && !recipe.patchPackageRootHint.isEmpty()) {
        hints.append(recipe.patchPackageRootHint);
    }

    QStringList candidates;
    auto appendCandidate = [&candidates](const QString &path) {
        const QString cleaned = QDir::cleanPath(path);
        if (!cleaned.isEmpty() && !candidates.contains(cleaned)) {
            candidates.append(cleaned);
        }
    };

    for (const QString &hint : hints) {
        if (hint.isEmpty()) {
            continue;
        }
        const QFileInfo hintInfo(hint);
        if (hintInfo.isAbsolute()) {
            appendCandidate(hint);
            continue;
        }

        appendCandidate(QDir(catalog.rootDir).filePath(hint));
        appendCandidate(QDir(recipeDir).filePath(hint));
    }

    return candidates;
}

QString resolvePatchPackageRoot(const CatalogData &catalog, const PatchRecipeData &recipe)
{
    const QStringList candidates = candidatePatchPackageRoots(catalog, recipe);
    for (const QString &candidate : candidates) {
        if (QFileInfo(candidate).isDir()) {
            return candidate;
        }
    }
    return candidates.isEmpty() ? QString() : candidates.constFirst();
}

const VersionProfile *findProfileById(const CatalogData &catalog, const QString &profileId)
{
    for (const VersionProfile &profile : catalog.profiles) {
        if (profile.id == profileId) {
            return &profile;
        }
    }
    return nullptr;
}

QString readFileText(const QString &path, QString *errorMessage)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to open %1: %2").arg(path, file.errorString());
        }
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

bool writeFileText(const QString &path, const QString &text, QString *errorMessage)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to write %1: %2").arg(path, file.errorString());
        }
        return false;
    }

    const QByteArray bytes = text.toUtf8();
    if (file.write(bytes) != bytes.size()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to write full content into %1").arg(path);
        }
        return false;
    }
    if (!file.commit()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to commit %1: %2").arg(path, file.errorString());
        }
        return false;
    }
    return true;
}

QStringList splitFragmentBlocks(const QString &fragmentText)
{
    QString normalized = fragmentText;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalized.replace(QStringLiteral("\r"), QStringLiteral("\n"));

    QStringList blocks;
    const QStringList parts = normalized.split(QRegularExpression(QStringLiteral("\n\\s*\n\\s*\n*")), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty()) {
            blocks.append(trimmed);
        }
    }
    if (blocks.isEmpty() && !normalized.trimmed().isEmpty()) {
        blocks.append(normalized.trimmed());
    }
    return blocks;
}

QString selectFragmentBlock(const PatchOperation &operation,
                            const QString &fragmentText,
                            QString *errorMessage)
{
    const QString insertMode = operation.insertMode.trimmed();
    if (insertMode.isEmpty() || insertMode == QStringLiteral("after")) {
        return fragmentText.trimmed();
    }

    const QStringList blocks = splitFragmentBlocks(fragmentText);
    if (insertMode == QStringLiteral("after_first_fragment_block")) {
        if (blocks.size() < 1) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Fragment '%1' does not have a first block.").arg(operation.sourcePath);
            }
            return {};
        }
        return blocks.at(0);
    }
    if (insertMode == QStringLiteral("after_second_fragment_block")) {
        if (blocks.size() < 2) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Fragment '%1' does not have a second block.").arg(operation.sourcePath);
            }
            return {};
        }
        return blocks.at(1);
    }

    if (errorMessage) {
        *errorMessage = QStringLiteral("Unsupported insert mode: %1").arg(insertMode);
    }
    return {};
}

QString backupPathForTarget(const QString &targetPath, const QString &backupSuffix)
{
    const QString suffix = backupSuffix.isEmpty() ? QStringLiteral(".ldpatcher.bak") : backupSuffix;
    return QStringLiteral("%1%2").arg(targetPath, suffix);
}

struct ApplyRollbackState
{
    QHash<QString, bool> targetExistedInitially;
    QHash<QString, QString> backupByTarget;
    QHash<QString, QString> targetPathByKey;
};

bool copyFileReplacing(const QString &sourcePath, const QString &targetPath, QString *errorMessage);

bool ensureBackup(const QString &targetPath,
                  const QString &backupSuffix,
                  QStringList *messages,
                  QString *errorMessage)
{
    const QString backupPath = backupPathForTarget(targetPath, backupSuffix);
    const bool backupExisted = QFileInfo::exists(backupPath);
    if (backupExisted && !QFile::remove(backupPath)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to refresh backup %1").arg(backupPath);
        }
        return false;
    }

    if (!QFile::copy(targetPath, backupPath)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to create backup %1").arg(backupPath);
        }
        return false;
    }

    if (messages) {
        messages->append(QStringLiteral("%1: %2")
                             .arg(backupExisted ? QStringLiteral("Backup refreshed")
                                                : QStringLiteral("Backup created"),
                                  backupPath));
    }
    return true;
}

bool prepareTargetForModification(const QString &targetPath,
                                  const PatchRecipeData &recipe,
                                  ApplyRollbackState *rollbackState,
                                  QStringList *messages,
                                  QString *errorMessage)
{
    if (!rollbackState) {
        return true;
    }

    const QString comparableTarget = comparablePath(targetPath);
    if (rollbackState->targetExistedInitially.contains(comparableTarget)) {
        return true;
    }

    const bool existedInitially = QFileInfo::exists(targetPath);
    rollbackState->targetExistedInitially.insert(comparableTarget, existedInitially);
    rollbackState->targetPathByKey.insert(comparableTarget, targetPath);
    if (!existedInitially) {
        return true;
    }

    if (!ensureBackup(targetPath, recipe.backupSuffix, messages, errorMessage)) {
        return false;
    }
    rollbackState->backupByTarget.insert(comparableTarget,
                                         backupPathForTarget(targetPath, recipe.backupSuffix));
    return true;
}

bool rollbackPatchedTargets(const ApplyRollbackState &rollbackState,
                            QStringList *messages,
                            const WorkflowService::LogCallback &logCallback,
                            QString *errorMessage)
{
    QStringList failures;
    QStringList targetKeys = rollbackState.targetPathByKey.keys();
    std::sort(targetKeys.begin(), targetKeys.end());
    for (auto it = targetKeys.crbegin(); it != targetKeys.crend(); ++it) {
        const QString targetKey = *it;
        const bool existedInitially = rollbackState.targetExistedInitially.value(targetKey, false);
        const QString backupPath = rollbackState.backupByTarget.value(targetKey);
        const QString targetPath = rollbackState.targetPathByKey.value(targetKey, targetKey);

        QString rollbackMessage;
        if (existedInitially) {
            if (backupPath.isEmpty() || !QFileInfo::exists(backupPath)) {
                failures.append(QStringLiteral("Missing rollback backup for %1").arg(targetPath));
                continue;
            }
            QDir().mkpath(QFileInfo(targetPath).absolutePath());
            if (QFileInfo::exists(targetPath) && !QFile::remove(targetPath)) {
                failures.append(QStringLiteral("Failed to remove modified file during rollback: %1").arg(targetPath));
                continue;
            }
            if (!QFile::copy(backupPath, targetPath)) {
                failures.append(QStringLiteral("Failed to restore backup for %1").arg(targetPath));
                continue;
            }
            rollbackMessage = QStringLiteral("Rollback restored: %1").arg(targetPath);
        } else {
            if (QFileInfo::exists(targetPath) && !QFile::remove(targetPath)) {
                failures.append(QStringLiteral("Failed to remove newly created file during rollback: %1").arg(targetPath));
                continue;
            }
            rollbackMessage = QStringLiteral("Rollback removed new file: %1").arg(targetPath);
        }

        if (messages) {
            messages->append(rollbackMessage);
        }
        emitLog(logCallback, rollbackMessage);
    }

    if (!failures.isEmpty()) {
        if (messages) {
            messages->append(failures);
        }
        if (errorMessage) {
            *errorMessage = failures.join(QStringLiteral("; "));
        }
        return false;
    }

    return true;
}

bool applyCopyFile(const QString &payloadRoot,
                   const QString &workingRoot,
                   const PatchRecipeData &recipe,
                   const PatchOperation &operation,
                   ApplyRollbackState *rollbackState,
                   QStringList *messages,
                   QString *errorMessage)
{
    QString sourcePath;
    QString targetPath;
    if (!resolvePathUnderRoot(payloadRoot,
                              operation.sourcePath,
                              QStringLiteral("Patch payload file"),
                              &sourcePath,
                              errorMessage)) {
        return false;
    }
    if (!resolvePathUnderRoot(workingRoot,
                              operation.targetPath,
                              QStringLiteral("Patch target file"),
                              &targetPath,
                              errorMessage)) {
        return false;
    }

    if (!QFileInfo(sourcePath).isFile()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Payload file does not exist: %1").arg(sourcePath);
        }
        return false;
    }

    if (!prepareTargetForModification(targetPath, recipe, rollbackState, messages, errorMessage)) {
        return false;
    }

    if (!copyFileReplacing(sourcePath, targetPath, errorMessage)) {
        return false;
    }

    messages->append(QStringLiteral("%1: copied %2 -> %3").arg(operation.description, operation.sourcePath, operation.targetPath));
    return true;
}

bool applyInsertAfterRegex(const QString &payloadRoot,
                           const QString &workingRoot,
                           const PatchRecipeData &recipe,
                           const PatchOperation &operation,
                           ApplyRollbackState *rollbackState,
                           QStringList *messages,
                           QString *errorMessage)
{
    QString sourcePath;
    QString targetPath;
    if (!resolvePathUnderRoot(payloadRoot,
                              operation.sourcePath,
                              QStringLiteral("Patch fragment"),
                              &sourcePath,
                              errorMessage)) {
        return false;
    }
    if (!resolvePathUnderRoot(workingRoot,
                              operation.targetPath,
                              QStringLiteral("Patch target file"),
                              &targetPath,
                              errorMessage)) {
        return false;
    }

    QString fragmentText = readFileText(sourcePath, errorMessage);
    if (fragmentText.isNull()) {
        return false;
    }

    const QString fragmentBlock = selectFragmentBlock(operation, fragmentText, errorMessage);
    if (fragmentBlock.isNull()) {
        return false;
    }

    QString targetText = readFileText(targetPath, errorMessage);
    if (targetText.isNull()) {
        return false;
    }

    if (targetText.contains(fragmentBlock)) {
        messages->append(QStringLiteral("%1: fragment already present in %2").arg(operation.description, operation.targetPath));
        return true;
    }

    const QRegularExpression regex(operation.matchRegex, QRegularExpression::MultilineOption);
    if (!regex.isValid()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Operation '%1' has an invalid regex: %2")
                                .arg(operation.description, regex.errorString());
        }
        return false;
    }
    QRegularExpressionMatchIterator it = regex.globalMatch(targetText);
    QList<QRegularExpressionMatch> matches;
    while (it.hasNext()) {
        matches.append(it.next());
    }

    const int expectedCount = operation.expectedMatchCount;
    if (expectedCount >= 0 && matches.size() != expectedCount) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Operation '%1' expected %2 regex match(es) in %3 but found %4")
                                .arg(operation.description)
                                .arg(expectedCount)
                                .arg(operation.targetPath)
                                .arg(matches.size());
        }
        return false;
    }
    if (matches.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Operation '%1' found no insertion anchor in %2").arg(operation.description, operation.targetPath);
        }
        return false;
    }

    if (!prepareTargetForModification(targetPath, recipe, rollbackState, messages, errorMessage)) {
        return false;
    }

    const QRegularExpressionMatch &match = matches.constFirst();
    const int insertPos = match.capturedEnd();
    QString insertionText = fragmentBlock;
    if (!insertionText.startsWith(QLatin1Char('\n'))) {
        insertionText.prepend(QLatin1Char('\n'));
    }
    if (!insertionText.endsWith(QLatin1Char('\n'))) {
        insertionText.append(QLatin1Char('\n'));
    }
    targetText.insert(insertPos, insertionText);

    if (!writeFileText(targetPath, targetText, errorMessage)) {
        return false;
    }

    messages->append(QStringLiteral("%1: inserted fragment into %2").arg(operation.description, operation.targetPath));
    return true;
}

bool applyAppendIfMissing(const QString &payloadRoot,
                          const QString &workingRoot,
                          const PatchRecipeData &recipe,
                          const PatchOperation &operation,
                          ApplyRollbackState *rollbackState,
                          QStringList *messages,
                          QString *errorMessage)
{
    QString sourcePath;
    QString targetPath;
    if (!resolvePathUnderRoot(payloadRoot,
                              operation.sourcePath,
                              QStringLiteral("Patch fragment"),
                              &sourcePath,
                              errorMessage)) {
        return false;
    }
    if (!resolvePathUnderRoot(workingRoot,
                              operation.targetPath,
                              QStringLiteral("Patch target file"),
                              &targetPath,
                              errorMessage)) {
        return false;
    }

    QString fragmentText = readFileText(sourcePath, errorMessage);
    if (fragmentText.isNull()) {
        return false;
    }
    fragmentText = fragmentText.trimmed();

    QString targetText = readFileText(targetPath, errorMessage);
    if (targetText.isNull()) {
        return false;
    }

    if (targetText.contains(fragmentText)) {
        messages->append(QStringLiteral("%1: fragment already present in %2").arg(operation.description, operation.targetPath));
        return true;
    }

    if (!prepareTargetForModification(targetPath, recipe, rollbackState, messages, errorMessage)) {
        return false;
    }

    if (!targetText.endsWith(QLatin1Char('\n'))) {
        targetText.append(QLatin1Char('\n'));
    }
    targetText.append(QLatin1Char('\n'));
    targetText.append(fragmentText);
    targetText.append(QLatin1Char('\n'));

    if (!writeFileText(targetPath, targetText, errorMessage)) {
        return false;
    }

    messages->append(QStringLiteral("%1: appended fragment to %2").arg(operation.description, operation.targetPath));
    return true;
}

QString resolveRecipeRef(const CatalogData &catalog, const QString &recipeFilePath, const QString &ref)
{
    if (ref.isEmpty()) {
        return {};
    }

    const QFileInfo refInfo(ref);
    if (refInfo.isAbsolute()) {
        return QDir::cleanPath(ref);
    }

    const QString workspaceRoot = workspaceRootFromCatalog(catalog);
    const QString recipeFullPath = QDir(catalog.rootDir).filePath(QStringLiteral("catalog/%1").arg(recipeFilePath));
    const QString recipeDir = QFileInfo(recipeFullPath).absolutePath();

    const QStringList candidates = {
        QDir(catalog.rootDir).filePath(ref),
        QDir(recipeDir).filePath(ref),
        QDir(workspaceRoot).filePath(ref)
    };

    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return QDir::cleanPath(candidate);
        }
    }

    return QDir::cleanPath(candidates.constFirst());
}

QString displayVersionFromProfile(const VersionProfile &profile)
{
    QString text = profile.displayName.trimmed();
    const QString prefix = QStringLiteral("GNU Tools for STM32 ");
    if (text.startsWith(prefix, Qt::CaseInsensitive)) {
        text.remove(0, prefix.size());
    }
    return text;
}

QString packageVersionFromWorkingRoot(const VersionProfile &profile, const QString &workingRootPath)
{
    QString text = QFileInfo(workingRootPath).fileName().trimmed();
    const QStringList prefixes = {
        QStringLiteral("gnu-tools-for-stm32-"),
        QStringLiteral("GNU Tools for STM32 ")
    };
    for (const QString &prefix : prefixes) {
        if (text.startsWith(prefix, Qt::CaseInsensitive)) {
            text.remove(0, prefix.size());
            break;
        }
    }

    text = text.trimmed();
    while (text.startsWith(QLatin1Char('_')) || text.startsWith(QLatin1Char('-'))) {
        text.remove(0, 1);
    }
    while (text.endsWith(QLatin1Char('_')) || text.endsWith(QLatin1Char('-'))) {
        text.chop(1);
    }

    if (text.isEmpty()) {
        text = displayVersionFromProfile(profile);
    }
    return text;
}

QString sanitizePathToken(QString value)
{
    value.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")), QStringLiteral("_"));
    value = value.trimmed();
    while (value.contains(QStringLiteral("__"))) {
        value.replace(QStringLiteral("__"), QStringLiteral("_"));
    }
    if (value.isEmpty()) {
        value = QStringLiteral("run");
    }
    return value;
}

QHash<QString, QString> makeWindowsVariables(const CatalogData &catalog,
                                             const VersionProfile &profile,
                                             const QString &workingRootPath,
                                             const QString &buildRecipeId,
                                             const QString &buildRootOverride)
{
    const QString workspaceRoot = workspaceRootFromCatalog(catalog);
    const QString cleanWorkingRoot = absoluteCleanPath(workingRootPath);
    const QString sourceName = QFileInfo(cleanWorkingRoot).fileName();
    const QString buildStem = sanitizePathToken(QStringLiteral("%1_%2").arg(sourceName, buildRecipeId));
    const QString packageStem =
        sanitizePathToken(QStringLiteral("_cubeide-arm-linker-st-%1-jsonpatch")
                              .arg(packageVersionFromWorkingRoot(profile, workingRootPath)));
    const QString buildRoot = !buildRootOverride.trimmed().isEmpty()
                                  ? absoluteCleanPath(buildRootOverride)
                                  : absoluteCleanPath(QDir(cleanWorkingRoot).filePath(QStringLiteral("build")));
    const QString buildDir = absoluteCleanPath(QDir(buildRoot).filePath(buildStem));
    const QString installDir = QStringLiteral("%1_install").arg(buildDir);
    const QString dropDir = QStringLiteral("%1_drop").arg(buildDir);
    const QString packageDir = absoluteCleanPath(QDir(buildRoot).filePath(packageStem));

    QHash<QString, QString> vars;
    vars.insert(QStringLiteral("workspace_root"), absoluteCleanPath(workspaceRoot));
    vars.insert(QStringLiteral("source_root"), cleanWorkingRoot);
    vars.insert(QStringLiteral("working_root"), cleanWorkingRoot);
    vars.insert(QStringLiteral("build_root"), buildRoot);
    vars.insert(QStringLiteral("build_dir_name"), buildStem);
    vars.insert(QStringLiteral("build_dir"), buildDir);
    vars.insert(QStringLiteral("install_dir"), absoluteCleanPath(installDir));
    vars.insert(QStringLiteral("drop_dir"), absoluteCleanPath(dropDir));
    vars.insert(QStringLiteral("package_dir"), packageDir);
    vars.insert(QStringLiteral("display_version"), displayVersionFromProfile(profile));
    vars.insert(QStringLiteral("jobs"), QString::number(1));
    return vars;
}

QHash<QString, QString> toShellVariables(const QHash<QString, QString> &windowsVariables,
                                         const QString &shellKind)
{
    if (shellKind != QStringLiteral("msys2_mingw64")) {
        return windowsVariables;
    }

    QHash<QString, QString> vars = windowsVariables;
    const QStringList pathKeys = {
        QStringLiteral("workspace_root"), QStringLiteral("source_root"), QStringLiteral("working_root"),
        QStringLiteral("build_root"),
        QStringLiteral("build_dir"), QStringLiteral("install_dir"), QStringLiteral("drop_dir"),
        QStringLiteral("package_dir")
    };
    for (const QString &key : pathKeys) {
        if (vars.contains(key)) {
            vars.insert(key, toMsysPath(vars.value(key)));
        }
    }
    return vars;
}

QString materializeTemplate(QString text, const QHash<QString, QString> &variables)
{
    for (auto it = variables.cbegin(); it != variables.cend(); ++it) {
        text.replace(QStringLiteral("{{%1}}").arg(it.key()), it.value());
    }
    return text;
}

QStringList materializeList(const QStringList &items, const QHash<QString, QString> &variables)
{
    QStringList result;
    for (const QString &item : items) {
        const QString materialized = materializeTemplate(item, variables).trimmed();
        if (!materialized.isEmpty()) {
            result.append(materialized);
        }
    }
    return result;
}

QString findMsys2Shell()
{
    const QString fromPath = QStandardPaths::findExecutable(QStringLiteral("msys2_shell.cmd"));
    if (!fromPath.isEmpty()) {
        return QDir::toNativeSeparators(QDir::cleanPath(fromPath));
    }

    const QStringList candidates = {
        QStringLiteral("C:/msys64/msys2_shell.cmd"),
        QStringLiteral("C:/tools/msys64/msys2_shell.cmd")
    };
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return QDir::toNativeSeparators(QDir::cleanPath(candidate));
        }
    }

    return {};
}

QString findPowerShellExecutable()
{
    const QString fromPath = QStandardPaths::findExecutable(QStringLiteral("powershell.exe"));
    if (!fromPath.isEmpty()) {
        return QDir::toNativeSeparators(QDir::cleanPath(fromPath));
    }

    const QString systemRoot = qEnvironmentVariable("SystemRoot", QStringLiteral("C:/Windows"));
    const QStringList candidates = {
        QDir(systemRoot).filePath(QStringLiteral("System32/WindowsPowerShell/v1.0/powershell.exe")),
        QDir(systemRoot).filePath(QStringLiteral("SysWOW64/WindowsPowerShell/v1.0/powershell.exe"))
    };
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return QDir::toNativeSeparators(QDir::cleanPath(candidate));
        }
    }

    return QStringLiteral("powershell.exe");
}

QString findMsys2MingwBin(const QString &msys2Root = {})
{
    QStringList candidates;
    if (!msys2Root.trimmed().isEmpty()) {
        candidates.append(QDir(msys2Root).filePath(QStringLiteral("mingw64/bin")));
        candidates.append(QDir(msys2Root).filePath(QStringLiteral("ucrt64/bin")));
    }
    candidates.append(QStringLiteral("C:/msys64/mingw64/bin"));
    candidates.append(QStringLiteral("C:/msys64/ucrt64/bin"));

    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(QDir(candidate).filePath(QStringLiteral("objdump.exe")))) {
            return QDir::cleanPath(candidate);
        }
    }
    return {};
}

QString findMsys2Bash(const QString &msys2Root)
{
    if (msys2Root.trimmed().isEmpty()) {
        return {};
    }
    const QString bashPath = QDir(msys2Root).filePath(QStringLiteral("usr/bin/bash.exe"));
    return QFileInfo::exists(bashPath) ? QDir::cleanPath(bashPath) : QString();
}

QList<int> cubeIdeVersionParts(const QString &path)
{
    const QFileInfo info(path);
    const QStringList names = {
        info.fileName(),
        info.dir().dirName()
    };

    static const QRegularExpression versionRegex(
        QStringLiteral("STM32CubeIDE[_-]?(\\d+)\\.(\\d+)\\.(\\d+)"),
        QRegularExpression::CaseInsensitiveOption);
    for (const QString &name : names) {
        const QRegularExpressionMatch match = versionRegex.match(name);
        if (match.hasMatch()) {
            return { match.captured(1).toInt(), match.captured(2).toInt(), match.captured(3).toInt() };
        }
    }

    return {};
}

bool isCubeIdeVersionGreater(const QString &left, const QString &right)
{
    const QList<int> leftParts = cubeIdeVersionParts(left);
    const QList<int> rightParts = cubeIdeVersionParts(right);
    const int count = qMax(leftParts.size(), rightParts.size());
    for (int i = 0; i < count; ++i) {
        const int leftValue = i < leftParts.size() ? leftParts.at(i) : 0;
        const int rightValue = i < rightParts.size() ? rightParts.at(i) : 0;
        if (leftValue != rightValue) {
            return leftValue > rightValue;
        }
    }

    return QFileInfo(left).lastModified() > QFileInfo(right).lastModified();
}

QString normalizedCubeIdeCandidate(const QString &path)
{
    const QFileInfo info(absoluteCleanPath(path));
    if (!info.exists() || !info.isDir()) {
        return {};
    }

    const QDir dir(info.absoluteFilePath());
    if (dir.dirName().compare(QStringLiteral("STM32CubeIDE"), Qt::CaseInsensitive) == 0) {
        return dir.absolutePath();
    }

    const QString nestedCubeIde = dir.filePath(QStringLiteral("STM32CubeIDE"));
    if (QFileInfo(nestedCubeIde).isDir()) {
        return absoluteCleanPath(nestedCubeIde);
    }

    return {};
}

QString detectNewestCubeIdeRoot()
{
    const QStringList searchRoots = {
        QStringLiteral("C:/ST"),
        QStringLiteral("C:/Program Files/STMicroelectronics")
    };

    QString bestPath;
    for (const QString &rootPath : searchRoots) {
        const QDir root(rootPath);
        if (!root.exists()) {
            continue;
        }

        const QFileInfoList entries = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                         QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo &entry : entries) {
            if (!entry.fileName().startsWith(QStringLiteral("STM32CubeIDE"), Qt::CaseInsensitive)) {
                continue;
            }

            const QString candidate = normalizedCubeIdeCandidate(entry.absoluteFilePath());
            if (candidate.isEmpty()) {
                continue;
            }

            if (bestPath.isEmpty() || isCubeIdeVersionGreater(candidate, bestPath)) {
                bestPath = candidate;
            }
        }
    }

    return bestPath;
}

bool toolExists(const QString &path)
{
    return !path.trimmed().isEmpty() && QFileInfo(path).exists();
}

bool hasArmGccCompilerAvailable()
{
    const QString envCompiler = qEnvironmentVariable("STM32_GCC");
    return toolExists(envCompiler) || !QStandardPaths::findExecutable(QStringLiteral("arm-none-eabi-g++.exe")).isEmpty();
}

bool validateVerifyRequiredInputs(const VerifyRecipeData &recipe,
                                  QHash<QString, QString> *variables,
                                  QProcessEnvironment *environment,
                                  QStringList *messages,
                                  const WorkflowService::LogCallback &logCallback,
                                  QString *errorMessage)
{
    if (!variables || !environment) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Verify input validation is missing output storage.");
        }
        return false;
    }

    for (const QString &input : recipe.requiredInputs) {
        if (input == QStringLiteral("workspace_root")) {
            const QString workspaceRoot = absoluteCleanPath(variables->value(QStringLiteral("workspace_root")));
            if (workspaceRoot.isEmpty() || !QFileInfo(workspaceRoot).isDir()) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("Verify recipe requires a valid workspace_root.");
                }
                return false;
            }
            variables->insert(QStringLiteral("workspace_root"), workspaceRoot);
            environment->insert(QStringLiteral("LDPATCHER_WORKSPACE_ROOT"), workspaceRoot);
            continue;
        }

        if (input == QStringLiteral("drop_dir")) {
            const QString dropDir = absoluteCleanPath(variables->value(QStringLiteral("drop_dir")));
            if (dropDir.isEmpty() || !QFileInfo(dropDir).isDir()) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("Verify recipe requires an existing drop_dir.");
                }
                return false;
            }
            variables->insert(QStringLiteral("drop_dir"), dropDir);
            environment->insert(QStringLiteral("LDPATCHER_DROP_DIR"), dropDir);
            continue;
        }

        if (input == QStringLiteral("cubeide_path")) {
            const QString provided = variables->value(QStringLiteral("cubeide_path"));
            const QString resolved = !provided.trimmed().isEmpty()
                                         ? normalizedCubeIdeCandidate(provided)
                                         : detectNewestCubeIdeRoot();
            if (!resolved.isEmpty()) {
                variables->insert(QStringLiteral("cubeide_path"), resolved);
                environment->insert(QStringLiteral("LDPATCHER_CUBEIDE_PATH"), resolved);
                const QString note = QStringLiteral("Verify input resolved: cubeide_path=%1").arg(resolved);
                if (messages) {
                    messages->append(note);
                }
                emitLog(logCallback, note);
                continue;
            }

            if (hasArmGccCompilerAvailable()) {
                const QString note = QStringLiteral(
                    "Verify input 'cubeide_path' was not resolved, but arm-none-eabi-g++.exe is available via STM32_GCC or PATH.");
                if (messages) {
                    messages->append(note);
                }
                emitLog(logCallback, note);
                continue;
            }

            if (errorMessage) {
                *errorMessage = QStringLiteral(
                    "Verify recipe requires cubeide_path or another visible arm-none-eabi-g++.exe compiler source.");
            }
            return false;
        }

        if (errorMessage) {
            *errorMessage = QStringLiteral("Unsupported verify required input: %1").arg(input);
        }
        return false;
    }

    return true;
}

bool validateBuildRequiredTools(const BuildRecipeData &recipe,
                                const QString &msys2Root,
                                const QString &msys2ShellPath,
                                const QString &mingwBinPath,
                                QStringList *messages,
                                const WorkflowService::LogCallback &logCallback,
                                QString *errorMessage)
{
    const QStringList requiredTools = recipe.requiredTools;
    for (const QString &tool : requiredTools) {
        const QString lower = tool.trimmed().toLower();
        bool knownTool = true;
        bool present = false;

        if (lower.contains(QStringLiteral("msys2 shell"))) {
            present = toolExists(msys2ShellPath);
        } else if (lower.contains(QStringLiteral("gcc"))) {
            present = QFileInfo(QDir(mingwBinPath).filePath(QStringLiteral("x86_64-w64-mingw32-gcc.exe"))).isFile();
        } else if (lower.contains(QStringLiteral("make"))) {
            present = QFileInfo(QDir(msys2Root).filePath(QStringLiteral("usr/bin/make.exe"))).isFile()
                      || QFileInfo(QDir(mingwBinPath).filePath(QStringLiteral("make.exe"))).isFile()
                      || QFileInfo(QDir(mingwBinPath).filePath(QStringLiteral("mingw32-make.exe"))).isFile();
        } else if (lower.contains(QStringLiteral("binutils"))) {
            present = QFileInfo(QDir(mingwBinPath).filePath(QStringLiteral("objdump.exe"))).isFile();
        } else if (lower.contains(QStringLiteral("zstd"))) {
            present = QFileInfo(QDir(mingwBinPath).filePath(QStringLiteral("libzstd.dll"))).isFile();
        } else {
            knownTool = false;
            present = true;
        }

        const QString note = knownTool
                                 ? QStringLiteral("Required tool %1: %2")
                                       .arg(tool, present ? QStringLiteral("ready")
                                                          : QStringLiteral("missing"))
                                 : QStringLiteral("Required tool %1 is listed but not machine-checkable.")
                                       .arg(tool);
        if (messages) {
            messages->append(note);
        }
        emitLog(logCallback, note);

        if (knownTool && !present) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Required build tool is missing: %1").arg(tool);
            }
            return false;
        }
    }

    return true;
}

void appendMatchedBuildLogRules(const BuildRecipeData &recipe,
                                const QString &combinedOutput,
                                QSet<QString> *matchedRuleKeys,
                                QStringList *messages,
                                const WorkflowService::LogCallback &logCallback)
{
    for (const BuildLogParsingRule &rule : recipe.logParsingRules) {
        const QString ruleKey = QStringLiteral("%1|%2|%3")
                                    .arg(rule.type, rule.regex, rule.description);
        if (matchedRuleKeys && matchedRuleKeys->contains(ruleKey)) {
            continue;
        }

        const QRegularExpression regex(rule.regex);
        if (!regex.isValid()) {
            const QString message = QStringLiteral("Ignoring invalid build log rule '%1': %2")
                                        .arg(rule.description.isEmpty() ? rule.regex
                                                                        : rule.description,
                                             regex.errorString());
            if (messages) {
                messages->append(message);
            }
            emitLog(logCallback, message);
            if (matchedRuleKeys) {
                matchedRuleKeys->insert(ruleKey);
            }
            continue;
        }
        if (!regex.match(combinedOutput).hasMatch()) {
            continue;
        }

        if (matchedRuleKeys) {
            matchedRuleKeys->insert(ruleKey);
        }

        const QString message = rule.type == QStringLiteral("warning_only")
                                    ? QStringLiteral("Known build warning matched: %1").arg(
                                          rule.description.isEmpty() ? rule.regex : rule.description)
                                    : QStringLiteral("Build log rule matched: %1").arg(
                                          rule.description.isEmpty() ? rule.regex : rule.description);
        if (messages) {
            messages->append(message);
        }
        emitLog(logCallback, message);
    }
}

bool validateManagedCleanCommand(const QStringList &cleanCommand,
                                 const QHash<QString, QString> &windowsVariables,
                                 QString *errorMessage)
{
    if (cleanCommand.isEmpty()) {
        return true;
    }

    const QString programName = QFileInfo(cleanCommand.constFirst()).fileName().toLower();
    if (programName != QStringLiteral("rm")) {
        return true;
    }

    const QSet<QString> allowedTargets = {
        comparablePath(windowsVariables.value(QStringLiteral("build_dir"))),
        comparablePath(windowsVariables.value(QStringLiteral("install_dir"))),
        comparablePath(windowsVariables.value(QStringLiteral("drop_dir"))),
        comparablePath(windowsVariables.value(QStringLiteral("package_dir")))
    };
    for (int i = 1; i < cleanCommand.size(); ++i) {
        const QString token = cleanCommand.at(i).trimmed();
        if (token.isEmpty() || token.startsWith(QLatin1Char('-'))
            || token == QStringLiteral("&&") || token == QStringLiteral("||")
            || token == QStringLiteral(";") || token == QStringLiteral("|")) {
            continue;
        }

        const QString targetPath = comparablePath(token);
        if (!allowedTargets.contains(targetPath)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral(
                    "Clean command may only delete managed build directories. Unexpected target: %1")
                    .arg(token);
            }
            return false;
        }
    }

    return true;
}

bool copyFileReplacing(const QString &sourcePath, const QString &targetPath, QString *errorMessage)
{
    QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.isFile()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Source file does not exist: %1").arg(sourcePath);
        }
        return false;
    }

    QDir().mkpath(QFileInfo(targetPath).absolutePath());
    if (QFileInfo::exists(targetPath) && !QFile::remove(targetPath)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to replace existing file: %1").arg(targetPath);
        }
        return false;
    }
    if (!QFile::copy(sourcePath, targetPath)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to copy %1 -> %2").arg(sourcePath, targetPath);
        }
        return false;
    }
    return true;
}

bool isSystemDllName(const QString &dllName)
{
    const QString lower = dllName.trimmed().toLower();
    if (lower.isEmpty()) {
        return true;
    }
    if (lower.startsWith(QStringLiteral("api-ms-win-"))) {
        return true;
    }

    static const QStringList systemDlls = {
        QStringLiteral("kernel32.dll"),
        QStringLiteral("msvcrt.dll"),
        QStringLiteral("ntdll.dll"),
        QStringLiteral("user32.dll"),
        QStringLiteral("advapi32.dll"),
        QStringLiteral("shell32.dll"),
        QStringLiteral("gdi32.dll"),
        QStringLiteral("ws2_32.dll")
    };
    return systemDlls.contains(lower);
}

QStringList detectDllDependencies(const QString &exePath, const WorkflowService::LogCallback &logCallback)
{
    const QString mingwBin = findMsys2MingwBin();
    if (mingwBin.isEmpty()) {
        return {};
    }

    const QString objdumpPath = QDir(mingwBin).filePath(QStringLiteral("objdump.exe"));
    if (!QFileInfo(objdumpPath).isFile() || !QFileInfo(exePath).isFile()) {
        return {};
    }

    QProcess process;
    process.setProgram(objdumpPath);
    process.setArguments({ QStringLiteral("-p"), exePath });
    process.start();
    if (!process.waitForStarted(5000)) {
        emitLog(logCallback, QStringLiteral("Packaging note: failed to start objdump for dependency scan."));
        return {};
    }
    if (!process.waitForFinished(15000)) {
        process.kill();
        process.waitForFinished(3000);
        emitLog(logCallback, QStringLiteral("Packaging note: objdump dependency scan timed out."));
        return {};
    }

    QStringList dlls;
    const QStringList lines = splitOutputLines(process.readAllStandardOutput());
    static const QRegularExpression dllRegex(QStringLiteral("^\\s*DLL Name:\\s*(.+?)\\s*$"));
    for (const QString &line : lines) {
        const QRegularExpressionMatch match = dllRegex.match(line);
        if (!match.hasMatch()) {
            continue;
        }
        const QString dllName = match.captured(1).trimmed();
        if (!dllName.isEmpty() && !isSystemDllName(dllName) && !dlls.contains(dllName, Qt::CaseInsensitive)) {
            dlls.append(dllName);
        }
    }
    return dlls;
}

QStringList runtimeSearchDirs()
{
    QStringList dirs;
    const QString mingwBin = findMsys2MingwBin();
    if (!mingwBin.isEmpty()) {
        dirs.append(mingwBin);
    }

    const QStringList pathDirs = QProcessEnvironment::systemEnvironment()
                                     .value(QStringLiteral("PATH"))
                                     .split(QDir::listSeparator(), Qt::SkipEmptyParts);
    for (const QString &dir : pathDirs) {
        const QString cleaned = QDir::cleanPath(dir);
        if (!cleaned.isEmpty() && !dirs.contains(cleaned, Qt::CaseInsensitive)) {
            dirs.append(cleaned);
        }
    }
    return dirs;
}

QString findRuntimeDll(const QString &dllName)
{
    const QStringList searchDirs = runtimeSearchDirs();
    for (const QString &dir : searchDirs) {
        const QString candidate = QDir(dir).filePath(dllName);
        if (QFileInfo(candidate).isFile()) {
            return QDir::cleanPath(candidate);
        }
    }
    return {};
}

bool materializeCubeIdeDrop(const QString &installDir,
                            const QString &dropDir,
                            QStringList *messages,
                            const WorkflowService::LogCallback &logCallback,
                            QString *errorMessage)
{
    if (installDir.trimmed().isEmpty() || dropDir.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Packaging requires both install_dir and drop_dir.");
        }
        return false;
    }

    const QString installBinDir = QDir(installDir).filePath(QStringLiteral("bin"));
    if (!QFileInfo(installBinDir).isDir()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Install bin directory does not exist: %1").arg(installBinDir);
        }
        return false;
    }

    struct PackageFileMapping
    {
        QString targetName;
        QStringList candidateSourceNames;
    };

    const QList<PackageFileMapping> mappings = {
        { QStringLiteral("ld.exe"), { QStringLiteral("ld.exe"), QStringLiteral("arm-none-eabi-ld.exe") } },
        { QStringLiteral("ld.bfd.exe"), { QStringLiteral("ld.bfd.exe"), QStringLiteral("arm-none-eabi-ld.bfd.exe") } },
        { QStringLiteral("arm-none-eabi-ld.exe"), { QStringLiteral("arm-none-eabi-ld.exe"), QStringLiteral("ld.exe") } },
        { QStringLiteral("arm-none-eabi-ld.bfd.exe"), { QStringLiteral("arm-none-eabi-ld.bfd.exe"), QStringLiteral("ld.bfd.exe") } }
    };

    QDir().mkpath(dropDir);

    QString primaryExecutable;
    for (const PackageFileMapping &mapping : mappings) {
        QString selectedSourcePath;
        for (const QString &candidateName : mapping.candidateSourceNames) {
            const QString candidatePath = QDir(installBinDir).filePath(candidateName);
            if (QFileInfo(candidatePath).isFile()) {
                selectedSourcePath = candidatePath;
                break;
            }
        }
        if (selectedSourcePath.isEmpty()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Could not find a source executable for %1 inside %2")
                                    .arg(mapping.targetName, installBinDir);
            }
            return false;
        }

        const QString targetPath = QDir(dropDir).filePath(mapping.targetName);
        if (!copyFileReplacing(selectedSourcePath, targetPath, errorMessage)) {
            return false;
        }
        if (messages) {
            messages->append(QStringLiteral("Packaged: %1").arg(targetPath));
        }
        emitLog(logCallback, QStringLiteral("Packaged linker executable: %1").arg(targetPath));
        if (mapping.targetName == QStringLiteral("ld.exe")) {
            primaryExecutable = targetPath;
        }
    }

    QStringList runtimeDlls = detectDllDependencies(primaryExecutable, logCallback);
    if (runtimeDlls.isEmpty()) {
        runtimeDlls = {
            QStringLiteral("libwinpthread-1.dll"),
            QStringLiteral("libzstd.dll")
        };
        emitLog(logCallback, QStringLiteral("Packaging note: using fallback runtime DLL list."));
    }

    for (const QString &dllName : runtimeDlls) {
        const QString sourceDllPath = findRuntimeDll(dllName);
        if (sourceDllPath.isEmpty()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Required runtime DLL was not found: %1").arg(dllName);
            }
            return false;
        }

        const QString targetDllPath = QDir(dropDir).filePath(dllName);
        if (!copyFileReplacing(sourceDllPath, targetDllPath, errorMessage)) {
            return false;
        }
        if (messages) {
            messages->append(QStringLiteral("Packaged runtime DLL: %1").arg(targetDllPath));
        }
        emitLog(logCallback, QStringLiteral("Packaged runtime DLL: %1").arg(targetDllPath));
    }

    return true;
}

struct ProcessRunResult
{
    bool ok = false;
    bool cancelled = false;
    int exitCode = -1;
    QString errorMessage;
    QString stdoutText;
    QString stderrText;
};

ProcessRunResult runProcess(const QString &program,
                            const QStringList &arguments,
                            const QString &workingDirectory,
                            const QProcessEnvironment &environment,
                            int timeoutSeconds,
                            const WorkflowService::LogCallback &logCallback,
                            const WorkflowService::CancelToken &cancelToken)
{
    ProcessRunResult result;
    QProcess process;
    ProcessStreamLogState stdoutLogState;
    ProcessStreamLogState stderrLogState;
    process.setProcessEnvironment(environment);
    if (!workingDirectory.isEmpty()) {
        process.setWorkingDirectory(workingDirectory);
    }
    process.setProgram(program);
    process.setArguments(arguments);
    process.start();
    if (!process.waitForStarted(15000)) {
        result.errorMessage = QStringLiteral("Failed to start process %1: %2")
                                  .arg(program, process.errorString());
        return result;
    }

    auto killProcessTree = [&process]() {
#ifdef Q_OS_WIN
        const qint64 pid = process.processId();
        if (pid > 0) {
            QProcess taskkill;
            taskkill.start(QStringLiteral("taskkill"),
                           { QStringLiteral("/PID"),
                             QString::number(pid),
                             QStringLiteral("/T"),
                             QStringLiteral("/F") });
            taskkill.waitForFinished(10000);
        }
#endif
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(5000);
        }
    };

    const int timeoutMs = timeoutSeconds > 0 ? timeoutSeconds * 1000 : 0;
    QElapsedTimer elapsedTimer;
    if (timeoutMs > 0) {
        elapsedTimer.start();
    }
    while (process.state() != QProcess::NotRunning) {
        if (isCancelled(cancelToken)) {
            killProcessTree();
            result.cancelled = true;
            result.errorMessage = QStringLiteral("Operation cancelled.");
            return result;
        }

        process.waitForReadyRead(200);
        const QByteArray outChunk = process.readAllStandardOutput();
        const QByteArray errChunk = process.readAllStandardError();
        appendProcessChunkToLogs(outChunk, &result.stdoutText, &stdoutLogState, logCallback);
        appendProcessChunkToLogs(errChunk, &result.stderrText, &stderrLogState, logCallback);

        if (!process.waitForFinished(50)) {
            if (timeoutMs > 0 && elapsedTimer.hasExpired(timeoutMs)) {
                killProcessTree();
                result.errorMessage = QStringLiteral("Process timed out: %1").arg(program);
                return result;
            }
        }
    }

    const QByteArray remainingOut = process.readAllStandardOutput();
    const QByteArray remainingErr = process.readAllStandardError();
    appendProcessChunkToLogs(remainingOut, &result.stdoutText, &stdoutLogState, logCallback);
    appendProcessChunkToLogs(remainingErr, &result.stderrText, &stderrLogState, logCallback);
    flushProcessLogRemainder(&stdoutLogState, logCallback);
    flushProcessLogRemainder(&stderrLogState, logCallback);

    result.exitCode = process.exitCode();
    result.ok = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    if (!result.ok && result.errorMessage.isEmpty()) {
        result.errorMessage = QStringLiteral("Process exited with code %1: %2").arg(process.exitCode()).arg(program);
    }
    return result;
}

QString msys2RootFromShellPath(const QString &shellPath)
{
    if (shellPath.trimmed().isEmpty()) {
        return {};
    }
    QFileInfo shellInfo(shellPath);
    if (!shellInfo.exists()) {
        return {};
    }

    QDir dir = shellInfo.dir();
    if (dir.dirName().compare(QStringLiteral("bin"), Qt::CaseInsensitive) == 0) {
        dir.cdUp();
        if (dir.dirName().compare(QStringLiteral("usr"), Qt::CaseInsensitive) == 0) {
            dir.cdUp();
        }
    }
    return QDir::cleanPath(dir.absolutePath());
}

QString findWingetExecutable()
{
    const QStringList names = { QStringLiteral("winget.exe"), QStringLiteral("winget") };
    for (const QString &name : names) {
        const QString found = QStandardPaths::findExecutable(name);
        if (!found.isEmpty()) {
            return QDir::cleanPath(found);
        }
    }
    return {};
}

bool isMingw64ToolchainReady(const QString &msys2Root, const QString &mingwBin)
{
    if (msys2Root.trimmed().isEmpty() || mingwBin.trimmed().isEmpty()) {
        return false;
    }

    const QStringList requiredFiles = {
        QStringLiteral("x86_64-w64-mingw32-gcc.exe"),
        QStringLiteral("objdump.exe"),
        QStringLiteral("libzstd.dll")
    };
    for (const QString &fileName : requiredFiles) {
        if (!QFileInfo::exists(QDir(mingwBin).filePath(fileName))) {
            return false;
        }
    }
    if (!QFileInfo::exists(QDir(msys2Root).filePath(QStringLiteral("usr/bin/make.exe")))) {
        return false;
    }
    return true;
}

bool ensureMsys2BuildEnvironment(const WorkflowService::StepProgressCallback &progressCallback,
                                 const WorkflowService::LogCallback &logCallback,
                                 const WorkflowService::CancelToken &cancelToken,
                                 QString *bashPathOut,
                                 QString *mingwBinOut,
                                 QString *errorMessage)
{
    QString shellPath = findMsys2Shell();
    if (!QFileInfo::exists(shellPath)) {
        const QString wingetPath = findWingetExecutable();
        if (wingetPath.isEmpty()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral(
                    "MSYS2 was not found and winget is unavailable, so ld_patcher cannot install build dependencies automatically.");
            }
            return false;
        }

        reportProgress(progressCallback, 8, QStringLiteral("Installing MSYS2..."));
        emitLog(logCallback, QStringLiteral("MSYS2 shell was not found. Installing MSYS2 via winget..."));
        const ProcessRunResult installRun = runProcess(
            wingetPath,
            {
                QStringLiteral("install"),
                QStringLiteral("--id"), QStringLiteral("MSYS2.MSYS2"),
                QStringLiteral("-e"),
                QStringLiteral("--accept-source-agreements"),
                QStringLiteral("--accept-package-agreements"),
                QStringLiteral("--disable-interactivity")
            },
            QDir::homePath(),
            QProcessEnvironment::systemEnvironment(),
            2400,
            logCallback,
            cancelToken);
        if (!installRun.ok) {
            if (errorMessage) {
                *errorMessage = installRun.cancelled
                                    ? QStringLiteral("MSYS2 installation was cancelled.")
                                    : installRun.errorMessage;
            }
            return false;
        }

        shellPath = findMsys2Shell();
        if (!QFileInfo::exists(shellPath)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("MSYS2 installation completed, but msys2_shell.cmd still was not found.");
            }
            return false;
        }
    }

    const QString msys2Root = msys2RootFromShellPath(shellPath);
    const QString bashPath = findMsys2Bash(msys2Root);
    if (bashPath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("MSYS2 was found, but bash.exe is missing from %1").arg(msys2Root);
        }
        return false;
    }
    QString mingwBin = findMsys2MingwBin(msys2Root);
    if (!isMingw64ToolchainReady(msys2Root, mingwBin)) {
        reportProgress(progressCallback, 12, QStringLiteral("Installing MSYS2 build dependencies..."));
        emitLog(logCallback, QStringLiteral("Ensuring required MSYS2 mingw64 packages are installed..."));

        const QString installCommand =
            QStringLiteral("pacman -Sy --noconfirm --needed "
                           "make "
                           "mingw-w64-x86_64-gcc "
                           "mingw-w64-x86_64-make "
                           "mingw-w64-x86_64-binutils "
                           "mingw-w64-x86_64-zstd");
        QProcessEnvironment packageEnv = QProcessEnvironment::systemEnvironment();
        QString packagePath = QDir::toNativeSeparators(QDir(msys2Root).filePath(QStringLiteral("usr/bin")));
        if (!mingwBin.isEmpty()) {
            packagePath += QDir::listSeparator() + QDir::toNativeSeparators(mingwBin);
        }
        const QString existingPath = packageEnv.value(QStringLiteral("PATH"));
        if (!existingPath.isEmpty()) {
            packagePath += QDir::listSeparator() + existingPath;
        }
        packageEnv.insert(QStringLiteral("PATH"), packagePath);
        const ProcessRunResult pacmanRun = runProcess(
            bashPath,
            { QStringLiteral("-lc"), wrapMsys2BashCommand(installCommand) },
            msys2Root.isEmpty() ? QDir::homePath() : msys2Root,
            packageEnv,
            3600,
            logCallback,
            cancelToken);
        if (!pacmanRun.ok) {
            if (errorMessage) {
                *errorMessage = pacmanRun.cancelled
                                    ? QStringLiteral("MSYS2 dependency installation was cancelled.")
                                    : pacmanRun.errorMessage;
            }
            return false;
        }

        mingwBin = findMsys2MingwBin(msys2Root);
        if (!isMingw64ToolchainReady(msys2Root, mingwBin)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("MSYS2 dependency installation finished, but the mingw64 toolchain is still incomplete.");
            }
            return false;
        }
    }

    if (bashPathOut) {
        *bashPathOut = bashPath;
    }
    if (mingwBinOut) {
        *mingwBinOut = mingwBin;
    }
    return true;
}

bool ensureRequiredOutputs(const QStringList &expectedOutputs,
                           QStringList *messages,
                           QString *errorMessage)
{
    for (const QString &output : expectedOutputs) {
        if (!QFileInfo::exists(output)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Expected output is missing: %1").arg(output);
            }
            return false;
        }
        if (messages) {
            messages->append(QStringLiteral("Output exists: %1").arg(output));
        }
    }
    return true;
}

} // namespace

ExtractResult WorkflowService::extractSource(const QString &inputPath,
                                             const ExtractPlan &plan,
                                             const ExtractProgressCallback &progressCallback,
                                             const CancelToken &cancelToken)
{
    ExtractResult result;
    result.sourcePath = inputPath;

    SourcePackage source;
    QString errorMessage;
    if (!source.open(inputPath, &errorMessage)) {
        result.errorMessage = errorMessage;
        return result;
    }

    if (source.kind() == SourceKind::Directory) {
        result.ok = true;
        result.skipped = true;
        result.workingRootPath = source.inputPath();
        result.messages.append(QStringLiteral("Directory input does not need extraction."));
        return result;
    }

    if (source.kind() != SourceKind::ZipArchive) {
        result.errorMessage = QStringLiteral("Unsupported source kind for extraction.");
        return result;
    }

    QString destinationRoot;
    if (!resolveChildDirectoryPath(plan.destinationParentDir,
                                   plan.directoryName,
                                   &destinationRoot,
                                   &errorMessage)) {
        result.errorMessage = errorMessage;
        return result;
    }
    if (QFileInfo::exists(destinationRoot)) {
        result.errorMessage = QStringLiteral("Destination directory already exists: %1").arg(destinationRoot);
        return result;
    }

    auto fail = [&](const QString &message) {
        result.errorMessage = message;
        QString cleanupError;
        if (QFileInfo(destinationRoot).exists()
            && validateRecursiveDeleteTarget(destinationRoot,
                                             plan.destinationParentDir,
                                             QStringLiteral("Extraction cleanup"),
                                             &cleanupError)) {
            QDir(destinationRoot).removeRecursively();
        } else if (!cleanupError.isEmpty()) {
            result.messages.append(QStringLiteral("Cleanup skipped: %1").arg(cleanupError));
        }
        return result;
    };

    if (!QDir().mkpath(destinationRoot)) {
        result.errorMessage = QStringLiteral("Failed to create destination directory: %1").arg(destinationRoot);
        return result;
    }

    const QStringList entries = source.listRelativeFileEntries();
    if (progressCallback) {
        progressCallback(0, entries.size(), QString());
    }

    int lastReportedPercent = -1;
    int extractedCount = 0;
    for (const QString &entry : entries) {
        if (isCancelled(cancelToken)) {
            result.cancelled = true;
            return fail(QStringLiteral("Extraction cancelled."));
        }

        QString readError;
        const QByteArray bytes = source.readBytesRelative(entry, &readError);
        if (bytes.isNull()) {
            return fail(readError.isEmpty() ? QStringLiteral("Failed to read archive entry: %1").arg(entry) : readError);
        }

        QString targetPath;
        if (!resolvePathUnderRoot(destinationRoot,
                                  entry,
                                  QStringLiteral("Archive entry"),
                                  &targetPath,
                                  &readError)) {
            return fail(readError);
        }
        if (!QDir().mkpath(QFileInfo(targetPath).absolutePath())) {
            return fail(QStringLiteral("Failed to create directory for extracted file: %1").arg(targetPath));
        }
        QSaveFile file(targetPath);
        if (!file.open(QIODevice::WriteOnly)) {
            return fail(QStringLiteral("Failed to write extracted file %1: %2").arg(targetPath, file.errorString()));
        }
        if (file.write(bytes) != bytes.size()) {
            return fail(QStringLiteral("Failed to write full extracted file %1").arg(targetPath));
        }
        if (!file.commit()) {
            return fail(QStringLiteral("Failed to commit extracted file %1: %2").arg(targetPath, file.errorString()));
        }

        ++extractedCount;
        if (progressCallback && !entries.isEmpty()) {
            const int percent = (extractedCount * 100) / entries.size();
            if (percent != lastReportedPercent || extractedCount == entries.size()) {
                lastReportedPercent = percent;
                progressCallback(extractedCount, entries.size(), entry);
            }
        }
    }

    result.ok = true;
    result.workingRootPath = destinationRoot;
    result.messages.append(QStringLiteral("Extracted %1 file(s) into %2").arg(entries.size()).arg(destinationRoot));
    return result;
}

ApplyResult WorkflowService::applyPatch(const CatalogData &catalog,
                                        const QString &profileId,
                                        const QString &workingRootPath,
                                        const StepProgressCallback &progressCallback,
                                        const LogCallback &logCallback,
                                        const CancelToken &cancelToken)
{
    ApplyResult result;
    result.workingRootPath = workingRootPath;

    const VersionProfile *profile = findProfileById(catalog, profileId);
    if (!profile) {
        result.errorMessage = QStringLiteral("Selected profile was not found in the catalog.");
        return result;
    }
    if (workingRootPath.trimmed().isEmpty() || !QFileInfo(workingRootPath).isDir()) {
        result.errorMessage = QStringLiteral("Working root does not exist: %1").arg(workingRootPath);
        return result;
    }

    PatchRecipeData recipe;
    QString errorMessage;
    if (!CatalogLoader::loadPatchRecipe(catalog, profile->patchRecipeId, &recipe, &errorMessage)) {
        result.errorMessage = errorMessage;
        return result;
    }

    if (!recipe.backupMode.trimmed().isEmpty()
        && recipe.backupMode != QStringLiteral("copy_before_modify")) {
        result.errorMessage = QStringLiteral("Unsupported backup policy mode: %1").arg(recipe.backupMode);
        return result;
    }
    if (!recipe.rollbackMode.trimmed().isEmpty()
        && recipe.rollbackMode != QStringLiteral("restore_backups")) {
        result.errorMessage = QStringLiteral("Unsupported rollback strategy mode: %1").arg(recipe.rollbackMode);
        return result;
    }

    const QString payloadRoot = resolvePatchPackageRoot(catalog, recipe);
    if (!QFileInfo(payloadRoot).isDir()) {
        result.errorMessage = QStringLiteral("Patch package root is not available: %1").arg(payloadRoot);
        return result;
    }

    const bool restoreBackupsOnFailure = recipe.rollbackMode == QStringLiteral("restore_backups");
    ApplyRollbackState rollbackState;
    auto failApply = [&](const QString &message, bool cancelled) {
        result.cancelled = cancelled;
        result.errorMessage = message;
        emitLog(logCallback, QStringLiteral("Apply failed: %1").arg(message));

        if (restoreBackupsOnFailure && !rollbackState.targetExistedInitially.isEmpty()) {
            emitLog(logCallback, QStringLiteral("Rolling back modified files..."));
            QString rollbackError;
            if (!rollbackPatchedTargets(rollbackState, &result.messages, logCallback, &rollbackError)) {
                result.errorMessage += QStringLiteral(" Rollback also failed: %1").arg(rollbackError);
                emitLog(logCallback, QStringLiteral("Rollback failed: %1").arg(rollbackError));
            } else {
                result.messages.append(QStringLiteral("Rollback completed successfully."));
                emitLog(logCallback, QStringLiteral("Rollback completed successfully."));
            }
        }

        return result;
    };

    emitLog(logCallback, QStringLiteral("Patch recipe: %1 (%2)").arg(recipe.displayName, recipe.id));
    emitLog(logCallback, QStringLiteral("Working root: %1").arg(workingRootPath));
    emitLog(logCallback, QStringLiteral("Payload root: %1").arg(payloadRoot));

    const int totalOperations = qMax(1, recipe.operations.size());
    for (int i = 0; i < recipe.operations.size(); ++i) {
        if (isCancelled(cancelToken)) {
            return failApply(QStringLiteral("Apply cancelled."), true);
        }

        const PatchOperation &operation = recipe.operations.at(i);
        reportProgress(progressCallback, (i * 90) / totalOperations, QStringLiteral("Applying patch operation %1 of %2...").arg(i + 1).arg(totalOperations));
        const QString operationLabel = operation.description.isEmpty()
                                           ? QStringLiteral("%1 -> %2").arg(operation.type, operation.targetPath)
                                           : operation.description;
        emitLog(logCallback,
                QStringLiteral("Apply operation %1/%2: %3")
                    .arg(i + 1)
                    .arg(totalOperations)
                    .arg(operationLabel));
        emitLog(logCallback,
                QStringLiteral("  type=%1 | target=%2%3")
                    .arg(operation.type,
                         operation.targetPath,
                         operation.sourcePath.isEmpty()
                             ? QString()
                             : QStringLiteral(" | source=%1").arg(operation.sourcePath)));

        bool ok = false;
        const int previousMessageCount = result.messages.size();
        if (operation.type == QStringLiteral("copy_file")) {
            ok = applyCopyFile(payloadRoot,
                               workingRootPath,
                               recipe,
                               operation,
                               &rollbackState,
                               &result.messages,
                               &errorMessage);
        } else if (operation.type == QStringLiteral("insert_after_regex")) {
            ok = applyInsertAfterRegex(payloadRoot,
                                       workingRootPath,
                                       recipe,
                                       operation,
                                       &rollbackState,
                                       &result.messages,
                                       &errorMessage);
        } else if (operation.type == QStringLiteral("append_if_missing")) {
            ok = applyAppendIfMissing(payloadRoot,
                                      workingRootPath,
                                      recipe,
                                      operation,
                                      &rollbackState,
                                      &result.messages,
                                      &errorMessage);
        } else {
            errorMessage = QStringLiteral("Unsupported patch operation type: %1").arg(operation.type);
        }

        if (!ok) {
            return failApply(errorMessage, false);
        }

        for (int messageIndex = previousMessageCount; messageIndex < result.messages.size(); ++messageIndex) {
            emitLog(logCallback, result.messages.at(messageIndex));
        }
    }

    reportProgress(progressCallback, 95, QStringLiteral("Running post-apply validation..."));
    emitLog(logCallback, QStringLiteral("Running post-apply validation..."));
    result.validation = AnalysisService::validatePath(catalog, workingRootPath, profileId, {}, cancelToken);
    if (!result.validation.ok) {
        return failApply(result.validation.errorMessage.isEmpty()
                             ? QStringLiteral("Post-apply validation failed to run.")
                             : result.validation.errorMessage,
                         result.validation.errorMessage == QStringLiteral("Validation cancelled."));
    }
    if (!result.validation.validation.alreadyPatched) {
        return failApply(QStringLiteral("Patch apply finished, but post-apply idempotency markers are still incomplete."),
                         false);
    }
    if (!result.validation.validation.postApplyContractSatisfied) {
        return failApply(QStringLiteral("Patch apply finished, but one or more blocking post-apply checks failed."),
                         false);
    }
    emitLog(logCallback, QStringLiteral("Post-apply validation confirmed the patch markers and contract checks."));

    result.ok = true;
    result.messages.append(QStringLiteral("Patch applied successfully to %1").arg(workingRootPath));
    emitLog(logCallback, result.messages.constLast());
    reportProgress(progressCallback, 100, QStringLiteral("Apply completed."));
    return result;
}

BuildResult WorkflowService::buildPatchedTree(const CatalogData &catalog,
                                              const QString &profileId,
                                              const QString &buildRecipeId,
                                              const QString &workingRootPath,
                                              const QString &buildRootOverride,
                                              const StepProgressCallback &progressCallback,
                                              const LogCallback &logCallback,
                                              const CancelToken &cancelToken)
{
    BuildResult result;
    result.workingRootPath = workingRootPath;

    const VersionProfile *profile = findProfileById(catalog, profileId);
    if (!profile) {
        result.errorMessage = QStringLiteral("Selected profile was not found in the catalog.");
        return result;
    }

    const QString selectedBuildRecipeId = !buildRecipeId.trimmed().isEmpty()
                                              ? buildRecipeId.trimmed()
                                              : (profile->buildRecipeIds.isEmpty() ? QString() : profile->buildRecipeIds.constFirst());
    if (selectedBuildRecipeId.isEmpty()) {
        result.errorMessage = QStringLiteral("No build recipe is configured for the selected profile.");
        return result;
    }

    BuildRecipeData recipe;
    QString errorMessage;
    if (!CatalogLoader::loadBuildRecipe(catalog, selectedBuildRecipeId, &recipe, &errorMessage)) {
        result.errorMessage = errorMessage;
        return result;
    }

    result.recipeId = recipe.id;
    result.recipeDisplayName = recipe.displayName;

    QHash<QString, QString> windowsVariables = makeWindowsVariables(catalog, *profile, workingRootPath, recipe.id, buildRootOverride);
    if (!validateManagedBuildLayout(workingRootPath, windowsVariables, &errorMessage)) {
        result.errorMessage = errorMessage;
        return result;
    }
    const QString workspaceRoot = workspaceRootFromCatalog(catalog);

    result.buildDir = windowsVariables.value(QStringLiteral("build_dir"));
    result.installDir = windowsVariables.value(QStringLiteral("install_dir"));
    result.dropDir = windowsVariables.value(QStringLiteral("drop_dir"));
    result.packageDir = windowsVariables.value(QStringLiteral("package_dir"));

    QString workingDirectory = materializeTemplate(recipe.workingDirectoryTemplate, windowsVariables);
    if (workingDirectory.isEmpty()) {
        workingDirectory = workspaceRoot;
    }
    QDir().mkpath(workingDirectory);

    QHash<QString, QString> shellVariables = toShellVariables(windowsVariables, recipe.environmentShell);
    QHash<QString, QString> envVariables =
        recipe.environmentShell == QStringLiteral("msys2_mingw64") ? shellVariables : windowsVariables;

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("LDPATCHER_WORKSPACE_ROOT"), envVariables.value(QStringLiteral("workspace_root")));
    environment.insert(QStringLiteral("LDPATCHER_SOURCE_ROOT"), envVariables.value(QStringLiteral("source_root")));
    environment.insert(QStringLiteral("LDPATCHER_BUILD_DIR"), envVariables.value(QStringLiteral("build_dir")));
    environment.insert(QStringLiteral("LDPATCHER_INSTALL_DIR"), envVariables.value(QStringLiteral("install_dir")));
    environment.insert(QStringLiteral("LDPATCHER_DROP_DIR"), envVariables.value(QStringLiteral("drop_dir")));
    environment.insert(QStringLiteral("LDPATCHER_PACKAGE_DIR"), envVariables.value(QStringLiteral("package_dir")));
    environment.insert(QStringLiteral("LDPATCHER_DISPLAY_VERSION"), windowsVariables.value(QStringLiteral("display_version")));

    const int timeoutSeconds = recipe.buildTimeoutSeconds > 0 ? recipe.buildTimeoutSeconds : 7200;
    QString msys2ShellPath;
    QString msys2Root;
    QString mingwBinPath;
    QSet<QString> matchedBuildLogRules;
    const QString longPathInclude = QDir(workingRootPath).filePath(QStringLiteral("src/liblongpath-win32/include"));

    reportProgress(progressCallback, 5, QStringLiteral("Preparing build recipe..."));
    emitLog(logCallback, QStringLiteral("Build recipe: %1 (%2)").arg(recipe.displayName, recipe.id));
    emitLog(logCallback, QStringLiteral("Working root: %1").arg(workingRootPath));
    emitLog(logCallback, QStringLiteral("Build working directory: %1").arg(workingDirectory));
    emitLog(logCallback, QStringLiteral("Build root override: %1")
                          .arg(buildRootOverride.trimmed().isEmpty()
                                   ? QStringLiteral("<default>")
                                   : QDir::toNativeSeparators(buildRootOverride)));

    if (recipe.environmentShell == QStringLiteral("msys2_mingw64")) {
        if (!ensureMsys2BuildEnvironment(progressCallback,
                                         logCallback,
                                         cancelToken,
                                         &msys2ShellPath,
                                         &mingwBinPath,
                                         &errorMessage)) {
            result.cancelled = isCancelled(cancelToken);
            result.errorMessage = errorMessage;
            return result;
        }
        msys2Root = msys2RootFromShellPath(msys2ShellPath);
        const QString msysUsrBin = msys2Root.isEmpty()
                                       ? QString()
                                       : QDir::toNativeSeparators(QDir(msys2Root).filePath(QStringLiteral("usr/bin")));
        emitLog(logCallback, QStringLiteral("MSYS2 bash: %1").arg(msys2ShellPath));
        emitLog(logCallback, QStringLiteral("MSYS2 mingw64 bin: %1").arg(mingwBinPath));

        QString pathValue = environment.value(QStringLiteral("PATH"));
        if (!msysUsrBin.isEmpty() && !pathValue.contains(msysUsrBin, Qt::CaseInsensitive)) {
            pathValue = msysUsrBin + QDir::listSeparator() + pathValue;
        }
        if (!mingwBinPath.isEmpty()) {
            const QString nativeMingwBin = QDir::toNativeSeparators(mingwBinPath);
            if (!pathValue.contains(nativeMingwBin, Qt::CaseInsensitive)) {
                pathValue = nativeMingwBin + QDir::listSeparator() + pathValue;
            }
        }
        environment.insert(QStringLiteral("PATH"), pathValue);

    }

    if (!validateBuildRequiredTools(recipe,
                                    msys2Root,
                                    msys2ShellPath,
                                    mingwBinPath,
                                    &result.messages,
                                    logCallback,
                                    &errorMessage)) {
        result.errorMessage = errorMessage;
        return result;
    }

    if (QFileInfo(longPathInclude).isDir()) {
        const QString shellInclude = recipe.environmentShell == QStringLiteral("msys2_mingw64")
                                         ? toMsysPath(longPathInclude)
                                         : QDir::cleanPath(longPathInclude);
        const QString includeFlag = QStringLiteral("-I%1").arg(shellInclude);
        const QString configureCflagsArg = QStringLiteral("CFLAGS=%1").arg(includeFlag);
        const QString configureCppflagsArg = QStringLiteral("CPPFLAGS=%1").arg(includeFlag);

        shellVariables.insert(QStringLiteral("longpath_include_dir"), shellInclude);
        shellVariables.insert(QStringLiteral("longpath_include_flag"), includeFlag);
        shellVariables.insert(QStringLiteral("configure_cflags_arg"), configureCflagsArg);
        shellVariables.insert(QStringLiteral("configure_cppflags_arg"), configureCppflagsArg);

        windowsVariables.insert(QStringLiteral("longpath_include_dir"), QDir::cleanPath(longPathInclude));
        windowsVariables.insert(QStringLiteral("longpath_include_flag"), includeFlag);
        windowsVariables.insert(QStringLiteral("configure_cflags_arg"), configureCflagsArg);
        windowsVariables.insert(QStringLiteral("configure_cppflags_arg"), configureCppflagsArg);

        envVariables.insert(QStringLiteral("CFLAGS"), includeFlag);
        envVariables.insert(QStringLiteral("CPPFLAGS"), includeFlag);
        environment.insert(QStringLiteral("CFLAGS"), includeFlag);
        environment.insert(QStringLiteral("CPPFLAGS"), includeFlag);
        emitLog(logCallback, QStringLiteral("Configured long-path include flags: %1").arg(includeFlag));
    } else {
        shellVariables.insert(QStringLiteral("longpath_include_dir"), QString());
        shellVariables.insert(QStringLiteral("longpath_include_flag"), QString());
        shellVariables.insert(QStringLiteral("configure_cflags_arg"), QString());
        shellVariables.insert(QStringLiteral("configure_cppflags_arg"), QString());
        windowsVariables.insert(QStringLiteral("longpath_include_dir"), QString());
        windowsVariables.insert(QStringLiteral("longpath_include_flag"), QString());
        windowsVariables.insert(QStringLiteral("configure_cflags_arg"), QString());
        windowsVariables.insert(QStringLiteral("configure_cppflags_arg"), QString());
    }

    if (!recipe.cleanCommand.isEmpty()) {
        reportProgress(progressCallback, 15, QStringLiteral("Cleaning previous build outputs..."));
        const QStringList cleanCommandWindows = materializeList(recipe.cleanCommand, windowsVariables);
        if (!validateManagedCleanCommand(cleanCommandWindows, windowsVariables, &errorMessage)) {
            result.errorMessage = errorMessage;
            return result;
        }
        const QStringList command = materializeList(recipe.cleanCommand, shellVariables);
        QStringList quoted;
        for (const QString &token : command) {
            quoted.append(shellQuoteBash(token));
        }
        const ProcessRunResult cleanRun = runProcess(msys2ShellPath,
                                                     { QStringLiteral("-lc"), wrapMsys2BashCommandInDirectory(workingDirectory, quoted.join(QLatin1Char(' '))) },
                                                     workingDirectory,
                                                     environment,
                                                     300,
                                                     logCallback,
                                                     cancelToken);
        appendMatchedBuildLogRules(recipe,
                                   cleanRun.stdoutText + QStringLiteral("\n") + cleanRun.stderrText,
                                   &matchedBuildLogRules,
                                   &result.messages,
                                   logCallback);
        if (!cleanRun.ok) {
            result.cancelled = cleanRun.cancelled;
            result.errorMessage = cleanRun.errorMessage;
            return result;
        }
    }

    QDir().mkpath(workingDirectory);

    if (!recipe.scriptRef.isEmpty()) {
        reportProgress(progressCallback, 25, QStringLiteral("Launching build script..."));
        const QString scriptPath = resolveRecipeRef(catalog, recipe.filePath, recipe.scriptRef);
        if (!QFileInfo(scriptPath).isFile()) {
            result.errorMessage = QStringLiteral("Build script does not exist: %1").arg(scriptPath);
            return result;
        }
        emitLog(logCallback, QStringLiteral("Script: %1").arg(scriptPath));

        auto scriptLogCallback = [&](const QString &line) {
            int percent = 0;
            QString progressMessage;
            if (parseProgressMarker(line, &percent, &progressMessage)) {
                reportProgress(progressCallback,
                               percent,
                               progressMessage.isEmpty()
                                   ? QStringLiteral("Running build recipe...")
                                   : progressMessage);
                return;
            }
            emitLog(logCallback, line);
        };

        QString program;
        QStringList args;
        if (scriptPath.endsWith(QStringLiteral(".ps1"), Qt::CaseInsensitive)) {
            program = QStringLiteral("powershell");
            args = { QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"), QStringLiteral("-File"), scriptPath };
        } else {
            program = msys2ShellPath;
            const QString scriptCommand = QStringLiteral("bash %1").arg(shellQuoteBash(toMsysPath(scriptPath)));
            args = { QStringLiteral("-lc"), wrapMsys2BashCommandInDirectory(workingDirectory, scriptCommand) };
        }

        const ProcessRunResult scriptRun = runProcess(program, args, workingDirectory, environment, timeoutSeconds, scriptLogCallback, cancelToken);
        appendMatchedBuildLogRules(recipe,
                                   scriptRun.stdoutText + QStringLiteral("\n") + scriptRun.stderrText,
                                   &matchedBuildLogRules,
                                   &result.messages,
                                   logCallback);
        if (!scriptRun.ok) {
            result.cancelled = scriptRun.cancelled;
            result.errorMessage = scriptRun.errorMessage;
            return result;
        }
        reportProgress(progressCallback, 90, QStringLiteral("Build script completed. Checking outputs..."));
    } else {
        if (!recipe.configureCommand.isEmpty()) {
            reportProgress(progressCallback, 25, QStringLiteral("Configuring build tree..."));
            const QStringList command = materializeList(recipe.configureCommand, shellVariables);
            QStringList quoted;
            for (const QString &token : command) {
                quoted.append(shellQuoteBash(token));
            }
            const ProcessRunResult configureRun = runProcess(msys2ShellPath,
                                                             { QStringLiteral("-lc"), wrapMsys2BashCommandInDirectory(workingDirectory, quoted.join(QLatin1Char(' '))) },
                                                             workingDirectory,
                                                             environment,
                                                             timeoutSeconds,
                                                             logCallback,
                                                             cancelToken);
            appendMatchedBuildLogRules(recipe,
                                       configureRun.stdoutText + QStringLiteral("\n") + configureRun.stderrText,
                                       &matchedBuildLogRules,
                                       &result.messages,
                                       logCallback);
            if (!configureRun.ok) {
                result.cancelled = configureRun.cancelled;
                result.errorMessage = configureRun.errorMessage;
                return result;
            }
        }

        if (!recipe.buildCommand.isEmpty()) {
            reportProgress(progressCallback, 60, QStringLiteral("Building patched linker..."));
            const QStringList command = materializeList(recipe.buildCommand, shellVariables);
            QStringList quoted;
            for (const QString &token : command) {
                quoted.append(shellQuoteBash(token));
            }
            const ProcessRunResult buildRun = runProcess(msys2ShellPath,
                                                         { QStringLiteral("-lc"), wrapMsys2BashCommandInDirectory(workingDirectory, quoted.join(QLatin1Char(' '))) },
                                                         workingDirectory,
                                                         environment,
                                                         timeoutSeconds,
                                                         logCallback,
                                                         cancelToken);
            appendMatchedBuildLogRules(recipe,
                                       buildRun.stdoutText + QStringLiteral("\n") + buildRun.stderrText,
                                       &matchedBuildLogRules,
                                       &result.messages,
                                       logCallback);
            if (!buildRun.ok) {
                result.cancelled = buildRun.cancelled;
                result.errorMessage = buildRun.errorMessage;
                return result;
            }
        }
        reportProgress(progressCallback, 90, QStringLiteral("Build completed. Checking outputs..."));
    }

    reportProgress(progressCallback, 94, QStringLiteral("Preparing verification artifacts..."));
    if (!materializeCubeIdeDrop(result.installDir, result.dropDir, &result.messages, logCallback, &errorMessage)) {
        result.errorMessage = errorMessage;
        return result;
    }

    const QStringList expectedOutputs = materializeList(recipe.expectedOutputs, windowsVariables);
    if (!ensureRequiredOutputs(expectedOutputs, &result.messages, &errorMessage)) {
        result.errorMessage = errorMessage;
        return result;
    }

    const QString artifactDropDir = materializeTemplate(recipe.artifactDropDir, windowsVariables);
    if (!artifactDropDir.isEmpty()) {
        result.dropDir = artifactDropDir;
    }

    result.ok = true;
    result.messages.append(QStringLiteral("Build completed successfully."));
    reportProgress(progressCallback, 100, QStringLiteral("Build completed."));
    return result;
}

BuildLayoutPreview WorkflowService::previewBuildLayout(const CatalogData &catalog,
                                                       const QString &profileId,
                                                       const QString &buildRecipeId,
                                                       const QString &workingRootPath,
                                                       const QString &buildRootOverride)
{
    BuildLayoutPreview preview;
    preview.profileId = profileId.trimmed();
    preview.workingRootPath = QDir::cleanPath(workingRootPath);
    preview.buildRootPath = QDir::cleanPath(buildRootOverride);

    const VersionProfile *profile = findProfileById(catalog, preview.profileId);
    if (!profile) {
        preview.errorMessage = QStringLiteral("Selected profile was not found in the catalog.");
        return preview;
    }

    if (preview.workingRootPath.isEmpty()) {
        preview.errorMessage = QStringLiteral("Working root is empty.");
        return preview;
    }

    const QString selectedBuildRecipeId = !buildRecipeId.trimmed().isEmpty()
                                              ? buildRecipeId.trimmed()
                                              : (profile->buildRecipeIds.isEmpty() ? QString() : profile->buildRecipeIds.constFirst());
    if (selectedBuildRecipeId.isEmpty()) {
        preview.errorMessage = QStringLiteral("No build recipe is configured for the selected profile.");
        return preview;
    }

    BuildRecipeData recipe;
    QString errorMessage;
    if (!CatalogLoader::loadBuildRecipe(catalog, selectedBuildRecipeId, &recipe, &errorMessage)) {
        preview.errorMessage = errorMessage;
        return preview;
    }

    QHash<QString, QString> windowsVariables =
        makeWindowsVariables(catalog, *profile, preview.workingRootPath, recipe.id, preview.buildRootPath);
    if (!validateManagedBuildLayout(preview.workingRootPath, windowsVariables, &errorMessage)) {
        preview.errorMessage = errorMessage;
        return preview;
    }

    preview.recipeId = recipe.id;
    preview.recipeDisplayName = recipe.displayName;
    preview.buildRootPath = windowsVariables.value(QStringLiteral("build_root"));
    preview.buildDir = windowsVariables.value(QStringLiteral("build_dir"));
    preview.installDir = windowsVariables.value(QStringLiteral("install_dir"));
    preview.dropDir = windowsVariables.value(QStringLiteral("drop_dir"));
    preview.packageDir = windowsVariables.value(QStringLiteral("package_dir"));
    preview.ok = !preview.buildDir.isEmpty()
                 && !preview.installDir.isEmpty()
                 && !preview.dropDir.isEmpty()
                 && !preview.packageDir.isEmpty();
    if (!preview.ok && preview.errorMessage.isEmpty()) {
        preview.errorMessage = QStringLiteral("Build layout preview could not be resolved.");
    }
    return preview;
}

PackageResult WorkflowService::createCubeIdePackage(const QString &sourceDropDir,
                                                    const QString &packageDir,
                                                    const StepProgressCallback &progressCallback,
                                                    const LogCallback &logCallback,
                                                    const CancelToken &cancelToken)
{
    PackageResult result;
    result.sourceDropDir = absoluteCleanPath(sourceDropDir);
    result.packageDir = absoluteCleanPath(packageDir);

    if (result.sourceDropDir.isEmpty() || !QFileInfo(result.sourceDropDir).isDir()) {
        result.errorMessage = QStringLiteral("Build drop directory does not exist: %1").arg(sourceDropDir);
        return result;
    }
    if (result.packageDir.isEmpty()) {
        result.errorMessage = QStringLiteral("CubeIDE package directory is empty.");
        return result;
    }
    if (isUnsafeRootPath(result.sourceDropDir) || isUnsafeRootPath(result.packageDir)) {
        result.errorMessage = QStringLiteral("CubeIDE packaging paths are too broad to use safely.");
        return result;
    }

    if (QDir::cleanPath(result.sourceDropDir).compare(QDir::cleanPath(result.packageDir), Qt::CaseInsensitive) == 0) {
        result.ok = true;
        result.skipped = true;
        result.messages.append(QStringLiteral("Build artifacts already live in the CubeIDE package directory."));
        reportProgress(progressCallback, 100, QStringLiteral("CubeIDE package already available."));
        return result;
    }
    if (pathsOverlap(result.sourceDropDir, result.packageDir)) {
        result.errorMessage = QStringLiteral(
            "CubeIDE package directory must not overlap the source drop directory: %1")
            .arg(result.packageDir);
        return result;
    }

    QDir sourceDir(result.sourceDropDir);
    const QStringList relativeFiles = sourceDir.entryList(QDir::Files | QDir::NoDotAndDotDot);
    if (relativeFiles.isEmpty()) {
        result.errorMessage = QStringLiteral("No files were found in the build drop directory: %1").arg(result.sourceDropDir);
        return result;
    }

    if (QFileInfo(result.packageDir).exists()) {
        QString cleanupError;
        if (!validateRecursiveDeleteTarget(result.packageDir,
                                           QFileInfo(result.packageDir).absolutePath(),
                                           QStringLiteral("CubeIDE package cleanup"),
                                           &cleanupError)) {
            result.errorMessage = cleanupError;
            return result;
        }
        if (!QDir(result.packageDir).removeRecursively()) {
            result.errorMessage = QStringLiteral("Failed to clear existing CubeIDE package directory: %1").arg(result.packageDir);
            return result;
        }
    }
    QDir().mkpath(result.packageDir);

    reportProgress(progressCallback, 5, QStringLiteral("Creating CubeIDE linker package..."));
    const int totalFiles = relativeFiles.size();
    for (int i = 0; i < relativeFiles.size(); ++i) {
        if (isCancelled(cancelToken)) {
            result.cancelled = true;
            result.errorMessage = QStringLiteral("CubeIDE packaging cancelled.");
            return result;
        }

        const QString fileName = relativeFiles.at(i);
        const QString sourcePath = sourceDir.filePath(fileName);
        const QString targetPath = QDir(result.packageDir).filePath(fileName);
        QString errorMessage;
        if (!copyFileReplacing(sourcePath, targetPath, &errorMessage)) {
            result.errorMessage = errorMessage;
            return result;
        }

        const int percent = qBound(5, ((i + 1) * 100) / totalFiles, 100);
        reportProgress(progressCallback,
                       percent,
                       QStringLiteral("Packaging CubeIDE linker folder... %1% (%2/%3)")
                           .arg(percent)
                           .arg(i + 1)
                           .arg(totalFiles));
        emitLog(logCallback, QStringLiteral("Packaged file: %1").arg(targetPath));
        result.messages.append(QStringLiteral("Packaged file: %1").arg(targetPath));
    }

    result.ok = true;
    result.messages.append(QStringLiteral("CubeIDE linker package is ready: %1").arg(result.packageDir));
    reportProgress(progressCallback, 100, QStringLiteral("CubeIDE package completed."));
    return result;
}

VerifyResult WorkflowService::verifyBuild(const CatalogData &catalog,
                                          const QString &profileId,
                                          const QStringList &verifyRecipeIds,
                                          const QString &dropDir,
                                          const QString &cubeIdePath,
                                          const StepProgressCallback &progressCallback,
                                          const LogCallback &logCallback,
                                          const CancelToken &cancelToken)
{
    VerifyResult result;
    result.dropDir = absoluteCleanPath(dropDir);
    if (result.dropDir.isEmpty() || !QFileInfo(result.dropDir).isDir()) {
        result.errorMessage = QStringLiteral("Verify drop directory does not exist: %1").arg(dropDir);
        return result;
    }

    const VersionProfile *profile = findProfileById(catalog, profileId);
    if (!profile) {
        result.errorMessage = QStringLiteral("Selected profile was not found in the catalog.");
        return result;
    }

    const QStringList selectedVerifyIds = !verifyRecipeIds.isEmpty() ? verifyRecipeIds : profile->verifyRecipeIds;
    if (selectedVerifyIds.isEmpty()) {
        result.errorMessage = QStringLiteral("No verify recipes are configured for the selected profile.");
        return result;
    }

    const QString workspaceRoot = workspaceRootFromCatalog(catalog);
    QHash<QString, QString> variables;
    variables.insert(QStringLiteral("workspace_root"), absoluteCleanPath(workspaceRoot));
    variables.insert(QStringLiteral("drop_dir"), result.dropDir);
    if (!cubeIdePath.trimmed().isEmpty()) {
        variables.insert(QStringLiteral("cubeide_path"), absoluteCleanPath(cubeIdePath.trimmed()));
    }

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("LDPATCHER_WORKSPACE_ROOT"), variables.value(QStringLiteral("workspace_root")));
    environment.insert(QStringLiteral("LDPATCHER_DROP_DIR"), result.dropDir);
    if (!cubeIdePath.trimmed().isEmpty()) {
        environment.insert(QStringLiteral("LDPATCHER_CUBEIDE_PATH"),
                           variables.value(QStringLiteral("cubeide_path")));
    }

    const int totalRecipes = qMax(1, selectedVerifyIds.size());
    int completedRecipes = 0;
    for (const QString &verifyRecipeId : selectedVerifyIds) {
        if (isCancelled(cancelToken)) {
            result.cancelled = true;
            result.errorMessage = QStringLiteral("Verify cancelled.");
            return result;
        }

        VerifyRecipeData recipe;
        QString errorMessage;
        if (!CatalogLoader::loadVerifyRecipe(catalog, verifyRecipeId, &recipe, &errorMessage)) {
            result.errorMessage = errorMessage;
            return result;
        }

        VerifyRecipeRunResult recipeRun;
        recipeRun.recipeId = recipe.id;
        recipeRun.recipeDisplayName = recipe.displayName;
        emitLog(logCallback, QStringLiteral("Verify recipe: %1 (%2)").arg(recipe.displayName, recipe.id));

        if (!validateVerifyRequiredInputs(recipe,
                                          &variables,
                                          &environment,
                                          &recipeRun.messages,
                                          logCallback,
                                          &errorMessage)) {
            result.errorMessage = errorMessage;
            return result;
        }

        bool recipeFailed = false;
        QStringList scriptOutputs;
        const int recipeTotal = qMax(1, recipe.checks.size());
        for (int i = 0; i < recipe.checks.size(); ++i) {
            if (isCancelled(cancelToken)) {
                result.cancelled = true;
                result.errorMessage = QStringLiteral("Verify cancelled.");
                return result;
            }

            const VerifyCheck &check = recipe.checks.at(i);
            VerifyCheckRunResult checkRun;
            checkRun.description = check.description;
            const int percent = ((completedRecipes * 100) / totalRecipes) + (((i + 1) * 100) / (totalRecipes * recipeTotal));
            reportProgress(progressCallback, qBound(0, percent, 99), QStringLiteral("Running verify check: %1").arg(check.description));

            if (check.type == QStringLiteral("file_exists")) {
                const QString targetPath = materializeTemplate(check.path, variables);
                checkRun.passed = QFileInfo::exists(targetPath);
                checkRun.detail = targetPath;
            } else if (check.type == QStringLiteral("command_exit_zero") || check.type == QStringLiteral("stdout_regex")) {
                const QStringList command = materializeList(check.command, variables);
                if (command.isEmpty()) {
                    checkRun.passed = false;
                    checkRun.detail = QStringLiteral("Command is empty.");
                } else {
                    const ProcessRunResult run = runProcess(command.constFirst(), command.mid(1), workspaceRoot, environment, 600, logCallback, cancelToken);
                    if (run.cancelled) {
                        result.cancelled = true;
                        result.errorMessage = QStringLiteral("Verify cancelled.");
                        return result;
                    }
                    if (check.type == QStringLiteral("command_exit_zero")) {
                        checkRun.passed = run.ok;
                        checkRun.detail = run.ok ? QStringLiteral("exit=0") : run.errorMessage;
                    } else {
                        const QRegularExpression regex(check.regex);
                        if (!regex.isValid()) {
                            checkRun.passed = false;
                            checkRun.detail = QStringLiteral("Invalid regex '%1': %2")
                                                  .arg(check.regex, regex.errorString());
                        } else {
                            checkRun.passed = run.ok && regex.match(run.stdoutText).hasMatch();
                            checkRun.detail = checkRun.passed
                                                  ? QStringLiteral("stdout matched regex")
                                                  : QStringLiteral("stdout did not match regex: %1")
                                                        .arg(check.regex);
                        }
                    }
                }
            } else if (check.type == QStringLiteral("script_exit_zero")) {
                const QString scriptPath = resolveRecipeRef(catalog, recipe.filePath, check.scriptRef);
                if (!QFileInfo(scriptPath).isFile()) {
                    checkRun.passed = false;
                    checkRun.detail = QStringLiteral("Script does not exist: %1").arg(scriptPath);
                    recipeRun.checks.append(checkRun);
                    recipeRun.messages.append(QStringLiteral("%1: FAIL").arg(check.description));
                    recipeFailed = true;
                    continue;
                }
                const QString powerShellPath = findPowerShellExecutable();
                auto verifyLogCallback = [&](const QString &line) {
                    int percent = 0;
                    QString progressMessage;
                    if (parseProgressMarker(line, &percent, &progressMessage)) {
                        reportProgress(progressCallback,
                                       percent,
                                       progressMessage.isEmpty()
                                           ? QStringLiteral("Running verify script...")
                                           : progressMessage);
                        return;
                    }
                    emitLog(logCallback, line);
                };
                const ProcessRunResult run = runProcess(powerShellPath,
                                                        { QStringLiteral("-ExecutionPolicy"),
                                                          QStringLiteral("Bypass"),
                                                          QStringLiteral("-File"),
                                                          scriptPath },
                                                        workspaceRoot,
                                                        environment,
                                                        3600,
                                                        verifyLogCallback,
                                                        cancelToken);
                if (run.cancelled) {
                    result.cancelled = true;
                    result.errorMessage = QStringLiteral("Verify cancelled.");
                    return result;
                }
                checkRun.passed = run.ok;
                checkRun.detail = run.ok ? QStringLiteral("script exit=0") : run.errorMessage;
                scriptOutputs.append(run.stdoutText);
            } else {
                checkRun.passed = false;
                checkRun.detail = QStringLiteral("Unsupported verify check type: %1").arg(check.type);
            }

            recipeRun.checks.append(checkRun);
            recipeRun.messages.append(QStringLiteral("%1: %2").arg(check.description, checkRun.passed ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
            emitLog(logCallback,
                    QStringLiteral("Verify check [%1]: %2 | %3")
                        .arg(recipe.displayName,
                             check.description,
                             checkRun.passed ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
            if (!checkRun.detail.isEmpty()) {
                emitLog(logCallback, QStringLiteral("  detail: %1").arg(checkRun.detail));
            }
            if (!checkRun.passed && recipe.allChecksMustPass) {
                recipeFailed = true;
            }
        }

        if (recipe.allChecksMustPass && !recipe.expectedSummaryFields.isEmpty() && !scriptOutputs.isEmpty()) {
            const QString combinedOutput = scriptOutputs.join(QStringLiteral("\n"));
            for (const QString &field : recipe.expectedSummaryFields) {
                if (!combinedOutput.contains(field, Qt::CaseInsensitive)) {
                    VerifyCheckRunResult summaryCheck;
                    summaryCheck.description = QStringLiteral("Verify summary contains %1").arg(field);
                    summaryCheck.detail = QStringLiteral("Summary field not found in script output.");
                    summaryCheck.passed = false;
                    recipeRun.checks.append(summaryCheck);
                    recipeFailed = true;
                }
            }
        }

        for (const QString &artifactRef : recipe.resultArtifacts) {
            const QString artifactPath = materializeTemplate(artifactRef, variables);
            VerifyCheckRunResult artifactCheck;
            artifactCheck.description = QStringLiteral("Result artifact exists");
            artifactCheck.detail = artifactPath;
            artifactCheck.passed = QFileInfo::exists(artifactPath);
            recipeRun.checks.append(artifactCheck);
            if (!artifactCheck.passed) {
                recipeFailed = true;
            }
        }

        recipeRun.ok = !recipeFailed;
        result.recipes.append(recipeRun);
        if (!recipeRun.ok && recipe.allChecksMustPass) {
            for (const VerifyCheckRunResult &failedCheck : recipeRun.checks) {
                if (!failedCheck.passed) {
                    emitLog(logCallback,
                            QStringLiteral("Verify recipe failed at check: %1").arg(failedCheck.description));
                    if (!failedCheck.detail.isEmpty()) {
                        emitLog(logCallback, QStringLiteral("  failure detail: %1").arg(failedCheck.detail));
                    }
                }
            }
            result.errorMessage = QStringLiteral("Verify recipe failed: %1").arg(recipe.displayName);
            return result;
        }

        ++completedRecipes;
    }

    result.ok = true;
    result.messages.append(QStringLiteral("Verify completed successfully."));
    reportProgress(progressCallback, 100, QStringLiteral("Verify completed."));
    return result;
}

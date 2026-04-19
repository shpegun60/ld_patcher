#include "sourcepackage.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

namespace {

QString normalizeReleaseLevel(const QString &releaseLevel)
{
    if (releaseLevel.isEmpty()) {
        return {};
    }

    return releaseLevel.trimmed().toLower();
}

QString inferRefFromVersion(const QString &baseVersion, const QString &releaseLevel)
{
    const QStringList parts = baseVersion.split(QLatin1Char('.'));
    if (parts.size() < 2 || releaseLevel.isEmpty()) {
        return {};
    }

    return QStringLiteral("%1.%2.%3")
        .arg(parts.at(0), parts.at(1), normalizeReleaseLevel(releaseLevel));
}

QString extractFirstMatch(const QString &text, const QRegularExpression &regex)
{
    const QRegularExpressionMatch match = regex.match(text);
    if (!match.hasMatch() || match.lastCapturedIndex() < 1) {
        return {};
    }
    return match.captured(1).trimmed();
}

QString prefixedArchivePath(const QString &prefix, const QString &relativePath)
{
    if (prefix.isEmpty()) {
        return relativePath;
    }
    if (relativePath.isEmpty()) {
        return prefix;
    }
    return QStringLiteral("%1/%2").arg(prefix, relativePath);
}

QString candidateRootName(const QString &prefix, const QString &archiveFileBaseName)
{
    if (prefix.isEmpty()) {
        return archiveFileBaseName;
    }

    const int slashIndex = prefix.lastIndexOf(QLatin1Char('/'));
    return slashIndex >= 0 ? prefix.mid(slashIndex + 1) : prefix;
}

QString longestCommonDirectoryPrefix(const QStringList &paths)
{
    if (paths.isEmpty()) {
        return {};
    }

    QStringList common = paths.constFirst().split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (!common.isEmpty()) {
        common.removeLast();
    }

    for (int i = 1; i < paths.size() && !common.isEmpty(); ++i) {
        QStringList parts = paths.at(i).split(QLatin1Char('/'), Qt::SkipEmptyParts);
        if (!parts.isEmpty()) {
            parts.removeLast();
        }

        int j = 0;
        while (j < common.size() && j < parts.size() && common.at(j) == parts.at(j)) {
            ++j;
        }
        common = common.mid(0, j);
    }

    return common.join(QLatin1Char('/'));
}

QString detectArchiveRootPrefix(const ZipArchive &archive, const QString &archiveFileBaseName)
{
    const QStringList markerPaths = {
        QStringLiteral("build-common.sh"),
        QStringLiteral("src/binutils/ld/ldlang.c"),
        QStringLiteral("src/gcc/gcc/BASE-VER")
    };

    auto candidateWorks = [&archive, &markerPaths](const QString &candidate) {
        for (const QString &markerPath : markerPaths) {
            if (!archive.containsNormalizedPath(prefixedArchivePath(candidate, markerPath))) {
                return false;
            }
        }
        return true;
    };

    QStringList candidates;
    QSet<QString> seen;
    auto addCandidate = [&candidates, &seen](const QString &candidate) {
        if (!seen.contains(candidate)) {
            seen.insert(candidate);
            candidates.append(candidate);
        }
    };

    const quint64 sampleCount = qMin<quint64>(archive.entryCount(), 4096);
    QStringList sampleEntries;
    sampleEntries.reserve(static_cast<qsizetype>(sampleCount));
    for (quint64 i = 0; i < sampleCount; ++i) {
        const QString entry = archive.normalizedEntryAt(i);
        if (!entry.isEmpty()) {
            sampleEntries.append(entry);
        }
    }

    const QString samplePrefix = longestCommonDirectoryPrefix(sampleEntries);
    if (!samplePrefix.isEmpty() && candidateWorks(samplePrefix)) {
        return samplePrefix;
    }
    if (candidateWorks(QString())) {
        return {};
    }

    addCandidate(QString());

    for (const QString &markerPath : markerPaths) {
        const QString markerSuffix = QStringLiteral("/%1").arg(markerPath);
        for (quint64 i = 0; i < archive.entryCount(); ++i) {
            const QString entry = archive.normalizedEntryAt(i);
            if (entry.isEmpty()) {
                continue;
            }
            if (entry == markerPath) {
                addCandidate(QString());
            } else if (entry.endsWith(markerSuffix)) {
                addCandidate(entry.left(entry.size() - markerSuffix.size()));
            }
        }
    }

    int bestScore = -1;
    QString bestPrefix;
    for (const QString &candidate : candidates) {
        int score = 0;
        if (archive.containsNormalizedPath(prefixedArchivePath(candidate, QStringLiteral("build-common.sh")))) {
            score += 4;
        }
        if (archive.containsNormalizedPath(prefixedArchivePath(candidate, QStringLiteral("src/binutils/ld/ldlang.c")))) {
            score += 4;
        }
        if (archive.containsNormalizedPath(prefixedArchivePath(candidate, QStringLiteral("src/gcc/gcc/BASE-VER")))) {
            score += 2;
        }
        const QString candidateName = candidateRootName(candidate, archiveFileBaseName);
        if (candidateName.contains(QStringLiteral("gnu-tools-for-stm32"), Qt::CaseInsensitive)) {
            score += 1;
        }

        if (score > bestScore
            || (score == bestScore
                && (bestPrefix.isEmpty() || candidate.count(QLatin1Char('/')) < bestPrefix.count(QLatin1Char('/'))))) {
            bestScore = score;
            bestPrefix = candidate;
        }
    }

    if (bestScore > 0) {
        return bestPrefix;
    }

    QString commonTopLevel;
    bool first = true;
    for (quint64 i = 0; i < archive.entryCount(); ++i) {
        const QString cleanPath = QDir::cleanPath(archive.normalizedEntryAt(i));
        const int slashIndex = cleanPath.indexOf(QLatin1Char('/'));
        const QString topLevel = slashIndex > 0 ? cleanPath.left(slashIndex) : QString();
        if (first) {
            commonTopLevel = topLevel;
            first = false;
        } else if (commonTopLevel != topLevel) {
            return {};
        }
    }

    return commonTopLevel;
}

QString comparableFsPath(const QString &path)
{
    QString normalized = QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath());
#ifdef Q_OS_WIN
    normalized = normalized.toLower();
#endif
    return QDir::cleanPath(normalized);
}

bool isPathInsideOrEqual(const QString &candidatePath, const QString &rootPath)
{
    const QString candidate = comparableFsPath(candidatePath);
    const QString root = comparableFsPath(rootPath);
    return !candidate.isEmpty()
           && !root.isEmpty()
           && (candidate == root || candidate.startsWith(root + QLatin1Char('/')));
}

bool resolveDirectoryMemberPath(const QString &rootPath,
                                const QString &relativePath,
                                QString *normalizedRelativePath,
                                QString *resolvedPath,
                                QString *errorMessage)
{
    QString normalized = QDir::fromNativeSeparators(relativePath.trimmed());
    while (normalized.startsWith(QStringLiteral("./"))) {
        normalized.remove(0, 2);
    }

    const QFileInfo relativeInfo(normalized);
    if (relativeInfo.isAbsolute() || normalized.startsWith(QLatin1Char('/'))
        || normalized.startsWith(QStringLiteral("//"))) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Relative path must not be absolute: %1").arg(relativePath);
        }
        return false;
    }

    normalized = QDir::cleanPath(normalized);
    if (normalized == QStringLiteral(".")) {
        normalized.clear();
    }

    const QString root = QFileInfo(rootPath).absoluteFilePath();
    const QString candidate = QDir(root).absoluteFilePath(normalized);
    if (!isPathInsideOrEqual(candidate, root)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Relative path escapes the source root: %1").arg(relativePath);
        }
        return false;
    }

    if (normalizedRelativePath) {
        *normalizedRelativePath = normalized;
    }
    if (resolvedPath) {
        *resolvedPath = QDir::cleanPath(candidate);
    }
    return true;
}

} // namespace

bool SourcePackage::open(const QString &inputPath, QString *errorMessage)
{
    m_zipArchive.close();
    m_kind = SourceKind::Unknown;
    m_inputPath.clear();
    m_rootName.clear();
    m_archiveRootPrefix.clear();
    m_textCache.clear();
    m_basicInspectionCached = false;
    m_cachedBasicInspection = SourceInspection();

    const QFileInfo info(inputPath);
    if (!info.exists()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Path does not exist: %1").arg(inputPath);
        }
        return false;
    }

    if (info.isDir()) {
        m_kind = SourceKind::Directory;
        m_inputPath = info.absoluteFilePath();
        m_rootName = info.fileName();
        return true;
    }

    if (info.isFile() && info.suffix().compare(QStringLiteral("zip"), Qt::CaseInsensitive) == 0) {
        QString archiveError;
        if (!m_zipArchive.open(info.absoluteFilePath(), &archiveError)) {
            if (errorMessage) {
                *errorMessage = archiveError;
            }
            return false;
        }

        if (m_zipArchive.entryCount() == 0) {
            m_zipArchive.close();
            if (errorMessage) {
                *errorMessage = QStringLiteral("ZIP archive appears to be empty: %1").arg(inputPath);
            }
            return false;
        }

        m_kind = SourceKind::ZipArchive;
        m_inputPath = info.absoluteFilePath();
        m_archiveRootPrefix = detectArchiveRootPrefix(m_zipArchive, info.completeBaseName());
        m_rootName = candidateRootName(m_archiveRootPrefix, info.completeBaseName());
        return true;
    }

    if (errorMessage) {
        *errorMessage = QStringLiteral("Unsupported source input. Use a directory or a .zip archive.");
    }
    return false;
}

bool SourcePackage::isOpen() const
{
    return m_kind != SourceKind::Unknown && !m_inputPath.isEmpty();
}

SourceKind SourcePackage::kind() const
{
    return m_kind;
}

QString SourcePackage::inputPath() const
{
    return m_inputPath;
}

QString SourcePackage::rootName() const
{
    return m_rootName;
}

QString SourcePackage::normalizeRelativePath(const QString &relativePath) const
{
    QString path = QDir::fromNativeSeparators(relativePath);
    while (path.startsWith(QStringLiteral("./"))) {
        path.remove(0, 2);
    }
    if (path.startsWith(QLatin1Char('/'))) {
        path.remove(0, 1);
    }
    path = QDir::cleanPath(path);
    if (path == QStringLiteral(".")) {
        path.clear();
    }
    return path;
}

QString SourcePackage::archiveMemberPath(const QString &relativePath) const
{
    const QString normalized = normalizeRelativePath(relativePath);
    return prefixedArchivePath(m_archiveRootPrefix, normalized);
}

bool SourcePackage::existsRelative(const QString &relativePath) const
{
    if (!isOpen()) {
        return false;
    }

    if (m_kind == SourceKind::Directory) {
        QString resolvedPath;
        if (!resolveDirectoryMemberPath(m_inputPath,
                                        relativePath,
                                        nullptr,
                                        &resolvedPath,
                                        nullptr)) {
            return false;
        }
        return QFileInfo::exists(resolvedPath);
    }

    const QString normalized = normalizeRelativePath(relativePath);
    if (m_kind == SourceKind::ZipArchive) {
        return m_zipArchive.containsNormalizedPath(archiveMemberPath(normalized));
    }

    return false;
}

QByteArray SourcePackage::readBytesRelative(const QString &relativePath, QString *errorMessage) const
{
    if (!isOpen()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Source package is not open");
        }
        return {};
    }

    if (m_kind == SourceKind::Directory) {
        QString resolvedPath;
        if (!resolveDirectoryMemberPath(m_inputPath,
                                        relativePath,
                                        nullptr,
                                        &resolvedPath,
                                        errorMessage)) {
            return {};
        }
        QFile file(resolvedPath);
        if (!file.open(QIODevice::ReadOnly)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Failed to open %1: %2")
                                    .arg(file.fileName(), file.errorString());
            }
            return {};
        }
        return file.readAll();
    }

    const QString normalized = normalizeRelativePath(relativePath);
    if (m_kind == SourceKind::ZipArchive) {
        return m_zipArchive.readBytesByNormalizedPath(archiveMemberPath(normalized), errorMessage);
    }

    return {};
}

QString SourcePackage::readTextRelative(const QString &relativePath, QString *errorMessage) const
{
    if (!isOpen()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Source package is not open");
        }
        return {};
    }

    QString normalized = normalizeRelativePath(relativePath);
    QString resolvedPath;
    if (m_kind == SourceKind::Directory) {
        if (!resolveDirectoryMemberPath(m_inputPath,
                                        relativePath,
                                        &normalized,
                                        &resolvedPath,
                                        errorMessage)) {
            return {};
        }
    }
    if (m_textCache.contains(normalized)) {
        return m_textCache.value(normalized);
    }

    QString text;
    if (m_kind == SourceKind::Directory) {
        QFile file(resolvedPath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Failed to open %1: %2")
                                    .arg(file.fileName(), file.errorString());
            }
            return {};
        }
        text = QString::fromUtf8(file.readAll());
    } else if (m_kind == SourceKind::ZipArchive) {
        text = m_zipArchive.readTextByNormalizedPath(archiveMemberPath(normalized), errorMessage);
        if (text.isNull()) {
            return {};
        }
    }

    m_textCache.insert(normalized, text);
    return text;
}

QStringList SourcePackage::listRelativeFileEntries() const
{
    QStringList entries;
    if (!isOpen()) {
        return entries;
    }

    if (m_kind == SourceKind::Directory) {
        QDirIterator it(m_inputPath,
                        QDir::Files | QDir::NoDotAndDotDot | QDir::NoSymLinks,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            const QString relative = QDir(m_inputPath).relativeFilePath(it.filePath());
            entries.append(QDir::fromNativeSeparators(relative));
        }
        return entries;
    }

    if (m_kind == SourceKind::ZipArchive) {
        for (quint64 i = 0; i < m_zipArchive.entryCount(); ++i) {
            if (m_zipArchive.entryIsDirectory(i)) {
                continue;
            }

            const QString entry = QDir::cleanPath(m_zipArchive.normalizedEntryAt(i));
            if (entry.isEmpty()) {
                continue;
            }

            QString relative = entry;
            if (!m_archiveRootPrefix.isEmpty()) {
                if (entry == m_archiveRootPrefix) {
                    continue;
                }
                const QString prefixed = QStringLiteral("%1/").arg(m_archiveRootPrefix);
                if (!entry.startsWith(prefixed)) {
                    continue;
                }
                relative = entry.mid(prefixed.size());
            }

            if (relative.isEmpty() || relative.endsWith(QLatin1Char('/'))) {
                continue;
            }
            entries.append(relative);
        }
        entries.removeDuplicates();
    }

    return entries;
}

SourceInspection SourcePackage::inspectBasic(QString *errorMessage) const
{
    if (m_basicInspectionCached) {
        return m_cachedBasicInspection;
    }

    SourceInspection inspection;
    inspection.kind = m_kind;
    inspection.inputPath = m_inputPath;
    inspection.rootName = m_rootName;

    if (!isOpen()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Source package is not open");
        }
        return inspection;
    }

    inspection.evidence.append(QStringLiteral("Input kind: %1").arg(sourceKindToString(m_kind)));
    inspection.evidence.append(QStringLiteral("Root name: %1").arg(m_rootName));

    inspection.hasLdLayout = existsRelative(QStringLiteral("src/binutils/ld/ldlang.c"))
                             && existsRelative(QStringLiteral("src/binutils/ld/lexsup.c"))
                             && existsRelative(QStringLiteral("src/binutils/ld/ld.h"))
                             && existsRelative(QStringLiteral("src/binutils/ld/ldlex.h"));
    if (inspection.hasLdLayout) {
        inspection.evidence.append(QStringLiteral("Found expected src/binutils/ld layout"));
    } else {
        inspection.warnings.append(QStringLiteral("Expected src/binutils/ld layout is incomplete"));
    }

    QString localError;
    const QString buildCommon = readTextRelative(QStringLiteral("build-common.sh"), &localError);
    if (!buildCommon.isEmpty()) {
        inspection.productName = extractFirstMatch(
            buildCommon,
            QRegularExpression(QStringLiteral("PKGROOTNAME=\"([^\"]+)\"")));
        inspection.releaseLevel = extractFirstMatch(
            buildCommon,
            QRegularExpression(QStringLiteral("RELEASEVER=([A-Za-z0-9_.-]+)")));

        if (!inspection.productName.isEmpty()) {
            inspection.evidence.append(QStringLiteral("Product marker: %1").arg(inspection.productName));
        }
        if (!inspection.releaseLevel.isEmpty()) {
            inspection.evidence.append(QStringLiteral("Release level: %1").arg(inspection.releaseLevel));
        }
    } else if (!localError.isEmpty()) {
        inspection.warnings.append(localError);
    }

    localError.clear();
    const QString baseVer = readTextRelative(QStringLiteral("src/gcc/gcc/BASE-VER"), &localError);
    if (!baseVer.isEmpty()) {
        inspection.gccBaseVersion = baseVer.trimmed();
        inspection.evidence.append(QStringLiteral("BASE-VER: %1").arg(inspection.gccBaseVersion));
    } else if (!localError.isEmpty()) {
        inspection.warnings.append(localError);
    }

    inspection.inferredRef = inferRefFromVersion(inspection.gccBaseVersion, inspection.releaseLevel);
    if (!inspection.inferredRef.isEmpty()) {
        inspection.evidence.append(QStringLiteral("Inferred ref: %1").arg(inspection.inferredRef));
    }

    if (inspection.productName.contains(QStringLiteral("GNU Tools for STM32"), Qt::CaseInsensitive)) {
        inspection.evidence.append(QStringLiteral("Family marker suggests ST GNU Tools for STM32"));
    }

    m_cachedBasicInspection = inspection;
    m_basicInspectionCached = true;
    return inspection;
}

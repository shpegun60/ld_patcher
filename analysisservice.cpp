#include "analysisservice.h"

#include "catalogloader.h"
#include "detector.h"
#include "sourcepackage.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <utility>

namespace {

bool isCancelled(const AnalysisService::CancelToken &cancelToken)
{
    return cancelToken && cancelToken->load();
}

void reportProgress(const AnalysisService::ProgressCallback &progressCallback,
                    int percent,
                    const QString &message)
{
    if (progressCallback) {
        progressCallback(percent, message);
    }
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

QString summarizeRecipe(const QString &id, const QHash<QString, RecipeSummary> &recipes)
{
    if (!recipes.contains(id)) {
        return QStringLiteral("%1 [missing in catalog]").arg(id);
    }

    const RecipeSummary summary = recipes.value(id);
    const QString enabledText = summary.enabled ? QStringLiteral("enabled")
                                                : QStringLiteral("disabled");
    return QStringLiteral("%1 [%2, %3]")
        .arg(summary.displayName, summary.status, enabledText);
}

QStringList summarizeRecipes(const QStringList &ids, const QHash<QString, RecipeSummary> &recipes)
{
    QStringList lines;
    for (const QString &id : ids) {
        lines.append(summarizeRecipe(id, recipes));
    }
    return lines;
}

void fillSelectedProfileDetails(AnalysisResult *result,
                                const CatalogData &catalog,
                                const VersionProfile *profile)
{
    result->selectedProfile = VersionProfile();
    result->hasSelectedProfile = false;
    result->patchRecipeSummary.clear();
    result->buildRecipeSummaries.clear();
    result->verifyRecipeSummaries.clear();

    if (!profile) {
        return;
    }

    result->selectedProfile = *profile;
    result->hasSelectedProfile = true;
    result->patchRecipeSummary = summarizeRecipe(profile->patchRecipeId, catalog.patchRecipes);
    result->buildRecipeSummaries = summarizeRecipes(profile->buildRecipeIds, catalog.buildRecipes);
    result->verifyRecipeSummaries = summarizeRecipes(profile->verifyRecipeIds,
                                                     catalog.verifyRecipes);
}

SourcePreparationResult prepareSourceRoot(const CatalogData &catalog,
                                          const SourcePackage &source)
{
    Q_UNUSED(catalog);
    SourcePreparationResult result;
    result.kind = source.kind();
    result.inputPath = source.inputPath();
    result.rootName = source.rootName();

    if (source.kind() == SourceKind::Directory) {
        result.ok = true;
        result.workingRootPath = source.inputPath();
        result.messages.append(QStringLiteral("Directory input does not need extraction."));
        return result;
    }

    if (source.kind() != SourceKind::ZipArchive) {
        result.errorMessage = QStringLiteral("Unsupported source kind for preparation.");
        return result;
    }

    result.ok = true;
    result.workingRootPath = source.inputPath();
    result.messages.append(QStringLiteral(
        "ZIP input is validated directly from the archive through the libzip-backed ZIP reader."));
    result.messages.append(QStringLiteral(
        "Full extract-to-workdir is reserved for future apply/build steps, where mutable files are needed."));
    return result;
}

int countRegexMatches(const QString &text, const QRegularExpression &regex)
{
    int count = 0;
    QRegularExpressionMatchIterator it = regex.globalMatch(text);
    while (it.hasNext()) {
        it.next();
        ++count;
    }
    return count;
}

ValidationCheckResult makeCheckResult(bool passed,
                                      bool blocking,
                                      const QString &description,
                                      const QString &detail)
{
    ValidationCheckResult result;
    result.passed = passed;
    result.blocking = blocking;
    result.description = description;
    result.detail = detail;
    return result;
}

bool hasBlockingFailures(const QVector<ValidationCheckResult> &checks)
{
    for (const ValidationCheckResult &check : checks) {
        if (!check.passed && check.blocking) {
            return true;
        }
    }
    return false;
}

void appendNonBlockingFailuresAsWarnings(const QVector<ValidationCheckResult> &checks,
                                         const QString &prefix,
                                         QStringList *warnings)
{
    if (!warnings) {
        return;
    }

    for (const ValidationCheckResult &check : checks) {
        if (!check.passed && !check.blocking) {
            warnings->append(QStringLiteral("%1%2: %3")
                                 .arg(prefix, check.description, check.detail));
        }
    }
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

void appendPatchPackageChecks(const CatalogData &catalog,
                              const PatchRecipeData &recipe,
                              PatchValidationDetails *details)
{
    const QStringList candidates = candidatePatchPackageRoots(catalog, recipe);
    const QString patchRoot = resolvePatchPackageRoot(catalog, recipe);
    details->patchPackageRoot = patchRoot;
    const bool rootExists = QFileInfo(patchRoot).isDir();
    details->checks.append(makeCheckResult(rootExists,
                                           true,
                                           QStringLiteral("Patch package root exists"),
                                           rootExists
                                               ? patchRoot
                                               : candidates.join(QStringLiteral(" | "))));

    for (const QString &relativePath : recipe.patchPackageRequiredFiles) {
        const QString fullPath = QDir(patchRoot).filePath(relativePath);
        const bool exists = QFileInfo::exists(fullPath);
        details->checks.append(makeCheckResult(exists,
                                               true,
                                               QStringLiteral("Patch package file exists: %1")
                                                   .arg(relativePath),
                                               fullPath));
    }
}

QString sourceLocationText(const SourcePackage &source, const QString &relativePath)
{
    if (source.kind() == SourceKind::Directory) {
        return QDir(source.inputPath()).filePath(relativePath);
    }
    return QStringLiteral("%1::%2").arg(source.inputPath(), relativePath);
}

ValidationCheckResult evaluateApplicabilityCheck(const SourcePackage &source,
                                                 const ApplicabilityCheck &check)
{
    const QString location = sourceLocationText(source, check.path);
    const QString description = check.description.isEmpty()
                                    ? QStringLiteral("%1 [%2]").arg(check.type, check.path)
                                    : check.description;

    if (check.type == QStringLiteral("file_exists")
        || check.type == QStringLiteral("path_exists")) {
        const bool exists = source.existsRelative(check.path);
        return makeCheckResult(exists, check.blocking, description, location);
    }

    QString readError;
    const QString text = source.readTextRelative(check.path, &readError);
    if (text.isNull()) {
        return makeCheckResult(false,
                               check.blocking,
                               description,
                               readError.isEmpty() ? location : readError);
    }
    const QRegularExpression regex(check.regex, QRegularExpression::MultilineOption);
    if (!regex.isValid()) {
        return makeCheckResult(false,
                               check.blocking,
                               description,
                               QStringLiteral("Invalid regex '%1': %2")
                                   .arg(check.regex, regex.errorString()));
    }
    const int matchCount = countRegexMatches(text, regex);
    const int expectedCount = check.expectedCount;

    if (check.type == QStringLiteral("regex_present")) {
        const bool passed = expectedCount >= 0 ? matchCount == expectedCount : matchCount > 0;
        const QString detail = expectedCount >= 0
                                   ? QStringLiteral("%1 (matches=%2, expected=%3)")
                                         .arg(location)
                                         .arg(matchCount)
                                         .arg(expectedCount)
                                   : QStringLiteral("%1 (matches=%2)").arg(location).arg(matchCount);
        return makeCheckResult(passed, check.blocking, description, detail);
    }

    if (check.type == QStringLiteral("regex_absent")) {
        const bool passed = matchCount == 0;
        const QString detail = QStringLiteral("%1 (matches=%2)").arg(location).arg(matchCount);
        return makeCheckResult(passed, check.blocking, description, detail);
    }

    return makeCheckResult(false,
                           check.blocking,
                           description,
                           QStringLiteral("Unsupported check type: %1").arg(check.type));
}

PatchValidationDetails validatePatchRecipe(const CatalogData &catalog,
                                           const VersionProfile &profile,
                                           const PatchRecipeData &recipe,
                                           const SourcePackage &source,
                                           const QString &sourceRootPath,
                                           const AnalysisService::ProgressCallback &progressCallback,
                                           const AnalysisService::CancelToken &cancelToken)
{
    PatchValidationDetails details;
    details.ok = true;
    details.recipeId = recipe.id;
    details.recipeDisplayName = recipe.displayName;
    details.sourceRootPath = sourceRootPath;
    details.postApplyContractSatisfied = recipe.postApplyChecks.isEmpty();

    if (!recipe.supportedFamilies.isEmpty()
        && !recipe.supportedFamilies.contains(profile.family, Qt::CaseInsensitive)) {
        details.checks.append(makeCheckResult(false,
                                              true,
                                              QStringLiteral("Patch recipe supports the selected profile family"),
                                              QStringLiteral("profile=%1, recipe_supported_families=%2")
                                                  .arg(profile.family,
                                                       recipe.supportedFamilies.join(QStringLiteral(", ")))));
    }

    appendPatchPackageChecks(catalog, recipe, &details);

    const int totalValidationUnits = recipe.requiredFiles.size()
                                     + recipe.applicabilityChecks.size()
                                     + recipe.idempotencyRules.size()
                                     + recipe.postApplyChecks.size() + 1;
    int completedUnits = 1;
    reportProgress(progressCallback,
                   80,
                   QStringLiteral("Checking patch package and source anchors..."));

    for (const QString &requiredFile : recipe.requiredFiles) {
        if (isCancelled(cancelToken)) {
            details.ok = false;
            details.errorMessage = QStringLiteral("Validation cancelled.");
            return details;
        }
        const QString fullPath = sourceLocationText(source, requiredFile);
        const bool exists = source.existsRelative(requiredFile);
        details.checks.append(makeCheckResult(exists,
                                              true,
                                              QStringLiteral("Required source file exists: %1")
                                                  .arg(requiredFile),
                                              fullPath));
        ++completedUnits;
        reportProgress(progressCallback,
                       80 + ((completedUnits * 20) / qMax(1, totalValidationUnits)),
                       QStringLiteral("Checking required source files..."));
    }

    for (const ApplicabilityCheck &check : recipe.applicabilityChecks) {
        if (isCancelled(cancelToken)) {
            details.ok = false;
            details.errorMessage = QStringLiteral("Validation cancelled.");
            return details;
        }
        details.checks.append(evaluateApplicabilityCheck(source, check));
        ++completedUnits;
        reportProgress(progressCallback,
                       80 + ((completedUnits * 20) / qMax(1, totalValidationUnits)),
                       QStringLiteral("Checking patch anchors..."));
    }

    for (const ApplicabilityCheck &check : recipe.idempotencyRules) {
        if (isCancelled(cancelToken)) {
            details.ok = false;
            details.errorMessage = QStringLiteral("Validation cancelled.");
            return details;
        }
        details.idempotencyChecks.append(evaluateApplicabilityCheck(source, check));
        ++completedUnits;
        reportProgress(progressCallback,
                       80 + ((completedUnits * 20) / qMax(1, totalValidationUnits)),
                       QStringLiteral("Checking already-patched markers..."));
    }

    const bool blockingFailure = hasBlockingFailures(details.checks);
    appendNonBlockingFailuresAsWarnings(details.checks, QString(), &details.warnings);

    if (!details.idempotencyChecks.isEmpty()) {
        int passedIdempotencyChecks = 0;
        for (const ValidationCheckResult &check : std::as_const(details.idempotencyChecks)) {
            if (check.passed) {
                ++passedIdempotencyChecks;
            }
        }

        details.alreadyPatched = passedIdempotencyChecks == details.idempotencyChecks.size();
        if (passedIdempotencyChecks > 0 && !details.alreadyPatched) {
            details.warnings.append(QStringLiteral(
                "Patch markers are partially present; the source tree may already be partially patched."));
        }
    }

    if (details.alreadyPatched && blockingFailure) {
        details.warnings.append(QStringLiteral(
            "Some pristine-source applicability checks now fail because the patch is already present. "
            "The tree is treated as applicable in its already-patched state."));
    }

    if (details.alreadyPatched) {
        for (const ApplicabilityCheck &check : recipe.postApplyChecks) {
            if (isCancelled(cancelToken)) {
                details.ok = false;
                details.errorMessage = QStringLiteral("Validation cancelled.");
                return details;
            }
            details.postApplyChecks.append(evaluateApplicabilityCheck(source, check));
            ++completedUnits;
            reportProgress(progressCallback,
                           80 + ((completedUnits * 20) / qMax(1, totalValidationUnits)),
                           QStringLiteral("Checking post-apply contract..."));
        }

        appendNonBlockingFailuresAsWarnings(details.postApplyChecks,
                                            QStringLiteral("Post-apply check: "),
                                            &details.warnings);
        details.postApplyContractSatisfied = !hasBlockingFailures(details.postApplyChecks);
        if (!details.postApplyContractSatisfied) {
            details.warnings.append(QStringLiteral(
                "One or more blocking post-apply checks failed on the already-patched tree."));
        }
    } else {
        completedUnits += recipe.postApplyChecks.size();
    }

    details.applicable = !blockingFailure || details.alreadyPatched;
    if (details.alreadyPatched && !details.postApplyContractSatisfied) {
        details.applicable = false;
    }
    details.supportLevel = details.applicable
                               ? (!profile.status.isEmpty() ? profile.status : recipe.status)
                               : QStringLiteral("unsupported");
    reportProgress(progressCallback, 100, QStringLiteral("Validation checks completed."));
    return details;
}

} // namespace

AnalysisResult AnalysisService::analyzePath(const CatalogData &catalog,
                                            const QString &path,
                                            const ProgressCallback &progressCallback,
                                            const CancelToken &cancelToken)
{
    AnalysisResult result;

    if (catalog.isEmpty()) {
        result.errorMessage = QStringLiteral("Catalog is empty");
        return result;
    }

    if (isCancelled(cancelToken)) {
        result.errorMessage = QStringLiteral("Analysis cancelled.");
        return result;
    }

    reportProgress(progressCallback, 5, QStringLiteral("Opening source package..."));
    SourcePackage source;
    QString errorMessage;
    if (!source.open(path, &errorMessage)) {
        result.errorMessage = errorMessage;
        return result;
    }

    if (isCancelled(cancelToken)) {
        result.errorMessage = QStringLiteral("Analysis cancelled.");
        return result;
    }

    reportProgress(progressCallback, 35, QStringLiteral("Inspecting source package..."));
    result.inspection = source.inspectBasic(&errorMessage);
    if (!errorMessage.isEmpty()) {
        if (!result.errorMessage.isEmpty()) {
            result.errorMessage += QStringLiteral("\n");
        }
        result.errorMessage += errorMessage;
    }

    if (isCancelled(cancelToken)) {
        result.errorMessage = QStringLiteral("Analysis cancelled.");
        return result;
    }

    errorMessage.clear();
    reportProgress(progressCallback, 70, QStringLiteral("Matching source package against catalog profiles..."));
    result.match = Detector::matchBestProfile(source, catalog, &errorMessage);
    if (!errorMessage.isEmpty()) {
        if (!result.errorMessage.isEmpty()) {
            result.errorMessage += QStringLiteral("\n");
        }
        result.errorMessage += errorMessage;
    }

    if (isCancelled(cancelToken)) {
        result.errorMessage = QStringLiteral("Analysis cancelled.");
        return result;
    }

    const VersionProfile *profile = result.match.matched
                                        ? findProfileById(catalog, result.match.matchedProfileId)
                                        : nullptr;
    fillSelectedProfileDetails(&result, catalog, profile);

    result.ok = true;
    reportProgress(progressCallback, 100, QStringLiteral("Analysis completed."));
    return result;
}

ValidationResult AnalysisService::validatePath(const CatalogData &catalog,
                                               const QString &path,
                                               const QString &selectedProfileId,
                                               const ProgressCallback &progressCallback,
                                               const CancelToken &cancelToken)
{
    ValidationResult result;
    result.analysis = analyzePath(
        catalog,
        path,
        [progressCallback](int percent, const QString &message) {
            reportProgress(progressCallback, qBound(0, (percent * 35) / 100, 35), message);
        },
        cancelToken);
    if (!result.analysis.ok) {
        result.errorMessage = result.analysis.errorMessage;
        return result;
    }

    if (isCancelled(cancelToken)) {
        result.errorMessage = QStringLiteral("Validation cancelled.");
        return result;
    }

    reportProgress(progressCallback, 45, QStringLiteral("Resolving selected profile..."));
    const VersionProfile *selectedProfile = nullptr;
    if (!selectedProfileId.isEmpty()) {
        selectedProfile = findProfileById(catalog, selectedProfileId);
        if (!selectedProfile) {
            result.errorMessage = QStringLiteral("Selected profile was not found in the catalog.");
            return result;
        }
        if (!selectedProfile->enabled) {
            result.errorMessage = QStringLiteral("Selected profile is disabled and cannot be used.");
            return result;
        }
        fillSelectedProfileDetails(&result.analysis, catalog, selectedProfile);
    } else if (result.analysis.hasSelectedProfile) {
        selectedProfile = &result.analysis.selectedProfile;
    }

    if (!selectedProfile) {
        result.errorMessage = QStringLiteral(
            "No verified profile match is selected, so validation cannot continue.");
        return result;
    }

    if (isCancelled(cancelToken)) {
        result.errorMessage = QStringLiteral("Validation cancelled.");
        return result;
    }

    SourcePackage source;
    QString errorMessage;
    reportProgress(progressCallback, 55, QStringLiteral("Opening selected source for validation..."));
    if (!source.open(path, &errorMessage)) {
        result.errorMessage = errorMessage;
        return result;
    }

    if (isCancelled(cancelToken)) {
        result.errorMessage = QStringLiteral("Validation cancelled.");
        return result;
    }

    reportProgress(progressCallback, 65, QStringLiteral("Preparing validation input..."));
    result.preparation = prepareSourceRoot(catalog, source);
    if (!result.preparation.ok) {
        result.errorMessage = result.preparation.errorMessage;
        return result;
    }

    if (isCancelled(cancelToken)) {
        result.errorMessage = QStringLiteral("Validation cancelled.");
        return result;
    }

    PatchRecipeData recipe;
    reportProgress(progressCallback, 75, QStringLiteral("Loading patch recipe..."));
    if (!CatalogLoader::loadPatchRecipe(catalog,
                                        selectedProfile->patchRecipeId,
                                        &recipe,
                                        &errorMessage)) {
        result.errorMessage = errorMessage;
        return result;
    }

    result.validation = validatePatchRecipe(catalog,
                                            *selectedProfile,
                                            recipe,
                                            source,
                                            result.preparation.workingRootPath,
                                            progressCallback,
                                            cancelToken);
    if (!result.validation.ok) {
        result.errorMessage = result.validation.errorMessage.isEmpty()
                                  ? QStringLiteral("Validation failed.")
                                  : result.validation.errorMessage;
        return result;
    }
    result.ok = true;
    return result;
}

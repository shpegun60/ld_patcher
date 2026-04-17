#include "catalogloader.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

QString readFileText(const QString &path, QString *errorMessage)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to open %1: %2").arg(path, file.errorString());
        }
        return QString();
    }

    return QString::fromUtf8(file.readAll());
}

QJsonObject readJsonObject(const QString &path, QString *errorMessage)
{
    const QString text = readFileText(path, errorMessage);
    if (text.isNull()) {
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to parse JSON object %1: %2")
                                .arg(path, parseError.errorString());
        }
        return {};
    }

    return doc.object();
}

bool writeJsonObject(const QString &path, const QJsonObject &object, QString *errorMessage)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to write %1: %2").arg(path, file.errorString());
        }
        return false;
    }

    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to write full JSON payload into %1").arg(path);
        }
        return false;
    }

    return true;
}

RecipeSummary parseRecipeSummary(const QString &filePath, const QJsonObject &object)
{
    RecipeSummary summary;
    summary.enabled = object.value(QStringLiteral("enabled")).toBool(true);
    summary.id = object.value(QStringLiteral("id")).toString();
    summary.displayName = object.value(QStringLiteral("display_name")).toString();
    summary.status = object.value(QStringLiteral("status")).toString();
    summary.schemaType = object.value(QStringLiteral("schema_type")).toString();
    summary.filePath = filePath;
    return summary;
}

QStringList parseStringArray(const QJsonObject &object, const QString &key)
{
    QStringList values;
    for (const QJsonValue &value : object.value(key).toArray()) {
        const QString text = value.toString();
        if (!text.isEmpty()) {
            values.append(text);
        }
    }
    return values;
}

VersionProfile parseProfile(const QString &filePath, const QJsonObject &object)
{
    VersionProfile profile;
    profile.enabled = object.value(QStringLiteral("enabled")).toBool(true);
    profile.filePath = filePath;
    profile.id = object.value(QStringLiteral("id")).toString();
    profile.displayName = object.value(QStringLiteral("display_name")).toString();
    profile.family = object.value(QStringLiteral("family")).toString();
    profile.patchRecipeId = object.value(QStringLiteral("patch_recipe_id")).toString();
    profile.status = object.value(QStringLiteral("status")).toString();
    profile.priority = object.value(QStringLiteral("priority")).toInt();

    const QJsonObject selectors = object.value(QStringLiteral("selectors")).toObject();
    for (const QJsonValue &value : selectors.value(QStringLiteral("tags")).toArray()) {
        profile.tags.append(value.toString());
    }
    for (const QJsonValue &value : selectors.value(QStringLiteral("archive_roots")).toArray()) {
        profile.archiveRoots.append(value.toString());
    }
    for (const QJsonValue &value : selectors.value(QStringLiteral("folder_name_patterns")).toArray()) {
        profile.folderNamePatterns.append(value.toString());
    }

    for (const QJsonValue &value : object.value(QStringLiteral("detection_hints")).toArray()) {
        const QJsonObject hintObject = value.toObject();
        DetectionHint hint;
        hint.type = hintObject.value(QStringLiteral("type")).toString();
        hint.path = hintObject.value(QStringLiteral("path")).toString();
        hint.regex = hintObject.value(QStringLiteral("regex")).toString();
        hint.expectedText = hintObject.value(QStringLiteral("expected_text")).toString();
        hint.description = hintObject.value(QStringLiteral("description")).toString();
        profile.detectionHints.append(hint);
    }

    for (const QJsonValue &value : object.value(QStringLiteral("build_recipe_ids")).toArray()) {
        profile.buildRecipeIds.append(value.toString());
    }
    for (const QJsonValue &value : object.value(QStringLiteral("verify_recipe_ids")).toArray()) {
        profile.verifyRecipeIds.append(value.toString());
    }
    for (const QJsonValue &value : object.value(QStringLiteral("notes")).toArray()) {
        profile.notes.append(value.toString());
    }

    return profile;
}

ApplicabilityCheck parseApplicabilityCheck(const QJsonObject &object)
{
    ApplicabilityCheck check;
    check.type = object.value(QStringLiteral("type")).toString();
    check.path = object.value(QStringLiteral("path")).toString();
    check.regex = object.value(QStringLiteral("regex")).toString();
    check.expectedCount = object.contains(QStringLiteral("expected_count"))
                              ? object.value(QStringLiteral("expected_count")).toInt(-1)
                              : -1;
    check.blocking = object.value(QStringLiteral("blocking")).toBool(false);
    check.description = object.value(QStringLiteral("description")).toString();
    return check;
}

PatchOperation parsePatchOperation(const QJsonObject &object)
{
    PatchOperation operation;
    operation.type = object.value(QStringLiteral("type")).toString();
    operation.targetPath = object.value(QStringLiteral("target_path")).toString();
    operation.sourcePath = object.value(QStringLiteral("source_path")).toString();
    operation.matchRegex = object.value(QStringLiteral("match_regex")).toString();
    operation.insertMode = object.value(QStringLiteral("insert_mode")).toString();
    operation.expectedMatchCount = object.contains(QStringLiteral("expected_match_count"))
                                       ? object.value(QStringLiteral("expected_match_count")).toInt(-1)
                                       : -1;
    operation.description = object.value(QStringLiteral("description")).toString();
    return operation;
}

BuildLogParsingRule parseBuildLogParsingRule(const QJsonObject &object)
{
    BuildLogParsingRule rule;
    rule.type = object.value(QStringLiteral("type")).toString();
    rule.regex = object.value(QStringLiteral("regex")).toString();
    rule.description = object.value(QStringLiteral("description")).toString();
    return rule;
}

PatchRecipeData parsePatchRecipe(const QString &filePath, const QJsonObject &object)
{
    PatchRecipeData recipe;
    recipe.enabled = object.value(QStringLiteral("enabled")).toBool(true);
    recipe.filePath = filePath;
    recipe.id = object.value(QStringLiteral("id")).toString();
    recipe.displayName = object.value(QStringLiteral("display_name")).toString();
    recipe.description = object.value(QStringLiteral("description")).toString();
    recipe.status = object.value(QStringLiteral("status")).toString();
    for (const QJsonValue &value : object.value(QStringLiteral("supported_families")).toArray()) {
        recipe.supportedFamilies.append(value.toString());
    }

    const QJsonObject patchPackage = object.value(QStringLiteral("patch_package")).toObject();
    recipe.patchPackageId = patchPackage.value(QStringLiteral("id")).toString();
    recipe.patchPackageRootHint = patchPackage.value(QStringLiteral("root_hint")).toString();
    for (const QJsonValue &value : patchPackage.value(QStringLiteral("root_hints")).toArray()) {
        const QString hint = value.toString();
        if (!hint.isEmpty() && !recipe.patchPackageRootHints.contains(hint)) {
            recipe.patchPackageRootHints.append(hint);
        }
    }
    if (!recipe.patchPackageRootHint.isEmpty()
        && !recipe.patchPackageRootHints.contains(recipe.patchPackageRootHint)) {
        recipe.patchPackageRootHints.append(recipe.patchPackageRootHint);
    }
    for (const QJsonValue &value : patchPackage.value(QStringLiteral("required_files")).toArray()) {
        recipe.patchPackageRequiredFiles.append(value.toString());
    }

    for (const QJsonValue &value : object.value(QStringLiteral("required_files")).toArray()) {
        recipe.requiredFiles.append(value.toString());
    }
    for (const QJsonValue &value : object.value(QStringLiteral("optional_files")).toArray()) {
        recipe.optionalFiles.append(value.toString());
    }
    for (const QJsonValue &value : object.value(QStringLiteral("applicability_checks")).toArray()) {
        recipe.applicabilityChecks.append(parseApplicabilityCheck(value.toObject()));
    }
    for (const QJsonValue &value : object.value(QStringLiteral("operations")).toArray()) {
        recipe.operations.append(parsePatchOperation(value.toObject()));
    }
    const QJsonObject backupPolicy = object.value(QStringLiteral("backup_policy")).toObject();
    recipe.backupMode = backupPolicy.value(QStringLiteral("mode")).toString();
    recipe.backupSuffix = backupPolicy.value(QStringLiteral("suffix")).toString();
    for (const QJsonValue &value : object.value(QStringLiteral("idempotency_rules")).toArray()) {
        recipe.idempotencyRules.append(parseApplicabilityCheck(value.toObject()));
    }
    for (const QJsonValue &value : object.value(QStringLiteral("post_apply_checks")).toArray()) {
        recipe.postApplyChecks.append(parseApplicabilityCheck(value.toObject()));
    }
    for (const QJsonValue &value : object.value(QStringLiteral("notes")).toArray()) {
        recipe.notes.append(value.toString());
    }

    return recipe;
}

BuildRecipeData parseBuildRecipe(const QString &filePath, const QJsonObject &object)
{
    BuildRecipeData recipe;
    recipe.enabled = object.value(QStringLiteral("enabled")).toBool(true);
    recipe.filePath = filePath;
    recipe.id = object.value(QStringLiteral("id")).toString();
    recipe.displayName = object.value(QStringLiteral("display_name")).toString();
    recipe.description = object.value(QStringLiteral("description")).toString();
    recipe.status = object.value(QStringLiteral("status")).toString();

    const QJsonObject environment = object.value(QStringLiteral("environment")).toObject();
    recipe.environmentOs = environment.value(QStringLiteral("os")).toString();
    recipe.environmentShell = environment.value(QStringLiteral("shell")).toString();
    recipe.environmentHostTriplet = environment.value(QStringLiteral("host_triplet")).toString();
    recipe.environmentTargetTriplet = environment.value(QStringLiteral("target_triplet")).toString();

    recipe.workingDirectoryTemplate = object.value(QStringLiteral("working_directory_template")).toString();
    recipe.scriptRef = object.value(QStringLiteral("script_ref")).toString();
    recipe.cleanCommand = parseStringArray(object, QStringLiteral("clean_command"));
    recipe.configureCommand = parseStringArray(object, QStringLiteral("configure_command"));
    recipe.buildCommand = parseStringArray(object, QStringLiteral("build_command"));
    recipe.requiredTools = parseStringArray(object, QStringLiteral("required_tools"));
    recipe.expectedOutputs = parseStringArray(object, QStringLiteral("expected_outputs"));
    for (const QJsonValue &value : object.value(QStringLiteral("log_parsing_rules")).toArray()) {
        recipe.logParsingRules.append(parseBuildLogParsingRule(value.toObject()));
    }

    const QJsonObject timeouts = object.value(QStringLiteral("timeouts_seconds")).toObject();
    recipe.buildTimeoutSeconds = timeouts.value(QStringLiteral("build")).toInt();

    const QJsonObject artifactCollection = object.value(QStringLiteral("artifact_collection")).toObject();
    recipe.artifactDropDir = artifactCollection.value(QStringLiteral("drop_dir")).toString();
    recipe.platformNotes = parseStringArray(object, QStringLiteral("platform_notes"));
    recipe.notes = parseStringArray(object, QStringLiteral("notes"));
    return recipe;
}

VerifyCheck parseVerifyCheck(const QJsonObject &object)
{
    VerifyCheck check;
    check.type = object.value(QStringLiteral("type")).toString();
    check.path = object.value(QStringLiteral("path")).toString();
    check.command = parseStringArray(object, QStringLiteral("command"));
    check.regex = object.value(QStringLiteral("regex")).toString();
    check.scriptRef = object.value(QStringLiteral("script_ref")).toString();
    check.description = object.value(QStringLiteral("description")).toString();
    return check;
}

VerifyRecipeData parseVerifyRecipe(const QString &filePath, const QJsonObject &object)
{
    VerifyRecipeData recipe;
    recipe.enabled = object.value(QStringLiteral("enabled")).toBool(true);
    recipe.filePath = filePath;
    recipe.id = object.value(QStringLiteral("id")).toString();
    recipe.displayName = object.value(QStringLiteral("display_name")).toString();
    recipe.description = object.value(QStringLiteral("description")).toString();
    recipe.status = object.value(QStringLiteral("status")).toString();
    for (const QJsonValue &value : object.value(QStringLiteral("checks")).toArray()) {
        recipe.checks.append(parseVerifyCheck(value.toObject()));
    }
    recipe.requiredInputs = parseStringArray(object, QStringLiteral("required_inputs"));
    const QJsonObject successPolicy = object.value(QStringLiteral("success_policy")).toObject();
    recipe.allChecksMustPass = successPolicy.value(QStringLiteral("all_checks_must_pass")).toBool(true);
    recipe.expectedSummaryFields = parseStringArray(successPolicy, QStringLiteral("expected_summary_fields"));
    recipe.resultArtifacts = parseStringArray(object, QStringLiteral("result_artifacts"));
    recipe.notes = parseStringArray(object, QStringLiteral("notes"));
    return recipe;
}

bool loadRecipeGroup(const QString &rootDir,
                     const QStringList &relativePaths,
                     QHash<QString, RecipeSummary> *target,
                     QString *errorMessage)
{
    for (const QString &relativePath : relativePaths) {
        const QString fullPath = QDir(rootDir).filePath(QStringLiteral("catalog/%1").arg(relativePath));
        if (!QFileInfo::exists(fullPath)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Catalog entry is missing on disk: %1").arg(fullPath);
            }
            return false;
        }
        const QJsonObject object = readJsonObject(fullPath, errorMessage);
        if (object.isEmpty()) {
            return false;
        }

        const RecipeSummary summary = parseRecipeSummary(relativePath, object);
        if (summary.id.isEmpty()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Recipe file %1 has no id").arg(fullPath);
            }
            return false;
        }
        if (target->contains(summary.id)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Duplicate recipe id '%1' found in %2")
                                    .arg(summary.id, fullPath);
            }
            return false;
        }
        target->insert(summary.id, summary);
    }

    return true;
}

} // namespace

QString CatalogLoader::findCatalogRoot(const QString &startDir)
{
    QDir dir(startDir);
    for (int i = 0; i < 8; ++i) {
        if (QFileInfo::exists(dir.filePath(QStringLiteral("catalog/catalog.json")))) {
            return dir.absolutePath();
        }
        if (!dir.cdUp()) {
            break;
        }
    }

    QDir appDir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 8; ++i) {
        if (QFileInfo::exists(appDir.filePath(QStringLiteral("catalog/catalog.json")))) {
            return appDir.absolutePath();
        }
        if (!appDir.cdUp()) {
            break;
        }
    }

    return {};
}

bool CatalogLoader::loadCatalog(const QString &catalogRoot, CatalogData *catalog, QString *errorMessage)
{
    if (!catalog) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Catalog output pointer is null");
        }
        return false;
    }

    const QString catalogPath = QDir(catalogRoot).filePath(QStringLiteral("catalog/catalog.json"));
    const QJsonObject root = readJsonObject(catalogPath, errorMessage);
    if (root.isEmpty()) {
        return false;
    }
    if (root.value(QStringLiteral("schema_type")).toString() != QStringLiteral("CatalogIndex")) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("catalog.json is not a CatalogIndex: %1").arg(catalogPath);
        }
        return false;
    }

    CatalogData result;
    result.rootDir = catalogRoot;
    result.catalogPath = catalogPath;

    const QStringList profilePaths = parseStringArray(root, QStringLiteral("profiles"));
    const QStringList patchRecipePaths = parseStringArray(root, QStringLiteral("patch_recipes"));
    const QStringList buildRecipePaths = parseStringArray(root, QStringLiteral("build_recipes"));
    const QStringList verifyRecipePaths = parseStringArray(root, QStringLiteral("verify_recipes"));

    if (profilePaths.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("catalog.json does not list any profiles");
        }
        return false;
    }

    for (const QString &relativePath : profilePaths) {
        const QString fullPath = QDir(catalogRoot).filePath(QStringLiteral("catalog/%1").arg(relativePath));
        if (!QFileInfo::exists(fullPath)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Catalog profile entry is missing on disk: %1").arg(fullPath);
            }
            return false;
        }
        const QJsonObject object = readJsonObject(fullPath, errorMessage);
        if (object.isEmpty()) {
            return false;
        }

        VersionProfile profile = parseProfile(relativePath, object);
        if (profile.id.isEmpty()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Profile file %1 has no id").arg(fullPath);
            }
            return false;
        }
        for (const VersionProfile &existing : std::as_const(result.profiles)) {
            if (existing.id == profile.id) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral("Duplicate profile id '%1' found in %2")
                                        .arg(profile.id, fullPath);
                }
                return false;
            }
        }
        result.profiles.append(profile);
    }

    if (!loadRecipeGroup(catalogRoot,
                         patchRecipePaths,
                         &result.patchRecipes,
                         errorMessage)) {
        return false;
    }
    if (!loadRecipeGroup(catalogRoot,
                         buildRecipePaths,
                         &result.buildRecipes,
                         errorMessage)) {
        return false;
    }
    if (!loadRecipeGroup(catalogRoot,
                         verifyRecipePaths,
                         &result.verifyRecipes,
                         errorMessage)) {
        return false;
    }

    *catalog = result;
    return true;
}

bool CatalogLoader::setProfileEnabled(const CatalogData &catalog,
                                      const QString &profileId,
                                      bool enabled,
                                      QString *errorMessage)
{
    const VersionProfile *targetProfile = nullptr;
    for (const VersionProfile &profile : catalog.profiles) {
        if (profile.id == profileId) {
            targetProfile = &profile;
            break;
        }
    }

    if (!targetProfile) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Profile not found in catalog: %1").arg(profileId);
        }
        return false;
    }

    const QString fullPath = QDir(catalog.rootDir)
                                 .filePath(QStringLiteral("catalog/%1").arg(targetProfile->filePath));
    QJsonObject object = readJsonObject(fullPath, errorMessage);
    if (object.isEmpty()) {
        return false;
    }

    object.insert(QStringLiteral("enabled"), enabled);
    return writeJsonObject(fullPath, object, errorMessage);
}

bool CatalogLoader::loadPatchRecipe(const CatalogData &catalog,
                                    const QString &recipeId,
                                    PatchRecipeData *recipe,
                                    QString *errorMessage)
{
    if (!recipe) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("PatchRecipe output pointer is null");
        }
        return false;
    }

    if (!catalog.patchRecipes.contains(recipeId)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Patch recipe not found in catalog: %1").arg(recipeId);
        }
        return false;
    }

    const RecipeSummary summary = catalog.patchRecipes.value(recipeId);
    if (!summary.enabled) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Patch recipe is disabled in catalog: %1").arg(recipeId);
        }
        return false;
    }
    const QString fullPath = QDir(catalog.rootDir).filePath(QStringLiteral("catalog/%1").arg(summary.filePath));
    const QJsonObject object = readJsonObject(fullPath, errorMessage);
    if (object.isEmpty()) {
        return false;
    }

    PatchRecipeData parsed = parsePatchRecipe(summary.filePath, object);
    if (parsed.id.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Patch recipe file %1 has no id").arg(fullPath);
        }
        return false;
    }

    *recipe = parsed;
    return true;
}

bool CatalogLoader::loadBuildRecipe(const CatalogData &catalog,
                                    const QString &recipeId,
                                    BuildRecipeData *recipe,
                                    QString *errorMessage)
{
    if (!recipe) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("BuildRecipe output pointer is null");
        }
        return false;
    }

    if (!catalog.buildRecipes.contains(recipeId)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Build recipe not found in catalog: %1").arg(recipeId);
        }
        return false;
    }

    const RecipeSummary summary = catalog.buildRecipes.value(recipeId);
    if (!summary.enabled) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Build recipe is disabled in catalog: %1").arg(recipeId);
        }
        return false;
    }

    const QString fullPath = QDir(catalog.rootDir).filePath(QStringLiteral("catalog/%1").arg(summary.filePath));
    const QJsonObject object = readJsonObject(fullPath, errorMessage);
    if (object.isEmpty()) {
        return false;
    }

    BuildRecipeData parsed = parseBuildRecipe(summary.filePath, object);
    if (parsed.id.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Build recipe file %1 has no id").arg(fullPath);
        }
        return false;
    }

    *recipe = parsed;
    return true;
}

bool CatalogLoader::loadVerifyRecipe(const CatalogData &catalog,
                                     const QString &recipeId,
                                     VerifyRecipeData *recipe,
                                     QString *errorMessage)
{
    if (!recipe) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("VerifyRecipe output pointer is null");
        }
        return false;
    }

    if (!catalog.verifyRecipes.contains(recipeId)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Verify recipe not found in catalog: %1").arg(recipeId);
        }
        return false;
    }

    const RecipeSummary summary = catalog.verifyRecipes.value(recipeId);
    if (!summary.enabled) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Verify recipe is disabled in catalog: %1").arg(recipeId);
        }
        return false;
    }

    const QString fullPath = QDir(catalog.rootDir).filePath(QStringLiteral("catalog/%1").arg(summary.filePath));
    const QJsonObject object = readJsonObject(fullPath, errorMessage);
    if (object.isEmpty()) {
        return false;
    }

    VerifyRecipeData parsed = parseVerifyRecipe(summary.filePath, object);
    if (parsed.id.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Verify recipe file %1 has no id").arg(fullPath);
        }
        return false;
    }

    *recipe = parsed;
    return true;
}

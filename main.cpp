#include "catalogloader.h"
#include "mainwindow.h"
#include "analysisservice.h"
#include "workflowservice.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QTextStream>

namespace {

int runCliDetect(const QString &inputPath)
{
    QTextStream out(stdout);
    QTextStream err(stderr);

    CatalogData catalog;
    QString errorMessage;
    const QString catalogRoot = CatalogLoader::findCatalogRoot(QDir::currentPath());
    if (catalogRoot.isEmpty() || !CatalogLoader::loadCatalog(catalogRoot, &catalog, &errorMessage)) {
        err << "Catalog load failed: "
            << (errorMessage.isEmpty() ? QStringLiteral("catalog root not found") : errorMessage)
            << Qt::endl;
        return 1;
    }

    const AnalysisResult analysis = AnalysisService::analyzePath(catalog, inputPath);
    if (!analysis.errorMessage.isEmpty()) {
        err << analysis.errorMessage << Qt::endl;
    }
    if (!analysis.ok) {
        return 2;
    }

    out << "input=" << analysis.inspection.inputPath << Qt::endl;
    out << "kind=" << sourceKindToString(analysis.inspection.kind) << Qt::endl;
    out << "root=" << analysis.inspection.rootName << Qt::endl;
    out << "product=" << analysis.inspection.productName << Qt::endl;
    out << "base_ver=" << analysis.inspection.gccBaseVersion << Qt::endl;
    out << "release_level=" << analysis.inspection.releaseLevel << Qt::endl;
    out << "inferred_ref=" << analysis.inspection.inferredRef << Qt::endl;
    out << "has_ld_layout=" << (analysis.inspection.hasLdLayout ? "true" : "false") << Qt::endl;
    out << "matched=" << (analysis.match.matched ? "true" : "false") << Qt::endl;
    out << "best_candidate_profile=" << analysis.match.bestCandidateProfileId << Qt::endl;
    out << "best_candidate_display_name=" << analysis.match.bestCandidateDisplayName << Qt::endl;
    out << "matched_profile=" << analysis.match.matchedProfileId << Qt::endl;
    out << "matched_display_name=" << analysis.match.matchedDisplayName << Qt::endl;
    out << "match_confidence=" << analysis.match.confidence << Qt::endl;
    out << "match_score=" << analysis.match.score << Qt::endl;

    for (const QString &line : analysis.inspection.evidence) {
        out << "evidence.source=" << line << Qt::endl;
    }
    for (const QString &line : analysis.match.evidence) {
        out << "evidence.match=" << line << Qt::endl;
    }
    for (const QString &line : analysis.inspection.warnings) {
        out << "warning.source=" << line << Qt::endl;
    }
    for (const QString &line : analysis.match.warnings) {
        out << "warning.match=" << line << Qt::endl;
    }

    return 0;
}

int runCliValidate(const QString &inputPath)
{
    QTextStream out(stdout);
    QTextStream err(stderr);

    CatalogData catalog;
    QString errorMessage;
    const QString catalogRoot = CatalogLoader::findCatalogRoot(QDir::currentPath());
    if (catalogRoot.isEmpty() || !CatalogLoader::loadCatalog(catalogRoot, &catalog, &errorMessage)) {
        err << "Catalog load failed: "
            << (errorMessage.isEmpty() ? QStringLiteral("catalog root not found") : errorMessage)
            << Qt::endl;
        return 1;
    }

    const ValidationResult validation = AnalysisService::validatePath(catalog, inputPath);
    if (!validation.errorMessage.isEmpty()) {
        err << validation.errorMessage << Qt::endl;
    }
    if (!validation.ok) {
        return 2;
    }

    out << "profile=" << validation.analysis.selectedProfile.id << Qt::endl;
    out << "working_root=" << validation.preparation.workingRootPath << Qt::endl;
    out << "recipe=" << validation.validation.recipeId << Qt::endl;
    out << "patch_package_root=" << validation.validation.patchPackageRoot << Qt::endl;
    out << "applicable=" << (validation.validation.applicable ? "true" : "false") << Qt::endl;
    out << "already_patched=" << (validation.validation.alreadyPatched ? "true" : "false")
        << Qt::endl;
    out << "support_level=" << validation.validation.supportLevel << Qt::endl;
    for (const ValidationCheckResult &check : validation.validation.checks) {
        out << "check=" << (check.passed ? "PASS" : "FAIL")
            << "|" << (check.blocking ? "blocking" : "non-blocking")
            << "|" << check.description
            << "|" << check.detail << Qt::endl;
    }
    for (const ValidationCheckResult &check : validation.validation.idempotencyChecks) {
        out << "idempotency_check=" << (check.passed ? "PASS" : "FAIL")
            << "|" << check.description
            << "|" << check.detail << Qt::endl;
    }
    out << "post_apply_contract="
        << (validation.validation.postApplyContractSatisfied ? "true" : "false")
        << Qt::endl;
    for (const ValidationCheckResult &check : validation.validation.postApplyChecks) {
        out << "post_apply_check=" << (check.passed ? "PASS" : "FAIL")
            << "|" << (check.blocking ? "blocking" : "non-blocking")
            << "|" << check.description
            << "|" << check.detail << Qt::endl;
    }
    for (const QString &line : validation.validation.warnings) {
        out << "warning.validation=" << line << Qt::endl;
    }

    return validation.validation.applicable ? 0 : 3;
}

int runCliExtract(const QString &inputPath, const QString &parentDir, const QString &directoryName)
{
    QTextStream out(stdout);
    QTextStream err(stderr);

    ExtractPlan plan;
    plan.destinationParentDir = parentDir;
    plan.directoryName = directoryName;

    const ExtractResult extraction = WorkflowService::extractSource(inputPath, plan);
    if (!extraction.errorMessage.isEmpty()) {
        err << extraction.errorMessage << Qt::endl;
    }
    if (!extraction.ok) {
        return 2;
    }

    out << "working_root=" << extraction.workingRootPath << Qt::endl;
    out << "skipped=" << (extraction.skipped ? "true" : "false") << Qt::endl;
    for (const QString &line : extraction.messages) {
        out << "message=" << line << Qt::endl;
    }
    return 0;
}

int runCliApply(const QString &profileId, const QString &workingRootPath)
{
    QTextStream out(stdout);
    QTextStream err(stderr);

    CatalogData catalog;
    QString errorMessage;
    const QString catalogRoot = CatalogLoader::findCatalogRoot(QDir::currentPath());
    if (catalogRoot.isEmpty() || !CatalogLoader::loadCatalog(catalogRoot, &catalog, &errorMessage)) {
        err << "Catalog load failed: "
            << (errorMessage.isEmpty() ? QStringLiteral("catalog root not found") : errorMessage)
            << Qt::endl;
        return 1;
    }

    const ApplyResult apply = WorkflowService::applyPatch(
        catalog,
        profileId,
        workingRootPath,
        {},
        [&err](const QString &line) {
            err << line << Qt::endl;
        });
    if (!apply.errorMessage.isEmpty()) {
        err << apply.errorMessage << Qt::endl;
    }
    if (!apply.ok) {
        return 2;
    }

    out << "working_root=" << apply.workingRootPath << Qt::endl;
    for (const QString &line : apply.messages) {
        out << "message=" << line << Qt::endl;
    }
    out << "already_patched=" << (apply.validation.validation.alreadyPatched ? "true" : "false")
        << Qt::endl;
    return 0;
}

int runCliBuild(const QString &profileId,
                const QString &workingRootPath,
                const QString &buildRecipeId,
                const QString &buildRootOverride)
{
    QTextStream out(stdout);
    QTextStream err(stderr);

    CatalogData catalog;
    QString errorMessage;
    const QString catalogRoot = CatalogLoader::findCatalogRoot(QDir::currentPath());
    if (catalogRoot.isEmpty() || !CatalogLoader::loadCatalog(catalogRoot, &catalog, &errorMessage)) {
        err << "Catalog load failed: "
            << (errorMessage.isEmpty() ? QStringLiteral("catalog root not found") : errorMessage)
            << Qt::endl;
        return 1;
    }

    const BuildResult build = WorkflowService::buildPatchedTree(
        catalog,
        profileId,
        buildRecipeId,
        workingRootPath,
        buildRootOverride,
        {},
        [&err](const QString &line) {
            err << line << Qt::endl;
        });
    if (!build.errorMessage.isEmpty()) {
        err << build.errorMessage << Qt::endl;
    }
    if (!build.ok) {
        return 2;
    }

    out << "recipe=" << build.recipeId << Qt::endl;
    out << "drop_dir=" << build.dropDir << Qt::endl;
    out << "package_dir=" << build.packageDir << Qt::endl;
    out << "build_dir=" << build.buildDir << Qt::endl;
    for (const QString &line : build.messages) {
        out << "message=" << line << Qt::endl;
    }
    return 0;
}

int runCliPackage(const QString &sourceDropDir, const QString &packageDir)
{
    QTextStream out(stdout);
    QTextStream err(stderr);

    const PackageResult package = WorkflowService::createCubeIdePackage(
        sourceDropDir,
        packageDir,
        {},
        [&err](const QString &line) {
            err << line << Qt::endl;
        });
    if (!package.errorMessage.isEmpty()) {
        err << package.errorMessage << Qt::endl;
    }
    if (!package.ok) {
        return 2;
    }

    out << "source_drop_dir=" << package.sourceDropDir << Qt::endl;
    out << "package_dir=" << package.packageDir << Qt::endl;
    out << "skipped=" << (package.skipped ? "true" : "false") << Qt::endl;
    for (const QString &line : package.messages) {
        out << "message=" << line << Qt::endl;
    }
    return 0;
}

int runCliVerify(const QString &profileId, const QString &dropDir, const QString &cubeIdePath)
{
    QTextStream out(stdout);
    QTextStream err(stderr);

    CatalogData catalog;
    QString errorMessage;
    const QString catalogRoot = CatalogLoader::findCatalogRoot(QDir::currentPath());
    if (catalogRoot.isEmpty() || !CatalogLoader::loadCatalog(catalogRoot, &catalog, &errorMessage)) {
        err << "Catalog load failed: "
            << (errorMessage.isEmpty() ? QStringLiteral("catalog root not found") : errorMessage)
            << Qt::endl;
        return 1;
    }

    const VerifyResult verify = WorkflowService::verifyBuild(
        catalog,
        profileId,
        {},
        dropDir,
        cubeIdePath,
        {},
        [&err](const QString &line) {
            err << line << Qt::endl;
        });
    if (!verify.errorMessage.isEmpty()) {
        err << verify.errorMessage << Qt::endl;
    }
    if (!verify.ok) {
        return 2;
    }

    out << "drop_dir=" << verify.dropDir << Qt::endl;
    for (const VerifyRecipeRunResult &recipeRun : verify.recipes) {
        out << "recipe=" << recipeRun.recipeId << "|"
            << (recipeRun.ok ? "PASS" : "FAIL") << Qt::endl;
    }
    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    QStringList rawArgs;
    for (int i = 0; i < argc; ++i) {
        rawArgs.append(QString::fromLocal8Bit(argv[i]));
    }
    if (rawArgs.size() >= 3 && rawArgs.at(1) == QStringLiteral("--detect")) {
        QCoreApplication a(argc, argv);
        return runCliDetect(rawArgs.at(2));
    }
    if (rawArgs.size() >= 3 && rawArgs.at(1) == QStringLiteral("--validate")) {
        QCoreApplication a(argc, argv);
        return runCliValidate(rawArgs.at(2));
    }
    if (rawArgs.size() >= 5 && rawArgs.at(1) == QStringLiteral("--extract")) {
        QCoreApplication a(argc, argv);
        return runCliExtract(rawArgs.at(2), rawArgs.at(3), rawArgs.at(4));
    }
    if (rawArgs.size() >= 4 && rawArgs.at(1) == QStringLiteral("--apply")) {
        QCoreApplication a(argc, argv);
        return runCliApply(rawArgs.at(2), rawArgs.at(3));
    }
    if (rawArgs.size() >= 4 && rawArgs.at(1) == QStringLiteral("--build")) {
        QCoreApplication a(argc, argv);
        const QString buildRecipeId = rawArgs.size() >= 5 ? rawArgs.at(4) : QString();
        const QString buildRootOverride = rawArgs.size() >= 6 ? rawArgs.at(5) : QString();
        return runCliBuild(rawArgs.at(2), rawArgs.at(3), buildRecipeId, buildRootOverride);
    }
    if (rawArgs.size() >= 4 && rawArgs.at(1) == QStringLiteral("--verify")) {
        QCoreApplication a(argc, argv);
        const QString cubeIdePath = rawArgs.size() >= 5 ? rawArgs.at(4) : QString();
        return runCliVerify(rawArgs.at(2), rawArgs.at(3), cubeIdePath);
    }
    if (rawArgs.size() >= 4 && rawArgs.at(1) == QStringLiteral("--package")) {
        QCoreApplication a(argc, argv);
        return runCliPackage(rawArgs.at(2), rawArgs.at(3));
    }

    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    return a.exec();
}

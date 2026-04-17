#ifndef WORKFLOWSERVICE_H
#define WORKFLOWSERVICE_H

#include "analysisservice.h"
#include "catalogtypes.h"

#include <atomic>
#include <functional>
#include <memory>

struct ExtractPlan
{
    QString destinationParentDir;
    QString directoryName;
};

struct ExtractResult
{
    bool ok = false;
    bool skipped = false;
    bool cancelled = false;
    QString sourcePath;
    QString workingRootPath;
    QString errorMessage;
    QStringList messages;
};

struct ApplyResult
{
    bool ok = false;
    bool skipped = false;
    bool cancelled = false;
    QString workingRootPath;
    QString errorMessage;
    QStringList messages;
    ValidationResult validation;
};

struct BuildResult
{
    bool ok = false;
    bool skipped = false;
    bool cancelled = false;
    QString workingRootPath;
    QString recipeId;
    QString recipeDisplayName;
    QString buildDir;
    QString installDir;
    QString dropDir;
    QString packageDir;
    QString errorMessage;
    QStringList messages;
};

struct BuildLayoutPreview
{
    bool ok = false;
    QString profileId;
    QString recipeId;
    QString recipeDisplayName;
    QString workingRootPath;
    QString buildRootPath;
    QString buildDir;
    QString installDir;
    QString dropDir;
    QString packageDir;
    QString errorMessage;
};

struct PackageResult
{
    bool ok = false;
    bool skipped = false;
    bool cancelled = false;
    QString sourceDropDir;
    QString packageDir;
    QString errorMessage;
    QStringList messages;
};

struct VerifyCheckRunResult
{
    bool passed = false;
    QString description;
    QString detail;
};

struct VerifyRecipeRunResult
{
    QString recipeId;
    QString recipeDisplayName;
    bool ok = false;
    QStringList messages;
    QVector<VerifyCheckRunResult> checks;
};

struct VerifyResult
{
    bool ok = false;
    bool skipped = false;
    bool cancelled = false;
    QString dropDir;
    QString errorMessage;
    QStringList messages;
    QVector<VerifyRecipeRunResult> recipes;
};

class WorkflowService
{
public:
    using CancelToken = std::shared_ptr<std::atomic_bool>;
    using ExtractProgressCallback = std::function<void(int current, int total, const QString &relativePath)>;
    using StepProgressCallback = std::function<void(int percent, const QString &message)>;
    using LogCallback = std::function<void(const QString &line)>;

    static ExtractResult extractSource(const QString &inputPath,
                                       const ExtractPlan &plan,
                                       const ExtractProgressCallback &progressCallback = {},
                                       const CancelToken &cancelToken = {});
    static ApplyResult applyPatch(const CatalogData &catalog,
                                  const QString &profileId,
                                  const QString &workingRootPath,
                                  const StepProgressCallback &progressCallback = {},
                                  const LogCallback &logCallback = {},
                                  const CancelToken &cancelToken = {});
    static BuildResult buildPatchedTree(const CatalogData &catalog,
                                        const QString &profileId,
                                        const QString &buildRecipeId,
                                        const QString &workingRootPath,
                                        const QString &buildRootOverride = {},
                                        const StepProgressCallback &progressCallback = {},
                                        const LogCallback &logCallback = {},
                                        const CancelToken &cancelToken = {});
    static BuildLayoutPreview previewBuildLayout(const CatalogData &catalog,
                                                 const QString &profileId,
                                                 const QString &buildRecipeId,
                                                 const QString &workingRootPath,
                                                 const QString &buildRootOverride = {});
    static PackageResult createCubeIdePackage(const QString &sourceDropDir,
                                              const QString &packageDir,
                                              const StepProgressCallback &progressCallback = {},
                                              const LogCallback &logCallback = {},
                                              const CancelToken &cancelToken = {});
    static VerifyResult verifyBuild(const CatalogData &catalog,
                                    const QString &profileId,
                                    const QStringList &verifyRecipeIds,
                                    const QString &dropDir,
                                    const QString &cubeIdePath = {},
                                    const StepProgressCallback &progressCallback = {},
                                    const LogCallback &logCallback = {},
                                    const CancelToken &cancelToken = {});
};

#endif // WORKFLOWSERVICE_H

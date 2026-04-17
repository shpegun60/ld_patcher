#ifndef ANALYSISSERVICE_H
#define ANALYSISSERVICE_H

#include "catalogtypes.h"

#include <atomic>
#include <functional>
#include <memory>

struct AnalysisResult
{
    bool ok = false;
    QString errorMessage;
    SourceInspection inspection;
    ProfileMatchResult match;
    VersionProfile selectedProfile;
    bool hasSelectedProfile = false;
    QString patchRecipeSummary;
    QStringList buildRecipeSummaries;
    QStringList verifyRecipeSummaries;
};

struct ValidationResult
{
    bool ok = false;
    QString errorMessage;
    AnalysisResult analysis;
    SourcePreparationResult preparation;
    PatchValidationDetails validation;
};

class AnalysisService
{
public:
    using ProgressCallback = std::function<void(int percent, const QString &message)>;
    using CancelToken = std::shared_ptr<std::atomic_bool>;

    static AnalysisResult analyzePath(const CatalogData &catalog,
                                      const QString &path,
                                      const ProgressCallback &progressCallback = {},
                                      const CancelToken &cancelToken = {});
    static ValidationResult validatePath(const CatalogData &catalog,
                                         const QString &path,
                                         const QString &selectedProfileId = QString(),
                                         const ProgressCallback &progressCallback = {},
                                         const CancelToken &cancelToken = {});
};

#endif // ANALYSISSERVICE_H

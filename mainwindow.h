#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "analysisservice.h"
#include "catalogtypes.h"
#include "workflowservice.h"

#include <QAction>
#include <QFutureWatcher>
#include <QLabel>
#include <QList>
#include <QMainWindow>
#include <atomic>
#include <memory>

class QProgressBar;
class QDockWidget;
class QListWidget;
class QPushButton;
class QLabel;
class QWidget;

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void browseFolder();
    void browseZip();
    void browseCubeIdeRoot();
    void browseBuildRoot();
    void analyzeSelectedPath();
    void validateSelectedPath();
    void openProfilesDialog();
    void handleAnalysisFinished();
    void handleValidationFinished();
    void handleExtractionFinished();
    void handleApplyFinished();
    void handleBuildFinished();
    void handleVerifyFinished();
    void handlePackageFinished();
    void handleWorkflowSelectionChanged();
    void resetWorkflow();
    void cancelCurrentStep();
    void openDropDir();

private:
    enum class WorkflowStepId
    {
        AnalyzeInput,
        ValidateInput,
        Extract,
        AnalyzeWorking,
        ValidateWorking,
        Apply,
        Build,
        Verify,
        Package
    };

    enum class WorkflowStepState
    {
        Pending,
        Ready,
        Running,
        Completed,
        Skipped,
        Failed
    };

    enum class StatusVisualState
    {
        Idle,
        Running,
        Success,
        Failed,
        Cancelled
    };

    struct WorkflowStepEntry
    {
        WorkflowStepId id;
        QString title;
        WorkflowStepState state = WorkflowStepState::Pending;
        QString detail;
        QStringList logs;
        QString statusPrimaryMessage;
        QString statusDetailMessage;
        int statusProgressPercent = 0;
        bool statusProgressIndeterminate = false;
        StatusVisualState statusVisualState = StatusVisualState::Idle;
    };

    struct WorkflowState
    {
        QString sourcePath;
        QString selectedProfileId;
        QString workingRootPath;
        QString extractedRootPath;
        AnalysisResult inputAnalysis;
        ValidationResult inputValidation;
        AnalysisResult workingAnalysis;
        ValidationResult workingValidation;
        ExtractResult extraction;
        ApplyResult apply;
        BuildResult build;
        VerifyResult verify;
        PackageResult package;
    };

private:
    void buildWorkflowUi();
    void loadCatalog();
    void setStatusMessage(const QString &message, int timeoutMs = 0);
    void setBusyState(bool busy, const QString &message = QString());
    void applyStatusVisualState(StatusVisualState state);
    void setProgressIndeterminate(const QString &format = QString());
    void setProgressPercent(int percent, const QString &format = QString());
    void hideProgress();
    SourceKind workflowInputKind() const;
    bool workflowStepVisible(WorkflowStepId id) const;
    QList<WorkflowStepId> visibleWorkflowStepIds() const;
    void resetWorkflowState(bool preserveLog = false);
    void syncWorkflowSourcePath();
    void updateWorkflowView();
    QString workflowStepStateText(WorkflowStepState state) const;
    int stepIndex(WorkflowStepId id) const;
    WorkflowStepEntry *stepEntry(WorkflowStepId id);
    const WorkflowStepEntry *stepEntry(WorkflowStepId id) const;
    QString workflowStepTitle(WorkflowStepId id) const;
    void selectLogStep(WorkflowStepId id);
    void refreshLogView();
    void renderStatusForStep(WorkflowStepId id);
    void setLogContext(WorkflowStepId id);
    void setStepState(WorkflowStepId id, WorkflowStepState state, const QString &detail = QString());
    void markNextReadyAfter(WorkflowStepId completedStep);
    WorkflowStepId nextRunnableStep() const;
    bool promptExtractPlan(ExtractPlan *plan);
    void runWorkflowStep(WorkflowStepId id);
    void startAnalysisInput();
    void startValidationInputStep();
    void startExtractionStep();
    void startAnalysisWorkingStep();
    void startValidationWorkingStep();
    void startBuildStep();
    void startVerifyStep();
    void startPackageStep();
    void appendLogLine(const QString &line);
    void appendSection(const QString &title, const QStringList &lines);
    void appendAnalysisSections(const AnalysisResult &analysis);
    void appendValidationSections(const ValidationResult &validation);
    void appendExtractionSections(const ExtractResult &extraction);
    void appendApplySections(const ApplyResult &apply);
    void appendBuildSections(const BuildResult &build);
    void appendVerifySections(const VerifyResult &verify);
    void appendPackageSections(const PackageResult &package);
    QString chooseProfileForValidation(const AnalysisResult &analysis, const QString &preferredProfileId = QString());
    QString chooseBuildRecipeForProfile(const VersionProfile &profile, const QString &preferredRecipeId = QString());
    QString defaultWorkingRootForSelectedInput() const;
    void applyAutoBuildRootSuggestion(const QString &workingRootPath);
    void refreshDefaultBuildRoot();
    void refreshBuildLayoutPreview();
    void refreshPackagePathDisplay();
    void autoFillCubeIdeRoot();
    QString currentPreviewProfileId() const;
    QString currentPreviewBuildRecipeId(const VersionProfile *profile) const;
    QString selectedBuildRootOverride() const;
    QString selectedCubeIdeRoot() const;
    void startAnalysisStep(WorkflowStepId id, const QString &path, const QString &message);
    void startValidationStep(WorkflowStepId id,
                             const QString &path,
                             const QString &selectedProfileId,
                             const QString &message);
    void startExtraction(const QString &path, const ExtractPlan &plan);
    void startApply(const QString &profileId, const QString &workingRootPath);
    void setSelectedPath(const QString &path);

private:
    Ui::MainWindow *ui;
    CatalogData m_catalog;
    QFutureWatcher<AnalysisResult> *m_analysisWatcher = nullptr;
    QFutureWatcher<ValidationResult> *m_validationWatcher = nullptr;
    QLabel *m_logTitleLabel = nullptr;
    QLabel *m_buildLayoutPreviewLabel = nullptr;
    QWidget *m_bottomStatusPanel = nullptr;
    QLabel *m_statusPrimaryLabel = nullptr;
    QLabel *m_statusDetailLabel = nullptr;
    QProgressBar *m_statusProgress = nullptr;
    QAction *m_profilesAction = nullptr;
    QAction *m_reloadCatalogAction = nullptr;
    QDockWidget *m_workflowDock = nullptr;
    QListWidget *m_workflowList = nullptr;
    QPushButton *m_runNextStepButton = nullptr;
    QPushButton *m_resetWorkflowButton = nullptr;
    QPushButton *m_cancelStepButton = nullptr;
    QList<WorkflowStepEntry> m_workflowSteps;
    WorkflowState m_workflowState;
    QString m_pendingPath;
    WorkflowStepId m_logContextStepId = WorkflowStepId::AnalyzeInput;
    WorkflowStepId m_selectedLogStepId = WorkflowStepId::AnalyzeInput;
    WorkflowStepId m_runningAnalysisStepId = WorkflowStepId::AnalyzeInput;
    WorkflowStepId m_runningValidationStepId = WorkflowStepId::ValidateInput;
    int m_lastExtractProgressLoggedPercent = -1;
    QFutureWatcher<ExtractResult> *m_extractionWatcher = nullptr;
    QFutureWatcher<ApplyResult> *m_applyWatcher = nullptr;
    QFutureWatcher<BuildResult> *m_buildWatcher = nullptr;
    QFutureWatcher<VerifyResult> *m_verifyWatcher = nullptr;
    QFutureWatcher<PackageResult> *m_packageWatcher = nullptr;
    QString m_selectedBuildRecipeId;
    QString m_autoBuildRootPath;
    bool m_buildRootUserEdited = false;
    StatusVisualState m_statusVisualState = StatusVisualState::Idle;
    std::shared_ptr<std::atomic_bool> m_cancelToken;
    bool m_cancelRequested = false;
};

#endif // MAINWINDOW_H

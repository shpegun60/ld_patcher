#include "mainwindow.h"

#include "analysisservice.h"
#include "catalogloader.h"
#include "profilesdialog.h"
#include "ui_mainwindow.h"

#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QDockWidget>
#include <QFileDialog>
#include <QFormLayout>
#include <QFuture>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QDesktopServices>
#include <QProgressBar>
#include <QPushButton>
#include <QSizePolicy>
#include <QStatusBar>
#include <QTextCursor>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <QSignalBlocker>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>

namespace {

QList<int> cubeIdeVersionParts(const QString &path)
{
    const QFileInfo info(path);
    const QStringList names = {
        info.fileName(),
        info.dir().dirName()
    };

    const QRegularExpression versionRegex(QStringLiteral("STM32CubeIDE[_-]?(\\d+)\\.(\\d+)\\.(\\d+)"),
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

    const QFileInfo leftInfo(left);
    const QFileInfo rightInfo(right);
    return leftInfo.lastModified() > rightInfo.lastModified();
}

QString normalizedCubeIdeCandidate(const QString &path)
{
    const QFileInfo info(QDir::cleanPath(path));
    if (!info.exists() || !info.isDir()) {
        return {};
    }

    const QDir dir(info.absoluteFilePath());
    if (dir.dirName().compare(QStringLiteral("STM32CubeIDE"), Qt::CaseInsensitive) == 0) {
        return dir.absolutePath();
    }

    const QString nestedCubeIde = dir.filePath(QStringLiteral("STM32CubeIDE"));
    if (QFileInfo::exists(nestedCubeIde) && QFileInfo(nestedCubeIde).isDir()) {
        return QDir(nestedCubeIde).absolutePath();
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

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setWindowTitle(QStringLiteral("ld_patcher"));

    ui->analyzeButton->setText(QStringLiteral("Run Next Step"));
    ui->validateButton->setText(QStringLiteral("Reset Workflow"));
    ui->validateButton->show();
    m_runNextStepButton = ui->analyzeButton;
    m_resetWorkflowButton = ui->validateButton;
    m_cancelStepButton = ui->cancelButton;

    statusBar()->hide();

    m_bottomStatusPanel = new QWidget(this);
    m_bottomStatusPanel->setObjectName(QStringLiteral("bottomStatusPanel"));
    auto *statusPanelLayout = new QVBoxLayout(m_bottomStatusPanel);
    statusPanelLayout->setContentsMargins(0, 6, 0, 0);
    statusPanelLayout->setSpacing(3);

    m_statusProgress = new QProgressBar(m_bottomStatusPanel);
    m_statusProgress->setObjectName(QStringLiteral("statusProgressBar"));
    m_statusProgress->setRange(0, 100);
    m_statusProgress->setValue(0);
    m_statusProgress->setTextVisible(true);
    m_statusProgress->setAlignment(Qt::AlignCenter);
    m_statusProgress->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_statusProgress->setFormat(QStringLiteral("%p%"));
    statusPanelLayout->addWidget(m_statusProgress);

    m_statusPrimaryLabel = new QLabel(m_bottomStatusPanel);
    m_statusPrimaryLabel->setObjectName(QStringLiteral("statusPrimaryLabel"));
    m_statusPrimaryLabel->setText(QStringLiteral("Ready"));
    m_statusPrimaryLabel->setWordWrap(true);
    statusPanelLayout->addWidget(m_statusPrimaryLabel);

    m_statusDetailLabel = new QLabel(m_bottomStatusPanel);
    m_statusDetailLabel->setObjectName(QStringLiteral("statusDetailLabel"));
    m_statusDetailLabel->setText(QStringLiteral("Idle"));
    m_statusDetailLabel->setWordWrap(true);
    statusPanelLayout->addWidget(m_statusDetailLabel);

    ui->statusLabel->hide();
    ui->verticalLayout->addWidget(m_bottomStatusPanel);

    m_logTitleLabel = new QLabel(this);
    m_logTitleLabel->setText(QStringLiteral("Logs: Analyze (Input)"));
    ui->verticalLayout->insertWidget(ui->verticalLayout->indexOf(ui->logEdit), m_logTitleLabel);
    m_buildLayoutPreviewLabel = ui->buildLayoutPreviewLabel;

    m_analysisWatcher = new QFutureWatcher<AnalysisResult>(this);
    m_validationWatcher = new QFutureWatcher<ValidationResult>(this);
    m_extractionWatcher = new QFutureWatcher<ExtractResult>(this);
    m_applyWatcher = new QFutureWatcher<ApplyResult>(this);
    m_buildWatcher = new QFutureWatcher<BuildResult>(this);
    m_verifyWatcher = new QFutureWatcher<VerifyResult>(this);
    m_packageWatcher = new QFutureWatcher<PackageResult>(this);

    buildWorkflowUi();
    resetWorkflowState(true);
    setStatusMessage(QStringLiteral("Ready"));
    applyStatusVisualState(StatusVisualState::Idle);

    connect(ui->browseFolderButton, &QPushButton::clicked, this, &MainWindow::browseFolder);
    connect(ui->browseZipButton, &QPushButton::clicked, this, &MainWindow::browseZip);
    connect(ui->browseCubeIdeRootButton, &QPushButton::clicked, this, &MainWindow::browseCubeIdeRoot);
    connect(ui->browseBuildRootButton, &QPushButton::clicked, this, &MainWindow::browseBuildRoot);
    connect(ui->buildRootEdit, &QLineEdit::textEdited, this, [this](const QString &) {
        m_buildRootUserEdited = true;
        refreshBuildLayoutPreview();
    });
    connect(ui->analyzeButton, &QPushButton::clicked, this, &MainWindow::analyzeSelectedPath);
    connect(ui->validateButton, &QPushButton::clicked, this, &MainWindow::resetWorkflow);
    connect(ui->cancelButton, &QPushButton::clicked, this, &MainWindow::cancelCurrentStep);
    connect(ui->openDropDirButton, &QPushButton::clicked, this, &MainWindow::openDropDir);
    connect(m_workflowList, &QListWidget::currentRowChanged, this, &MainWindow::handleWorkflowSelectionChanged);
    connect(m_analysisWatcher, &QFutureWatcher<AnalysisResult>::finished, this, &MainWindow::handleAnalysisFinished);
    connect(m_validationWatcher, &QFutureWatcher<ValidationResult>::finished, this, &MainWindow::handleValidationFinished);
    connect(m_extractionWatcher, &QFutureWatcher<ExtractResult>::finished, this, &MainWindow::handleExtractionFinished);
    connect(m_applyWatcher, &QFutureWatcher<ApplyResult>::finished, this, &MainWindow::handleApplyFinished);
    connect(m_buildWatcher, &QFutureWatcher<BuildResult>::finished, this, &MainWindow::handleBuildFinished);
    connect(m_verifyWatcher, &QFutureWatcher<VerifyResult>::finished, this, &MainWindow::handleVerifyFinished);
    connect(m_packageWatcher, &QFutureWatcher<PackageResult>::finished, this, &MainWindow::handlePackageFinished);

    auto *catalogMenu = menuBar()->addMenu(QStringLiteral("&Catalog"));
    m_profilesAction = catalogMenu->addAction(QStringLiteral("&Profiles..."));
    m_reloadCatalogAction = catalogMenu->addAction(QStringLiteral("&Reload Catalog"));
    connect(m_profilesAction, &QAction::triggered, this, &MainWindow::openProfilesDialog);
    connect(m_reloadCatalogAction, &QAction::triggered, this, &MainWindow::loadCatalog);

    loadCatalog();
    autoFillCubeIdeRoot();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::buildWorkflowUi()
{
    m_workflowDock = new QDockWidget(QStringLiteral("Workflow"), this);
    m_workflowDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_workflowDock->setFeatures(QDockWidget::NoDockWidgetFeatures);

    auto *container = new QWidget(m_workflowDock);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(8, 8, 8, 8);

    auto *label = new QLabel(
        QStringLiteral("Run the flow step by step. Each next stage unlocks only after the previous one succeeds."),
        container);
    label->setWordWrap(true);
    layout->addWidget(label);

    m_workflowList = new QListWidget(container);
    m_workflowList->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_workflowList, 1);

    m_workflowDock->setWidget(container);
    addDockWidget(Qt::LeftDockWidgetArea, m_workflowDock);
}

void MainWindow::browseFolder()
{
    const QString path = QFileDialog::getExistingDirectory(this,
                                                           QStringLiteral("Select source tree directory"),
                                                           ui->pathEdit->text());
    if (!path.isEmpty()) {
        setSelectedPath(path);
    }
}

void MainWindow::browseZip()
{
    const QString path = QFileDialog::getOpenFileName(this,
                                                      QStringLiteral("Select source ZIP archive"),
                                                      ui->pathEdit->text(),
                                                      QStringLiteral("ZIP archives (*.zip)"));
    if (!path.isEmpty()) {
        setSelectedPath(path);
    }
}

void MainWindow::browseCubeIdeRoot()
{
    const QString initialPath = ui->cubeIdeRootEdit->text().trimmed().isEmpty()
                                    ? QDir::currentPath()
                                    : ui->cubeIdeRootEdit->text().trimmed();
    const QString path = QFileDialog::getExistingDirectory(this,
                                                           QStringLiteral("Select CubeIDE / Compiler Root"),
                                                           initialPath);
    if (path.isEmpty()) {
        return;
    }

    ui->cubeIdeRootEdit->setText(QDir::toNativeSeparators(path));
    appendSection(QStringLiteral("CubeIDE Root"), {
        QStringLiteral("User-selected CubeIDE / compiler root: %1")
            .arg(QDir::toNativeSeparators(path))
    });
    setStatusMessage(QStringLiteral("CubeIDE / compiler root updated"));
}

void MainWindow::browseBuildRoot()
{
    const QString initialPath = ui->buildRootEdit->text().trimmed().isEmpty()
                                    ? QDir::currentPath()
                                    : ui->buildRootEdit->text().trimmed();
    const QString path = QFileDialog::getExistingDirectory(this,
                                                           QStringLiteral("Select Build / Output Root"),
                                                           initialPath);
    if (path.isEmpty()) {
        return;
    }
    m_buildRootUserEdited = true;
    ui->buildRootEdit->setText(QDir::toNativeSeparators(path));
    appendSection(QStringLiteral("Build Root"), {
        QStringLiteral("User-selected build/output root: %1").arg(QDir::toNativeSeparators(path))
    });
    setStatusMessage(QStringLiteral("Build root updated"));
}

void MainWindow::openDropDir()
{
    const QString path = ui->packageDirEdit ? ui->packageDirEdit->text().trimmed() : QString();
    if (path.isEmpty() || !QFileInfo(path).isDir()) {
        setStatusMessage(QStringLiteral("CubeIDE linker package directory is not available yet."));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void MainWindow::analyzeSelectedPath()
{
    syncWorkflowSourcePath();

    const QString path = ui->pathEdit->text().trimmed();
    if (path.isEmpty()) {
        setStatusMessage(QStringLiteral("Choose a folder or ZIP first."));
        appendLogLine(QStringLiteral("No source path selected."));
        return;
    }

    if (m_catalog.isEmpty()) {
        setStatusMessage(QStringLiteral("Catalog is not loaded."));
        appendLogLine(QStringLiteral("Catalog is empty, workflow cannot continue."));
        return;
    }

    const WorkflowStepId nextStep = nextRunnableStep();
    const WorkflowStepEntry *entry = stepEntry(nextStep);
    if (entry == nullptr || entry->state != WorkflowStepState::Ready) {
        setStatusMessage(QStringLiteral("No workflow step is currently ready."));
        appendLogLine(QStringLiteral("The workflow has no runnable step right now."));
        return;
    }

    runWorkflowStep(nextStep);
}

void MainWindow::validateSelectedPath()
{
    resetWorkflow();
}

void MainWindow::resetWorkflow()
{
    resetWorkflowState(false);
}

void MainWindow::cancelCurrentStep()
{
    if (!m_cancelToken) {
        setStatusMessage(QStringLiteral("No running step to cancel."));
        return;
    }

    m_cancelRequested = true;
    m_cancelToken->store(true);
    applyStatusVisualState(StatusVisualState::Cancelled);
    setStatusMessage(QStringLiteral("Cancelling current step..."));
    appendLogLine(QStringLiteral("Cancellation requested by the user."));
}

void MainWindow::openProfilesDialog()
{
    if (m_catalog.isEmpty()) {
        appendLogLine(QStringLiteral("Catalog is not loaded, so there are no profiles to show."));
        return;
    }

    ProfilesDialog dialog(m_catalog, this);
    connect(&dialog, &ProfilesDialog::profilesChanged, this, &MainWindow::loadCatalog);
    dialog.exec();
}

void MainWindow::loadCatalog()
{
    const QString catalogRoot = CatalogLoader::findCatalogRoot(QDir::currentPath());
    QString errorMessage;
    if (catalogRoot.isEmpty() || !CatalogLoader::loadCatalog(catalogRoot, &m_catalog, &errorMessage)) {
        setStatusMessage(QStringLiteral("Catalog load failed"));
        appendLogLine(QStringLiteral("Failed to load catalog: %1")
                          .arg(errorMessage.isEmpty() ? QStringLiteral("catalog root not found")
                                                      : errorMessage));
        return;
    }

    const int enabledProfiles = static_cast<int>(std::count_if(m_catalog.profiles.cbegin(),
                                                               m_catalog.profiles.cend(),
                                                               [](const VersionProfile &profile) {
                                                                   return profile.enabled;
                                                               }));

    setStatusMessage(QStringLiteral("Catalog loaded"));
    appendSection(QStringLiteral("Catalog"), {
        QStringLiteral("Root: %1").arg(m_catalog.rootDir),
        QStringLiteral("Profiles: %1").arg(m_catalog.profiles.size()),
        QStringLiteral("Enabled profiles: %1").arg(enabledProfiles),
        QStringLiteral("Patch recipes: %1").arg(m_catalog.patchRecipes.size()),
        QStringLiteral("Build recipes: %1").arg(m_catalog.buildRecipes.size()),
        QStringLiteral("Verify recipes: %1").arg(m_catalog.verifyRecipes.size())
    });
    refreshBuildLayoutPreview();
}

void MainWindow::setStatusMessage(const QString &message, int timeoutMs)
{
    Q_UNUSED(timeoutMs);
    if (WorkflowStepEntry *entry = stepEntry(m_logContextStepId)) {
        entry->statusPrimaryMessage = message;
    }
    ui->statusLabel->setText(message);
    if (m_statusPrimaryLabel && m_selectedLogStepId == m_logContextStepId) {
        m_statusPrimaryLabel->setText(message);
    }
}

void MainWindow::setBusyState(bool busy, const QString &message)
{
    ui->browseFolderButton->setEnabled(!busy);
    ui->browseZipButton->setEnabled(!busy);
    ui->pathEdit->setEnabled(!busy);
    if (ui->buildRootEdit) {
        ui->buildRootEdit->setEnabled(!busy);
    }
    if (ui->browseBuildRootButton) {
        ui->browseBuildRootButton->setEnabled(!busy);
    }
    if (ui->cubeIdeRootEdit) {
        ui->cubeIdeRootEdit->setEnabled(!busy);
    }
    if (ui->browseCubeIdeRootButton) {
        ui->browseCubeIdeRootButton->setEnabled(!busy);
    }
    if (m_runNextStepButton != nullptr) {
        m_runNextStepButton->setEnabled(!busy);
    }
    if (m_resetWorkflowButton != nullptr) {
        m_resetWorkflowButton->setEnabled(!busy);
    }
    if (m_cancelStepButton != nullptr) {
        m_cancelStepButton->setEnabled(busy);
    }
    if (m_profilesAction != nullptr) {
        m_profilesAction->setEnabled(!busy);
    }
    if (m_reloadCatalogAction != nullptr) {
        m_reloadCatalogAction->setEnabled(!busy);
    }
    if (m_statusProgress != nullptr) {
        m_statusProgress->setVisible(true);
    }
    if (!message.isEmpty()) {
        setStatusMessage(message);
    }
}

void MainWindow::applyStatusVisualState(StatusVisualState state)
{
    m_statusVisualState = state;
    if (WorkflowStepEntry *entry = stepEntry(m_logContextStepId)) {
        entry->statusVisualState = state;
    }

    QString panelBackground;
    QString panelBorder;
    QString primaryText;
    QString detailText;
    QString progressBackground;
    QString progressBorder;
    QString progressChunk;
    QString progressText;

    switch (state) {
    case StatusVisualState::Idle:
        panelBackground = QStringLiteral("#f8fafc");
        panelBorder = QStringLiteral("#cbd5e1");
        primaryText = QStringLiteral("#334155");
        detailText = QStringLiteral("#64748b");
        progressBackground = QStringLiteral("#e2e8f0");
        progressBorder = QStringLiteral("#cbd5e1");
        progressChunk = QStringLiteral("#94a3b8");
        progressText = QStringLiteral("#334155");
        break;
    case StatusVisualState::Running:
        panelBackground = QStringLiteral("#eff6ff");
        panelBorder = QStringLiteral("#60a5fa");
        primaryText = QStringLiteral("#1d4ed8");
        detailText = QStringLiteral("#1e40af");
        progressBackground = QStringLiteral("#dbeafe");
        progressBorder = QStringLiteral("#60a5fa");
        progressChunk = QStringLiteral("#2563eb");
        progressText = QStringLiteral("#1e3a8a");
        break;
    case StatusVisualState::Success:
        panelBackground = QStringLiteral("#f0fdf4");
        panelBorder = QStringLiteral("#4ade80");
        primaryText = QStringLiteral("#15803d");
        detailText = QStringLiteral("#166534");
        progressBackground = QStringLiteral("#dcfce7");
        progressBorder = QStringLiteral("#4ade80");
        progressChunk = QStringLiteral("#16a34a");
        progressText = QStringLiteral("#14532d");
        break;
    case StatusVisualState::Failed:
        panelBackground = QStringLiteral("#fef2f2");
        panelBorder = QStringLiteral("#f87171");
        primaryText = QStringLiteral("#dc2626");
        detailText = QStringLiteral("#991b1b");
        progressBackground = QStringLiteral("#fee2e2");
        progressBorder = QStringLiteral("#f87171");
        progressChunk = QStringLiteral("#dc2626");
        progressText = QStringLiteral("#7f1d1d");
        break;
    case StatusVisualState::Cancelled:
        panelBackground = QStringLiteral("#fffbeb");
        panelBorder = QStringLiteral("#fbbf24");
        primaryText = QStringLiteral("#d97706");
        detailText = QStringLiteral("#92400e");
        progressBackground = QStringLiteral("#fef3c7");
        progressBorder = QStringLiteral("#fbbf24");
        progressChunk = QStringLiteral("#d97706");
        progressText = QStringLiteral("#78350f");
        break;
    }

    if (m_selectedLogStepId != m_logContextStepId) {
        return;
    }

    if (m_bottomStatusPanel) {
        m_bottomStatusPanel->setStyleSheet(
            QStringLiteral(
                "QWidget#bottomStatusPanel {"
                " background-color: %1;"
                " border-top: 1px solid %2;"
                " padding-top: 4px;"
                " }")
                .arg(panelBackground, panelBorder));
    }

    if (m_statusPrimaryLabel) {
        m_statusPrimaryLabel->setStyleSheet(
            QStringLiteral("QLabel#statusPrimaryLabel { color: %1; font-weight: 600; }")
                .arg(primaryText));
    }

    if (m_statusDetailLabel) {
        m_statusDetailLabel->setStyleSheet(
            QStringLiteral("QLabel#statusDetailLabel { color: %1; }")
                .arg(detailText));
    }

    if (m_statusProgress) {
        m_statusProgress->setStyleSheet(
            QStringLiteral(
                "QProgressBar#statusProgressBar {"
                " border: 1px solid %1;"
                " border-radius: 4px;"
                " background-color: %2;"
                " color: %3;"
                " min-height: 22px;"
                " text-align: center;"
                " }"
                "QProgressBar#statusProgressBar::chunk {"
                " background-color: %4;"
                " border-radius: 3px;"
                " }")
                .arg(progressBorder, progressBackground, progressText, progressChunk));
    }
}

void MainWindow::setProgressIndeterminate(const QString &format)
{
    if (!m_statusProgress) {
        return;
    }

    if (WorkflowStepEntry *entry = stepEntry(m_logContextStepId)) {
        entry->statusProgressIndeterminate = true;
        entry->statusDetailMessage = format.isEmpty() ? QStringLiteral("Working...") : format;
    }

    if (m_selectedLogStepId != m_logContextStepId) {
        return;
    }

    m_statusProgress->setVisible(true);
    m_statusProgress->setRange(0, 0);
    m_statusProgress->setTextVisible(false);
    if (m_statusDetailLabel) {
        m_statusDetailLabel->setText(format.isEmpty() ? QStringLiteral("Working...") : format);
    }
    m_statusProgress->update();
}

void MainWindow::setProgressPercent(int percent, const QString &format)
{
    if (!m_statusProgress) {
        return;
    }

    const int boundedPercent = qBound(0, percent, 100);
    QString detail = format;
    detail.replace(QStringLiteral("%p%"), QStringLiteral("%1%").arg(boundedPercent));
    if (detail.isEmpty()) {
        detail = QStringLiteral("Progress: %1%").arg(boundedPercent);
    }

    if (WorkflowStepEntry *entry = stepEntry(m_logContextStepId)) {
        entry->statusProgressIndeterminate = false;
        entry->statusProgressPercent = boundedPercent;
        entry->statusDetailMessage = detail;
    }

    if (m_selectedLogStepId != m_logContextStepId) {
        return;
    }

    m_statusProgress->setVisible(true);
    m_statusProgress->setRange(0, 100);
    m_statusProgress->setValue(boundedPercent);
    m_statusProgress->setTextVisible(true);
    m_statusProgress->setFormat(QStringLiteral("%p%"));
    if (m_statusDetailLabel) {
        m_statusDetailLabel->setText(detail);
    }
    m_statusProgress->update();
}

void MainWindow::hideProgress()
{
    if (!m_statusProgress) {
        return;
    }

    renderStatusForStep(m_selectedLogStepId);
}

SourceKind MainWindow::workflowInputKind() const
{
    if (m_workflowState.inputAnalysis.ok) {
        return m_workflowState.inputAnalysis.inspection.kind;
    }

    const QString path = ui->pathEdit->text().trimmed();
    if (path.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)) {
        return SourceKind::ZipArchive;
    }

    const QFileInfo info(path);
    if (info.exists() && info.isDir()) {
        return SourceKind::Directory;
    }

    return SourceKind::Unknown;
}

bool MainWindow::workflowStepVisible(WorkflowStepId id) const
{
    const SourceKind kind = workflowInputKind();
    const bool isZip = kind == SourceKind::ZipArchive;
    const bool alreadyPatched = (m_workflowState.inputValidation.ok && m_workflowState.inputValidation.validation.alreadyPatched)
                                || (m_workflowState.workingValidation.ok && m_workflowState.workingValidation.validation.alreadyPatched)
                                || (m_workflowState.apply.ok && m_workflowState.apply.validation.validation.alreadyPatched);
    const WorkflowStepEntry *applyEntry = stepEntry(WorkflowStepId::Apply);

    switch (id) {
    case WorkflowStepId::AnalyzeInput:
    case WorkflowStepId::ValidateInput:
    case WorkflowStepId::Build:
    case WorkflowStepId::Verify:
    case WorkflowStepId::Package:
        return true;
    case WorkflowStepId::Extract:
    case WorkflowStepId::AnalyzeWorking:
    case WorkflowStepId::ValidateWorking:
        return isZip;
    case WorkflowStepId::Apply:
        if (applyEntry != nullptr && applyEntry->state != WorkflowStepState::Pending) {
            return true;
        }
        return !alreadyPatched;
    }
    return true;
}

QList<MainWindow::WorkflowStepId> MainWindow::visibleWorkflowStepIds() const
{
    QList<WorkflowStepId> ids;
    for (const WorkflowStepEntry &step : m_workflowSteps) {
        if (workflowStepVisible(step.id)) {
            ids.append(step.id);
        }
    }
    return ids;
}

void MainWindow::resetWorkflowState(bool preserveLog)
{
    m_workflowState = WorkflowState();
    m_workflowState.sourcePath = ui->pathEdit->text().trimmed();
    m_pendingPath.clear();
    m_selectedBuildRecipeId.clear();
    m_cancelToken.reset();
    m_cancelRequested = false;
    m_logContextStepId = WorkflowStepId::AnalyzeInput;
    m_selectedLogStepId = WorkflowStepId::AnalyzeInput;
    m_runningAnalysisStepId = WorkflowStepId::AnalyzeInput;
    m_runningValidationStepId = WorkflowStepId::ValidateInput;
    m_lastExtractProgressLoggedPercent = -1;

    m_workflowSteps = {
        { WorkflowStepId::AnalyzeInput, QStringLiteral("Analyze (Input)"), WorkflowStepState::Ready, QString(), {}, {}, {}, 0, false, StatusVisualState::Idle },
        { WorkflowStepId::ValidateInput, QStringLiteral("Validate (Input)"), WorkflowStepState::Pending, QString(), {}, {}, {}, 0, false, StatusVisualState::Idle },
        { WorkflowStepId::Extract, QStringLiteral("Extract"), WorkflowStepState::Pending, QStringLiteral("ZIP only"), {}, {}, {}, 0, false, StatusVisualState::Idle },
        { WorkflowStepId::AnalyzeWorking, QStringLiteral("Analyze (Working Tree)"), WorkflowStepState::Pending, QStringLiteral("after extract"), {}, {}, {}, 0, false, StatusVisualState::Idle },
        { WorkflowStepId::ValidateWorking, QStringLiteral("Validate (Working Tree)"), WorkflowStepState::Pending, QStringLiteral("after working-tree analyze"), {}, {}, {}, 0, false, StatusVisualState::Idle },
        { WorkflowStepId::Apply, QStringLiteral("Apply"), WorkflowStepState::Pending, QString(), {}, {}, {}, 0, false, StatusVisualState::Idle },
        { WorkflowStepId::Build, QStringLiteral("Build"), WorkflowStepState::Pending, QStringLiteral("planned"), {}, {}, {}, 0, false, StatusVisualState::Idle },
        { WorkflowStepId::Verify, QStringLiteral("Verify"), WorkflowStepState::Pending, QStringLiteral("planned"), {}, {}, {}, 0, false, StatusVisualState::Idle },
        { WorkflowStepId::Package, QStringLiteral("Package (CubeIDE)"), WorkflowStepState::Pending, QStringLiteral("optional"), {}, {}, {}, 0, false, StatusVisualState::Idle }
    };

    updateWorkflowView();
    refreshDefaultBuildRoot();
    refreshBuildLayoutPreview();
    refreshPackagePathDisplay();
    hideProgress();
    if (!preserveLog) {
        refreshLogView();
    }
    setStatusMessage(QStringLiteral("Workflow reset"));
}
void MainWindow::syncWorkflowSourcePath()
{
    const QString path = ui->pathEdit->text().trimmed();
    if (m_workflowState.sourcePath != path) {
        resetWorkflowState(false);
        m_workflowState.sourcePath = path;
        updateWorkflowView();
    }
}

void MainWindow::updateWorkflowView()
{
    if (m_workflowList == nullptr) {
        return;
    }

    const QSignalBlocker blocker(m_workflowList);
    m_workflowList->clear();
    int visibleIndex = 0;
    for (const WorkflowStepEntry &step : std::as_const(m_workflowSteps)) {
        if (!workflowStepVisible(step.id)) {
            continue;
        }
        QString label = QStringLiteral("%1. %2 [%3]")
                            .arg(++visibleIndex)
                            .arg(step.title, workflowStepStateText(step.state));
        if (!step.detail.isEmpty()) {
            label.append(QStringLiteral(" - %1").arg(step.detail));
        }
        auto *item = new QListWidgetItem(label, m_workflowList);
        item->setData(Qt::UserRole, static_cast<int>(step.id));
        m_workflowList->addItem(item);
    }

    selectLogStep(m_selectedLogStepId);
}

QString MainWindow::workflowStepStateText(WorkflowStepState state) const
{
    switch (state) {
    case WorkflowStepState::Ready:
        return QStringLiteral("Ready");
    case WorkflowStepState::Running:
        return QStringLiteral("Running");
    case WorkflowStepState::Completed:
        return QStringLiteral("Completed");
    case WorkflowStepState::Skipped:
        return QStringLiteral("Skipped");
    case WorkflowStepState::Failed:
        return QStringLiteral("Failed");
    case WorkflowStepState::Pending:
    default:
        return QStringLiteral("Pending");
    }
}

QString MainWindow::workflowStepTitle(WorkflowStepId id) const
{
    const WorkflowStepEntry *entry = stepEntry(id);
    return entry ? entry->title : QStringLiteral("Workflow Step");
}

void MainWindow::selectLogStep(WorkflowStepId id)
{
    m_selectedLogStepId = id;
    if (!m_workflowList) {
        return;
    }

    const QSignalBlocker blocker(m_workflowList);
    bool found = false;
    for (int i = 0; i < m_workflowList->count(); ++i) {
        QListWidgetItem *item = m_workflowList->item(i);
        if (!item) {
            continue;
        }
        if (item->data(Qt::UserRole).toInt() == static_cast<int>(id)) {
            m_workflowList->setCurrentRow(i);
            found = true;
            break;
        }
    }
    if (!found && m_workflowList->count() > 0) {
        m_workflowList->setCurrentRow(0);
        m_selectedLogStepId = static_cast<WorkflowStepId>(m_workflowList->item(0)->data(Qt::UserRole).toInt());
    }

    refreshLogView();
    renderStatusForStep(m_selectedLogStepId);
}

void MainWindow::refreshLogView()
{
    if (!ui->logEdit) {
        return;
    }

    const WorkflowStepEntry *entry = stepEntry(m_selectedLogStepId);
    if (m_logTitleLabel) {
        m_logTitleLabel->setText(QStringLiteral("Logs: %1").arg(workflowStepTitle(m_selectedLogStepId)));
    }

    ui->logEdit->clear();
    if (!entry) {
        return;
    }

    for (const QString &line : entry->logs) {
        ui->logEdit->appendPlainText(line);
    }
    QTextCursor cursor = ui->logEdit->textCursor();
    cursor.movePosition(QTextCursor::End);
    ui->logEdit->setTextCursor(cursor);
}

void MainWindow::renderStatusForStep(WorkflowStepId id)
{
    const WorkflowStepEntry *entry = stepEntry(id);
    if (!entry || !m_statusProgress) {
        return;
    }

    applyStatusVisualState(entry->statusVisualState);

    if (m_statusPrimaryLabel) {
        m_statusPrimaryLabel->setText(entry->statusPrimaryMessage.isEmpty()
                                          ? entry->title
                                          : entry->statusPrimaryMessage);
    }

    if (m_statusDetailLabel) {
        const QString fallbackDetail = entry->detail.isEmpty()
                                           ? workflowStepStateText(entry->state)
                                           : entry->detail;
        m_statusDetailLabel->setText(entry->statusDetailMessage.isEmpty()
                                         ? fallbackDetail
                                         : entry->statusDetailMessage);
    }

    m_statusProgress->setVisible(true);
    if (entry->statusProgressIndeterminate) {
        m_statusProgress->setRange(0, 0);
        m_statusProgress->setTextVisible(false);
    } else {
        m_statusProgress->setRange(0, 100);
        m_statusProgress->setValue(qBound(0, entry->statusProgressPercent, 100));
        m_statusProgress->setTextVisible(true);
        m_statusProgress->setFormat(QStringLiteral("%p%"));
    }
    m_statusProgress->update();
}

void MainWindow::setLogContext(WorkflowStepId id)
{
    m_logContextStepId = id;
    if (stepEntry(id) != nullptr) {
        selectLogStep(id);
    }
}

int MainWindow::stepIndex(WorkflowStepId id) const
{
    for (int i = 0; i < m_workflowSteps.size(); ++i) {
        if (m_workflowSteps.at(i).id == id) {
            return i;
        }
    }
    return -1;
}

MainWindow::WorkflowStepEntry *MainWindow::stepEntry(WorkflowStepId id)
{
    const int index = stepIndex(id);
    return index >= 0 ? &m_workflowSteps[index] : nullptr;
}

const MainWindow::WorkflowStepEntry *MainWindow::stepEntry(WorkflowStepId id) const
{
    const int index = stepIndex(id);
    return index >= 0 ? &m_workflowSteps.at(index) : nullptr;
}

void MainWindow::handleWorkflowSelectionChanged()
{
    if (!m_workflowList || !m_workflowList->currentItem()) {
        return;
    }

    m_selectedLogStepId =
        static_cast<WorkflowStepId>(m_workflowList->currentItem()->data(Qt::UserRole).toInt());
    refreshLogView();
    renderStatusForStep(m_selectedLogStepId);
}

void MainWindow::setStepState(WorkflowStepId id, WorkflowStepState state, const QString &detail)
{
    WorkflowStepEntry *entry = stepEntry(id);
    if (!entry) {
        return;
    }
    entry->state = state;
    if (!detail.isNull()) {
        entry->detail = detail;
    }

    switch (state) {
    case WorkflowStepState::Pending:
        entry->statusVisualState = StatusVisualState::Idle;
        entry->statusProgressIndeterminate = false;
        entry->statusProgressPercent = 0;
        entry->statusPrimaryMessage = entry->title;
        entry->statusDetailMessage = entry->detail.isEmpty()
                                         ? QStringLiteral("Pending")
                                         : entry->detail;
        break;
    case WorkflowStepState::Ready:
        entry->statusVisualState = StatusVisualState::Idle;
        entry->statusProgressIndeterminate = false;
        entry->statusProgressPercent = 0;
        entry->statusPrimaryMessage = entry->title;
        entry->statusDetailMessage = entry->detail.isEmpty()
                                         ? QStringLiteral("Ready")
                                         : entry->detail;
        break;
    case WorkflowStepState::Running:
        entry->statusVisualState = StatusVisualState::Running;
        entry->statusPrimaryMessage = entry->title;
        if (entry->statusDetailMessage.isEmpty()) {
            entry->statusDetailMessage = QStringLiteral("Running");
        }
        break;
    case WorkflowStepState::Completed:
        entry->statusVisualState = StatusVisualState::Success;
        entry->statusProgressIndeterminate = false;
        entry->statusProgressPercent = 100;
        entry->statusPrimaryMessage = entry->title;
        entry->statusDetailMessage = entry->detail.isEmpty()
                                         ? QStringLiteral("Completed")
                                         : entry->detail;
        break;
    case WorkflowStepState::Skipped:
        entry->statusVisualState = StatusVisualState::Idle;
        entry->statusProgressIndeterminate = false;
        entry->statusProgressPercent = 100;
        entry->statusPrimaryMessage = entry->title;
        entry->statusDetailMessage = entry->detail.isEmpty()
                                         ? QStringLiteral("Skipped")
                                         : entry->detail;
        break;
    case WorkflowStepState::Failed:
        entry->statusVisualState = StatusVisualState::Failed;
        entry->statusProgressIndeterminate = false;
        entry->statusProgressPercent = 0;
        entry->statusPrimaryMessage = entry->title;
        entry->statusDetailMessage = entry->detail.isEmpty()
                                         ? QStringLiteral("Failed")
                                         : entry->detail;
        break;
    }

    updateWorkflowView();
    if (m_selectedLogStepId == id) {
        renderStatusForStep(id);
    }
}

void MainWindow::markNextReadyAfter(WorkflowStepId completedStep)
{
    switch (completedStep) {
    case WorkflowStepId::AnalyzeInput:
        setStepState(WorkflowStepId::ValidateInput, WorkflowStepState::Ready, QString());
        break;
    case WorkflowStepId::ValidateInput:
        if (m_workflowState.inputValidation.validation.alreadyPatched) {
            setStepState(WorkflowStepId::Extract, WorkflowStepState::Skipped, QStringLiteral("not needed"));
            setStepState(WorkflowStepId::AnalyzeWorking, WorkflowStepState::Skipped, QStringLiteral("not needed"));
            setStepState(WorkflowStepId::ValidateWorking, WorkflowStepState::Skipped, QStringLiteral("not needed"));
            setStepState(WorkflowStepId::Apply, WorkflowStepState::Skipped, QStringLiteral("already patched"));
            setStepState(WorkflowStepId::Build, WorkflowStepState::Ready, QStringLiteral("planned"));
        } else if (m_workflowState.inputValidation.analysis.inspection.kind == SourceKind::Directory) {
            setStepState(WorkflowStepId::Extract, WorkflowStepState::Skipped, QStringLiteral("directory input"));
            setStepState(WorkflowStepId::AnalyzeWorking, WorkflowStepState::Skipped, QStringLiteral("directory input"));
            setStepState(WorkflowStepId::ValidateWorking, WorkflowStepState::Skipped, QStringLiteral("directory input"));
            setStepState(WorkflowStepId::Apply, WorkflowStepState::Ready, QString());
        } else {
            setStepState(WorkflowStepId::Extract, WorkflowStepState::Ready, QString());
        }
        break;
    case WorkflowStepId::Extract:
        setStepState(WorkflowStepId::AnalyzeWorking, WorkflowStepState::Ready, QString());
        break;
    case WorkflowStepId::AnalyzeWorking:
        setStepState(WorkflowStepId::ValidateWorking, WorkflowStepState::Ready, QString());
        break;
    case WorkflowStepId::ValidateWorking:
        if (m_workflowState.workingValidation.validation.alreadyPatched) {
            setStepState(WorkflowStepId::Apply, WorkflowStepState::Skipped, QStringLiteral("already patched"));
            setStepState(WorkflowStepId::Build, WorkflowStepState::Ready, QStringLiteral("planned"));
        } else {
            setStepState(WorkflowStepId::Apply, WorkflowStepState::Ready, QString());
        }
        break;
    case WorkflowStepId::Apply:
        setStepState(WorkflowStepId::Build, WorkflowStepState::Ready, QStringLiteral("planned"));
        break;
    case WorkflowStepId::Build:
        setStepState(WorkflowStepId::Verify, WorkflowStepState::Ready, QStringLiteral("planned"));
        break;
    case WorkflowStepId::Verify:
        setStepState(WorkflowStepId::Package, WorkflowStepState::Ready, QStringLiteral("optional"));
        break;
    case WorkflowStepId::Package:
        break;
    }
}

MainWindow::WorkflowStepId MainWindow::nextRunnableStep() const
{
    for (const WorkflowStepEntry &step : m_workflowSteps) {
        if (workflowStepVisible(step.id) && step.state == WorkflowStepState::Ready) {
            return step.id;
        }
    }
    return WorkflowStepId::AnalyzeInput;
}

bool MainWindow::promptExtractPlan(ExtractPlan *plan)
{
    if (!plan) {
        return false;
    }

    const QString currentPath = ui->pathEdit->text().trimmed();
    const QFileInfo inputInfo(currentPath);
    const QString defaultParentDir = inputInfo.absolutePath();
    const QString defaultName = !m_workflowState.inputAnalysis.inspection.rootName.isEmpty()
                                    ? m_workflowState.inputAnalysis.inspection.rootName
                                    : inputInfo.completeBaseName();

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Choose Extract Destination"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *description = new QLabel(
        QStringLiteral("ZIP input must be extracted into a writable working directory before Apply."),
        &dialog);
    description->setWordWrap(true);
    layout->addWidget(description);

    auto *formLayout = new QFormLayout();
    auto *parentDirEdit = new QLineEdit(defaultParentDir, &dialog);
    auto *dirNameEdit = new QLineEdit(defaultName, &dialog);
    auto *browseButton = new QPushButton(QStringLiteral("Browse..."), &dialog);
    auto *parentRow = new QWidget(&dialog);
    auto *parentRowLayout = new QHBoxLayout(parentRow);
    parentRowLayout->setContentsMargins(0, 0, 0, 0);
    parentRowLayout->addWidget(parentDirEdit, 1);
    parentRowLayout->addWidget(browseButton);
    formLayout->addRow(QStringLiteral("Parent folder:"), parentRow);
    formLayout->addRow(QStringLiteral("Working directory name:"), dirNameEdit);
    layout->addLayout(formLayout);

    connect(browseButton, &QPushButton::clicked, &dialog, [this, parentDirEdit]() {
        const QString path = QFileDialog::getExistingDirectory(this,
                                                               QStringLiteral("Select parent extraction directory"),
                                                               parentDirEdit->text());
        if (!path.isEmpty()) {
            parentDirEdit->setText(QDir::toNativeSeparators(path));
        }
    });

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttonBox);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    while (dialog.exec() == QDialog::Accepted) {
        const QString parentDir = parentDirEdit->text().trimmed();
        const QString dirName = dirNameEdit->text().trimmed();
        if (parentDir.isEmpty() || dirName.isEmpty()) {
            QMessageBox::warning(this,
                                 QStringLiteral("Incomplete Extract Destination"),
                                 QStringLiteral("Choose a parent folder and provide a working directory name."));
            continue;
        }

        const QString destinationPath = QDir(parentDir).filePath(dirName);
        if (QFileInfo::exists(destinationPath)) {
            QMessageBox::warning(this,
                                 QStringLiteral("Destination Already Exists"),
                                 QStringLiteral("The destination directory already exists:\n%1\n\nChoose another name or folder.")
                                     .arg(QDir::toNativeSeparators(destinationPath)));
            continue;
        }

        plan->destinationParentDir = parentDir;
        plan->directoryName = dirName;
        return true;
    }

    return false;
}

void MainWindow::runWorkflowStep(WorkflowStepId id)
{
    switch (id) {
    case WorkflowStepId::AnalyzeInput:
        startAnalysisInput();
        break;
    case WorkflowStepId::ValidateInput:
        startValidationInputStep();
        break;
    case WorkflowStepId::Extract:
        startExtractionStep();
        break;
    case WorkflowStepId::AnalyzeWorking:
        startAnalysisWorkingStep();
        break;
    case WorkflowStepId::ValidateWorking:
        startValidationWorkingStep();
        break;
    case WorkflowStepId::Apply:
        startApply(m_workflowState.selectedProfileId, m_workflowState.workingRootPath);
        break;
    case WorkflowStepId::Build:
        startBuildStep();
        break;
    case WorkflowStepId::Verify:
        startVerifyStep();
        break;
    case WorkflowStepId::Package:
        startPackageStep();
        break;
    }
}

void MainWindow::startAnalysisInput()
{
    m_pendingPath = m_workflowState.sourcePath;
    setLogContext(WorkflowStepId::AnalyzeInput);
    appendSection(QStringLiteral("Step Start"), {
        QStringLiteral("Action: Analyze input"),
        QStringLiteral("Path: %1").arg(m_workflowState.sourcePath)
    });
    startAnalysisStep(WorkflowStepId::AnalyzeInput,
                      m_workflowState.sourcePath,
                      QStringLiteral("Analyzing source package..."));
}

void MainWindow::startValidationInputStep()
{
    const AnalysisResult &analysis = m_workflowState.inputAnalysis;
    setLogContext(WorkflowStepId::ValidateInput);
    appendSection(QStringLiteral("Step Start"), {
        QStringLiteral("Action: Validate input"),
        QStringLiteral("Path: %1").arg(m_workflowState.sourcePath)
    });

    const int matchedCandidateCount = static_cast<int>(std::count_if(analysis.match.candidates.cbegin(),
                                                                     analysis.match.candidates.cend(),
                                                                     [](const ProfileCandidate &candidate) {
                                                                         return candidate.matched;
                                                                     }));

    QString selectedProfileId;
    if (matchedCandidateCount > 1
        || (!analysis.hasSelectedProfile && analysis.match.candidates.size() > 1)) {
        selectedProfileId = chooseProfileForValidation(analysis);
        if (selectedProfileId.isEmpty()) {
            applyStatusVisualState(StatusVisualState::Cancelled);
            setStatusMessage(QStringLiteral("Profile selection was cancelled."));
            appendLogLine(QStringLiteral("Validation was skipped because no profile was chosen."));
            return;
        }
    } else if (analysis.hasSelectedProfile) {
        selectedProfileId = analysis.selectedProfile.id;
    } else if (analysis.match.candidates.size() == 1) {
        selectedProfileId = analysis.match.candidates.constFirst().profileId;
        appendLogLine(QStringLiteral(
            "No verified match was selected automatically, so the only available candidate will be used for validation."));
        appendLogLine(QString());
    } else {
        setStepState(WorkflowStepId::ValidateInput,
                     WorkflowStepState::Failed,
                     QStringLiteral("no candidate profile"));
        applyStatusVisualState(StatusVisualState::Failed);
        setStatusMessage(QStringLiteral("Validation cannot continue without a candidate profile."));
        appendLogLine(QStringLiteral("Validation was skipped because no candidate profile is available."));
        return;
    }

    m_workflowState.selectedProfileId = selectedProfileId;
    startValidationStep(WorkflowStepId::ValidateInput,
                        m_workflowState.sourcePath,
                        selectedProfileId,
                        QStringLiteral("Validating selected profile on the input source..."));
}

void MainWindow::startExtractionStep()
{
    setLogContext(WorkflowStepId::Extract);
    ExtractPlan plan;
    if (!promptExtractPlan(&plan)) {
        applyStatusVisualState(StatusVisualState::Cancelled);
        setStatusMessage(QStringLiteral("Extraction was cancelled."));
        appendLogLine(QStringLiteral("Extraction was cancelled by the user."));
        return;
    }

    appendSection(QStringLiteral("Step Start"), {
        QStringLiteral("Action: Extract ZIP into a working directory"),
        QStringLiteral("Archive: %1").arg(m_workflowState.sourcePath),
        QStringLiteral("Parent folder: %1").arg(plan.destinationParentDir),
        QStringLiteral("Working directory name: %1").arg(plan.directoryName)
    });
    startExtraction(m_workflowState.sourcePath, plan);
}

void MainWindow::startAnalysisWorkingStep()
{
    setLogContext(WorkflowStepId::AnalyzeWorking);
    appendSection(QStringLiteral("Step Start"), {
        QStringLiteral("Action: Analyze extracted working tree"),
        QStringLiteral("Working root: %1").arg(m_workflowState.workingRootPath)
    });
    startAnalysisStep(WorkflowStepId::AnalyzeWorking,
                      m_workflowState.workingRootPath,
                      QStringLiteral("Analyzing working directory..."));
}

void MainWindow::startValidationWorkingStep()
{
    const AnalysisResult &analysis = m_workflowState.workingAnalysis;
    setLogContext(WorkflowStepId::ValidateWorking);
    appendSection(QStringLiteral("Step Start"), {
        QStringLiteral("Action: Validate extracted working tree"),
        QStringLiteral("Working root: %1").arg(m_workflowState.workingRootPath)
    });

    QString selectedProfileId = m_workflowState.selectedProfileId;
    const bool profileStillCandidate =
        std::any_of(analysis.match.candidates.cbegin(),
                    analysis.match.candidates.cend(),
                    [&selectedProfileId](const ProfileCandidate &candidate) {
                        return candidate.profileId == selectedProfileId;
                    });
    if (selectedProfileId.isEmpty() || !profileStillCandidate) {
        selectedProfileId = chooseProfileForValidation(analysis, m_workflowState.selectedProfileId);
    }
    if (selectedProfileId.isEmpty()) {
        setStepState(WorkflowStepId::ValidateWorking,
                     WorkflowStepState::Failed,
                     QStringLiteral("no selected profile"));
        setStatusMessage(QStringLiteral("Working-tree validation was cancelled."));
        appendLogLine(QStringLiteral("No profile was selected for working-tree validation."));
        return;
    }

    m_workflowState.selectedProfileId = selectedProfileId;
    startValidationStep(WorkflowStepId::ValidateWorking,
                        m_workflowState.workingRootPath,
                        selectedProfileId,
                        QStringLiteral("Validating the extracted working tree..."));
}
void MainWindow::appendLogLine(const QString &line)
{
    WorkflowStepEntry *entry = stepEntry(m_logContextStepId);
    if (!entry) {
        return;
    }

    entry->logs.append(line);
    if (m_selectedLogStepId == m_logContextStepId) {
        ui->logEdit->appendPlainText(line);
        QTextCursor cursor = ui->logEdit->textCursor();
        cursor.movePosition(QTextCursor::End);
        ui->logEdit->setTextCursor(cursor);
    }
}

void MainWindow::appendSection(const QString &title, const QStringList &lines)
{
    if (lines.isEmpty()) {
        return;
    }

    appendLogLine(QStringLiteral("[%1]").arg(title));
    for (const QString &line : lines) {
        appendLogLine(QStringLiteral("- %1").arg(line));
    }
    appendLogLine(QString());
}

void MainWindow::appendAnalysisSections(const AnalysisResult &analysis)
{
    appendSection(QStringLiteral("Source"), {
        QStringLiteral("Input: %1").arg(analysis.inspection.inputPath),
        QStringLiteral("Kind: %1").arg(sourceKindToString(analysis.inspection.kind)),
        QStringLiteral("Root: %1").arg(analysis.inspection.rootName),
        QStringLiteral("Product: %1").arg(analysis.inspection.productName.isEmpty()
                                              ? QStringLiteral("<unknown>")
                                              : analysis.inspection.productName),
        QStringLiteral("BASE-VER: %1").arg(analysis.inspection.gccBaseVersion.isEmpty()
                                               ? QStringLiteral("<unknown>")
                                               : analysis.inspection.gccBaseVersion),
        QStringLiteral("Release level: %1").arg(analysis.inspection.releaseLevel.isEmpty()
                                                    ? QStringLiteral("<unknown>")
                                                    : analysis.inspection.releaseLevel),
        QStringLiteral("Inferred ref: %1").arg(analysis.inspection.inferredRef.isEmpty()
                                                   ? QStringLiteral("<unknown>")
                                                   : analysis.inspection.inferredRef),
        QStringLiteral("Has ld layout: %1").arg(analysis.inspection.hasLdLayout ? QStringLiteral("yes")
                                                                                  : QStringLiteral("no"))
    });

    appendSection(QStringLiteral("Source Evidence"), analysis.inspection.evidence);
    if (!analysis.inspection.warnings.isEmpty()) {
        appendSection(QStringLiteral("Source Warnings"), analysis.inspection.warnings);
    }

    if (!analysis.match.bestCandidateProfileId.isEmpty()) {
        const QString matchedProfileId = analysis.match.matchedProfileId.isEmpty()
                                             ? QStringLiteral("<none>")
                                             : analysis.match.matchedProfileId;
        const QString matchedDisplayName = analysis.match.matchedDisplayName.isEmpty()
                                               ? QStringLiteral("<none>")
                                               : analysis.match.matchedDisplayName;
        appendSection(QStringLiteral("Profile Match"), {
            QStringLiteral("Matched: %1").arg(analysis.match.matched ? QStringLiteral("yes")
                                                                     : QStringLiteral("no")),
            QStringLiteral("Best candidate id: %1").arg(analysis.match.bestCandidateProfileId),
            QStringLiteral("Best candidate name: %1").arg(analysis.match.bestCandidateDisplayName),
            QStringLiteral("Matched profile id: %1").arg(matchedProfileId),
            QStringLiteral("Matched profile name: %1").arg(matchedDisplayName),
            QStringLiteral("Confidence: %1").arg(analysis.match.confidence.isEmpty()
                                                     ? QStringLiteral("unknown")
                                                     : analysis.match.confidence),
            QStringLiteral("Raw score: %1").arg(analysis.match.score)
        });

        appendSection(QStringLiteral("Match Evidence"), analysis.match.evidence);
        if (!analysis.match.warnings.isEmpty()) {
            appendSection(QStringLiteral("Match Warnings"), analysis.match.warnings);
        }

        QStringList candidateLines;
        for (const ProfileCandidate &candidate : analysis.match.candidates) {
            candidateLines.append(QStringLiteral("%1 | id=%2 | matched=%3 | score=%4 | adjusted=%5 | status=%6 | confidence=%7")
                                      .arg(candidate.displayName,
                                           candidate.profileId,
                                           candidate.matched ? QStringLiteral("yes")
                                                             : QStringLiteral("no"),
                                           QString::number(candidate.score),
                                           QString::number(candidate.adjustedScore),
                                           candidate.status.isEmpty() ? QStringLiteral("<unknown>")
                                                                      : candidate.status,
                                           candidate.confidence.isEmpty() ? QStringLiteral("unknown")
                                                                          : candidate.confidence));
        }
        if (!candidateLines.isEmpty()) {
            appendSection(QStringLiteral("Candidate Profiles"), candidateLines);
        }

        if (analysis.hasSelectedProfile) {
            appendSection(QStringLiteral("Planned Recipes"), {
                QStringLiteral("Patch: %1").arg(analysis.patchRecipeSummary)
            });
            appendSection(QStringLiteral("Build Recipes"), analysis.buildRecipeSummaries);
            appendSection(QStringLiteral("Verify Recipes"), analysis.verifyRecipeSummaries);
            if (!analysis.selectedProfile.notes.isEmpty()) {
                appendSection(QStringLiteral("Profile Notes"), analysis.selectedProfile.notes);
            }
        }
    } else {
        appendLogLine(QStringLiteral("No candidate profile was selected."));
    }
}

void MainWindow::appendValidationSections(const ValidationResult &validation)
{
    if (validation.analysis.hasSelectedProfile) {
        appendSection(QStringLiteral("Selected Profile"), {
            QStringLiteral("Profile id: %1").arg(validation.analysis.selectedProfile.id),
            QStringLiteral("Display name: %1").arg(validation.analysis.selectedProfile.displayName),
            QStringLiteral("Patch recipe: %1").arg(validation.analysis.patchRecipeSummary)
        });
    }

    appendSection(QStringLiteral("Preparation"), {
        QStringLiteral("Input kind: %1").arg(sourceKindToString(validation.preparation.kind)),
        QStringLiteral("Working root: %1").arg(validation.preparation.workingRootPath),
        QStringLiteral("Extracted workdir: %1").arg(
            validation.preparation.extractedWorkDir.isEmpty()
                ? QStringLiteral("<not used>")
                : validation.preparation.extractedWorkDir)
    });
    if (!validation.preparation.messages.isEmpty()) {
        appendSection(QStringLiteral("Preparation Notes"), validation.preparation.messages);
    }

    appendSection(QStringLiteral("Patch Validation"), {
        QStringLiteral("Recipe id: %1").arg(validation.validation.recipeId),
        QStringLiteral("Recipe: %1").arg(validation.validation.recipeDisplayName),
        QStringLiteral("Source root: %1").arg(validation.validation.sourceRootPath),
        QStringLiteral("Patch package root: %1").arg(validation.validation.patchPackageRoot),
        QStringLiteral("Applicable: %1").arg(validation.validation.applicable ? QStringLiteral("yes")
                                                                              : QStringLiteral("no")),
        QStringLiteral("Already patched: %1").arg(validation.validation.alreadyPatched
                                                      ? QStringLiteral("yes")
                                                      : QStringLiteral("no")),
        QStringLiteral("Support level: %1").arg(validation.validation.supportLevel)
    });

    QStringList checkLines;
    for (const ValidationCheckResult &check : validation.validation.checks) {
        const QString state = check.passed ? QStringLiteral("PASS") : QStringLiteral("FAIL");
        const QString severity = check.blocking ? QStringLiteral("blocking")
                                                : QStringLiteral("non-blocking");
        checkLines.append(QStringLiteral("[%1] %2 | %3 | %4")
                              .arg(state, severity, check.description, check.detail));
    }
    appendSection(QStringLiteral("Validation Checks"), checkLines);
    if (!validation.validation.idempotencyChecks.isEmpty()) {
        QStringList idempotencyLines;
        for (const ValidationCheckResult &check : validation.validation.idempotencyChecks) {
            const QString state = check.passed ? QStringLiteral("PASS") : QStringLiteral("FAIL");
            idempotencyLines.append(QStringLiteral("[%1] %2 | %3")
                                        .arg(state, check.description, check.detail));
        }
        appendSection(QStringLiteral("Already Patched Checks"), idempotencyLines);
    }
    if (!validation.validation.warnings.isEmpty()) {
        appendSection(QStringLiteral("Validation Warnings"), validation.validation.warnings);
    }
}

void MainWindow::appendExtractionSections(const ExtractResult &extraction)
{
    appendSection(QStringLiteral("Extraction"), {
        QStringLiteral("Source: %1").arg(extraction.sourcePath),
        QStringLiteral("Working root: %1").arg(extraction.workingRootPath),
        QStringLiteral("Skipped: %1").arg(extraction.skipped ? QStringLiteral("yes")
                                                             : QStringLiteral("no"))
    });
    if (!extraction.messages.isEmpty()) {
        appendSection(QStringLiteral("Extraction Notes"), extraction.messages);
    }
}

void MainWindow::appendApplySections(const ApplyResult &apply)
{
    appendSection(QStringLiteral("Apply"), {
        QStringLiteral("Working root: %1").arg(apply.workingRootPath),
        QStringLiteral("Skipped: %1").arg(apply.skipped ? QStringLiteral("yes")
                                                        : QStringLiteral("no"))
    });
    if (!apply.messages.isEmpty()) {
        appendSection(QStringLiteral("Apply Notes"), apply.messages);
    }
    if (apply.validation.ok) {
        appendValidationSections(apply.validation);
    }
}

void MainWindow::appendBuildSections(const BuildResult &build)
{
    appendSection(QStringLiteral("Build"), {
        QStringLiteral("Recipe id: %1").arg(build.recipeId),
        QStringLiteral("Recipe: %1").arg(build.recipeDisplayName),
        QStringLiteral("Working root: %1").arg(build.workingRootPath),
        QStringLiteral("Build dir: %1").arg(build.buildDir),
        QStringLiteral("Install dir: %1").arg(build.installDir),
        QStringLiteral("Verify drop dir: %1").arg(build.dropDir),
        QStringLiteral("CubeIDE package dir: %1").arg(build.packageDir)
    });
    if (!build.messages.isEmpty()) {
        appendSection(QStringLiteral("Build Notes"), build.messages);
    }
}

void MainWindow::appendVerifySections(const VerifyResult &verify)
{
    appendSection(QStringLiteral("Verify"), {
        QStringLiteral("Verify drop dir: %1").arg(verify.dropDir),
        QStringLiteral("Recipes run: %1").arg(verify.recipes.size())
    });
    for (const VerifyRecipeRunResult &recipeRun : verify.recipes) {
        appendSection(QStringLiteral("Verify Recipe"), {
            QStringLiteral("Recipe id: %1").arg(recipeRun.recipeId),
            QStringLiteral("Recipe: %1").arg(recipeRun.recipeDisplayName),
            QStringLiteral("Passed: %1").arg(recipeRun.ok ? QStringLiteral("yes") : QStringLiteral("no"))
        });
        QStringList checkLines;
        for (const VerifyCheckRunResult &check : recipeRun.checks) {
            checkLines.append(QStringLiteral("[%1] %2 | %3")
                                  .arg(check.passed ? QStringLiteral("PASS") : QStringLiteral("FAIL"),
                                       check.description,
                                       check.detail));
        }
        if (!checkLines.isEmpty()) {
            appendSection(QStringLiteral("Verify Checks"), checkLines);
        }
        if (!recipeRun.messages.isEmpty()) {
            appendSection(QStringLiteral("Verify Notes"), recipeRun.messages);
        }
    }
    if (!verify.messages.isEmpty()) {
        appendSection(QStringLiteral("Verify Summary"), verify.messages);
    }
}

void MainWindow::handleAnalysisFinished()
{
    setBusyState(false);

    const AnalysisResult analysis = m_analysisWatcher->result();
    setLogContext(m_runningAnalysisStepId);
    if (!analysis.ok) {
        applyStatusVisualState(m_cancelRequested ? StatusVisualState::Cancelled : StatusVisualState::Failed);
        setProgressPercent(0, m_cancelRequested ? QStringLiteral("Analyze cancelled") : QStringLiteral("Analyze failed"));
        setStepState(m_runningAnalysisStepId,
                     WorkflowStepState::Failed,
                     m_cancelRequested ? QStringLiteral("cancelled") : QStringLiteral("input open failed"));
        setStatusMessage(m_cancelRequested ? QStringLiteral("Analysis was cancelled.")
                                           : QStringLiteral("Failed to open input."));
        appendLogLine(analysis.errorMessage);
        m_cancelRequested = false;
        m_cancelToken.reset();
        return;
    }
    if (!analysis.errorMessage.isEmpty()) {
        appendLogLine(QStringLiteral("Analysis notes: %1").arg(analysis.errorMessage));
    }

    appendAnalysisSections(analysis);

    if (analysis.match.candidates.isEmpty()) {
        applyStatusVisualState(StatusVisualState::Failed);
        setProgressPercent(0, QStringLiteral("Analyze failed"));
        setStepState(m_runningAnalysisStepId,
                     WorkflowStepState::Failed,
                     QStringLiteral("no candidate profiles"));
        setStatusMessage(QStringLiteral("Analysis finished, but no candidate profiles were found."));
        m_cancelRequested = false;
        m_cancelToken.reset();
        return;
    }

    if (m_runningAnalysisStepId == WorkflowStepId::AnalyzeInput) {
        m_workflowState.inputAnalysis = analysis;
        refreshDefaultBuildRoot();
        setStepState(WorkflowStepId::AnalyzeInput, WorkflowStepState::Completed, QStringLiteral("ok"));
        markNextReadyAfter(WorkflowStepId::AnalyzeInput);
        applyStatusVisualState(StatusVisualState::Success);
        setProgressPercent(100, QStringLiteral("Analyze complete 100%"));
        setStatusMessage(QStringLiteral("Input analysis completed. Validate is now ready."));
    } else {
        m_workflowState.workingAnalysis = analysis;
        setStepState(WorkflowStepId::AnalyzeWorking, WorkflowStepState::Completed, QStringLiteral("ok"));
        markNextReadyAfter(WorkflowStepId::AnalyzeWorking);
        applyStatusVisualState(StatusVisualState::Success);
        setProgressPercent(100, QStringLiteral("Analyze complete 100%"));
        setStatusMessage(QStringLiteral("Working-tree analysis completed. Validate is now ready."));
    }
    m_cancelRequested = false;
    m_cancelToken.reset();
}

void MainWindow::handleValidationFinished()
{
    setBusyState(false);

    const ValidationResult validation = m_validationWatcher->result();
    setLogContext(m_runningValidationStepId);
    if (!validation.errorMessage.isEmpty()) {
        appendLogLine(QStringLiteral("Validation notes: %1").arg(validation.errorMessage));
    }
    if (!validation.ok) {
        applyStatusVisualState(m_cancelRequested ? StatusVisualState::Cancelled : StatusVisualState::Failed);
        setProgressPercent(0, m_cancelRequested ? QStringLiteral("Validate cancelled") : QStringLiteral("Validate failed"));
        setStepState(m_runningValidationStepId,
                     WorkflowStepState::Failed,
                     m_cancelRequested ? QStringLiteral("cancelled") : QStringLiteral("could not run"));
        setStatusMessage(m_cancelRequested ? QStringLiteral("Validation was cancelled.")
                                           : QStringLiteral("Validation could not run."));
        m_cancelRequested = false;
        m_cancelToken.reset();
        return;
    }

    appendValidationSections(validation);

    if (!validation.validation.applicable) {
        applyStatusVisualState(StatusVisualState::Failed);
        setProgressPercent(0, QStringLiteral("Validate failed"));
        setStepState(m_runningValidationStepId,
                     WorkflowStepState::Failed,
                     QStringLiteral("not applicable"));
        setStatusMessage(QStringLiteral("Validation failed for %1")
                             .arg(validation.analysis.selectedProfile.displayName));
        m_cancelRequested = false;
        m_cancelToken.reset();
        return;
    }

    m_workflowState.selectedProfileId = validation.analysis.selectedProfile.id;
    m_workflowState.workingRootPath = validation.preparation.workingRootPath;
    refreshBuildLayoutPreview();

    if (m_runningValidationStepId == WorkflowStepId::ValidateInput) {
        m_workflowState.inputValidation = validation;
        setStepState(WorkflowStepId::ValidateInput,
                     WorkflowStepState::Completed,
                     validation.validation.alreadyPatched ? QStringLiteral("already patched")
                                                          : QStringLiteral("ok"));
        markNextReadyAfter(WorkflowStepId::ValidateInput);

        if (validation.validation.alreadyPatched) {
            setStatusMessage(QStringLiteral("Input validation passed. Build is the next logical step."));
        } else if (validation.analysis.inspection.kind == SourceKind::Directory) {
            setStatusMessage(QStringLiteral("Directory input validated. Apply is now ready."));
        } else {
            setStatusMessage(QStringLiteral("Archive validated. Extract is now ready."));
        }
    } else {
        m_workflowState.workingValidation = validation;
        setStepState(WorkflowStepId::ValidateWorking,
                     WorkflowStepState::Completed,
                     validation.validation.alreadyPatched ? QStringLiteral("already patched")
                                                          : QStringLiteral("ok"));
        markNextReadyAfter(WorkflowStepId::ValidateWorking);

        if (validation.validation.alreadyPatched) {
            setStatusMessage(QStringLiteral("Working tree already looks patched. Build is the next logical step."));
        } else {
            setStatusMessage(QStringLiteral("Working-tree validation passed. Apply is now ready."));
        }
    }
    applyStatusVisualState(StatusVisualState::Success);
    setProgressPercent(100, QStringLiteral("Validate complete 100%"));
    m_cancelRequested = false;
    m_cancelToken.reset();
}
void MainWindow::handleExtractionFinished()
{
    setBusyState(false);
    setLogContext(WorkflowStepId::Extract);

    const ExtractResult extraction = m_extractionWatcher->result();
    m_workflowState.extraction = extraction;
    if (!extraction.ok) {
        applyStatusVisualState(extraction.cancelled ? StatusVisualState::Cancelled : StatusVisualState::Failed);
        setProgressPercent(0, extraction.cancelled ? QStringLiteral("Extraction cancelled") : QStringLiteral("Extraction failed"));
        setStepState(WorkflowStepId::Extract,
                     WorkflowStepState::Failed,
                     extraction.cancelled ? QStringLiteral("cancelled") : QStringLiteral("extract failed"));
        setStatusMessage(extraction.cancelled ? QStringLiteral("Extraction was cancelled.")
                                              : QStringLiteral("Extraction failed."));
        if (!extraction.errorMessage.isEmpty()) {
            appendLogLine(extraction.errorMessage);
        }
        m_cancelRequested = false;
        m_cancelToken.reset();
        return;
    }

    m_workflowState.workingRootPath = extraction.workingRootPath;
    m_workflowState.extractedRootPath = extraction.workingRootPath;
    refreshDefaultBuildRoot();
    appendExtractionSections(extraction);
    appendSection(QStringLiteral("Next Step"), {
        QStringLiteral("The workflow now switches to the extracted folder for the remaining mutable steps."),
        QStringLiteral("Working root: %1").arg(extraction.workingRootPath)
    });
    setStepState(WorkflowStepId::Extract,
                 extraction.skipped ? WorkflowStepState::Skipped : WorkflowStepState::Completed,
                 extraction.skipped ? QStringLiteral("not needed") : QStringLiteral("ok"));
    markNextReadyAfter(WorkflowStepId::Extract);
    applyStatusVisualState(StatusVisualState::Success);
    setProgressPercent(100, QStringLiteral("Extraction complete 100%"));
    setStatusMessage(QStringLiteral("Extraction completed. Analyze (Working Tree) is now ready."));
    m_cancelRequested = false;
    m_cancelToken.reset();
}

void MainWindow::handleApplyFinished()
{
    setBusyState(false);
    setLogContext(WorkflowStepId::Apply);

    const ApplyResult apply = m_applyWatcher->result();
    m_workflowState.apply = apply;
    if (!apply.ok) {
        applyStatusVisualState(apply.cancelled ? StatusVisualState::Cancelled : StatusVisualState::Failed);
        setProgressPercent(0, apply.cancelled ? QStringLiteral("Apply cancelled") : QStringLiteral("Apply failed"));
        setStepState(WorkflowStepId::Apply,
                     WorkflowStepState::Failed,
                     apply.cancelled ? QStringLiteral("cancelled") : QStringLiteral("apply failed"));
        setStatusMessage(apply.cancelled ? QStringLiteral("Apply was cancelled.")
                                         : QStringLiteral("Apply failed."));
        if (!apply.errorMessage.isEmpty()) {
            appendLogLine(apply.errorMessage);
        }
        m_cancelRequested = false;
        m_cancelToken.reset();
        return;
    }

    m_workflowState.workingValidation = apply.validation;
    appendApplySections(apply);
    setStepState(WorkflowStepId::Apply,
                 apply.skipped ? WorkflowStepState::Skipped : WorkflowStepState::Completed,
                 apply.skipped ? QStringLiteral("not needed") : QStringLiteral("ok"));
    markNextReadyAfter(WorkflowStepId::Apply);
    applyStatusVisualState(StatusVisualState::Success);
    setProgressPercent(100, QStringLiteral("Apply complete 100%"));
    setStatusMessage(QStringLiteral("Apply completed. Build is the next planned step."));
    m_cancelRequested = false;
    m_cancelToken.reset();
}

void MainWindow::handleBuildFinished()
{
    setBusyState(false);
    setLogContext(WorkflowStepId::Build);

    const BuildResult build = m_buildWatcher->result();
    m_workflowState.build = build;
    if (!build.ok) {
        applyStatusVisualState(build.cancelled ? StatusVisualState::Cancelled : StatusVisualState::Failed);
        setProgressPercent(0, build.cancelled ? QStringLiteral("Build cancelled") : QStringLiteral("Build failed"));
        setStepState(WorkflowStepId::Build,
                     WorkflowStepState::Failed,
                     build.cancelled ? QStringLiteral("cancelled") : QStringLiteral("build failed"));
        setStatusMessage(build.cancelled ? QStringLiteral("Build was cancelled.")
                                         : QStringLiteral("Build failed."));
        if (!build.errorMessage.isEmpty()) {
            appendLogLine(build.errorMessage);
        }
        refreshPackagePathDisplay();
        m_cancelRequested = false;
        m_cancelToken.reset();
        return;
    }

    appendBuildSections(build);
    setStepState(WorkflowStepId::Build, WorkflowStepState::Completed, QStringLiteral("ok"));
    markNextReadyAfter(WorkflowStepId::Build);
    applyStatusVisualState(StatusVisualState::Success);
    setProgressPercent(100, QStringLiteral("Build complete 100%"));
    setStatusMessage(QStringLiteral("Build completed. Verify is now ready."));
    refreshPackagePathDisplay();
    m_cancelRequested = false;
    m_cancelToken.reset();
}

void MainWindow::handleVerifyFinished()
{
    setBusyState(false);
    setLogContext(WorkflowStepId::Verify);

    const VerifyResult verify = m_verifyWatcher->result();
    m_workflowState.verify = verify;
    if (!verify.ok) {
        applyStatusVisualState(verify.cancelled ? StatusVisualState::Cancelled : StatusVisualState::Failed);
        setProgressPercent(0, verify.cancelled ? QStringLiteral("Verify cancelled") : QStringLiteral("Verify failed"));
        setStepState(WorkflowStepId::Verify,
                     WorkflowStepState::Failed,
                     verify.cancelled ? QStringLiteral("cancelled") : QStringLiteral("verify failed"));
        setStatusMessage(verify.cancelled ? QStringLiteral("Verify was cancelled.")
                                          : QStringLiteral("Verify failed."));
        if (!verify.errorMessage.isEmpty()) {
            appendLogLine(verify.errorMessage);
        }
        m_cancelRequested = false;
        m_cancelToken.reset();
        return;
    }

    appendVerifySections(verify);
    setStepState(WorkflowStepId::Verify, WorkflowStepState::Completed, QStringLiteral("ok"));
    markNextReadyAfter(WorkflowStepId::Verify);
    applyStatusVisualState(StatusVisualState::Success);
    setProgressPercent(100, QStringLiteral("Verify complete 100%"));
    setStatusMessage(QStringLiteral("Verify completed successfully. CubeIDE package is now available as an optional next step."));
    m_cancelRequested = false;
    m_cancelToken.reset();
}

void MainWindow::handlePackageFinished()
{
    setBusyState(false);
    setLogContext(WorkflowStepId::Package);

    const PackageResult package = m_packageWatcher->result();
    m_workflowState.package = package;
    if (!package.ok) {
        applyStatusVisualState(package.cancelled ? StatusVisualState::Cancelled : StatusVisualState::Failed);
        setProgressPercent(0, package.cancelled ? QStringLiteral("Package cancelled") : QStringLiteral("Package failed"));
        setStepState(WorkflowStepId::Package,
                     WorkflowStepState::Failed,
                     package.cancelled ? QStringLiteral("cancelled") : QStringLiteral("package failed"));
        setStatusMessage(package.cancelled ? QStringLiteral("CubeIDE package step was cancelled.")
                                           : QStringLiteral("CubeIDE package step failed."));
        if (!package.errorMessage.isEmpty()) {
            appendLogLine(package.errorMessage);
        }
        refreshPackagePathDisplay();
        m_cancelRequested = false;
        m_cancelToken.reset();
        return;
    }

    appendPackageSections(package);
    setStepState(WorkflowStepId::Package,
                 package.skipped ? WorkflowStepState::Skipped : WorkflowStepState::Completed,
                 package.skipped ? QStringLiteral("already available") : QStringLiteral("ok"));
    applyStatusVisualState(StatusVisualState::Success);
    setProgressPercent(100, QStringLiteral("Package complete 100%"));
    setStatusMessage(QStringLiteral("CubeIDE linker package is ready."));
    refreshPackagePathDisplay();
    m_cancelRequested = false;
    m_cancelToken.reset();
}

QString MainWindow::chooseProfileForValidation(const AnalysisResult &analysis, const QString &preferredProfileId)
{
    QStringList items;
    QHash<QString, QString> itemToProfileId;
    int preferredIndex = 0;

    for (int i = 0; i < analysis.match.candidates.size(); ++i) {
        const ProfileCandidate &candidate = analysis.match.candidates.at(i);
        const QString label = QStringLiteral("%1 | id=%2 | score=%3 | adjusted=%4 | %5")
                                  .arg(candidate.displayName,
                                       candidate.profileId,
                                       QString::number(candidate.score),
                                       QString::number(candidate.adjustedScore),
                                       candidate.matched ? QStringLiteral("matched")
                                                         : QStringLiteral("candidate"));
        items.append(label);
        itemToProfileId.insert(label, candidate.profileId);
        if (!preferredProfileId.isEmpty() && candidate.profileId == preferredProfileId) {
            preferredIndex = i;
        }
    }

    bool ok = false;
    const QString selectedItem = QInputDialog::getItem(
        this,
        QStringLiteral("Choose Profile"),
        QStringLiteral("Multiple candidate profiles were found. Choose which one to validate:"),
        items,
        preferredIndex,
        false,
        &ok);

    if (!ok || selectedItem.isEmpty()) {
        return QString();
    }

    const QString profileId = itemToProfileId.value(selectedItem);
    appendSection(QStringLiteral("Selected Candidate"), {
        QStringLiteral("User selected: %1").arg(selectedItem),
        QStringLiteral("Profile id: %1").arg(profileId)
    });
    return profileId;
}

QString MainWindow::chooseBuildRecipeForProfile(const VersionProfile &profile, const QString &preferredRecipeId)
{
    QStringList items;
    QHash<QString, QString> itemToRecipeId;
    int preferredIndex = 0;

    for (int i = 0; i < profile.buildRecipeIds.size(); ++i) {
        const QString &recipeId = profile.buildRecipeIds.at(i);
        if (!m_catalog.buildRecipes.contains(recipeId) || !m_catalog.buildRecipes.value(recipeId).enabled) {
            continue;
        }
        const RecipeSummary summary = m_catalog.buildRecipes.value(recipeId);
        const QString label = QStringLiteral("%1 | id=%2 | status=%3")
                                  .arg(summary.displayName.isEmpty() ? recipeId : summary.displayName,
                                       recipeId,
                                       summary.status.isEmpty() ? QStringLiteral("<unknown>") : summary.status);
        items.append(label);
        itemToRecipeId.insert(label, recipeId);
        if (!preferredRecipeId.isEmpty() && recipeId == preferredRecipeId) {
            preferredIndex = i;
        }
    }

    if (items.isEmpty()) {
        return {};
    }
    if (items.size() == 1) {
        return itemToRecipeId.value(items.constFirst());
    }

    bool ok = false;
    const QString selectedItem = QInputDialog::getItem(this,
                                                       QStringLiteral("Choose Build Recipe"),
                                                       QStringLiteral("Multiple build recipes are available. Choose which one to use:"),
                                                       items,
                                                       preferredIndex,
                                                       false,
                                                       &ok);
    if (!ok || selectedItem.isEmpty()) {
        return {};
    }

    const QString recipeId = itemToRecipeId.value(selectedItem);
    appendSection(QStringLiteral("Selected Build Recipe"), {
        QStringLiteral("User selected: %1").arg(selectedItem),
        QStringLiteral("Recipe id: %1").arg(recipeId)
    });
    return recipeId;
}

QString MainWindow::defaultWorkingRootForSelectedInput() const
{
    if (!m_workflowState.extractedRootPath.trimmed().isEmpty()) {
        return QDir::cleanPath(m_workflowState.extractedRootPath);
    }

    if (!m_workflowState.workingRootPath.trimmed().isEmpty()
        && workflowInputKind() == SourceKind::Directory) {
        return QDir::cleanPath(m_workflowState.workingRootPath);
    }

    const QString path = ui->pathEdit->text().trimmed();
    if (path.isEmpty()) {
        return {};
    }

    const QFileInfo inputInfo(path);
    if (inputInfo.exists() && inputInfo.isDir()) {
        return QDir::cleanPath(inputInfo.absoluteFilePath());
    }

    if (path.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)) {
        const QString defaultParentDir = inputInfo.absolutePath();
        const QString defaultName = !m_workflowState.inputAnalysis.inspection.rootName.isEmpty()
                                        ? m_workflowState.inputAnalysis.inspection.rootName
                                        : inputInfo.completeBaseName();
        if (!defaultParentDir.isEmpty() && !defaultName.isEmpty()) {
            return QDir(defaultParentDir).filePath(defaultName);
        }
    }

    return {};
}

void MainWindow::applyAutoBuildRootSuggestion(const QString &workingRootPath)
{
    if (!ui->buildRootEdit) {
        return;
    }

    const QString cleanedWorkingRoot = QDir::cleanPath(workingRootPath.trimmed());
    if (cleanedWorkingRoot.isEmpty()) {
        return;
    }

    const QString suggestedBuildRoot = QDir(cleanedWorkingRoot).filePath(QStringLiteral("build"));
    const QString currentText = ui->buildRootEdit->text().trimmed();
    const QString cleanedCurrent = currentText.isEmpty() ? QString() : QDir::cleanPath(currentText);
    const QString cleanedAuto = m_autoBuildRootPath.trimmed().isEmpty()
                                    ? QString()
                                    : QDir::cleanPath(m_autoBuildRootPath);

    const bool shouldReplace =
        !m_buildRootUserEdited
        || cleanedCurrent.isEmpty()
        || (!cleanedAuto.isEmpty() && cleanedCurrent == cleanedAuto);

    m_autoBuildRootPath = suggestedBuildRoot;
    if (shouldReplace) {
        ui->buildRootEdit->setText(QDir::toNativeSeparators(suggestedBuildRoot));
    }
    refreshBuildLayoutPreview();
}

void MainWindow::refreshDefaultBuildRoot()
{
    applyAutoBuildRootSuggestion(defaultWorkingRootForSelectedInput());
}

QString MainWindow::currentPreviewProfileId() const
{
    if (m_workflowState.workingValidation.ok && m_workflowState.workingValidation.analysis.hasSelectedProfile) {
        return m_workflowState.workingValidation.analysis.selectedProfile.id;
    }
    if (m_workflowState.inputValidation.ok && m_workflowState.inputValidation.analysis.hasSelectedProfile) {
        return m_workflowState.inputValidation.analysis.selectedProfile.id;
    }
    return m_workflowState.selectedProfileId.trimmed();
}

QString MainWindow::currentPreviewBuildRecipeId(const VersionProfile *profile) const
{
    if (!m_selectedBuildRecipeId.trimmed().isEmpty()) {
        return m_selectedBuildRecipeId.trimmed();
    }
    if (!profile || profile->buildRecipeIds.isEmpty()) {
        return {};
    }
    return profile->buildRecipeIds.constFirst();
}

void MainWindow::refreshBuildLayoutPreview()
{
    if (!m_buildLayoutPreviewLabel) {
        return;
    }

    const QString profileId = currentPreviewProfileId();
    if (profileId.isEmpty()) {
        m_buildLayoutPreviewLabel->setText(
            QStringLiteral("Build layout preview will appear here after validation selects a profile."));
        return;
    }

    const VersionProfile *profile = nullptr;
    for (const VersionProfile &candidate : m_catalog.profiles) {
        if (candidate.id == profileId) {
            profile = &candidate;
            break;
        }
    }
    if (!profile) {
        m_buildLayoutPreviewLabel->setText(
            QStringLiteral("Build layout preview is unavailable because the selected profile is missing from the catalog."));
        refreshPackagePathDisplay();
        return;
    }

    const QString workingRoot = defaultWorkingRootForSelectedInput();
    if (workingRoot.isEmpty()) {
        m_buildLayoutPreviewLabel->setText(
            QStringLiteral("Build layout preview is unavailable because the working root is not known yet."));
        refreshPackagePathDisplay();
        return;
    }

    const QString buildRecipeId = currentPreviewBuildRecipeId(profile);
    if (buildRecipeId.isEmpty()) {
        m_buildLayoutPreviewLabel->setText(
            QStringLiteral("Build layout preview is unavailable because no build recipe is configured for the selected profile."));
        refreshPackagePathDisplay();
        return;
    }

    const BuildLayoutPreview preview = WorkflowService::previewBuildLayout(
        m_catalog,
        profileId,
        buildRecipeId,
        workingRoot,
        selectedBuildRootOverride());
    if (!preview.ok) {
        m_buildLayoutPreviewLabel->setText(
            QStringLiteral("Build layout preview is unavailable: %1")
                .arg(preview.errorMessage.isEmpty()
                         ? QStringLiteral("unknown error")
                         : preview.errorMessage));
        refreshPackagePathDisplay();
        return;
    }

    m_buildLayoutPreviewLabel->setText(
        QStringLiteral("Recipe: %1 (%2)\nBuild dir: %3\nInstall dir: %4\nVerify drop dir: %5\nCubeIDE package dir: %6")
            .arg(preview.recipeDisplayName,
                 preview.recipeId,
                 QDir::toNativeSeparators(preview.buildDir),
                 QDir::toNativeSeparators(preview.installDir),
                 QDir::toNativeSeparators(preview.dropDir),
                 QDir::toNativeSeparators(preview.packageDir)));
    refreshPackagePathDisplay();
}

void MainWindow::refreshPackagePathDisplay()
{
    if (!ui->packageDirEdit || !ui->openDropDirButton) {
        return;
    }

    QString packagePath;
    if (m_workflowState.package.ok && !m_workflowState.package.packageDir.trimmed().isEmpty()) {
        packagePath = m_workflowState.package.packageDir;
    } else if (!m_workflowState.build.packageDir.trimmed().isEmpty()) {
        packagePath = m_workflowState.build.packageDir;
    } else {
        const QString profileId = currentPreviewProfileId();
        const VersionProfile *profile = nullptr;
        for (const VersionProfile &candidate : m_catalog.profiles) {
            if (candidate.id == profileId) {
                profile = &candidate;
                break;
            }
        }
        const QString workingRoot = defaultWorkingRootForSelectedInput();
        const QString buildRecipeId = currentPreviewBuildRecipeId(profile);
        if (profile && !workingRoot.isEmpty() && !buildRecipeId.isEmpty()) {
            const BuildLayoutPreview preview = WorkflowService::previewBuildLayout(
                m_catalog,
                profileId,
                buildRecipeId,
                workingRoot,
                selectedBuildRootOverride());
            if (preview.ok) {
                packagePath = preview.packageDir;
            }
        }
    }

    ui->packageDirEdit->setText(QDir::toNativeSeparators(packagePath));
    ui->openDropDirButton->setEnabled(!packagePath.isEmpty() && QFileInfo(packagePath).isDir());
}

void MainWindow::autoFillCubeIdeRoot()
{
    if (!ui->cubeIdeRootEdit || !ui->cubeIdeRootEdit->text().trimmed().isEmpty()) {
        return;
    }

    const QString autoRoot = detectNewestCubeIdeRoot();
    if (autoRoot.isEmpty()) {
        return;
    }

    const QSignalBlocker blocker(ui->cubeIdeRootEdit);
    ui->cubeIdeRootEdit->setText(QDir::toNativeSeparators(autoRoot));
    appendSection(QStringLiteral("CubeIDE Root"), {
        QStringLiteral("Auto-detected STM32CubeIDE root: %1")
            .arg(QDir::toNativeSeparators(autoRoot))
    });
    setStatusMessage(QStringLiteral("Auto-detected STM32CubeIDE root"));
}

void MainWindow::appendPackageSections(const PackageResult &package)
{
    appendSection(QStringLiteral("CubeIDE Package"), {
        QStringLiteral("Source drop dir: %1").arg(package.sourceDropDir),
        QStringLiteral("Package dir: %1").arg(package.packageDir)
    });
    if (!package.messages.isEmpty()) {
        appendSection(QStringLiteral("Package Notes"), package.messages);
    }
}

QString MainWindow::selectedBuildRootOverride() const
{
    if (!ui->buildRootEdit) {
        return {};
    }

    const QString value = ui->buildRootEdit->text().trimmed();
    if (value.isEmpty()) {
        return {};
    }
    return QDir::cleanPath(value);
}

QString MainWindow::selectedCubeIdeRoot() const
{
    if (!ui->cubeIdeRootEdit) {
        return {};
    }

    const QString value = ui->cubeIdeRootEdit->text().trimmed();
    if (value.isEmpty()) {
        return {};
    }
    return QDir::cleanPath(value);
}

void MainWindow::startBuildStep()
{
    const VersionProfile *profile = nullptr;
    if (m_workflowState.workingValidation.ok && m_workflowState.workingValidation.analysis.hasSelectedProfile) {
        profile = &m_workflowState.workingValidation.analysis.selectedProfile;
    } else if (m_workflowState.inputValidation.ok && m_workflowState.inputValidation.analysis.hasSelectedProfile) {
        profile = &m_workflowState.inputValidation.analysis.selectedProfile;
    }

    if (!profile) {
        setStepState(WorkflowStepId::Build, WorkflowStepState::Failed, QStringLiteral("no selected profile"));
        applyStatusVisualState(StatusVisualState::Failed);
        setStatusMessage(QStringLiteral("Build cannot continue without a selected profile."));
        appendLogLine(QStringLiteral("Build was skipped because no selected profile is available."));
        return;
    }

    const QString workingRoot = !m_workflowState.extractedRootPath.isEmpty()
                                    ? m_workflowState.extractedRootPath
                                    : m_workflowState.workingRootPath;
    if (workingRoot.trimmed().isEmpty()) {
        setStepState(WorkflowStepId::Build, WorkflowStepState::Failed, QStringLiteral("no working root"));
        applyStatusVisualState(StatusVisualState::Failed);
        setStatusMessage(QStringLiteral("Build cannot continue without a working root."));
        appendLogLine(QStringLiteral("Build was skipped because the working root path is empty."));
        return;
    }

    QString buildRecipeId = chooseBuildRecipeForProfile(*profile, m_selectedBuildRecipeId);
    if (buildRecipeId.isEmpty()) {
        applyStatusVisualState(StatusVisualState::Cancelled);
        setStatusMessage(QStringLiteral("Build was cancelled."));
        appendLogLine(QStringLiteral("Build was cancelled before a build recipe was selected."));
        return;
    }

    m_selectedBuildRecipeId = buildRecipeId;
    const QString buildRootOverride = selectedBuildRootOverride();
    m_cancelRequested = false;
    m_cancelToken = std::make_shared<std::atomic_bool>(false);
    setLogContext(WorkflowStepId::Build);
    setStepState(WorkflowStepId::Build, WorkflowStepState::Running, QString());
    applyStatusVisualState(StatusVisualState::Running);
    setBusyState(true, QStringLiteral("Building patched linker..."));
    setProgressPercent(0, QStringLiteral("Building... %p%"));
    appendSection(QStringLiteral("Step Start"), {
        QStringLiteral("Action: Build patched linker"),
        QStringLiteral("Profile id: %1").arg(profile->id),
        QStringLiteral("Build recipe id: %1").arg(buildRecipeId),
        QStringLiteral("Working root: %1").arg(workingRoot),
        QStringLiteral("Build / output root: %1")
            .arg(buildRootOverride.isEmpty()
                     ? QStringLiteral("<recipe default>")
                     : QDir::toNativeSeparators(buildRootOverride))
    });

    m_buildWatcher->setFuture(QtConcurrent::run([this, profileId = profile->id, buildRecipeId, workingRoot, buildRootOverride]() {
        return WorkflowService::buildPatchedTree(
            m_catalog,
            profileId,
            buildRecipeId,
            workingRoot,
            buildRootOverride,
            [this](int percent, const QString &message) {
                QMetaObject::invokeMethod(this, [this, percent, message]() {
                    setProgressPercent(percent, QStringLiteral("Build... %p%"));
                    setStatusMessage(message);
                }, Qt::QueuedConnection);
            },
            [this](const QString &line) {
                QMetaObject::invokeMethod(this, [this, line]() {
                    appendLogLine(line);
                }, Qt::QueuedConnection);
            },
            m_cancelToken);
    }));
}

void MainWindow::startVerifyStep()
{
    const VersionProfile *profile = nullptr;
    if (m_workflowState.workingValidation.ok && m_workflowState.workingValidation.analysis.hasSelectedProfile) {
        profile = &m_workflowState.workingValidation.analysis.selectedProfile;
    } else if (m_workflowState.inputValidation.ok && m_workflowState.inputValidation.analysis.hasSelectedProfile) {
        profile = &m_workflowState.inputValidation.analysis.selectedProfile;
    }

    if (!profile) {
        setStepState(WorkflowStepId::Verify, WorkflowStepState::Failed, QStringLiteral("no selected profile"));
        applyStatusVisualState(StatusVisualState::Failed);
        setStatusMessage(QStringLiteral("Verify cannot continue without a selected profile."));
        appendLogLine(QStringLiteral("Verify was skipped because no selected profile is available."));
        return;
    }

    const QString dropDir = m_workflowState.build.dropDir;
    const QString cubeIdeRoot = selectedCubeIdeRoot();
    if (dropDir.trimmed().isEmpty()) {
        setStepState(WorkflowStepId::Verify, WorkflowStepState::Failed, QStringLiteral("no drop dir"));
        applyStatusVisualState(StatusVisualState::Failed);
        setStatusMessage(QStringLiteral("Verify cannot continue without a built artifact directory."));
        appendLogLine(QStringLiteral("Verify was skipped because the build drop directory is empty."));
        return;
    }

    m_cancelRequested = false;
    m_cancelToken = std::make_shared<std::atomic_bool>(false);
    setLogContext(WorkflowStepId::Verify);
    setStepState(WorkflowStepId::Verify, WorkflowStepState::Running, QString());
    applyStatusVisualState(StatusVisualState::Running);
    setBusyState(true, QStringLiteral("Running verify recipes..."));
    setProgressPercent(0, QStringLiteral("Verify... %p%"));
    appendSection(QStringLiteral("Step Start"), {
        QStringLiteral("Action: Run verify recipes"),
        QStringLiteral("Profile id: %1").arg(profile->id),
        QStringLiteral("Drop dir: %1").arg(dropDir),
        QStringLiteral("CubeIDE / compiler root: %1")
            .arg(cubeIdeRoot.isEmpty()
                     ? QStringLiteral("<auto-detect from PATH / C:\\ST>")
                     : QDir::toNativeSeparators(cubeIdeRoot))
    });

    const QStringList verifyRecipeIds = profile->verifyRecipeIds;
    m_verifyWatcher->setFuture(QtConcurrent::run([this, profileId = profile->id, verifyRecipeIds, dropDir, cubeIdeRoot]() {
        return WorkflowService::verifyBuild(
            m_catalog,
            profileId,
            verifyRecipeIds,
            dropDir,
            cubeIdeRoot,
            [this](int percent, const QString &message) {
                QMetaObject::invokeMethod(this, [this, percent, message]() {
                    setProgressPercent(percent, QStringLiteral("Verify... %p%"));
                    setStatusMessage(message);
                }, Qt::QueuedConnection);
            },
            [this](const QString &line) {
                QMetaObject::invokeMethod(this, [this, line]() {
                    appendLogLine(line);
                }, Qt::QueuedConnection);
            },
            m_cancelToken);
    }));
}

void MainWindow::startPackageStep()
{
    const QString sourceDropDir = m_workflowState.build.dropDir;
    const QString packageDir = m_workflowState.build.packageDir;
    if (sourceDropDir.trimmed().isEmpty()) {
        setStepState(WorkflowStepId::Package, WorkflowStepState::Failed, QStringLiteral("no drop dir"));
        applyStatusVisualState(StatusVisualState::Failed);
        setStatusMessage(QStringLiteral("Package cannot continue without a build drop directory."));
        appendLogLine(QStringLiteral("CubeIDE package was skipped because the build drop directory is empty."));
        return;
    }
    if (packageDir.trimmed().isEmpty()) {
        setStepState(WorkflowStepId::Package, WorkflowStepState::Failed, QStringLiteral("no package dir"));
        applyStatusVisualState(StatusVisualState::Failed);
        setStatusMessage(QStringLiteral("Package cannot continue without a target package directory."));
        appendLogLine(QStringLiteral("CubeIDE package was skipped because the target package directory is empty."));
        return;
    }

    m_cancelRequested = false;
    m_cancelToken = std::make_shared<std::atomic_bool>(false);
    setLogContext(WorkflowStepId::Package);
    setStepState(WorkflowStepId::Package, WorkflowStepState::Running, QString());
    applyStatusVisualState(StatusVisualState::Running);
    setBusyState(true, QStringLiteral("Creating CubeIDE linker package..."));
    setProgressPercent(0, QStringLiteral("Package... %p%"));
    appendSection(QStringLiteral("Step Start"), {
        QStringLiteral("Action: Create CubeIDE linker package"),
        QStringLiteral("Source drop dir: %1").arg(sourceDropDir),
        QStringLiteral("Package dir: %1").arg(packageDir)
    });

    m_packageWatcher->setFuture(QtConcurrent::run([this, sourceDropDir, packageDir]() {
        return WorkflowService::createCubeIdePackage(
            sourceDropDir,
            packageDir,
            [this](int percent, const QString &message) {
                QMetaObject::invokeMethod(this, [this, percent, message]() {
                    setProgressPercent(percent, QStringLiteral("Package... %p%"));
                    setStatusMessage(message);
                }, Qt::QueuedConnection);
            },
            [this](const QString &line) {
                QMetaObject::invokeMethod(this, [this, line]() {
                    appendLogLine(line);
                }, Qt::QueuedConnection);
            },
            m_cancelToken);
    }));
}

void MainWindow::startAnalysisStep(WorkflowStepId id, const QString &path, const QString &message)
{
    m_runningAnalysisStepId = id;
    m_cancelRequested = false;
    m_cancelToken = std::make_shared<std::atomic_bool>(false);
    setLogContext(id);
    setStepState(id, WorkflowStepState::Running, QString());
    applyStatusVisualState(StatusVisualState::Running);
    setBusyState(true, message);
    setProgressPercent(0, QStringLiteral("%1 %p%").arg(message));
    m_analysisWatcher->setFuture(QtConcurrent::run([this, catalog = m_catalog, path]() {
        return AnalysisService::analyzePath(
            catalog,
            path,
            [this](int percent, const QString &progressMessage) {
                QMetaObject::invokeMethod(this, [this, percent, progressMessage]() {
                    setProgressPercent(percent, QStringLiteral("Analyze... %p%"));
                    setStatusMessage(progressMessage);
                }, Qt::QueuedConnection);
            },
            m_cancelToken);
    }));
}

void MainWindow::startValidationStep(WorkflowStepId id,
                                     const QString &path,
                                     const QString &selectedProfileId,
                                     const QString &message)
{
    m_runningValidationStepId = id;
    m_cancelRequested = false;
    m_cancelToken = std::make_shared<std::atomic_bool>(false);
    setLogContext(id);
    setStepState(id, WorkflowStepState::Running, QString());
    applyStatusVisualState(StatusVisualState::Running);
    setBusyState(true, message);
    setProgressPercent(0, QStringLiteral("Validate... %p%"));
    m_validationWatcher->setFuture(QtConcurrent::run([this, catalog = m_catalog, path, selectedProfileId]() {
        return AnalysisService::validatePath(
            catalog,
            path,
            selectedProfileId,
            [this](int percent, const QString &progressMessage) {
                QMetaObject::invokeMethod(this, [this, percent, progressMessage]() {
                    setProgressPercent(percent, QStringLiteral("Validate... %p%"));
                    setStatusMessage(progressMessage);
                }, Qt::QueuedConnection);
            },
            m_cancelToken);
    }));
}

void MainWindow::startExtraction(const QString &path, const ExtractPlan &plan)
{
    setLogContext(WorkflowStepId::Extract);
    m_cancelRequested = false;
    m_cancelToken = std::make_shared<std::atomic_bool>(false);
    setStepState(WorkflowStepId::Extract, WorkflowStepState::Running, QString());
    applyStatusVisualState(StatusVisualState::Running);
    setBusyState(true, QStringLiteral("Extracting ZIP into working directory..."));
    setProgressPercent(0, QStringLiteral("Extracting archive... %p%"));
    m_lastExtractProgressLoggedPercent = -1;
    m_extractionWatcher->setFuture(QtConcurrent::run([this, path, plan]() {
        return WorkflowService::extractSource(
            path,
            plan,
            [this](int current, int total, const QString &relativePath) {
                QMetaObject::invokeMethod(this, [this, current, total, relativePath]() {
                const int percent = total > 0 ? (current * 100) / total : 0;
                setProgressPercent(percent,
                                   QStringLiteral("Extracting archive... %p% (%1/%2 files)")
                                       .arg(current)
                                       .arg(total));
                setStatusMessage(QStringLiteral("Extracting archive: %1% (%2/%3)")
                                     .arg(percent)
                                     .arg(current)
                                     .arg(total));

                if (percent >= m_lastExtractProgressLoggedPercent + 10 || percent == 100) {
                    m_lastExtractProgressLoggedPercent = percent;
                    appendLogLine(QStringLiteral("Progress: %1% (%2/%3)%4")
                                      .arg(percent)
                                      .arg(current)
                                      .arg(total)
                                      .arg(relativePath.isEmpty()
                                               ? QString()
                                               : QStringLiteral(" | last file: %1").arg(relativePath)));
                }
                }, Qt::QueuedConnection);
            },
            m_cancelToken);
    }));
}

void MainWindow::startApply(const QString &profileId, const QString &workingRootPath)
{
    setLogContext(WorkflowStepId::Apply);
    if (profileId.trimmed().isEmpty()) {
        setStepState(WorkflowStepId::Apply,
                     WorkflowStepState::Failed,
                     QStringLiteral("no selected profile"));
        applyStatusVisualState(StatusVisualState::Failed);
        setStatusMessage(QStringLiteral("Apply cannot continue without a selected profile."));
        appendLogLine(QStringLiteral("Apply was skipped because no selected profile is available."));
        return;
    }

    if (workingRootPath.trimmed().isEmpty()) {
        setStepState(WorkflowStepId::Apply,
                     WorkflowStepState::Failed,
                     QStringLiteral("no working root"));
        applyStatusVisualState(StatusVisualState::Failed);
        setStatusMessage(QStringLiteral("Apply cannot continue without a working directory."));
        appendLogLine(QStringLiteral("Apply was skipped because the working root path is empty."));
        return;
    }

    const bool inPlaceDirectoryApply =
        m_workflowState.inputAnalysis.inspection.kind == SourceKind::Directory
        && QDir::cleanPath(workingRootPath) == QDir::cleanPath(m_workflowState.sourcePath)
        && m_workflowState.extractedRootPath.isEmpty();
    if (inPlaceDirectoryApply) {
        const auto answer = QMessageBox::warning(
            this,
            QStringLiteral("Apply In Place"),
            QStringLiteral("This source was opened from a directory, so Apply will modify that directory in place:\n\n%1\n\nContinue?")
                .arg(QDir::toNativeSeparators(workingRootPath)),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) {
            applyStatusVisualState(StatusVisualState::Cancelled);
            setStatusMessage(QStringLiteral("Apply was cancelled."));
            appendLogLine(QStringLiteral("Apply was cancelled before modifying the source directory in place."));
            return;
        }
    }

    m_cancelRequested = false;
    m_cancelToken = std::make_shared<std::atomic_bool>(false);
    setStepState(WorkflowStepId::Apply, WorkflowStepState::Running, QString());
    applyStatusVisualState(StatusVisualState::Running);
    setBusyState(true, QStringLiteral("Applying patch recipe..."));
    setProgressPercent(0, QStringLiteral("Apply... %p%"));
    appendSection(QStringLiteral("Step Start"), {
        QStringLiteral("Action: Apply patch recipe"),
        QStringLiteral("Profile id: %1").arg(profileId),
        QStringLiteral("Working root: %1").arg(workingRootPath)
    });
    m_applyWatcher->setFuture(QtConcurrent::run([this, catalog = m_catalog, profileId, workingRootPath]() {
        return WorkflowService::applyPatch(
            catalog,
            profileId,
            workingRootPath,
            [this](int percent, const QString &progressMessage) {
                QMetaObject::invokeMethod(this, [this, percent, progressMessage]() {
                    setProgressPercent(percent, QStringLiteral("Apply... %p%"));
                    setStatusMessage(progressMessage);
                }, Qt::QueuedConnection);
            },
            [this](const QString &line) {
                QMetaObject::invokeMethod(this, [this, line]() {
                    appendLogLine(line);
                }, Qt::QueuedConnection);
            },
            m_cancelToken);
    }));
}

void MainWindow::setSelectedPath(const QString &path)
{
    ui->pathEdit->setText(QDir::toNativeSeparators(path));
    resetWorkflowState(false);
    refreshDefaultBuildRoot();
    refreshBuildLayoutPreview();
}

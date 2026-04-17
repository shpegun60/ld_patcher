#include "profilesdialog.h"

#include "catalogloader.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>
#include <utility>

namespace {

QString profileListLabel(const VersionProfile &profile)
{
    const QString enabledText = profile.enabled ? QStringLiteral("enabled")
                                                : QStringLiteral("disabled");
    return QStringLiteral("%1 [%2, %3]")
        .arg(profile.displayName, profile.status, enabledText);
}

QString joinIndented(const QStringList &values)
{
    if (values.isEmpty()) {
        return QStringLiteral("<none>");
    }

    QStringList lines;
    for (const QString &value : values) {
        lines.append(QStringLiteral("- %1").arg(value));
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace

ProfilesDialog::ProfilesDialog(const CatalogData &catalog, QWidget *parent)
    : QDialog(parent)
    , m_catalog(catalog)
{
    buildUi();
    refreshList();
}

void ProfilesDialog::buildUi()
{
    setWindowTitle(QStringLiteral("Profiles"));
    resize(980, 620);

    auto *rootLayout = new QVBoxLayout(this);
    auto *descriptionLabel = new QLabel(
        QStringLiteral("These profiles route source trees to patch/build/verify recipes. "
                       "You can disable a profile without deleting its JSON file."),
        this);
    descriptionLabel->setWordWrap(true);
    rootLayout->addWidget(descriptionLabel);

    auto *splitter = new QSplitter(this);
    rootLayout->addWidget(splitter, 1);

    m_profileList = new QListWidget(splitter);
    m_detailsEdit = new QPlainTextEdit(splitter);
    m_detailsEdit->setReadOnly(true);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 3);

    auto *controlsLayout = new QHBoxLayout();
    m_enabledCheck = new QCheckBox(QStringLiteral("Profile enabled"), this);
    m_applyButton = new QPushButton(QStringLiteral("Apply"), this);
    m_statusLabel = new QLabel(QStringLiteral("Select a profile to inspect it."), this);
    controlsLayout->addWidget(m_enabledCheck);
    controlsLayout->addWidget(m_applyButton);
    controlsLayout->addWidget(m_statusLabel, 1);
    rootLayout->addLayout(controlsLayout);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    rootLayout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_profileList,
            &QListWidget::currentItemChanged,
            this,
            &ProfilesDialog::handleCurrentItemChanged);
    connect(m_applyButton, &QPushButton::clicked, this, &ProfilesDialog::applyCurrentProfileState);
}

void ProfilesDialog::refreshList()
{
    m_profileList->clear();
    for (const VersionProfile &profile : std::as_const(m_catalog.profiles)) {
        m_profileList->addItem(profileListLabel(profile));
    }

    if (m_profileList->count() > 0) {
        m_profileList->setCurrentRow(0);
    } else {
        m_detailsEdit->setPlainText(QStringLiteral("No profiles found."));
        m_enabledCheck->setEnabled(false);
        m_applyButton->setEnabled(false);
    }
}

void ProfilesDialog::handleCurrentItemChanged(QListWidgetItem *current, QListWidgetItem *previous)
{
    Q_UNUSED(previous);
    if (!current) {
        m_detailsEdit->clear();
        m_enabledCheck->setEnabled(false);
        m_applyButton->setEnabled(false);
        return;
    }

    const int index = m_profileList->row(current);
    showProfileDetails(index);
}

void ProfilesDialog::showProfileDetails(int index)
{
    if (index < 0 || index >= m_catalog.profiles.size()) {
        m_detailsEdit->clear();
        m_enabledCheck->setEnabled(false);
        m_applyButton->setEnabled(false);
        return;
    }

    const VersionProfile &profile = m_catalog.profiles.at(index);
    m_enabledCheck->setEnabled(true);
    m_enabledCheck->setChecked(profile.enabled);
    m_applyButton->setEnabled(true);

    QStringList lines;
    lines.append(QStringLiteral("Display name: %1").arg(profile.displayName));
    lines.append(QStringLiteral("Id: %1").arg(profile.id));
    lines.append(QStringLiteral("Family: %1").arg(profile.family));
    lines.append(QStringLiteral("Status: %1").arg(profile.status));
    lines.append(QStringLiteral("Enabled: %1").arg(profile.enabled ? QStringLiteral("true")
                                                                   : QStringLiteral("false")));
    lines.append(QStringLiteral("Catalog file: %1").arg(profile.filePath));
    lines.append(QString());
    lines.append(QStringLiteral("Patch recipe id: %1").arg(profile.patchRecipeId));
    lines.append(QStringLiteral("Build recipes:\n%1").arg(joinIndented(profile.buildRecipeIds)));
    lines.append(QStringLiteral("Verify recipes:\n%1").arg(joinIndented(profile.verifyRecipeIds)));
    lines.append(QString());
    lines.append(QStringLiteral("Tags:\n%1").arg(joinIndented(profile.tags)));
    lines.append(QStringLiteral("Archive roots:\n%1").arg(joinIndented(profile.archiveRoots)));
    lines.append(QStringLiteral("Folder patterns:\n%1").arg(joinIndented(profile.folderNamePatterns)));
    lines.append(QString());

    QStringList detectionHints;
    for (const DetectionHint &hint : profile.detectionHints) {
        detectionHints.append(QStringLiteral("%1 | %2 | %3")
                                  .arg(hint.type,
                                       hint.path.isEmpty() ? QStringLiteral("<no path>") : hint.path,
                                       hint.description));
    }
    lines.append(QStringLiteral("Detection hints:\n%1").arg(joinIndented(detectionHints)));
    lines.append(QString());
    lines.append(QStringLiteral("Notes:\n%1").arg(joinIndented(profile.notes)));

    m_detailsEdit->setPlainText(lines.join(QLatin1Char('\n')));
}

int ProfilesDialog::currentProfileIndex() const
{
    if (!m_profileList) {
        return -1;
    }
    return m_profileList->currentRow();
}

void ProfilesDialog::applyCurrentProfileState()
{
    const int index = currentProfileIndex();
    if (index < 0 || index >= m_catalog.profiles.size()) {
        return;
    }

    VersionProfile &profile = m_catalog.profiles[index];
    const bool newEnabled = m_enabledCheck->isChecked();
    if (profile.enabled == newEnabled) {
        m_statusLabel->setText(QStringLiteral("Nothing changed for the selected profile."));
        return;
    }

    QString errorMessage;
    if (!CatalogLoader::setProfileEnabled(m_catalog, profile.id, newEnabled, &errorMessage)) {
        m_statusLabel->setText(QStringLiteral("Failed to update profile."));
        return;
    }

    profile.enabled = newEnabled;
    m_profileList->item(index)->setText(profileListLabel(profile));
    showProfileDetails(index);
    m_statusLabel->setText(QStringLiteral("Updated %1.").arg(profile.displayName));
    emit profilesChanged();
}

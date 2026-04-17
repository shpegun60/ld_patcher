#ifndef PROFILESDIALOG_H
#define PROFILESDIALOG_H

#include "catalogtypes.h"

#include <QDialog>
#include <QVector>

class QCheckBox;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class QPushButton;

class ProfilesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ProfilesDialog(const CatalogData &catalog, QWidget *parent = nullptr);

signals:
    void profilesChanged();

private slots:
    void handleCurrentItemChanged(QListWidgetItem *current, QListWidgetItem *previous);
    void applyCurrentProfileState();

private:
    void buildUi();
    void refreshList();
    void showProfileDetails(int index);
    int currentProfileIndex() const;

private:
    CatalogData m_catalog;
    QListWidget *m_profileList = nullptr;
    QPlainTextEdit *m_detailsEdit = nullptr;
    QCheckBox *m_enabledCheck = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_applyButton = nullptr;
};

#endif // PROFILESDIALOG_H

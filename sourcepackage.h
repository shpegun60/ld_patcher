#ifndef SOURCEPACKAGE_H
#define SOURCEPACKAGE_H

#include "catalogtypes.h"
#include "ziparchive.h"

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

class SourcePackage
{
public:
    SourcePackage() = default;

    bool open(const QString &inputPath, QString *errorMessage);

    bool isOpen() const;
    SourceKind kind() const;
    QString inputPath() const;
    QString rootName() const;

    bool existsRelative(const QString &relativePath) const;
    QByteArray readBytesRelative(const QString &relativePath, QString *errorMessage) const;
    QString readTextRelative(const QString &relativePath, QString *errorMessage) const;
    QStringList listRelativeFileEntries() const;
    SourceInspection inspectBasic(QString *errorMessage) const;

private:
    QString normalizeRelativePath(const QString &relativePath) const;
    QString archiveMemberPath(const QString &relativePath) const;

private:
    SourceKind m_kind = SourceKind::Unknown;
    QString m_inputPath;
    QString m_rootName;
    QString m_archiveRootPrefix;
    ZipArchive m_zipArchive;
    mutable QHash<QString, QString> m_textCache;
    mutable bool m_basicInspectionCached = false;
    mutable SourceInspection m_cachedBasicInspection;
};

#endif // SOURCEPACKAGE_H

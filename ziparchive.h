#ifndef ZIPARCHIVE_H
#define ZIPARCHIVE_H

#include <QHash>
#include <QByteArray>
#include <QString>

struct zip;
struct zip_file;

class ZipArchive
{
public:
    ZipArchive();
    ~ZipArchive();

    ZipArchive(const ZipArchive &) = delete;
    ZipArchive &operator=(const ZipArchive &) = delete;

    bool open(const QString &archivePath, QString *errorMessage);
    bool isOpen() const;
    QString archivePath() const;

    quint64 entryCount() const;
    QString normalizedEntryAt(quint64 index) const;
    bool entryIsDirectory(quint64 index) const;
    bool containsNormalizedPath(const QString &normalizedPath) const;
    QByteArray readBytesByNormalizedPath(const QString &normalizedPath, QString *errorMessage) const;
    QString readTextByNormalizedPath(const QString &normalizedPath, QString *errorMessage) const;
    void close();

private:
    zip *m_archive = nullptr;
    QString m_archivePath;
    quint64 m_entryCount = 0;
    mutable QHash<QString, qint64> m_entryIndexByNormalizedPath;
};

#endif // ZIPARCHIVE_H

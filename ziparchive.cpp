#include "ziparchive.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <cstdint>

namespace {
using zip_t = ::zip;
using zip_file_t = ::zip_file;
using zip_int64_t = std::int64_t;
using zip_uint64_t = std::uint64_t;
using zip_uint32_t = std::uint32_t;
using zip_flags_t = zip_uint32_t;

struct zip_error_t
{
    int zip_err;
    int sys_err;
    char *str;
};

constexpr int ZIP_ER_OK = 0;
constexpr int ZIP_RDONLY = 16;
constexpr zip_flags_t ZIP_FL_UNCHANGED = 8u;
constexpr zip_flags_t ZIP_FL_ENC_GUESS = 0u;

struct LibZipApi
{
    using zip_open_fn = zip_t *(*)(const char *, int, int *);
    using zip_discard_fn = void (*)(zip_t *);
    using zip_get_num_entries_fn = zip_int64_t (*)(zip_t *, zip_flags_t);
    using zip_get_name_fn = const char *(*)(zip_t *, zip_uint64_t, zip_flags_t);
    using zip_name_locate_fn = zip_int64_t (*)(zip_t *, const char *, zip_flags_t);
    using zip_fopen_index_fn = zip_file_t *(*)(zip_t *, zip_uint64_t, zip_flags_t);
    using zip_fread_fn = zip_int64_t (*)(zip_file_t *, void *, zip_uint64_t);
    using zip_fclose_fn = int (*)(zip_file_t *);
    using zip_error_init_with_code_fn = void (*)(zip_error_t *, int);
    using zip_error_strerror_fn = const char *(*)(zip_error_t *);
    using zip_error_fini_fn = void (*)(zip_error_t *);

    bool ensureLoaded(QString *errorMessage)
    {
        if (loaded) {
            if (loadError.isEmpty()) {
                return true;
            }
            if (errorMessage) {
                *errorMessage = loadError;
            }
            return false;
        }

        loaded = true;
        library.setFileName(QStringLiteral("libzip"));
        if (!library.load()) {
            const QString appDirCandidate = QDir(QCoreApplication::applicationDirPath())
                                                .filePath(QStringLiteral("libzip.dll"));
            if (QFileInfo::exists(appDirCandidate)) {
                library.setFileName(appDirCandidate);
                if (!library.load()) {
                    loadError = QStringLiteral("Failed to load libzip.dll: %1")
                                    .arg(library.errorString());
                }
            } else {
                loadError = QStringLiteral("Failed to load libzip.dll: %1")
                                .arg(library.errorString());
            }
        }

        if (loadError.isEmpty()) {
            zip_open = resolve<zip_open_fn>("zip_open");
            zip_discard = resolve<zip_discard_fn>("zip_discard");
            zip_get_num_entries = resolve<zip_get_num_entries_fn>("zip_get_num_entries");
            zip_get_name = resolve<zip_get_name_fn>("zip_get_name");
            zip_name_locate = resolve<zip_name_locate_fn>("zip_name_locate");
            zip_fopen_index = resolve<zip_fopen_index_fn>("zip_fopen_index");
            zip_fread = resolve<zip_fread_fn>("zip_fread");
            zip_fclose = resolve<zip_fclose_fn>("zip_fclose");
            zip_error_init_with_code =
                resolve<zip_error_init_with_code_fn>("zip_error_init_with_code");
            zip_error_strerror = resolve<zip_error_strerror_fn>("zip_error_strerror");
            zip_error_fini = resolve<zip_error_fini_fn>("zip_error_fini");

            if (!zip_open || !zip_discard || !zip_get_num_entries || !zip_get_name
                || !zip_name_locate
                || !zip_fopen_index || !zip_fread || !zip_fclose
                || !zip_error_init_with_code || !zip_error_strerror || !zip_error_fini) {
                loadError = QStringLiteral("libzip.dll is missing one or more required exports");
                library.unload();
            }
        }

        if (!loadError.isEmpty() && errorMessage) {
            *errorMessage = loadError;
        }
        return loadError.isEmpty();
    }

    template <typename Fn>
    Fn resolve(const char *symbolName)
    {
        return reinterpret_cast<Fn>(library.resolve(symbolName));
    }

    QLibrary library;
    bool loaded = false;
    QString loadError;
    zip_open_fn zip_open = nullptr;
    zip_discard_fn zip_discard = nullptr;
    zip_get_num_entries_fn zip_get_num_entries = nullptr;
    zip_get_name_fn zip_get_name = nullptr;
    zip_name_locate_fn zip_name_locate = nullptr;
    zip_fopen_index_fn zip_fopen_index = nullptr;
    zip_fread_fn zip_fread = nullptr;
    zip_fclose_fn zip_fclose = nullptr;
    zip_error_init_with_code_fn zip_error_init_with_code = nullptr;
    zip_error_strerror_fn zip_error_strerror = nullptr;
    zip_error_fini_fn zip_error_fini = nullptr;
};

LibZipApi &libzipApi()
{
    static LibZipApi api;
    return api;
}

QString normalizeArchiveEntryPath(const QString &path)
{
    QString normalized = QDir::fromNativeSeparators(path);
    normalized = QDir::cleanPath(normalized);
    if (normalized == QStringLiteral(".")) {
        normalized.clear();
    }
    if (normalized.endsWith(QLatin1Char('/'))) {
        normalized.chop(1);
    }
    return normalized;
}

QString zipErrorToString(int errorCode)
{
    LibZipApi &api = libzipApi();
    zip_error_t error;
    api.zip_error_init_with_code(&error, errorCode);
    const QString message = QString::fromLocal8Bit(api.zip_error_strerror(&error));
    api.zip_error_fini(&error);
    return message;
}

} // namespace

ZipArchive::ZipArchive() = default;

ZipArchive::~ZipArchive()
{
    close();
}

bool ZipArchive::open(const QString &archivePath, QString *errorMessage)
{
    close();

    LibZipApi &api = libzipApi();
    if (!api.ensureLoaded(errorMessage)) {
        return false;
    }

    int errorCode = ZIP_ER_OK;
    const QByteArray archivePathUtf8 = QDir::toNativeSeparators(archivePath).toUtf8();
    m_archive = api.zip_open(archivePathUtf8.constData(), ZIP_RDONLY, &errorCode);
    if (!m_archive) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to open ZIP archive %1: %2")
                                .arg(archivePath, zipErrorToString(errorCode));
        }
        return false;
    }

    const zip_int64_t count = api.zip_get_num_entries(m_archive, ZIP_FL_UNCHANGED);
    if (count < 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to enumerate ZIP archive entries: %1")
                                .arg(archivePath);
        }
        close();
        return false;
    }

    m_archivePath = archivePath;
    m_entryCount = static_cast<quint64>(count);
    return true;
}

bool ZipArchive::isOpen() const
{
    return m_archive != nullptr;
}

QString ZipArchive::archivePath() const
{
    return m_archivePath;
}

quint64 ZipArchive::entryCount() const
{
    return m_entryCount;
}

QString ZipArchive::normalizedEntryAt(quint64 index) const
{
    if (!m_archive || index >= m_entryCount) {
        return {};
    }

    const char *rawName = libzipApi().zip_get_name(m_archive, static_cast<zip_uint64_t>(index), ZIP_FL_ENC_GUESS);
    if (!rawName) {
        return {};
    }

    return normalizeArchiveEntryPath(QString::fromUtf8(rawName));
}

bool ZipArchive::entryIsDirectory(quint64 index) const
{
    if (!m_archive || index >= m_entryCount) {
        return false;
    }

    const char *rawName =
        libzipApi().zip_get_name(m_archive, static_cast<zip_uint64_t>(index), ZIP_FL_ENC_GUESS);
    if (!rawName) {
        return false;
    }

    const QString original = QString::fromUtf8(rawName);
    return original.endsWith(QLatin1Char('/')) || original.endsWith(QLatin1Char('\\'));
}

bool ZipArchive::containsNormalizedPath(const QString &normalizedPath) const
{
    if (!m_archive) {
        return false;
    }

    auto resolvePath = [this](const QString &candidate) -> qint64 {
        const auto cached = m_entryIndexByNormalizedPath.constFind(candidate);
        if (cached != m_entryIndexByNormalizedPath.cend()) {
            return cached.value();
        }

        const QByteArray candidateUtf8 = candidate.toUtf8();
        const zip_int64_t located = libzipApi().zip_name_locate(m_archive, candidateUtf8.constData(), ZIP_FL_ENC_GUESS);
        m_entryIndexByNormalizedPath.insert(candidate, located >= 0 ? located : -1);
        return located;
    };

    if (resolvePath(normalizedPath) >= 0) {
        return true;
    }

    const QString windowsPath = QString(normalizedPath).replace(QLatin1Char('/'), QLatin1Char('\\'));
    if (windowsPath != normalizedPath && resolvePath(windowsPath) >= 0) {
        m_entryIndexByNormalizedPath.insert(normalizedPath, m_entryIndexByNormalizedPath.value(windowsPath));
        return true;
    }

    return false;
}

QByteArray ZipArchive::readBytesByNormalizedPath(const QString &normalizedPath, QString *errorMessage) const
{
    LibZipApi &api = libzipApi();
    if (!m_archive) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("ZIP archive is not open");
        }
        return {};
    }

    if (!containsNormalizedPath(normalizedPath)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("ZIP member not found: %1::%2")
                                .arg(m_archivePath, normalizedPath);
        }
        return {};
    }

    const qint64 entryIndex = m_entryIndexByNormalizedPath.value(normalizedPath, -1);
    if (entryIndex < 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("ZIP member not found: %1::%2")
                                .arg(m_archivePath, normalizedPath);
        }
        return {};
    }

    zip_file_t *file =
        api.zip_fopen_index(m_archive, static_cast<zip_uint64_t>(entryIndex), ZIP_FL_UNCHANGED);
    if (!file) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to open ZIP member %1::%2")
                                .arg(m_archivePath, normalizedPath);
        }
        return {};
    }

    QByteArray data;
    char buffer[8192];
    while (true) {
        const zip_int64_t bytesRead = api.zip_fread(file, buffer, sizeof(buffer));
        if (bytesRead < 0) {
            api.zip_fclose(file);
            if (errorMessage) {
                *errorMessage = QStringLiteral("Failed to read ZIP member %1::%2")
                                    .arg(m_archivePath, normalizedPath);
            }
            return {};
        }
        if (bytesRead == 0) {
            break;
        }
        data.append(buffer, static_cast<int>(bytesRead));
    }

    api.zip_fclose(file);
    if (data.isNull()) {
        return QByteArray("");
    }
    return data;
}

QString ZipArchive::readTextByNormalizedPath(const QString &normalizedPath, QString *errorMessage) const
{
    const QByteArray data = readBytesByNormalizedPath(normalizedPath, errorMessage);
    if (data.isNull()) {
        return {};
    }
    return QString::fromUtf8(data);
}

void ZipArchive::close()
{
    if (m_archive) {
        libzipApi().zip_discard(m_archive);
        m_archive = nullptr;
    }
    m_archivePath.clear();
    m_entryIndexByNormalizedPath.clear();
    m_entryCount = 0;
}

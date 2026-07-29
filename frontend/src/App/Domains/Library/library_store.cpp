#include "library_store.h"

#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMimeDatabase>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>

namespace
{
struct JsonReplyResult
{
    bool ok = false;
    QJsonObject object;
    QString errorMessage;
};

QString responseErrorMessage(const QJsonObject &object, const QString &fallback)
{
    const QJsonObject error = object.value(QStringLiteral("error")).toObject();
    const QString message = error.value(QStringLiteral("message")).toString();
    return message.isEmpty() ? fallback : message;
}

JsonReplyResult consumeJsonReply(QNetworkReply *reply)
{
    const QByteArray payload = reply->readAll();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    const QJsonObject object = document.isObject() ? document.object() : QJsonObject{};

    if (reply->error() != QNetworkReply::NoError) {
        return {
            false,
            object,
            responseErrorMessage(object, reply->errorString()),
        };
    }

    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return {
            false,
            {},
            QStringLiteral("Archivist API returned an invalid JSON response."),
        };
    }

    if (!object.value(QStringLiteral("ok")).toBool(false)) {
        return {
            false,
            object,
            responseErrorMessage(object, QStringLiteral("Archivist API request failed.")),
        };
    }

    return {true, object, {}};
}

QString encodedPathSegment(const QString &value)
{
    return QString::fromUtf8(QUrl::toPercentEncoding(value));
}

bool usesDirectImageRenderer(const QVariantMap &file)
{
    const QString fileName = file.value(QStringLiteral("name")).toString();
    const QString mimeType = QMimeDatabase()
        .mimeTypeForFile(fileName, QMimeDatabase::MatchExtension)
        .name();

    return mimeType.startsWith(QStringLiteral("image/"))
        && mimeType != QStringLiteral("image/svg+xml");
}

bool usesDocumentRenderer(const QVariantMap &file)
{
    static const QSet<QString> extensions{
        QStringLiteral("pdf"),
        QStringLiteral("doc"),
        QStringLiteral("docx"),
        QStringLiteral("odt"),
        QStringLiteral("rtf"),
        QStringLiteral("xls"),
        QStringLiteral("xlsx"),
        QStringLiteral("ods"),
        QStringLiteral("ppt"),
        QStringLiteral("pptx"),
        QStringLiteral("odp"),
    };

    QString extension = file.value(QStringLiteral("extension")).toString().toLower();
    if (extension.startsWith(QLatin1Char('.'))) {
        extension.remove(0, 1);
    }

    return extensions.contains(extension);
}
}

LibraryStore::LibraryStore(QObject *parent)
    : QObject(parent)
    , m_baseUrl(QStringLiteral("http://127.0.0.1:3333/api"))
{
    m_externalFileReloadTimer.setSingleShot(true);
    m_externalFileReloadTimer.setInterval(220);
    m_libraryRescanTimer.setSingleShot(true);
    m_libraryRescanTimer.setInterval(900);
    m_gitStatusRefreshTimer.setSingleShot(true);
    m_gitStatusRefreshTimer.setInterval(320);
    m_watcherSuppressionTimer.setSingleShot(true);
    m_watcherSuppressionTimer.setInterval(1200);

    connect(
        &m_fileWatcher,
        &QFileSystemWatcher::fileChanged,
        this,
        [this](const QString &) {
            if (m_watcherSuppressionTimer.isActive()) {
                return;
            }

            scheduleExternalFileReload();
            scheduleLibraryRescan();
            scheduleGitStatusRefresh();
            QTimer::singleShot(
                0,
                this,
                &LibraryStore::rebuildFileWatchers
            );
        }
    );

    connect(
        &m_fileWatcher,
        &QFileSystemWatcher::directoryChanged,
        this,
        [this](const QString &) {
            if (m_watcherSuppressionTimer.isActive()) {
                return;
            }

            scheduleExternalFileReload();
            scheduleLibraryRescan();
            scheduleGitStatusRefresh();
        }
    );

    connect(
        &m_externalFileReloadTimer,
        &QTimer::timeout,
        this,
        [this]() {
            if (
                m_selectedFileId.isEmpty()
                || m_loadingFilePreview
                || m_savingFile
            ) {
                return;
            }

            previewFileFromLibrary(
                m_activeFileLibraryId,
                m_selectedFileId,
                m_selectedFile
            );
        }
    );

    connect(
        &m_libraryRescanTimer,
        &QTimer::timeout,
        this,
        [this]() {
            if (m_selectedLibraryId.isEmpty()) {
                return;
            }

            if (m_scanning) {
                m_libraryRescanTimer.start();
                return;
            }

            scanSelectedLibrary();
        }
    );

    connect(
        &m_gitStatusRefreshTimer,
        &QTimer::timeout,
        this,
        &LibraryStore::refreshSelectedGitStatus
    );
}

QVariantList LibraryStore::libraries() const
{
    return m_libraries;
}

QString LibraryStore::selectedLibraryId() const
{
    return m_selectedLibraryId;
}

QVariantMap LibraryStore::selectedLibrary() const
{
    for (const QVariant &value : m_libraries) {
        const QVariantMap library = value.toMap();
        if (library.value(QStringLiteral("id")).toString() == m_selectedLibraryId) {
            return library;
        }
    }

    return {};
}

QVariantList LibraryStore::files() const
{
    return m_files;
}

QVariantList LibraryStore::directories() const
{
    return m_directories;
}

QVariantMap LibraryStore::latestScan() const
{
    return m_latestScan;
}

QVariantMap LibraryStore::gitStatus() const
{
    return m_gitStatus;
}

bool LibraryStore::loadingGitStatus() const
{
    return m_loadingGitStatus;
}

QString LibraryStore::activeFileLibraryId() const
{
    return m_activeFileLibraryId;
}

QVariantMap LibraryStore::activeFileLibrary() const
{
    for (const QVariant &value : m_libraries) {
        const QVariantMap library = value.toMap();
        if (
            library.value(QStringLiteral("id")).toString()
            == m_activeFileLibraryId
        ) {
            return library;
        }
    }

    return {};
}

QString LibraryStore::activeFileRootPath() const
{
    return activeFileLibrary()
        .value(QStringLiteral("rootPath"))
        .toString();
}

QString LibraryStore::selectedFileId() const
{
    return m_selectedFileId;
}

QVariantMap LibraryStore::selectedFile() const
{
    return m_selectedFile;
}

QVariantMap LibraryStore::filePreview() const
{
    return m_filePreview;
}

bool LibraryStore::loadingLibraries() const
{
    return m_loadingLibraries;
}

bool LibraryStore::loadingFiles() const
{
    return m_loadingFiles;
}

bool LibraryStore::loadingFilePreview() const
{
    return m_loadingFilePreview;
}

bool LibraryStore::scanning() const
{
    return m_scanning;
}

bool LibraryStore::movingFile() const
{
    return m_movingFile;
}

bool LibraryStore::mutatingEntry() const
{
    return m_mutatingEntry;
}

bool LibraryStore::savingFile() const
{
    return m_savingFile;
}

bool LibraryStore::creatingLibrary() const
{
    return m_creatingLibrary;
}

QString LibraryStore::errorMessage() const
{
    return m_errorMessage;
}

QString LibraryStore::filePreviewError() const
{
    return m_filePreviewError;
}

QString LibraryStore::fileSaveError() const
{
    return m_fileSaveError;
}

QNetworkRequest LibraryStore::requestFor(const QString &path) const
{
    QNetworkRequest request{QUrl(m_baseUrl.toString() + path)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    return request;
}

QString LibraryStore::selectedLibraryRootPath() const
{
    return selectedLibrary()
        .value(QStringLiteral("rootPath"))
        .toString();
}

QString LibraryStore::selectedFileAbsolutePath() const
{
    const QString rootPath = activeFileRootPath();
    const QVariantMap file = selectedFile();

    if (rootPath.isEmpty() || file.isEmpty()) {
        return {};
    }

    return QDir::cleanPath(
        QDir(rootPath).filePath(
            file.value(QStringLiteral("relativePath")).toString()
        )
    );
}

void LibraryStore::rebuildFileWatchers()
{
    const QStringList watchedFiles = m_fileWatcher.files();
    const QStringList watchedDirectories =
        m_fileWatcher.directories();

    if (!watchedFiles.isEmpty()) {
        m_fileWatcher.removePaths(watchedFiles);
    }

    if (!watchedDirectories.isEmpty()) {
        m_fileWatcher.removePaths(watchedDirectories);
    }

    const QString rootPath = selectedLibraryRootPath();

    if (rootPath.isEmpty()) {
        return;
    }

    QSet<QString> directories;
    const QFileInfo rootInfo(rootPath);

    if (rootInfo.exists() && rootInfo.isDir()) {
        directories.insert(rootInfo.absoluteFilePath());
    }

    for (const QVariant &value : m_files) {
        const QVariantMap file = value.toMap();
        const QString relativePath =
            file.value(QStringLiteral("relativePath")).toString();
        const QFileInfo fileInfo(
            QDir(rootPath).filePath(relativePath)
        );

        if (fileInfo.absoluteDir().exists()) {
            directories.insert(
                fileInfo.absolutePath()
            );
        }
    }

    if (!directories.isEmpty()) {
        m_fileWatcher.addPaths(directories.values());
    }

    const QString filePath = selectedFileAbsolutePath();
    const QFileInfo selectedFileInfo(filePath);

    if (
        !filePath.isEmpty()
        && selectedFileInfo.exists()
        && selectedFileInfo.isFile()
    ) {
        m_fileWatcher.addPath(filePath);
    }
}

void LibraryStore::scheduleExternalFileReload()
{
    if (m_selectedFileId.isEmpty()) {
        return;
    }

    m_externalFileReloadTimer.start();
}

void LibraryStore::scheduleLibraryRescan()
{
    if (m_selectedLibraryId.isEmpty()) {
        return;
    }

    m_libraryRescanTimer.start();
}

void LibraryStore::scheduleGitStatusRefresh()
{
    if (m_selectedLibraryId.isEmpty()) {
        return;
    }

    m_gitStatusRefreshTimer.start();
}

void LibraryStore::refresh()
{
    if (m_loadingLibraries) {
        return;
    }

    setErrorMessage({});
    setLoadingLibraries(true);

    QNetworkReply *reply = m_network.get(requestFor(QStringLiteral("/libraries")));

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const JsonReplyResult result = consumeJsonReply(reply);
        reply->deleteLater();

        if (!result.ok) {
            setLoadingLibraries(false);
            setErrorMessage(result.errorMessage);
            return;
        }

        setLibraries(result.object.value(QStringLiteral("libraries")).toArray().toVariantList());
        fetchAppState();
    });
}

void LibraryStore::createLibrary(const QUrl &folderUrl)
{
    if (m_creatingLibrary || !folderUrl.isValid()) {
        return;
    }

    const QString rootPath = folderUrl.isLocalFile()
        ? folderUrl.toLocalFile()
        : folderUrl.toString();

    if (rootPath.trimmed().isEmpty()) {
        setErrorMessage(QStringLiteral("Select a folder to create a Library."));
        return;
    }

    setErrorMessage({});
    setCreatingLibrary(true);

    QJsonObject body;
    body.insert(QStringLiteral("rootPath"), rootPath);

    QNetworkReply *reply = m_network.post(
        requestFor(QStringLiteral("/libraries")),
        QJsonDocument(body).toJson(QJsonDocument::Compact)
    );

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const JsonReplyResult result = consumeJsonReply(reply);
        reply->deleteLater();
        setCreatingLibrary(false);

        if (!result.ok) {
            const QJsonObject error =
                result.object.value(QStringLiteral("error")).toObject();
            const QJsonObject details =
                error.value(QStringLiteral("details")).toObject();

            if (
                details.value(QStringLiteral("code")).toString()
                    == QStringLiteral("LIBRARY_EXISTS")
            ) {
                const QString existingLibraryId =
                    details.value(QStringLiteral("libraryId")).toString();

                for (const QVariant &value : m_libraries) {
                    const QVariantMap library = value.toMap();

                    if (
                        library.value(QStringLiteral("id")).toString()
                        == existingLibraryId
                    ) {
                        setErrorMessage({});
                        emit libraryCreated(library);
                        return;
                    }
                }
            }

            setErrorMessage(result.errorMessage);
            return;
        }

        const QVariantMap library =
            result.object.value(QStringLiteral("library")).toObject().toVariantMap();

        if (library.isEmpty()) {
            setErrorMessage(QStringLiteral("Archivist created the Library but returned no Library data."));
            return;
        }

        upsertLibrary(library);
        emit libraryCreated(library);
    });
}

void LibraryStore::fetchAppState()
{
    QNetworkReply *reply = m_network.get(requestFor(QStringLiteral("/app-state")));

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const JsonReplyResult result = consumeJsonReply(reply);
        reply->deleteLater();

        if (!result.ok) {
            setLoadingLibraries(false);
            setErrorMessage(result.errorMessage);
            return;
        }

        const QJsonObject appState = result.object.value(QStringLiteral("appState")).toObject();
        QString nextLibraryId = appState.value(QStringLiteral("selectedLibraryId")).toString();

        if (!containsLibrary(nextLibraryId)) {
            nextLibraryId.clear();
        }

        setLoadingLibraries(false);

        if (nextLibraryId.isEmpty() && !m_libraries.isEmpty()) {
            selectLibrary(m_libraries.first().toMap().value(QStringLiteral("id")).toString());
            return;
        }

        setSelectedLibraryId(nextLibraryId);
        refreshSelectedFiles();
    });
}

void LibraryStore::selectLibrary(const QString &libraryId)
{
    if (libraryId.isEmpty() || !containsLibrary(libraryId)) {
        return;
    }

    if (!m_pendingLibrarySelectionId.isEmpty()) {
        if (libraryId == m_pendingLibrarySelectionId) {
            m_queuedLibrarySelectionId.clear();
        } else {
            m_queuedLibrarySelectionId = libraryId;
        }
        return;
    }

    if (libraryId == m_selectedLibraryId) {
        if (m_pendingScanLibraryId == libraryId) {
            m_pendingScanLibraryId.clear();
            scanSelectedLibrary();
            return;
        }

        refreshSelectedFiles();
        return;
    }

    startLibrarySelection(libraryId);
}

void LibraryStore::startLibrarySelection(const QString &libraryId)
{
    m_pendingLibrarySelectionId = libraryId;
    m_queuedLibrarySelectionId.clear();
    ++m_fileRequestRevision;
    ++m_gitStatusRequestRevision;
    m_gitStatusRefreshTimer.stop();

    setErrorMessage({});
    setFiles({});
    setLatestScan({});
    setGitStatus({});
    setLoadingGitStatus(false);
    setLoadingFiles(true);

    QJsonObject body;
    body.insert(QStringLiteral("selectedLibraryId"), libraryId);

    QNetworkReply *reply = m_network.sendCustomRequest(
        requestFor(QStringLiteral("/app-state/selected-library")),
        QByteArrayLiteral("PATCH"),
        QJsonDocument(body).toJson(QJsonDocument::Compact)
    );

    connect(
        reply,
        &QNetworkReply::finished,
        this,
        [this, reply, libraryId]() {
            const JsonReplyResult result = consumeJsonReply(reply);
            reply->deleteLater();

            if (m_pendingLibrarySelectionId != libraryId) {
                return;
            }

            m_pendingLibrarySelectionId.clear();

            if (!result.ok) {
                m_queuedLibrarySelectionId.clear();
                m_pendingScanLibraryId.clear();
                setLoadingFiles(false);
                setErrorMessage(result.errorMessage);
                return;
            }

            const QJsonObject appState =
                result.object.value(QStringLiteral("appState")).toObject();
            const QString selectedLibraryId =
                appState.value(QStringLiteral("selectedLibraryId")).toString();

            if (selectedLibraryId != libraryId) {
                m_queuedLibrarySelectionId.clear();
                m_pendingScanLibraryId.clear();
                setLoadingFiles(false);
                setErrorMessage(
                    QStringLiteral("Archivist selected an unexpected Library.")
                );
                return;
            }

            setSelectedLibraryId(selectedLibraryId);

            if (
                !m_queuedLibrarySelectionId.isEmpty()
                && m_queuedLibrarySelectionId != selectedLibraryId
            ) {
                const QString queuedLibraryId = m_queuedLibrarySelectionId;
                m_queuedLibrarySelectionId.clear();
                startLibrarySelection(queuedLibraryId);
                return;
            }

            m_queuedLibrarySelectionId.clear();

            if (m_pendingScanLibraryId == selectedLibraryId) {
                m_pendingScanLibraryId.clear();
                setLoadingFiles(false);
                scanSelectedLibrary();
                return;
            }

            refreshSelectedFiles();
        }
    );
}

void LibraryStore::selectLibraryAndScan(const QString &libraryId)
{
    if (libraryId.isEmpty() || !containsLibrary(libraryId)) {
        return;
    }

    m_pendingScanLibraryId = libraryId;
    selectLibrary(libraryId);
}

void LibraryStore::refreshSelectedFiles()
{
    if (m_selectedLibraryId.isEmpty()) {
        ++m_fileRequestRevision;
        ++m_gitStatusRequestRevision;
        setFiles({});
        setDirectories({});
        setLatestScan({});
        setGitStatus({});
        setLoadingFiles(false);
        setLoadingGitStatus(false);
        return;
    }

    const QString requestedLibraryId = m_selectedLibraryId;
    const quint64 requestRevision = ++m_fileRequestRevision;

    setErrorMessage({});
    setLoadingFiles(true);

    const QString path = QStringLiteral("/libraries/%1/files")
        .arg(encodedPathSegment(requestedLibraryId));
    QNetworkReply *reply = m_network.get(requestFor(path));

    connect(
        reply,
        &QNetworkReply::finished,
        this,
        [this, reply, requestedLibraryId, requestRevision]() {
            const JsonReplyResult result = consumeJsonReply(reply);
            reply->deleteLater();

            if (
                requestRevision != m_fileRequestRevision
                || requestedLibraryId != m_selectedLibraryId
            ) {
                return;
            }

            if (!result.ok) {
                setLoadingFiles(false);
                setErrorMessage(result.errorMessage);
                return;
            }

            setFiles(
                result.object.value(QStringLiteral("files")).toArray().toVariantList()
            );
            setDirectories(
                result.object.value(QStringLiteral("directories")).toArray().toVariantList()
            );
            setLatestScan(
                result.object.value(QStringLiteral("latestScan")).toObject().toVariantMap()
            );
            setLoadingFiles(false);
            refreshSelectedGitStatus();
        }
    );
}

void LibraryStore::refreshSelectedGitStatus()
{
    if (m_selectedLibraryId.isEmpty()) {
        ++m_gitStatusRequestRevision;
        setGitStatus({});
        setLoadingGitStatus(false);
        return;
    }

    const QString requestedLibraryId = m_selectedLibraryId;
    const quint64 requestRevision = ++m_gitStatusRequestRevision;

    setLoadingGitStatus(true);

    const QString path = QStringLiteral("/libraries/%1/git-status")
        .arg(encodedPathSegment(requestedLibraryId));
    QNetworkReply *reply = m_network.get(requestFor(path));

    connect(
        reply,
        &QNetworkReply::finished,
        this,
        [this, reply, requestedLibraryId, requestRevision]() {
            const JsonReplyResult result = consumeJsonReply(reply);
            reply->deleteLater();

            if (
                requestRevision != m_gitStatusRequestRevision
                || requestedLibraryId != m_selectedLibraryId
            ) {
                return;
            }

            setLoadingGitStatus(false);

            if (!result.ok) {
                setGitStatus({});
                return;
            }

            setGitStatus(
                result.object.value(QStringLiteral("gitStatus"))
                    .toObject()
                    .toVariantMap()
            );
        }
    );
}

void LibraryStore::scanSelectedLibrary()
{
    if (m_selectedLibraryId.isEmpty() || m_scanning) {
        return;
    }

    const QString requestedLibraryId = m_selectedLibraryId;
    const quint64 requestRevision = ++m_fileRequestRevision;

    m_fileToReloadAfterScanId =
        m_activeFileLibraryId == requestedLibraryId
            ? m_selectedFileId
            : QString{};
    setErrorMessage({});
    setLoadingFiles(true);
    setScanning(true);

    const QString path = QStringLiteral("/libraries/%1/scan")
        .arg(encodedPathSegment(requestedLibraryId));
    QNetworkReply *reply = m_network.post(requestFor(path), QByteArrayLiteral("{}"));

    connect(
        reply,
        &QNetworkReply::finished,
        this,
        [this, reply, requestedLibraryId, requestRevision]() {
            const JsonReplyResult result = consumeJsonReply(reply);
            reply->deleteLater();
            setScanning(false);

            if (
                requestRevision != m_fileRequestRevision
                || requestedLibraryId != m_selectedLibraryId
            ) {
                return;
            }

            if (!result.ok) {
                setLoadingFiles(false);
                setErrorMessage(result.errorMessage);
                m_fileToReloadAfterScanId.clear();
                return;
            }

            setFiles(
                result.object.value(QStringLiteral("files")).toArray().toVariantList()
            );
            setDirectories(
                result.object.value(QStringLiteral("directories")).toArray().toVariantList()
            );

            QVariantMap scan =
                result.object.value(QStringLiteral("latestScan")).toObject().toVariantMap();
            if (scan.isEmpty()) {
                scan = result.object.value(QStringLiteral("scan")).toObject().toVariantMap();
            }
            setLatestScan(scan);
            setLoadingFiles(false);
            rebuildFileWatchers();
            refreshSelectedGitStatus();

            const QString fileIdToReload =
                m_fileToReloadAfterScanId;
            m_fileToReloadAfterScanId.clear();

            if (
                !fileIdToReload.isEmpty()
                && m_activeFileLibraryId == requestedLibraryId
                && containsFile(fileIdToReload)
            ) {
                previewFile(fileIdToReload);
            }
        }
    );
}

void LibraryStore::moveFile(const QString &fileId, const QString &targetDirectory)
{
    if (
        m_selectedLibraryId.isEmpty()
        || fileId.isEmpty()
        || !containsFile(fileId)
        || m_movingFile
    ) {
        return;
    }

    setErrorMessage({});
    setMovingFile(true);

    QJsonObject body;
    body.insert(QStringLiteral("targetDirectory"), targetDirectory);

    const QString libraryId = m_selectedLibraryId;
    const QString path = QStringLiteral("/libraries/%1/files/%2")
        .arg(encodedPathSegment(libraryId), encodedPathSegment(fileId));
    QNetworkReply *reply = m_network.sendCustomRequest(
        requestFor(path),
        QByteArrayLiteral("PATCH"),
        QJsonDocument(body).toJson(QJsonDocument::Compact)
    );

    connect(reply, &QNetworkReply::finished, this, [this, reply, libraryId, fileId]() {
        const JsonReplyResult result = consumeJsonReply(reply);
        reply->deleteLater();
        setMovingFile(false);

        if (!result.ok) {
            setErrorMessage(result.errorMessage);
            return;
        }

        if (m_selectedLibraryId != libraryId) {
            return;
        }

        setFiles(result.object.value(QStringLiteral("files")).toArray().toVariantList());
        setDirectories(
            result.object.value(QStringLiteral("directories")).toArray().toVariantList()
        );
        const QVariantMap file = result.object.value(QStringLiteral("file")).toObject().toVariantMap();
        refreshSelectedGitStatus();
        emit fileMoved(
            fileId,
            file.value(QStringLiteral("relativePath")).toString()
        );
    });
}

void LibraryStore::createEntry(
    const QString &parentDirectory,
    const QString &name,
    bool directory
)
{
    if (
        m_selectedLibraryId.isEmpty()
        || name.trimmed().isEmpty()
        || m_mutatingEntry
    ) {
        return;
    }

    const QString libraryId = m_selectedLibraryId;
    setErrorMessage({});
    setMutatingEntry(true);

    QJsonObject body{
        {QStringLiteral("parentDirectory"), parentDirectory},
        {QStringLiteral("name"), name},
        {
            QStringLiteral("kind"),
            directory
                ? QStringLiteral("directory")
                : QStringLiteral("file")
        },
    };
    const QString path = QStringLiteral("/libraries/%1/files")
        .arg(encodedPathSegment(libraryId));
    QNetworkReply *reply = m_network.post(
        requestFor(path),
        QJsonDocument(body).toJson(QJsonDocument::Compact)
    );

    connect(reply, &QNetworkReply::finished, this, [this, reply, libraryId]() {
        const JsonReplyResult result = consumeJsonReply(reply);
        reply->deleteLater();
        setMutatingEntry(false);

        if (!result.ok) {
            setErrorMessage(result.errorMessage);
            return;
        }

        if (m_selectedLibraryId != libraryId) {
            return;
        }

        setFiles(result.object.value(QStringLiteral("files")).toArray().toVariantList());
        setDirectories(
            result.object.value(QStringLiteral("directories")).toArray().toVariantList()
        );

        const QVariantMap entry = result.object
            .value(QStringLiteral("entry"))
            .toObject()
            .toVariantMap();
        const QVariantMap file = entry
            .value(QStringLiteral("file"))
            .toMap();
        refreshSelectedGitStatus();
        emit entryCreated(
            entry.value(QStringLiteral("kind")).toString(),
            entry.value(QStringLiteral("relativePath")).toString(),
            file.value(QStringLiteral("id")).toString()
        );
    });
}

void LibraryStore::renameFile(const QString &fileId, const QString &name)
{
    if (
        m_selectedLibraryId.isEmpty()
        || fileId.isEmpty()
        || name.trimmed().isEmpty()
        || !containsFile(fileId)
        || m_mutatingEntry
    ) {
        return;
    }

    const QString libraryId = m_selectedLibraryId;
    setErrorMessage({});
    setMutatingEntry(true);

    QJsonObject body{{QStringLiteral("name"), name}};
    const QString path = QStringLiteral("/libraries/%1/files/%2/name")
        .arg(encodedPathSegment(libraryId), encodedPathSegment(fileId));
    QNetworkReply *reply = m_network.sendCustomRequest(
        requestFor(path),
        QByteArrayLiteral("PATCH"),
        QJsonDocument(body).toJson(QJsonDocument::Compact)
    );

    connect(reply, &QNetworkReply::finished, this, [this, reply, libraryId, fileId]() {
        const JsonReplyResult result = consumeJsonReply(reply);
        reply->deleteLater();
        setMutatingEntry(false);

        if (!result.ok) {
            setErrorMessage(result.errorMessage);
            return;
        }

        if (m_selectedLibraryId != libraryId) {
            return;
        }

        setFiles(result.object.value(QStringLiteral("files")).toArray().toVariantList());
        setDirectories(
            result.object.value(QStringLiteral("directories")).toArray().toVariantList()
        );
        const QVariantMap file = result.object
            .value(QStringLiteral("file"))
            .toObject()
            .toVariantMap();
        refreshSelectedGitStatus();
        emit fileRenamed(
            fileId,
            file.value(QStringLiteral("relativePath")).toString(),
            file.value(QStringLiteral("name")).toString()
        );
    });
}

void LibraryStore::duplicateFile(const QString &fileId, const QString &name)
{
    if (
        m_selectedLibraryId.isEmpty()
        || fileId.isEmpty()
        || name.trimmed().isEmpty()
        || !containsFile(fileId)
        || m_mutatingEntry
    ) {
        return;
    }

    const QString libraryId = m_selectedLibraryId;
    setErrorMessage({});
    setMutatingEntry(true);

    QJsonObject body{{QStringLiteral("name"), name}};
    const QString path = QStringLiteral("/libraries/%1/files/%2/duplicate")
        .arg(encodedPathSegment(libraryId), encodedPathSegment(fileId));
    QNetworkReply *reply = m_network.post(
        requestFor(path),
        QJsonDocument(body).toJson(QJsonDocument::Compact)
    );

    connect(reply, &QNetworkReply::finished, this, [this, reply, libraryId]() {
        const JsonReplyResult result = consumeJsonReply(reply);
        reply->deleteLater();
        setMutatingEntry(false);

        if (!result.ok) {
            setErrorMessage(result.errorMessage);
            return;
        }

        if (m_selectedLibraryId != libraryId) {
            return;
        }

        setFiles(result.object.value(QStringLiteral("files")).toArray().toVariantList());
        setDirectories(
            result.object.value(QStringLiteral("directories")).toArray().toVariantList()
        );
        const QVariantMap file = result.object
            .value(QStringLiteral("file"))
            .toObject()
            .toVariantMap();
        refreshSelectedGitStatus();
        emit fileDuplicated(
            file.value(QStringLiteral("id")).toString(),
            file.value(QStringLiteral("relativePath")).toString(),
            file.value(QStringLiteral("name")).toString()
        );
    });
}

void LibraryStore::revealEntry(const QString &relativePath)
{
    revealEntryFromLibrary(m_selectedLibraryId, relativePath);
}

void LibraryStore::revealEntryFromLibrary(
    const QString &libraryId,
    const QString &relativePath
)
{
    if (
        libraryId.isEmpty()
        || relativePath.isEmpty()
        || !containsLibrary(libraryId)
        || m_mutatingEntry
    ) {
        return;
    }

    setErrorMessage({});
    setMutatingEntry(true);

    QJsonObject body{{QStringLiteral("relativePath"), relativePath}};
    const QString path = QStringLiteral("/libraries/%1/reveal")
        .arg(encodedPathSegment(libraryId));
    QNetworkReply *reply = m_network.post(
        requestFor(path),
        QJsonDocument(body).toJson(QJsonDocument::Compact)
    );

    connect(reply, &QNetworkReply::finished, this, [this, reply, libraryId, relativePath]() {
        const JsonReplyResult result = consumeJsonReply(reply);
        reply->deleteLater();
        setMutatingEntry(false);

        if (!result.ok) {
            setErrorMessage(result.errorMessage);
            return;
        }

        if (m_selectedLibraryId == libraryId) {
            emit entryRevealed(relativePath);
        }
    });
}

void LibraryStore::copyEntryPath(
    const QString &libraryId,
    const QString &relativePath,
    bool absolute
)
{
    const QString normalizedRelativePath = QDir::fromNativeSeparators(
        relativePath.trimmed()
    );

    if (
        libraryId.isEmpty()
        || normalizedRelativePath.isEmpty()
    ) {
        return;
    }

    QString rootPath;

    for (const QVariant &value : m_libraries) {
        const QVariantMap library = value.toMap();
        if (
            library.value(QStringLiteral("id")).toString()
            == libraryId
        ) {
            rootPath = library
                .value(QStringLiteral("rootPath"))
                .toString();
            break;
        }
    }

    if (rootPath.isEmpty()) {
        return;
    }

    const QString normalizedRootPath = QDir::cleanPath(
        QFileInfo(rootPath).absoluteFilePath()
    );
    const QString absolutePath = QDir::cleanPath(
        QFileInfo(normalizedRelativePath).isAbsolute()
            ? normalizedRelativePath
            : QDir(normalizedRootPath).absoluteFilePath(
                normalizedRelativePath
            )
    );

    QString copiedPath;

    if (absolute) {
        copiedPath = QDir::toNativeSeparators(absolutePath);
    } else {
        const QString normalizedRoot = QDir::fromNativeSeparators(
            normalizedRootPath
        );
        const QString normalizedAbsolute = QDir::fromNativeSeparators(
            absolutePath
        );
        const QString rootPrefix = normalizedRoot.endsWith('/')
            ? normalizedRoot
            : normalizedRoot + '/';

        if (normalizedAbsolute == normalizedRoot) {
            copiedPath = QStringLiteral(".");
        } else if (normalizedAbsolute.startsWith(rootPrefix)) {
            copiedPath = normalizedAbsolute.mid(rootPrefix.size());
        } else {
            return;
        }
    }

    if (QClipboard *clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(copiedPath, QClipboard::Clipboard);
    }
}

void LibraryStore::previewFile(const QString &fileId)
{
    if (m_selectedLibraryId.isEmpty() || fileId.isEmpty() || !containsFile(fileId)) {
        return;
    }

    QVariantMap file;

    for (const QVariant &value : m_files) {
        const QVariantMap candidate = value.toMap();
        if (candidate.value(QStringLiteral("id")).toString() == fileId) {
            file = candidate;
            break;
        }
    }

    previewFileFromLibrary(m_selectedLibraryId, fileId, file);
}

void LibraryStore::previewFileFromLibrary(
    const QString &libraryId,
    const QString &fileId,
    const QVariantMap &file
)
{
    if (
        libraryId.isEmpty()
        || fileId.isEmpty()
        || !containsLibrary(libraryId)
    ) {
        return;
    }

    ++m_previewPathRequestRevision;
    QVariantMap resolvedFile = file;

    if (resolvedFile.isEmpty() && libraryId == m_selectedLibraryId) {
        for (const QVariant &value : m_files) {
            const QVariantMap candidate = value.toMap();
            if (candidate.value(QStringLiteral("id")).toString() == fileId) {
                resolvedFile = candidate;
                break;
            }
        }
    }

    if (resolvedFile.isEmpty()) {
        resolvedFile.insert(QStringLiteral("id"), fileId);
    }

    const bool reloadingSelectedFile =
        m_activeFileLibraryId == libraryId
        && m_selectedFileId == fileId
        && !m_filePreview.isEmpty();

    setActiveFileLibraryId(libraryId);
    setSelectedFileId(fileId);
    setSelectedFile(resolvedFile);

    if (!reloadingSelectedFile) {
        setFilePreview({});
    }

    setFilePreviewError({});
    setFileSaveError({});
    rebuildFileWatchers();

    const QString status = resolvedFile
        .value(QStringLiteral("status"))
        .toString();

    if (!status.isEmpty() && status != QStringLiteral("available")) {
        setLoadingFilePreview(false);
        setFilePreviewError(
            QStringLiteral("This file is not currently available. Rescan the Library and try again.")
        );
        return;
    }

    if (usesDirectImageRenderer(resolvedFile) || usesDocumentRenderer(resolvedFile)) {
        setLoadingFilePreview(false);
        return;
    }

    setLoadingFilePreview(true);

    const QString requestPath = QStringLiteral("/libraries/%1/files/%2/content")
        .arg(encodedPathSegment(libraryId), encodedPathSegment(fileId));
    QNetworkReply *reply = m_network.get(requestFor(requestPath));

    connect(
        reply,
        &QNetworkReply::finished,
        this,
        [this, reply, libraryId, fileId]() {
            const JsonReplyResult result = consumeJsonReply(reply);
            reply->deleteLater();

            if (
                m_activeFileLibraryId != libraryId
                || m_selectedFileId != fileId
            ) {
                return;
            }

            setLoadingFilePreview(false);

            if (!result.ok) {
                setFilePreviewError(result.errorMessage);
                return;
            }

            const QVariantMap preview = result.object
                .value(QStringLiteral("preview"))
                .toObject()
                .toVariantMap();
            const QVariantMap previewFile = preview
                .value(QStringLiteral("file"))
                .toMap();

            if (!previewFile.isEmpty()) {
                setSelectedFile(previewFile);
            }

            setFilePreview(preview);
        }
    );
}

void LibraryStore::previewFilePathFromLibrary(
    const QString &libraryId,
    const QString &relativePath
)
{
    QString normalizedPath = QDir::fromNativeSeparators(
        QDir::cleanPath(relativePath)
    );

    if (
        libraryId.isEmpty()
        || !containsLibrary(libraryId)
        || normalizedPath.isEmpty()
        || normalizedPath == QStringLiteral(".")
        || normalizedPath == QStringLiteral("..")
        || normalizedPath.startsWith(QStringLiteral("../"))
        || QDir::isAbsolutePath(normalizedPath)
    ) {
        return;
    }

    const quint64 requestRevision = ++m_previewPathRequestRevision;
    setFilePreviewError({});
    setLoadingFilePreview(true);

    const QString requestPath = QStringLiteral("/libraries/%1/files")
        .arg(encodedPathSegment(libraryId));
    QNetworkReply *reply = m_network.get(requestFor(requestPath));

    connect(
        reply,
        &QNetworkReply::finished,
        this,
        [this, reply, libraryId, normalizedPath, requestRevision]() {
            const JsonReplyResult result = consumeJsonReply(reply);
            reply->deleteLater();

            if (requestRevision != m_previewPathRequestRevision) {
                return;
            }

            if (!result.ok) {
                setLoadingFilePreview(false);
                setFilePreviewError(result.errorMessage);
                return;
            }

            const QVariantList files = result.object
                .value(QStringLiteral("files"))
                .toArray()
                .toVariantList();

            for (const QVariant &value : files) {
                const QVariantMap candidate = value.toMap();
                const QString candidatePath = QDir::fromNativeSeparators(
                    QDir::cleanPath(
                        candidate
                            .value(QStringLiteral("relativePath"))
                            .toString()
                    )
                );

                if (candidatePath != normalizedPath) {
                    continue;
                }

                previewFileFromLibrary(
                    libraryId,
                    candidate.value(QStringLiteral("id")).toString(),
                    candidate
                );
                return;
            }

            setLoadingFilePreview(false);
            setFilePreviewError(
                QStringLiteral("The requested file is not cataloged in this Library.")
            );
        }
    );
}

void LibraryStore::saveFileContent(
    const QString &fileId,
    const QString &content,
    const QString &expectedModifiedAt
)
{
    if (
        m_savingFile
        || m_activeFileLibraryId.isEmpty()
        || fileId.isEmpty()
        || expectedModifiedAt.isEmpty()
        || fileId != m_selectedFileId
    ) {
        return;
    }

    const QString libraryId = m_activeFileLibraryId;
    setFileSaveError({});
    setSavingFile(true);
    m_watcherSuppressionTimer.start();

    QJsonObject body{
        {QStringLiteral("content"), content},
        {QStringLiteral("expectedModifiedAt"), expectedModifiedAt},
    };
    const QString path = QStringLiteral("/libraries/%1/files/%2/content")
        .arg(encodedPathSegment(libraryId), encodedPathSegment(fileId));
    QNetworkReply *reply = m_network.sendCustomRequest(
        requestFor(path),
        QByteArrayLiteral("PUT"),
        QJsonDocument(body).toJson(QJsonDocument::Compact)
    );

    connect(
        reply,
        &QNetworkReply::finished,
        this,
        [this, reply, libraryId, fileId]() {
            const JsonReplyResult result = consumeJsonReply(reply);
            reply->deleteLater();
            setSavingFile(false);

            if (!result.ok) {
                setFileSaveError(result.errorMessage);
                emit fileSaveFailed(
                    libraryId,
                    fileId,
                    result.errorMessage
                );
                return;
            }

            const QVariantMap preview = result.object
                .value(QStringLiteral("preview"))
                .toObject()
                .toVariantMap();
            const QVariantMap savedFile = preview
                .value(QStringLiteral("file"))
                .toMap();

            if (m_selectedLibraryId == libraryId) {
                QVariantList nextFiles = m_files;

                for (
                    qsizetype index = 0;
                    index < nextFiles.size();
                    ++index
                ) {
                    if (
                        nextFiles.at(index)
                            .toMap()
                            .value(QStringLiteral("id"))
                            .toString()
                        == fileId
                    ) {
                        nextFiles[index] = savedFile;
                        break;
                    }
                }

                setFiles(nextFiles);
            }

            if (
                m_activeFileLibraryId == libraryId
                && m_selectedFileId == fileId
            ) {
                setSelectedFile(savedFile);
                setFilePreview(preview);
                setFilePreviewError({});
            }

            setFileSaveError({});
            m_watcherSuppressionTimer.start();
            rebuildFileWatchers();
            refreshSelectedGitStatus();
            emit fileSaved(libraryId, fileId, preview);
        }
    );
}

void LibraryStore::clearFilePreview()
{
    setActiveFileLibraryId({});
    setSelectedFileId({});
    setSelectedFile({});
    setFilePreview({});
    setLoadingFilePreview(false);
    setFilePreviewError({});
    setFileSaveError({});
}

void LibraryStore::setLibraries(const QVariantList &libraries)
{
    if (m_libraries == libraries) {
        return;
    }

    m_libraries = libraries;
    emit librariesChanged();
    emit selectedLibraryChanged();
    emit activeFileLibraryChanged();
}

void LibraryStore::setSelectedLibraryId(const QString &libraryId)
{
    if (m_selectedLibraryId == libraryId) {
        return;
    }

    m_selectedLibraryId = libraryId;
    setGitStatus({});
    emit selectedLibraryIdChanged();
    emit selectedLibraryChanged();
    rebuildFileWatchers();
    scheduleGitStatusRefresh();
}

void LibraryStore::setFiles(const QVariantList &files)
{
    QVariantList currentFiles;
    currentFiles.reserve(files.size());

    for (const QVariant &value : files) {
        const QVariantMap file = value.toMap();

        if (
            file.value(QStringLiteral("status")).toString()
            == QStringLiteral("missing")
        ) {
            continue;
        }

        currentFiles.append(file);
    }

    if (m_files == currentFiles) {
        return;
    }

    m_files = currentFiles;
    emit filesChanged();
    rebuildFileWatchers();

    if (
        !m_selectedFileId.isEmpty()
        && m_activeFileLibraryId == m_selectedLibraryId
    ) {
        QVariantMap refreshedFile;

        for (const QVariant &value : m_files) {
            const QVariantMap candidate = value.toMap();
            if (
                candidate.value(QStringLiteral("id")).toString()
                == m_selectedFileId
            ) {
                refreshedFile = candidate;
                break;
            }
        }

        if (!refreshedFile.isEmpty()) {
            setSelectedFile(refreshedFile);
        } else {
            QVariantMap missingFile = m_selectedFile;
            missingFile.insert(
                QStringLiteral("status"),
                QStringLiteral("missing")
            );
            setSelectedFile(missingFile);
            setFilePreview({});
            setFilePreviewError(
                QStringLiteral("This file is no longer available in the Library.")
            );
        }
    }
}

void LibraryStore::setDirectories(const QVariantList &directories)
{
    if (m_directories == directories) {
        return;
    }

    m_directories = directories;
    emit directoriesChanged();
}

void LibraryStore::setLatestScan(const QVariantMap &latestScan)
{
    if (m_latestScan == latestScan) {
        return;
    }

    m_latestScan = latestScan;
    emit latestScanChanged();
}

void LibraryStore::setGitStatus(const QVariantMap &gitStatus)
{
    if (m_gitStatus == gitStatus) {
        return;
    }

    m_gitStatus = gitStatus;
    emit gitStatusChanged();
}

void LibraryStore::setLoadingGitStatus(bool loading)
{
    if (m_loadingGitStatus == loading) {
        return;
    }

    m_loadingGitStatus = loading;
    emit loadingGitStatusChanged();
}

void LibraryStore::setActiveFileLibraryId(const QString &libraryId)
{
    if (m_activeFileLibraryId == libraryId) {
        return;
    }

    m_activeFileLibraryId = libraryId;
    emit activeFileLibraryChanged();
    rebuildFileWatchers();
}

void LibraryStore::setSelectedFile(const QVariantMap &file)
{
    if (m_selectedFile == file) {
        return;
    }

    m_selectedFile = file;
    emit selectedFileChanged();
    rebuildFileWatchers();
}

void LibraryStore::setSelectedFileId(const QString &fileId)
{
    if (m_selectedFileId == fileId) {
        return;
    }

    m_selectedFileId = fileId;
    emit selectedFileIdChanged();
    rebuildFileWatchers();
}

void LibraryStore::setFilePreview(const QVariantMap &preview)
{
    if (m_filePreview == preview) {
        return;
    }

    m_filePreview = preview;
    emit filePreviewChanged();
}

void LibraryStore::setLoadingLibraries(bool loading)
{
    if (m_loadingLibraries == loading) {
        return;
    }

    m_loadingLibraries = loading;
    emit loadingLibrariesChanged();
}

void LibraryStore::setLoadingFiles(bool loading)
{
    if (m_loadingFiles == loading) {
        return;
    }

    m_loadingFiles = loading;
    emit loadingFilesChanged();
}

void LibraryStore::setLoadingFilePreview(bool loading)
{
    if (m_loadingFilePreview == loading) {
        return;
    }

    m_loadingFilePreview = loading;
    emit loadingFilePreviewChanged();
}

void LibraryStore::setScanning(bool scanning)
{
    if (m_scanning == scanning) {
        return;
    }

    m_scanning = scanning;
    emit scanningChanged();
}

void LibraryStore::setMovingFile(bool moving)
{
    if (m_movingFile == moving) {
        return;
    }

    m_movingFile = moving;
    emit movingFileChanged();
}

void LibraryStore::setMutatingEntry(bool mutating)
{
    if (m_mutatingEntry == mutating) {
        return;
    }

    m_mutatingEntry = mutating;
    emit mutatingEntryChanged();
}

void LibraryStore::setSavingFile(bool saving)
{
    if (m_savingFile == saving) {
        return;
    }

    m_savingFile = saving;
    emit savingFileChanged();
}

void LibraryStore::setCreatingLibrary(bool creating)
{
    if (m_creatingLibrary == creating) {
        return;
    }

    m_creatingLibrary = creating;
    emit creatingLibraryChanged();
}

void LibraryStore::setErrorMessage(const QString &message)
{
    if (m_errorMessage == message) {
        return;
    }

    m_errorMessage = message;
    emit errorMessageChanged();
}

void LibraryStore::setFilePreviewError(const QString &message)
{
    if (m_filePreviewError == message) {
        return;
    }

    m_filePreviewError = message;
    emit filePreviewErrorChanged();
}

void LibraryStore::setFileSaveError(const QString &message)
{
    if (m_fileSaveError == message) {
        return;
    }

    m_fileSaveError = message;
    emit fileSaveErrorChanged();
}

void LibraryStore::upsertLibrary(const QVariantMap &library)
{
    const QString libraryId = library.value(QStringLiteral("id")).toString();

    if (libraryId.isEmpty()) {
        return;
    }

    QVariantList nextLibraries = m_libraries;
    bool replaced = false;

    for (qsizetype index = 0; index < nextLibraries.size(); ++index) {
        if (
            nextLibraries.at(index).toMap().value(QStringLiteral("id")).toString()
            == libraryId
        ) {
            nextLibraries[index] = library;
            replaced = true;
            break;
        }
    }

    if (!replaced) {
        nextLibraries.prepend(library);
    }

    setLibraries(nextLibraries);
}

bool LibraryStore::containsLibrary(const QString &libraryId) const
{
    if (libraryId.isEmpty()) {
        return false;
    }

    for (const QVariant &value : m_libraries) {
        if (value.toMap().value(QStringLiteral("id")).toString() == libraryId) {
            return true;
        }
    }

    return false;
}

bool LibraryStore::containsFile(const QString &fileId) const
{
    if (fileId.isEmpty()) {
        return false;
    }

    for (const QVariant &value : m_files) {
        if (value.toMap().value(QStringLiteral("id")).toString() == fileId) {
            return true;
        }
    }

    return false;
}

#include "library_store.h"

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

QVariantMap LibraryStore::latestScan() const
{
    return m_latestScan;
}

QString LibraryStore::selectedFileId() const
{
    return m_selectedFileId;
}

QVariantMap LibraryStore::selectedFile() const
{
    for (const QVariant &value : m_files) {
        const QVariantMap file = value.toMap();
        if (file.value(QStringLiteral("id")).toString() == m_selectedFileId) {
            return file;
        }
    }

    return {};
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

QNetworkRequest LibraryStore::requestFor(const QString &path) const
{
    QNetworkRequest request{QUrl(m_baseUrl.toString() + path)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    return request;
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

    clearFilePreview();
    setErrorMessage({});
    setFiles({});
    setLatestScan({});
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
        clearFilePreview();
        setFiles({});
        setLatestScan({});
        setLoadingFiles(false);
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
            setLatestScan(
                result.object.value(QStringLiteral("latestScan")).toObject().toVariantMap()
            );
            setLoadingFiles(false);
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

    clearFilePreview();
    setErrorMessage({});
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
                setErrorMessage(result.errorMessage);
                return;
            }

            setFiles(
                result.object.value(QStringLiteral("files")).toArray().toVariantList()
            );

            QVariantMap scan =
                result.object.value(QStringLiteral("latestScan")).toObject().toVariantMap();
            if (scan.isEmpty()) {
                scan = result.object.value(QStringLiteral("scan")).toObject().toVariantMap();
            }
            setLatestScan(scan);
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
        const QVariantMap file = result.object.value(QStringLiteral("file")).toObject().toVariantMap();
        emit fileMoved(
            fileId,
            file.value(QStringLiteral("relativePath")).toString()
        );
    });
}

void LibraryStore::previewFile(const QString &fileId)
{
    if (m_selectedLibraryId.isEmpty() || fileId.isEmpty() || !containsFile(fileId)) {
        return;
    }

    const QVariantMap file = [&]() {
        for (const QVariant &value : m_files) {
            const QVariantMap candidate = value.toMap();
            if (candidate.value(QStringLiteral("id")).toString() == fileId) {
                return candidate;
            }
        }

        return QVariantMap{};
    }();

    setSelectedFileId(fileId);
    setFilePreview({});
    setFilePreviewError({});

    if (file.value(QStringLiteral("status")).toString() != QStringLiteral("available")) {
        setLoadingFilePreview(false);
        setFilePreviewError(
            QStringLiteral("This file is not currently available. Rescan the Library and try again.")
        );
        return;
    }

    if (usesDirectImageRenderer(file) || usesDocumentRenderer(file)) {
        setLoadingFilePreview(false);
        return;
    }

    setLoadingFilePreview(true);

    const QString libraryId = m_selectedLibraryId;
    const QString path = QStringLiteral("/libraries/%1/files/%2/content")
        .arg(encodedPathSegment(libraryId), encodedPathSegment(fileId));
    QNetworkReply *reply = m_network.get(requestFor(path));

    connect(
        reply,
        &QNetworkReply::finished,
        this,
        [this, reply, libraryId, fileId]() {
            const JsonReplyResult result = consumeJsonReply(reply);
            reply->deleteLater();

            if (m_selectedLibraryId != libraryId || m_selectedFileId != fileId) {
                return;
            }

            setLoadingFilePreview(false);

            if (!result.ok) {
                setFilePreviewError(result.errorMessage);
                return;
            }

            setFilePreview(
                result.object.value(QStringLiteral("preview")).toObject().toVariantMap()
            );
        }
    );
}

void LibraryStore::clearFilePreview()
{
    setSelectedFileId({});
    setFilePreview({});
    setLoadingFilePreview(false);
    setFilePreviewError({});
}

void LibraryStore::setLibraries(const QVariantList &libraries)
{
    if (m_libraries == libraries) {
        return;
    }

    m_libraries = libraries;
    emit librariesChanged();
    emit selectedLibraryChanged();
}

void LibraryStore::setSelectedLibraryId(const QString &libraryId)
{
    if (m_selectedLibraryId == libraryId) {
        return;
    }

    clearFilePreview();
    m_selectedLibraryId = libraryId;
    emit selectedLibraryIdChanged();
    emit selectedLibraryChanged();
}

void LibraryStore::setFiles(const QVariantList &files)
{
    if (m_files == files) {
        return;
    }

    m_files = files;
    emit filesChanged();
    emit selectedFileChanged();

    if (!m_selectedFileId.isEmpty() && !containsFile(m_selectedFileId)) {
        clearFilePreview();
    }
}

void LibraryStore::setLatestScan(const QVariantMap &latestScan)
{
    if (m_latestScan == latestScan) {
        return;
    }

    m_latestScan = latestScan;
    emit latestScanChanged();
}

void LibraryStore::setSelectedFileId(const QString &fileId)
{
    if (m_selectedFileId == fileId) {
        return;
    }

    m_selectedFileId = fileId;
    emit selectedFileIdChanged();
    emit selectedFileChanged();
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

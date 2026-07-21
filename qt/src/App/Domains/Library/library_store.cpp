#include "library_store.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>

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

    if (libraryId == m_selectedLibraryId) {
        refreshSelectedFiles();
        return;
    }

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

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const JsonReplyResult result = consumeJsonReply(reply);
        reply->deleteLater();

        if (!result.ok) {
            setLoadingFiles(false);
            setErrorMessage(result.errorMessage);
            return;
        }

        const QJsonObject appState = result.object.value(QStringLiteral("appState")).toObject();
        setSelectedLibraryId(appState.value(QStringLiteral("selectedLibraryId")).toString());
        setLoadingFiles(false);
        refreshSelectedFiles();
    });
}

void LibraryStore::refreshSelectedFiles()
{
    if (m_selectedLibraryId.isEmpty()) {
        clearFilePreview();
        setFiles({});
        setLatestScan({});
        setLoadingFiles(false);
        return;
    }

    setErrorMessage({});
    setLoadingFiles(true);

    const QString path = QStringLiteral("/libraries/%1/files")
        .arg(encodedPathSegment(m_selectedLibraryId));
    QNetworkReply *reply = m_network.get(requestFor(path));

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const JsonReplyResult result = consumeJsonReply(reply);
        reply->deleteLater();
        setLoadingFiles(false);

        if (!result.ok) {
            setErrorMessage(result.errorMessage);
            return;
        }

        setFiles(result.object.value(QStringLiteral("files")).toArray().toVariantList());
        setLatestScan(result.object.value(QStringLiteral("latestScan")).toObject().toVariantMap());
    });
}

void LibraryStore::scanSelectedLibrary()
{
    if (m_selectedLibraryId.isEmpty() || m_scanning) {
        return;
    }

    clearFilePreview();
    setErrorMessage({});
    setScanning(true);

    const QString path = QStringLiteral("/libraries/%1/scan")
        .arg(encodedPathSegment(m_selectedLibraryId));
    QNetworkReply *reply = m_network.post(requestFor(path), QByteArrayLiteral("{}"));

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const JsonReplyResult result = consumeJsonReply(reply);
        reply->deleteLater();
        setScanning(false);

        if (!result.ok) {
            setErrorMessage(result.errorMessage);
            return;
        }

        setFiles(result.object.value(QStringLiteral("files")).toArray().toVariantList());

        QVariantMap scan = result.object.value(QStringLiteral("latestScan")).toObject().toVariantMap();
        if (scan.isEmpty()) {
            scan = result.object.value(QStringLiteral("scan")).toObject().toVariantMap();
        }
        setLatestScan(scan);
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

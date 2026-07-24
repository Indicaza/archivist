#include "collection_store.h"

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

QJsonObject collectionPayload(
    const QString &name,
    const QString &parentCollectionId,
    const QVariantList &libraryIds,
    const QVariantList &chatIds,
    const QVariantList &agentIds,
    const QString &defaultAgentId
)
{
    QJsonObject body;
    body.insert(QStringLiteral("name"), name.trimmed());
    body.insert(
        QStringLiteral("parentCollectionId"),
        parentCollectionId.isEmpty() ? QJsonValue::Null : QJsonValue(parentCollectionId)
    );
    body.insert(
        QStringLiteral("libraryIds"),
        QJsonArray::fromVariantList(libraryIds)
    );
    body.insert(
        QStringLiteral("chatIds"),
        QJsonArray::fromVariantList(chatIds)
    );
    body.insert(
        QStringLiteral("agentIds"),
        QJsonArray::fromVariantList(agentIds)
    );
    body.insert(
        QStringLiteral("defaultAgentId"),
        defaultAgentId.isEmpty() ? QJsonValue::Null : QJsonValue(defaultAgentId)
    );
    return body;
}
}

CollectionStore::CollectionStore(QObject *parent)
    : QObject(parent)
    , m_baseUrl(QStringLiteral("http://127.0.0.1:3333/api"))
{
}

QVariantList CollectionStore::collections() const
{
    return m_collections;
}

QVariantList CollectionStore::archivedCollections() const
{
    return m_archivedCollections;
}

QString CollectionStore::selectedCollectionId() const
{
    return m_selectedCollectionId;
}

QVariantMap CollectionStore::selectedCollection() const
{
    for (const QVariant &value : m_collections) {
        const QVariantMap collection = value.toMap();
        if (collection.value(QStringLiteral("id")).toString() == m_selectedCollectionId) {
            return collection;
        }
    }

    return {};
}

QVariantMap CollectionStore::scope() const
{
    return m_scope;
}

bool CollectionStore::loading() const
{
    return m_loading;
}

bool CollectionStore::loadingArchived() const
{
    return m_loadingArchived;
}

bool CollectionStore::mutating() const
{
    return m_mutating;
}

QString CollectionStore::errorMessage() const
{
    return m_errorMessage;
}

QNetworkRequest CollectionStore::requestFor(const QString &path) const
{
    QNetworkRequest request{QUrl(m_baseUrl.toString() + path)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    return request;
}

void CollectionStore::refresh()
{
    if (m_loading) {
        return;
    }

    setErrorMessage({});
    setLoading(true);

    QNetworkReply *reply = m_network.get(requestFor(QStringLiteral("/collections")));

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const JsonReplyResult result = consumeJsonReply(reply);
        reply->deleteLater();

        if (!result.ok) {
            setLoading(false);
            setErrorMessage(result.errorMessage);
            return;
        }

        setCollections(
            result.object.value(QStringLiteral("collections")).toArray().toVariantList()
        );
        fetchAppState();
    });
}

void CollectionStore::refreshArchived()
{
    if (m_loadingArchived) {
        return;
    }

    setErrorMessage({});
    setLoadingArchived(true);

    QNetworkReply *reply = m_network.get(requestFor(QStringLiteral("/collections/archived")));

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const JsonReplyResult result = consumeJsonReply(reply);
        reply->deleteLater();
        setLoadingArchived(false);

        if (!result.ok) {
            setErrorMessage(result.errorMessage);
            return;
        }

        setArchivedCollections(
            result.object.value(QStringLiteral("collections")).toArray().toVariantList()
        );
    });
}

void CollectionStore::fetchAppState()
{
    QNetworkReply *reply = m_network.get(requestFor(QStringLiteral("/app-state")));

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const JsonReplyResult result = consumeJsonReply(reply);
        reply->deleteLater();
        setLoading(false);

        if (!result.ok) {
            setErrorMessage(result.errorMessage);
            return;
        }

        const QJsonObject appState = result.object.value(QStringLiteral("appState")).toObject();
        QString nextCollectionId =
            appState.value(QStringLiteral("selectedCollectionId")).toString();

        if (!containsCollection(nextCollectionId)) {
            nextCollectionId.clear();
        }

        setSelectedCollectionId(nextCollectionId);

        if (nextCollectionId.isEmpty()) {
            setScope({});
            emit workspaceScopeChanged();
            return;
        }

        fetchSelectedScope();
    });
}

void CollectionStore::fetchSelectedScope()
{
    if (m_selectedCollectionId.isEmpty()) {
        setScope({});
        emit workspaceScopeChanged();
        return;
    }

    const QString requestedCollectionId = m_selectedCollectionId;
    const QString path = QStringLiteral("/collections/%1/scope")
        .arg(encodedPathSegment(requestedCollectionId));
    QNetworkReply *reply = m_network.get(requestFor(path));

    connect(
        reply,
        &QNetworkReply::finished,
        this,
        [this, reply, requestedCollectionId]() {
            const JsonReplyResult result = consumeJsonReply(reply);
            reply->deleteLater();

            if (requestedCollectionId != m_selectedCollectionId) {
                return;
            }

            if (!result.ok) {
                setErrorMessage(result.errorMessage);
                return;
            }

            setScope(result.object.value(QStringLiteral("scope")).toObject().toVariantMap());
            emit workspaceScopeChanged();
        }
    );
}

void CollectionStore::selectCollection(const QString &collectionId)
{
    if (
        m_mutating
        || (!collectionId.isEmpty() && !containsCollection(collectionId))
        || collectionId == m_selectedCollectionId
    ) {
        return;
    }

    setErrorMessage({});
    setMutating(true);

    QJsonObject body;
    body.insert(
        QStringLiteral("selectedCollectionId"),
        collectionId.isEmpty() ? QJsonValue::Null : QJsonValue(collectionId)
    );

    QNetworkReply *reply = m_network.sendCustomRequest(
        requestFor(QStringLiteral("/app-state/selected-collection")),
        QByteArrayLiteral("PATCH"),
        QJsonDocument(body).toJson(QJsonDocument::Compact)
    );

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const JsonReplyResult result = consumeJsonReply(reply);
        reply->deleteLater();
        setMutating(false);

        if (!result.ok) {
            setErrorMessage(result.errorMessage);
            return;
        }

        const QJsonObject appState = result.object.value(QStringLiteral("appState")).toObject();
        setSelectedCollectionId(
            appState.value(QStringLiteral("selectedCollectionId")).toString()
        );

        if (m_selectedCollectionId.isEmpty()) {
            setScope({});
            emit workspaceScopeChanged();
            return;
        }

        fetchSelectedScope();
    });
}

void CollectionStore::createCollection(
    const QString &name,
    const QString &parentCollectionId,
    const QVariantList &libraryIds,
    const QVariantList &chatIds,
    const QVariantList &agentIds,
    const QString &defaultAgentId
)
{
    if (m_mutating || name.trimmed().isEmpty()) {
        return;
    }

    setErrorMessage({});
    setMutating(true);
    const QJsonObject body = collectionPayload(
        name,
        parentCollectionId,
        libraryIds,
        chatIds,
        agentIds,
        defaultAgentId
    );
    QNetworkReply *reply = m_network.post(
        requestFor(QStringLiteral("/collections")),
        QJsonDocument(body).toJson(QJsonDocument::Compact)
    );

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const JsonReplyResult result = consumeJsonReply(reply);
        reply->deleteLater();
        setMutating(false);

        if (!result.ok) {
            setErrorMessage(result.errorMessage);
            return;
        }

        const QVariantMap collection =
            result.object.value(QStringLiteral("collection")).toObject().toVariantMap();
        upsertCollection(collection);
        emit collectionCreated(collection);
        selectCollection(collection.value(QStringLiteral("id")).toString());
    });
}

void CollectionStore::updateCollection(
    const QString &collectionId,
    const QString &name,
    const QString &parentCollectionId,
    const QVariantList &libraryIds,
    const QVariantList &chatIds,
    const QVariantList &agentIds,
    const QString &defaultAgentId
)
{
    if (
        m_mutating
        || collectionId.isEmpty()
        || !containsCollection(collectionId)
        || name.trimmed().isEmpty()
    ) {
        return;
    }

    setErrorMessage({});
    setMutating(true);
    const QJsonObject body = collectionPayload(
        name,
        parentCollectionId,
        libraryIds,
        chatIds,
        agentIds,
        defaultAgentId
    );
    const QString path =
        QStringLiteral("/collections/%1").arg(encodedPathSegment(collectionId));
    QNetworkReply *reply = m_network.sendCustomRequest(
        requestFor(path),
        QByteArrayLiteral("PATCH"),
        QJsonDocument(body).toJson(QJsonDocument::Compact)
    );

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const JsonReplyResult result = consumeJsonReply(reply);
        reply->deleteLater();
        setMutating(false);

        if (!result.ok) {
            setErrorMessage(result.errorMessage);
            return;
        }

        const QVariantMap collection =
            result.object.value(QStringLiteral("collection")).toObject().toVariantMap();
        upsertCollection(collection);
        emit collectionUpdated(collection);

        if (
            collection.value(QStringLiteral("id")).toString()
            == m_selectedCollectionId
        ) {
            fetchSelectedScope();
        }
    });
}

void CollectionStore::archiveCollection(const QString &collectionId)
{
    if (m_mutating || collectionId.isEmpty() || !containsCollection(collectionId)) {
        return;
    }

    setErrorMessage({});
    setMutating(true);
    const QString path = QStringLiteral("/collections/%1/archive")
        .arg(encodedPathSegment(collectionId));
    QNetworkReply *reply = m_network.post(requestFor(path), QByteArrayLiteral("{}"));

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const JsonReplyResult result = consumeJsonReply(reply);
        reply->deleteLater();
        setMutating(false);

        if (!result.ok) {
            setErrorMessage(result.errorMessage);
            return;
        }

        const QVariantMap collection =
            result.object.value(QStringLiteral("collection")).toObject().toVariantMap();
        const QString collectionId = collection.value(QStringLiteral("id")).toString();
        removeCollection(collectionId);
        upsertArchivedCollection(collection);
        setSelectedCollectionId(
            result.object.value(QStringLiteral("selectedCollectionId")).toString()
        );

        if (m_selectedCollectionId.isEmpty()) {
            setScope({});
            emit workspaceScopeChanged();
        } else {
            fetchSelectedScope();
        }

        emit collectionArchived(collection);
        refresh();
        refreshArchived();
    });
}

void CollectionStore::restoreCollection(const QString &collectionId)
{
    if (m_mutating || collectionId.isEmpty()) {
        return;
    }

    setErrorMessage({});
    setMutating(true);
    const QString path = QStringLiteral("/collections/%1/restore")
        .arg(encodedPathSegment(collectionId));
    QNetworkReply *reply = m_network.post(requestFor(path), QByteArrayLiteral("{}"));

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const JsonReplyResult result = consumeJsonReply(reply);
        reply->deleteLater();
        setMutating(false);

        if (!result.ok) {
            setErrorMessage(result.errorMessage);
            return;
        }

        const QVariantMap collection =
            result.object.value(QStringLiteral("collection")).toObject().toVariantMap();
        removeArchivedCollection(collection.value(QStringLiteral("id")).toString());
        upsertCollection(collection);
        emit collectionRestored(collection);
    });
}

bool CollectionStore::includesLibrary(const QString &libraryId) const
{
    return m_selectedCollectionId.isEmpty()
        || scopeContains(QStringLiteral("libraryIds"), libraryId);
}

bool CollectionStore::includesChat(const QString &chatId) const
{
    return m_selectedCollectionId.isEmpty()
        || scopeContains(QStringLiteral("chatIds"), chatId);
}

bool CollectionStore::isRosterAgent(const QString &agentId) const
{
    return !m_selectedCollectionId.isEmpty()
        && scopeContains(QStringLiteral("agentIds"), agentId);
}

void CollectionStore::setCollections(const QVariantList &collections)
{
    if (m_collections == collections) {
        return;
    }

    m_collections = collections;
    emit collectionsChanged();
    emit selectedCollectionChanged();
}

void CollectionStore::setArchivedCollections(const QVariantList &collections)
{
    if (m_archivedCollections == collections) {
        return;
    }

    m_archivedCollections = collections;
    emit archivedCollectionsChanged();
}

void CollectionStore::setSelectedCollectionId(const QString &collectionId)
{
    if (m_selectedCollectionId == collectionId) {
        return;
    }

    m_selectedCollectionId = collectionId;
    emit selectedCollectionIdChanged();
    emit selectedCollectionChanged();
}

void CollectionStore::setScope(const QVariantMap &scope)
{
    if (m_scope == scope) {
        return;
    }

    m_scope = scope;
    emit scopeChanged();
}

void CollectionStore::setLoading(bool loading)
{
    if (m_loading == loading) {
        return;
    }

    m_loading = loading;
    emit loadingChanged();
}

void CollectionStore::setLoadingArchived(bool loading)
{
    if (m_loadingArchived == loading) {
        return;
    }

    m_loadingArchived = loading;
    emit loadingArchivedChanged();
}

void CollectionStore::setMutating(bool mutating)
{
    if (m_mutating == mutating) {
        return;
    }

    m_mutating = mutating;
    emit mutatingChanged();
}

void CollectionStore::setErrorMessage(const QString &message)
{
    if (m_errorMessage == message) {
        return;
    }

    m_errorMessage = message;
    emit errorMessageChanged();
}

bool CollectionStore::containsCollection(const QString &collectionId) const
{
    if (collectionId.isEmpty()) {
        return false;
    }

    for (const QVariant &value : m_collections) {
        if (
            value.toMap().value(QStringLiteral("id")).toString()
            == collectionId
        ) {
            return true;
        }
    }

    return false;
}

bool CollectionStore::scopeContains(const QString &key, const QString &id) const
{
    if (id.isEmpty()) {
        return false;
    }

    const QVariantList ids = m_scope.value(key).toList();
    for (const QVariant &value : ids) {
        if (value.toString() == id) {
            return true;
        }
    }

    return false;
}

void CollectionStore::upsertCollection(const QVariantMap &collection)
{
    const QString collectionId = collection.value(QStringLiteral("id")).toString();
    QVariantList nextCollections = m_collections;
    bool replaced = false;

    for (qsizetype index = 0; index < nextCollections.size(); ++index) {
        if (
            nextCollections.at(index).toMap().value(QStringLiteral("id")).toString()
            == collectionId
        ) {
            nextCollections[index] = collection;
            replaced = true;
            break;
        }
    }

    if (!replaced) {
        nextCollections.append(collection);
    }

    setCollections(nextCollections);
}

void CollectionStore::removeCollection(const QString &collectionId)
{
    QVariantList nextCollections;

    for (const QVariant &value : m_collections) {
        if (
            value.toMap().value(QStringLiteral("id")).toString()
            != collectionId
        ) {
            nextCollections.append(value);
        }
    }

    setCollections(nextCollections);
}

void CollectionStore::upsertArchivedCollection(const QVariantMap &collection)
{
    const QString collectionId = collection.value(QStringLiteral("id")).toString();
    QVariantList nextCollections = m_archivedCollections;
    bool replaced = false;

    for (qsizetype index = 0; index < nextCollections.size(); ++index) {
        if (
            nextCollections.at(index).toMap().value(QStringLiteral("id")).toString()
            == collectionId
        ) {
            nextCollections[index] = collection;
            replaced = true;
            break;
        }
    }

    if (!replaced) {
        nextCollections.prepend(collection);
    }

    setArchivedCollections(nextCollections);
}

void CollectionStore::removeArchivedCollection(const QString &collectionId)
{
    QVariantList nextCollections;

    for (const QVariant &value : m_archivedCollections) {
        if (
            value.toMap().value(QStringLiteral("id")).toString()
            != collectionId
        ) {
            nextCollections.append(value);
        }
    }

    setArchivedCollections(nextCollections);
}

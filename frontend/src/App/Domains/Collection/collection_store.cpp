#include "collection_store.h"

#include "Core/Domains/AppState/app_state_repository.h"
#include "Core/Domains/Collections/collection_repository.h"
#include "Core/core_runtime.h"

namespace
{
const QString coreUnavailableMessage = QStringLiteral(
    "Archivist's data layer is not running. Restart the app."
);

QVariantMap collectionInput(
    const QString &name,
    const QString &parentCollectionId,
    const QVariantList &libraryIds,
    const QVariantList &chatIds,
    const QVariantList &agentIds,
    const QString &defaultAgentId
)
{
    QVariantMap input;
    input.insert(QStringLiteral("name"), name.trimmed());
    input.insert(QStringLiteral("parentCollectionId"), parentCollectionId);
    input.insert(QStringLiteral("libraryIds"), libraryIds);
    input.insert(QStringLiteral("chatIds"), chatIds);
    input.insert(QStringLiteral("agentIds"), agentIds);
    input.insert(QStringLiteral("defaultAgentId"), defaultAgentId);
    return input;
}
}

CollectionStore::CollectionStore(QObject *parent)
    : QObject(parent)
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

void CollectionStore::refresh()
{
    if (m_loading) {
        return;
    }

    CoreRuntime *core = CoreRuntime::active();

    if (!core) {
        setErrorMessage(coreUnavailableMessage);
        return;
    }

    setErrorMessage({});
    setLoading(true);

    core->read(
        [](CoreDatabase &database) {
            return QVariant(CollectionRepository::activeCollections(database));
        },
        this,
        [this](const CoreResult &result) {
            if (!result.ok) {
                setLoading(false);
                setErrorMessage(result.errorMessage);
                return;
            }

            setCollections(result.value.toList());
            fetchAppState();
        }
    );
}

void CollectionStore::refreshArchived()
{
    if (m_loadingArchived) {
        return;
    }

    CoreRuntime *core = CoreRuntime::active();

    if (!core) {
        setErrorMessage(coreUnavailableMessage);
        return;
    }

    setErrorMessage({});
    setLoadingArchived(true);

    core->read(
        [](CoreDatabase &database) {
            return QVariant(CollectionRepository::archivedCollections(database));
        },
        this,
        [this](const CoreResult &result) {
            setLoadingArchived(false);

            if (!result.ok) {
                setErrorMessage(result.errorMessage);
                return;
            }

            setArchivedCollections(result.value.toList());
        }
    );
}

void CollectionStore::fetchAppState()
{
    CoreRuntime *core = CoreRuntime::active();

    if (!core) {
        setLoading(false);
        setErrorMessage(coreUnavailableMessage);
        return;
    }

    core->read(
        [](CoreDatabase &database) {
            return QVariant(AppStateRepository::appState(database));
        },
        this,
        [this](const CoreResult &result) {
            setLoading(false);

            if (!result.ok) {
                setErrorMessage(result.errorMessage);
                return;
            }

            QString nextCollectionId = result.value.toMap()
                .value(QStringLiteral("selectedCollectionId")).toString();

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
        }
    );
}

void CollectionStore::refreshScope()
{
    fetchSelectedScope();
}

void CollectionStore::fetchSelectedScope()
{
    if (m_selectedCollectionId.isEmpty()) {
        setScope({});
        emit workspaceScopeChanged();
        return;
    }

    CoreRuntime *core = CoreRuntime::active();

    if (!core) {
        setErrorMessage(coreUnavailableMessage);
        return;
    }

    const QString requestedCollectionId = m_selectedCollectionId;

    core->read(
        [requestedCollectionId](CoreDatabase &database) {
            return QVariant(CollectionRepository::scope(database, requestedCollectionId));
        },
        this,
        [this, requestedCollectionId](const CoreResult &result) {
            if (requestedCollectionId != m_selectedCollectionId) {
                return;
            }

            if (!result.ok) {
                setErrorMessage(result.errorMessage);
                return;
            }

            setScope(result.value.toMap());
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

    CoreRuntime *core = CoreRuntime::active();

    if (!core) {
        setErrorMessage(coreUnavailableMessage);
        return;
    }

    setErrorMessage({});
    setMutating(true);

    core->write(
        [collectionId](CoreDatabase &database) {
            return QVariant(AppStateRepository::setSelectedCollection(database, collectionId));
        },
        this,
        [this](const CoreResult &result) {
            setMutating(false);

            if (!result.ok) {
                setErrorMessage(result.errorMessage);
                return;
            }

            setSelectedCollectionId(
                result.value.toMap().value(QStringLiteral("selectedCollectionId")).toString()
            );

            if (m_selectedCollectionId.isEmpty()) {
                setScope({});
                emit workspaceScopeChanged();
                return;
            }

            fetchSelectedScope();
        }
    );
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

    CoreRuntime *core = CoreRuntime::active();

    if (!core) {
        setErrorMessage(coreUnavailableMessage);
        return;
    }

    setErrorMessage({});
    setMutating(true);
    const QVariantMap input = collectionInput(
        name,
        parentCollectionId,
        libraryIds,
        chatIds,
        agentIds,
        defaultAgentId
    );

    core->write(
        [input](CoreDatabase &database) {
            return QVariant(CollectionRepository::createCollection(database, input));
        },
        this,
        [this](const CoreResult &result) {
            setMutating(false);

            if (!result.ok) {
                setErrorMessage(result.errorMessage);
                return;
            }

            const QVariantMap collection = result.value.toMap();
            upsertCollection(collection);
            emit collectionCreated(collection);
            selectCollection(collection.value(QStringLiteral("id")).toString());
        }
    );
}

void CollectionStore::addLibraryToCollection(
    const QString &collectionId,
    const QString &libraryId
)
{
    if (m_mutating || collectionId.isEmpty() || libraryId.isEmpty()) {
        return;
    }

    QVariantMap collection;

    for (const QVariant &value : m_collections) {
        const QVariantMap candidate = value.toMap();

        if (candidate.value(QStringLiteral("id")).toString() == collectionId) {
            collection = candidate;
            break;
        }
    }

    if (collection.isEmpty()) {
        setErrorMessage(QStringLiteral("The target Collection is no longer available."));
        return;
    }

    QVariantList libraryIds = collection.value(QStringLiteral("libraryIds")).toList();

    for (const QVariant &value : libraryIds) {
        if (value.toString() == libraryId) {
            if (collectionId == m_selectedCollectionId) {
                fetchSelectedScope();
            }
            return;
        }
    }

    libraryIds.append(libraryId);

    updateCollection(
        collectionId,
        collection.value(QStringLiteral("name")).toString(),
        collection.value(QStringLiteral("parentCollectionId")).toString(),
        libraryIds,
        collection.value(QStringLiteral("chatIds")).toList(),
        collection.value(QStringLiteral("agentIds")).toList(),
        collection.value(QStringLiteral("defaultAgentId")).toString()
    );
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

    CoreRuntime *core = CoreRuntime::active();

    if (!core) {
        setErrorMessage(coreUnavailableMessage);
        return;
    }

    setErrorMessage({});
    setMutating(true);
    const QVariantMap input = collectionInput(
        name,
        parentCollectionId,
        libraryIds,
        chatIds,
        agentIds,
        defaultAgentId
    );

    core->write(
        [collectionId, input](CoreDatabase &database) {
            return QVariant(CollectionRepository::updateCollection(database, collectionId, input));
        },
        this,
        [this](const CoreResult &result) {
            setMutating(false);

            if (!result.ok) {
                setErrorMessage(result.errorMessage);
                return;
            }

            const QVariantMap collection = result.value.toMap();
            upsertCollection(collection);
            emit collectionUpdated(collection);

            if (
                collection.value(QStringLiteral("id")).toString()
                == m_selectedCollectionId
            ) {
                fetchSelectedScope();
            }
        }
    );
}

void CollectionStore::archiveCollection(const QString &collectionId)
{
    if (m_mutating || collectionId.isEmpty() || !containsCollection(collectionId)) {
        return;
    }

    CoreRuntime *core = CoreRuntime::active();

    if (!core) {
        setErrorMessage(coreUnavailableMessage);
        return;
    }

    setErrorMessage({});
    setMutating(true);

    core->write(
        [collectionId](CoreDatabase &database) {
            return QVariant(CollectionRepository::archiveCollection(database, collectionId));
        },
        this,
        [this](const CoreResult &result) {
            setMutating(false);

            if (!result.ok) {
                setErrorMessage(result.errorMessage);
                return;
            }

            const QVariantMap payload = result.value.toMap();
            const QVariantMap collection = payload.value(QStringLiteral("collection")).toMap();
            removeCollection(collection.value(QStringLiteral("id")).toString());
            upsertArchivedCollection(collection);
            setSelectedCollectionId(
                payload.value(QStringLiteral("selectedCollectionId")).toString()
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
        }
    );
}

void CollectionStore::restoreCollection(const QString &collectionId)
{
    if (m_mutating || collectionId.isEmpty()) {
        return;
    }

    CoreRuntime *core = CoreRuntime::active();

    if (!core) {
        setErrorMessage(coreUnavailableMessage);
        return;
    }

    setErrorMessage({});
    setMutating(true);

    core->write(
        [collectionId](CoreDatabase &database) {
            return QVariant(CollectionRepository::restoreCollection(database, collectionId));
        },
        this,
        [this](const CoreResult &result) {
            setMutating(false);

            if (!result.ok) {
                setErrorMessage(result.errorMessage);
                return;
            }

            const QVariantMap collection = result.value.toMap();
            removeArchivedCollection(collection.value(QStringLiteral("id")).toString());
            upsertCollection(collection);
            emit collectionRestored(collection);
        }
    );
}

bool CollectionStore::includesLibrary(const QString &libraryId) const
{
    return m_selectedCollectionId.isEmpty()
        || scopeContains(QStringLiteral("libraryIds"), libraryId)
        || scopeContains(QStringLiteral("worktreeLibraryIds"), libraryId);
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

    if (!m_scope.isEmpty()) {
        m_scope.clear();
        emit scopeChanged();
    }

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

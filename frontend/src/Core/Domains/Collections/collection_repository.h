#pragma once

#include "Core/Database/core_database.h"

#include <QVariantList>
#include <QVariantMap>
#include <optional>

// Native port of backend/src/api/collections/models/Collection.ts.
// Every function takes the connection it should use: the UI-thread reader for
// reads, the writer connection (inside its transaction) for changes.
namespace CollectionRepository
{
[[nodiscard]] QVariantList activeCollections(CoreDatabase &database);
[[nodiscard]] QVariantList archivedCollections(CoreDatabase &database);
[[nodiscard]] std::optional<QVariantMap> collectionById(
    CoreDatabase &database,
    const QString &collectionId
);
[[nodiscard]] QVariantMap scope(CoreDatabase &database, const QString &collectionId);

// `input` keys match the API body: name, parentCollectionId, position,
// libraryIds, chatIds, agentIds, defaultAgentId. For update, a missing key
// keeps the current value and a null key clears it.
QVariantMap createCollection(CoreDatabase &database, const QVariantMap &input);
QVariantMap updateCollection(
    CoreDatabase &database,
    const QString &collectionId,
    const QVariantMap &input
);
// Returns {collection, selectedCollectionId}.
QVariantMap archiveCollection(CoreDatabase &database, const QString &collectionId);
QVariantMap restoreCollection(CoreDatabase &database, const QString &collectionId);
}

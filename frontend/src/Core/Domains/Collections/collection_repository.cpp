#include "collection_repository.h"

#include "Core/Support/core_values.h"
#include "Core/core_error.h"

#include <QHash>
#include <algorithm>

using namespace CoreValues;

namespace
{
struct CollectionRow
{
    QString id;
    QString parentCollectionId;
    QString name;
    qint64 position = 0;
    QVariant archivedAt;
    QString createdAt;
    QString updatedAt;
};

const QString collectionColumns = QStringLiteral(
    "id, parent_collection_id, name, position, archived_at, created_at, updated_at"
);

CollectionRow toCollectionRow(const QVariantMap &row)
{
    return {
        row.value(QStringLiteral("id")).toString(),
        row.value(QStringLiteral("parent_collection_id")).toString(),
        row.value(QStringLiteral("name")).toString(),
        row.value(QStringLiteral("position")).toLongLong(),
        row.value(QStringLiteral("archived_at")),
        row.value(QStringLiteral("created_at")).toString(),
        row.value(QStringLiteral("updated_at")).toString(),
    };
}

QList<CollectionRow> loadCollectionRows(CoreDatabase &database, bool archived)
{
    QList<CollectionRow> rows;
    const QString sql = QStringLiteral(
        "SELECT %1 FROM collections WHERE archived_at IS %2 "
        "ORDER BY position ASC, name COLLATE NOCASE ASC"
    ).arg(collectionColumns, archived ? QStringLiteral("NOT NULL") : QStringLiteral("NULL"));

    for (const QVariantMap &row : database.prepare(sql).all()) {
        rows.append(toCollectionRow(row));
    }

    return rows;
}

std::optional<CollectionRow> loadCollectionRow(CoreDatabase &database, const QString &collectionId)
{
    const auto row = database
        .prepare(QStringLiteral("SELECT %1 FROM collections WHERE id = ?").arg(collectionColumns))
        .bind(1, collectionId)
        .one();

    if (!row) {
        return std::nullopt;
    }

    return toCollectionRow(*row);
}

QList<QVariantMap> selectIn(
    CoreDatabase &database,
    const QString &sqlWithIn,
    const QStringList &ids,
    const QVariantList &leadingBindings = {}
)
{
    if (ids.isEmpty()) {
        return {};
    }

    auto statement = database.prepare(sqlWithIn.arg(placeholders(ids.size())));
    int index = 1;

    for (const QVariant &value : leadingBindings) {
        statement.bind(index++, value);
    }

    for (const QString &id : ids) {
        statement.bind(index++, id);
    }

    return statement.all();
}

QString collectionPath(const CollectionRow &row, const QHash<QString, CollectionRow> &rowsById)
{
    QStringList names{row.name};
    QSet<QString> seen{row.id};
    QString parentId = row.parentCollectionId;

    while (!parentId.isEmpty()) {
        if (seen.contains(parentId)) {
            break;
        }

        seen.insert(parentId);
        const auto parent = rowsById.constFind(parentId);

        if (parent == rowsById.constEnd()) {
            break;
        }

        names.prepend(parent->name);
        parentId = parent->parentCollectionId;
    }

    return names.join(QStringLiteral(" / "));
}

QVariantList mapCollections(CoreDatabase &database, const QList<CollectionRow> &rows)
{
    QStringList collectionIds;
    QHash<QString, CollectionRow> rowsById;

    for (const CollectionRow &row : rows) {
        collectionIds.append(row.id);
        rowsById.insert(row.id, row);
    }

    QHash<QString, QStringList> libraryIdsByCollection;
    QHash<QString, QStringList> chatIdsByCollection;
    QHash<QString, QStringList> agentIdsByCollection;
    QHash<QString, QString> defaultAgentByCollection;

    for (const QVariantMap &item : selectIn(
             database,
             QStringLiteral(
                 "SELECT collection_id, item_type, item_id FROM collection_items "
                 "WHERE collection_id IN (%1) "
                 "ORDER BY collection_id ASC, item_type ASC, position ASC, created_at ASC"
             ),
             collectionIds
         )) {
        const QString collectionId = item.value(QStringLiteral("collection_id")).toString();
        const QString type = item.value(QStringLiteral("item_type")).toString();
        const QString itemId = item.value(QStringLiteral("item_id")).toString();

        if (type == QLatin1String("library")) {
            libraryIdsByCollection[collectionId].append(itemId);
        } else if (type == QLatin1String("chat")) {
            chatIdsByCollection[collectionId].append(itemId);
        }
    }

    for (const QVariantMap &agent : selectIn(
             database,
             QStringLiteral(
                 "SELECT collection_id, agent_id, is_default FROM collection_agents "
                 "WHERE collection_id IN (%1) "
                 "ORDER BY collection_id ASC, position ASC, created_at ASC"
             ),
             collectionIds
         )) {
        const QString collectionId = agent.value(QStringLiteral("collection_id")).toString();
        const QString agentId = agent.value(QStringLiteral("agent_id")).toString();
        agentIdsByCollection[collectionId].append(agentId);

        if (agent.value(QStringLiteral("is_default")).toInt() == 1
            && !defaultAgentByCollection.contains(collectionId)) {
            defaultAgentByCollection.insert(collectionId, agentId);
        }
    }

    QList<QVariantMap> collections;
    collections.reserve(rows.size());

    for (const CollectionRow &row : rows) {
        QVariantMap collection;
        collection.insert(QStringLiteral("id"), row.id);
        collection.insert(QStringLiteral("parentCollectionId"), nullableString(row.parentCollectionId));
        collection.insert(QStringLiteral("name"), row.name);
        collection.insert(QStringLiteral("path"), collectionPath(row, rowsById));
        collection.insert(QStringLiteral("position"), row.position);
        collection.insert(QStringLiteral("libraryIds"), toVariantList(libraryIdsByCollection.value(row.id)));
        collection.insert(QStringLiteral("chatIds"), toVariantList(chatIdsByCollection.value(row.id)));
        collection.insert(QStringLiteral("agentIds"), toVariantList(agentIdsByCollection.value(row.id)));
        collection.insert(
            QStringLiteral("defaultAgentId"),
            nullableString(defaultAgentByCollection.value(row.id))
        );
        collection.insert(QStringLiteral("archivedAt"), nullable(row.archivedAt));
        collection.insert(QStringLiteral("createdAt"), row.createdAt);
        collection.insert(QStringLiteral("updatedAt"), row.updatedAt);
        collections.append(collection);
    }

    std::stable_sort(collections.begin(), collections.end(), [](const auto &left, const auto &right) {
        return QString::localeAwareCompare(
                   left.value(QStringLiteral("path")).toString(),
                   right.value(QStringLiteral("path")).toString()
               ) < 0;
    });

    QVariantList result;
    result.reserve(collections.size());

    for (const QVariantMap &collection : collections) {
        result.append(collection);
    }

    return result;
}

QVariantList allCollections(CoreDatabase &database)
{
    return mapCollections(
        database,
        loadCollectionRows(database, false) + loadCollectionRows(database, true)
    );
}

CollectionRow requireCollectionRow(CoreDatabase &database, const QString &collectionId)
{
    const auto row = loadCollectionRow(database, collectionId);

    if (!row) {
        throw CoreError(QStringLiteral("Collection not found."));
    }

    return *row;
}

CollectionRow requireActiveCollectionRow(CoreDatabase &database, const QString &collectionId)
{
    CollectionRow row = requireCollectionRow(database, collectionId);

    if (!isNullish(row.archivedAt)) {
        throw CoreError(QStringLiteral("This Collection is archived. Restore it before changing it."));
    }

    return row;
}

void validateParent(
    CoreDatabase &database,
    const QString &collectionId,
    const QString &parentCollectionId
)
{
    if (parentCollectionId.isEmpty()) {
        return;
    }

    if (parentCollectionId == collectionId) {
        throw CoreError(QStringLiteral("A Collection cannot contain itself."));
    }

    requireActiveCollectionRow(database, parentCollectionId);

    if (collectionId.isEmpty()) {
        return;
    }

    const bool descendant = database
        .prepare(QStringLiteral(
            "WITH RECURSIVE descendants(id) AS ("
            "  SELECT id FROM collections WHERE parent_collection_id = ?"
            "  UNION ALL"
            "  SELECT collections.id FROM collections"
            "  JOIN descendants ON collections.parent_collection_id = descendants.id"
            ") SELECT EXISTS(SELECT 1 FROM descendants WHERE id = ?)"
        ))
        .bind(1, collectionId)
        .bind(2, parentCollectionId)
        .scalar()
        .toInt() == 1;

    if (descendant) {
        throw CoreError(QStringLiteral("A Collection cannot be moved beneath one of its descendants."));
    }
}

void validateActiveTargets(CoreDatabase &database, const QString &targetType, const QStringList &ids)
{
    const QStringList uniqueTargetIds = uniqueIds(ids);

    if (uniqueTargetIds.isEmpty()) {
        return;
    }

    const QString table = targetType == QLatin1String("library")
        ? QStringLiteral("libraries")
        : targetType == QLatin1String("chat") ? QStringLiteral("chats") : QStringLiteral("agents");
    QSet<QString> found;

    for (const QVariantMap &row : selectIn(
             database,
             QStringLiteral("SELECT id FROM %1 WHERE id IN (%2) AND archived_at IS NULL")
                 .arg(table, QStringLiteral("%1")),
             uniqueTargetIds
         )) {
        found.insert(row.value(QStringLiteral("id")).toString());
    }

    for (const QString &id : uniqueTargetIds) {
        if (!found.contains(id)) {
            const QString label = targetType == QLatin1String("library")
                ? QStringLiteral("Library")
                : targetType == QLatin1String("chat") ? QStringLiteral("Chat") : QStringLiteral("Agent");
            throw CoreError(
                QStringLiteral("One or more Collection %1 references are unavailable.").arg(label)
            );
        }
    }
}

void replaceItems(
    CoreDatabase &database,
    const QString &collectionId,
    const QString &itemType,
    const QStringList &ids
)
{
    const QStringList targetIds = uniqueIds(ids);
    validateActiveTargets(database, itemType, targetIds);

    database
        .prepare(QStringLiteral("DELETE FROM collection_items WHERE collection_id = ? AND item_type = ?"))
        .bind(1, collectionId)
        .bind(2, itemType)
        .run();

    auto insert = database.prepare(QStringLiteral(
        "INSERT INTO collection_items (id, collection_id, item_type, item_id, position) "
        "VALUES (?, ?, ?, ?, ?)"
    ));

    for (qsizetype position = 0; position < targetIds.size(); ++position) {
        insert.bind(1, newId())
            .bind(2, collectionId)
            .bind(3, itemType)
            .bind(4, targetIds.at(position))
            .bind(5, position)
            .run();
    }
}

void replaceAgents(
    CoreDatabase &database,
    const QString &collectionId,
    const QStringList &agentIds,
    const QString &defaultAgentId
)
{
    const QStringList targetAgentIds = uniqueIds(agentIds);
    validateActiveTargets(database, QStringLiteral("agent"), targetAgentIds);

    if (!defaultAgentId.isEmpty() && !targetAgentIds.contains(defaultAgentId)) {
        throw CoreError(QStringLiteral("The default Agent must belong to the Collection roster."));
    }

    database
        .prepare(QStringLiteral("DELETE FROM collection_agents WHERE collection_id = ?"))
        .bind(1, collectionId)
        .run();

    auto insert = database.prepare(QStringLiteral(
        "INSERT INTO collection_agents (id, collection_id, agent_id, position, is_default) "
        "VALUES (?, ?, ?, ?, ?)"
    ));

    for (qsizetype position = 0; position < targetAgentIds.size(); ++position) {
        const QString &agentId = targetAgentIds.at(position);
        insert.bind(1, newId())
            .bind(2, collectionId)
            .bind(3, agentId)
            .bind(4, position)
            .bind(5, agentId == defaultAgentId ? 1 : 0)
            .run();
    }
}

QString selectedCollectionId(CoreDatabase &database)
{
    const auto row = database
        .prepare(QStringLiteral("SELECT selected_collection_id FROM app_settings WHERE id = 1"))
        .one();

    if (!row) {
        throw CoreError(QStringLiteral("Archivist app settings could not be loaded."));
    }

    return row->value(QStringLiteral("selected_collection_id")).toString();
}

QString validatedName(const QVariant &value)
{
    const QString name = value.toString().trimmed();

    if (name.isEmpty() || name.size() > 120) {
        throw CoreError(QStringLiteral("Invalid Collection data."));
    }

    return name;
}

QString validatedOptionalId(const QVariant &value)
{
    if (isNullish(value)) {
        return {};
    }

    const QString id = value.toString();

    if (!isUuid(id)) {
        throw CoreError(QStringLiteral("Invalid Collection data."));
    }

    return id;
}

QStringList validatedIdList(const QVariant &value)
{
    const QStringList ids = toStringList(value);

    if (ids.size() > 500) {
        throw CoreError(QStringLiteral("Invalid Collection data."));
    }

    for (const QString &id : ids) {
        if (!isUuid(id)) {
            throw CoreError(QStringLiteral("Invalid Collection data."));
        }
    }

    return ids;
}

qint64 validatedPosition(const QVariant &value, qint64 fallback)
{
    if (isNullish(value)) {
        return fallback;
    }

    bool ok = false;
    const double number = value.toDouble(&ok);

    if (!ok || number < 0 || number != static_cast<double>(static_cast<qint64>(number))) {
        throw CoreError(QStringLiteral("Invalid Collection data."));
    }

    return static_cast<qint64>(number);
}

QVariantMap requireMappedCollection(CoreDatabase &database, const QString &collectionId, const char *what)
{
    const auto collection = CollectionRepository::collectionById(database, collectionId);

    if (!collection) {
        throw CoreError(QStringLiteral("The %1 Collection could not be loaded.").arg(QString::fromLatin1(what)));
    }

    return *collection;
}
}

namespace CollectionRepository
{
QVariantList activeCollections(CoreDatabase &database)
{
    QVariantList result;

    for (const QVariant &collection : allCollections(database)) {
        if (isNullish(collection.toMap().value(QStringLiteral("archivedAt")))) {
            result.append(collection);
        }
    }

    return result;
}

QVariantList archivedCollections(CoreDatabase &database)
{
    QVariantList result;

    for (const QVariant &collection : allCollections(database)) {
        if (!isNullish(collection.toMap().value(QStringLiteral("archivedAt")))) {
            result.append(collection);
        }
    }

    return result;
}

std::optional<QVariantMap> collectionById(CoreDatabase &database, const QString &collectionId)
{
    if (!loadCollectionRow(database, collectionId)) {
        return std::nullopt;
    }

    for (const QVariant &collection : allCollections(database)) {
        const QVariantMap map = collection.toMap();

        if (map.value(QStringLiteral("id")).toString() == collectionId) {
            return map;
        }
    }

    return std::nullopt;
}

QVariantMap scope(CoreDatabase &database, const QString &collectionId)
{
    requireActiveCollectionRow(database, collectionId);

    QStringList collectionIds;

    for (const QVariantMap &row : database
             .prepare(QStringLiteral(
                 "WITH RECURSIVE scoped_collections(id, depth) AS ("
                 "  SELECT id, 0 FROM collections WHERE id = ? AND archived_at IS NULL"
                 "  UNION ALL"
                 "  SELECT collections.id, scoped_collections.depth + 1 FROM collections"
                 "  JOIN scoped_collections ON collections.parent_collection_id = scoped_collections.id"
                 "  WHERE collections.archived_at IS NULL"
                 ") SELECT scoped_collections.id FROM scoped_collections"
                 " JOIN collections ON collections.id = scoped_collections.id"
                 " ORDER BY scoped_collections.depth ASC, collections.position ASC,"
                 " collections.name COLLATE NOCASE ASC"
             ))
             .bind(1, collectionId)
             .all()) {
        collectionIds.append(row.value(QStringLiteral("id")).toString());
    }

    if (collectionIds.isEmpty()) {
        throw CoreError(QStringLiteral("Collection not found."));
    }

    QStringList referencedLibraryIds;
    QStringList referencedChatIds;

    for (const QVariantMap &item : selectIn(
             database,
             QStringLiteral(
                 "SELECT collection_items.item_type, collection_items.item_id FROM collection_items"
                 " JOIN collections ON collections.id = collection_items.collection_id"
                 " WHERE collection_items.collection_id IN (%1)"
                 " ORDER BY collections.position ASC, collection_items.position ASC,"
                 " collection_items.created_at ASC"
             ),
             collectionIds
         )) {
        const QString type = item.value(QStringLiteral("item_type")).toString();
        const QString itemId = item.value(QStringLiteral("item_id")).toString();

        if (type == QLatin1String("library")) {
            referencedLibraryIds.append(itemId);
        } else if (type == QLatin1String("chat")) {
            referencedChatIds.append(itemId);
        }
    }

    referencedLibraryIds = uniqueIds(referencedLibraryIds);
    referencedChatIds = uniqueIds(referencedChatIds);

    auto activeIds = [&](const QString &table, const QStringList &ids) {
        QSet<QString> active;

        for (const QVariantMap &row : selectIn(
                 database,
                 QStringLiteral("SELECT id FROM %1 WHERE id IN (%2) AND archived_at IS NULL")
                     .arg(table, QStringLiteral("%1")),
                 ids
             )) {
            active.insert(row.value(QStringLiteral("id")).toString());
        }

        QStringList ordered;

        for (const QString &id : ids) {
            if (active.contains(id)) {
                ordered.append(id);
            }
        }

        return ordered;
    };

    const QStringList activeLibraryIds = activeIds(QStringLiteral("libraries"), referencedLibraryIds);
    const QStringList activeDirectChatIds = activeIds(QStringLiteral("chats"), referencedChatIds);

    // Linked Git worktrees of an included Library belong to the Collection
    // through it, without becoming Collection items of their own.
    QStringList worktreeLibraryIds;

    for (const QVariantMap &row : selectIn(
             database,
             QStringLiteral(
                 "SELECT library_worktrees.library_id FROM library_worktrees"
                 " JOIN libraries ON libraries.id = library_worktrees.library_id"
                 " WHERE library_worktrees.anchor_library_id IN (%1)"
                 " AND libraries.archived_at IS NULL"
                 " ORDER BY library_worktrees.created_at ASC"
             ),
             activeLibraryIds
         )) {
        const QString worktreeLibraryId = row.value(QStringLiteral("library_id")).toString();

        if (!activeLibraryIds.contains(worktreeLibraryId)) {
            worktreeLibraryIds.append(worktreeLibraryId);
        }
    }

    worktreeLibraryIds = uniqueIds(worktreeLibraryIds);
    QStringList libraryChatIds;

    for (const QVariantMap &row : selectIn(
             database,
             QStringLiteral(
                 "SELECT id FROM chats WHERE library_id IN (%1) AND type = 'standard'"
                 " AND archived_at IS NULL ORDER BY updated_at DESC, created_at DESC"
             ),
             activeLibraryIds + worktreeLibraryIds
         )) {
        libraryChatIds.append(row.value(QStringLiteral("id")).toString());
    }

    const auto selected = collectionById(database, collectionId);

    if (!selected) {
        throw CoreError(QStringLiteral("Collection not found."));
    }

    const QStringList rosterIds = toStringList(selected->value(QStringLiteral("agentIds")));
    const QStringList activeRosterIds = activeIds(QStringLiteral("agents"), rosterIds);
    const QString defaultAgentId = selected->value(QStringLiteral("defaultAgentId")).toString();

    QVariantMap result;
    result.insert(QStringLiteral("collectionIds"), toVariantList(collectionIds));
    result.insert(QStringLiteral("libraryIds"), toVariantList(activeLibraryIds));
    result.insert(QStringLiteral("worktreeLibraryIds"), toVariantList(worktreeLibraryIds));
    result.insert(QStringLiteral("directChatIds"), toVariantList(activeDirectChatIds));
    result.insert(
        QStringLiteral("chatIds"),
        toVariantList(uniqueIds(activeDirectChatIds + libraryChatIds))
    );
    result.insert(QStringLiteral("agentIds"), toVariantList(activeRosterIds));
    result.insert(
        QStringLiteral("defaultAgentId"),
        nullableString(activeRosterIds.contains(defaultAgentId) ? defaultAgentId : QString())
    );
    return result;
}

QVariantMap createCollection(CoreDatabase &database, const QVariantMap &input)
{
    const QString collectionId = newId();
    const QString name = validatedName(input.value(QStringLiteral("name")));
    const QString parentCollectionId = validatedOptionalId(input.value(QStringLiteral("parentCollectionId")));
    const qint64 position = validatedPosition(input.value(QStringLiteral("position")), 0);
    const QStringList libraryIds = uniqueIds(validatedIdList(input.value(QStringLiteral("libraryIds"))));
    const QStringList chatIds = uniqueIds(validatedIdList(input.value(QStringLiteral("chatIds"))));
    const QStringList agentIds = uniqueIds(validatedIdList(input.value(QStringLiteral("agentIds"))));
    const QString defaultAgentId = validatedOptionalId(input.value(QStringLiteral("defaultAgentId")));

    validateParent(database, {}, parentCollectionId);

    database
        .prepare(QStringLiteral(
            "INSERT INTO collections (id, parent_collection_id, name, position) VALUES (?, ?, ?, ?)"
        ))
        .bind(1, collectionId)
        .bind(2, nullableString(parentCollectionId))
        .bind(3, name)
        .bind(4, position)
        .run();

    replaceItems(database, collectionId, QStringLiteral("library"), libraryIds);
    replaceItems(database, collectionId, QStringLiteral("chat"), chatIds);
    replaceAgents(database, collectionId, agentIds, defaultAgentId);

    return requireMappedCollection(database, collectionId, "new");
}

QVariantMap updateCollection(
    CoreDatabase &database,
    const QString &collectionId,
    const QVariantMap &input
)
{
    if (input.isEmpty()) {
        throw CoreError(QStringLiteral("At least one Collection field must be provided."));
    }

    const auto current = collectionById(database, collectionId);

    if (!current) {
        throw CoreError(QStringLiteral("Collection not found."));
    }

    if (!isNullish(current->value(QStringLiteral("archivedAt")))) {
        throw CoreError(QStringLiteral("This Collection is archived. Restore it before editing it."));
    }

    const QString parentCollectionId = input.contains(QStringLiteral("parentCollectionId"))
        ? validatedOptionalId(input.value(QStringLiteral("parentCollectionId")))
        : current->value(QStringLiteral("parentCollectionId")).toString();
    validateParent(database, collectionId, parentCollectionId);

    const bool hasAgentIds = input.contains(QStringLiteral("agentIds"));
    const bool hasDefaultAgent = input.contains(QStringLiteral("defaultAgentId"));
    const QStringList nextAgentIds = uniqueIds(
        hasAgentIds
            ? validatedIdList(input.value(QStringLiteral("agentIds")))
            : toStringList(current->value(QStringLiteral("agentIds")))
    );
    QString nextDefaultAgentId = hasDefaultAgent
        ? validatedOptionalId(input.value(QStringLiteral("defaultAgentId")))
        : current->value(QStringLiteral("defaultAgentId")).toString();

    if (hasAgentIds && !hasDefaultAgent && !nextDefaultAgentId.isEmpty()
        && !nextAgentIds.contains(nextDefaultAgentId)) {
        nextDefaultAgentId.clear();
    }

    const QString name = input.contains(QStringLiteral("name"))
        ? validatedName(input.value(QStringLiteral("name")))
        : current->value(QStringLiteral("name")).toString();
    const qint64 position = validatedPosition(
        input.value(QStringLiteral("position")),
        current->value(QStringLiteral("position")).toLongLong()
    );

    database
        .prepare(QStringLiteral(
            "UPDATE collections SET parent_collection_id = ?, name = ?, position = ?,"
            " updated_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') WHERE id = ?"
        ))
        .bind(1, nullableString(parentCollectionId))
        .bind(2, name)
        .bind(3, position)
        .bind(4, collectionId)
        .run();

    if (input.contains(QStringLiteral("libraryIds"))) {
        replaceItems(
            database,
            collectionId,
            QStringLiteral("library"),
            validatedIdList(input.value(QStringLiteral("libraryIds")))
        );
    }

    if (input.contains(QStringLiteral("chatIds"))) {
        replaceItems(
            database,
            collectionId,
            QStringLiteral("chat"),
            validatedIdList(input.value(QStringLiteral("chatIds")))
        );
    }

    if (hasAgentIds || hasDefaultAgent) {
        replaceAgents(database, collectionId, nextAgentIds, nextDefaultAgentId);
    }

    return requireMappedCollection(database, collectionId, "updated");
}

QVariantMap archiveCollection(CoreDatabase &database, const QString &collectionId)
{
    const CollectionRow collection = requireActiveCollectionRow(database, collectionId);
    QStringList descendantIds;

    for (const QVariantMap &row : database
             .prepare(QStringLiteral(
                 "WITH RECURSIVE descendants(id) AS ("
                 "  SELECT id FROM collections WHERE id = ?"
                 "  UNION ALL"
                 "  SELECT collections.id FROM collections"
                 "  JOIN descendants ON collections.parent_collection_id = descendants.id"
                 ") SELECT id FROM descendants"
             ))
             .bind(1, collectionId)
             .all()) {
        descendantIds.append(row.value(QStringLiteral("id")).toString());
    }

    auto archive = database.prepare(QStringLiteral(
        "UPDATE collections SET archived_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now'),"
        " updated_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') WHERE id IN (%1)"
    ).arg(placeholders(descendantIds.size())));
    archive.bindAll(toVariantList(descendantIds)).run();

    const QString selected = selectedCollectionId(database);

    if (!selected.isEmpty() && descendantIds.contains(selected)) {
        database.exec(QStringLiteral(
            "UPDATE app_settings SET selected_collection_id = NULL,"
            " updated_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') WHERE id = 1"
        ));
    }

    QVariantMap result;
    result.insert(QStringLiteral("collection"), requireMappedCollection(database, collection.id, "archived"));
    result.insert(QStringLiteral("selectedCollectionId"), nullableString(selectedCollectionId(database)));
    return result;
}

QVariantMap restoreCollection(CoreDatabase &database, const QString &collectionId)
{
    const CollectionRow collection = requireCollectionRow(database, collectionId);

    if (isNullish(collection.archivedAt)) {
        throw CoreError(QStringLiteral("Collection is not archived."));
    }

    if (!collection.parentCollectionId.isEmpty()) {
        const CollectionRow parent = requireCollectionRow(database, collection.parentCollectionId);

        if (!isNullish(parent.archivedAt)) {
            throw CoreError(QStringLiteral("Restore the parent Collection before restoring this Collection."));
        }
    }

    database
        .prepare(QStringLiteral(
            "UPDATE collections SET archived_at = NULL,"
            " updated_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') WHERE id = ?"
        ))
        .bind(1, collectionId)
        .run();

    return requireMappedCollection(database, collectionId, "restored");
}
}

#include "app_state_repository.h"

#include "Core/Domains/Collections/collection_repository.h"
#include "Core/Support/core_values.h"
#include "Core/core_error.h"

using namespace CoreValues;

namespace AppStateRepository
{
QVariantMap appState(CoreDatabase &database)
{
    const auto row = database
        .prepare(QStringLiteral(
            "SELECT selected_collection_id, selected_library_id, selected_chat_id"
            " FROM app_settings WHERE id = 1"
        ))
        .one();

    if (!row) {
        throw CoreError(QStringLiteral("Archivist app settings could not be loaded."));
    }

    QVariantMap result;
    result.insert(QStringLiteral("selectedCollectionId"), nullable(row->value(QStringLiteral("selected_collection_id"))));
    result.insert(QStringLiteral("selectedLibraryId"), nullable(row->value(QStringLiteral("selected_library_id"))));
    result.insert(QStringLiteral("selectedChatId"), nullable(row->value(QStringLiteral("selected_chat_id"))));
    return result;
}

QVariantMap setSelectedCollection(CoreDatabase &database, const QString &collectionId)
{
    if (collectionId.isEmpty()) {
        database.exec(QStringLiteral(
            "UPDATE app_settings SET selected_collection_id = NULL,"
            " updated_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') WHERE id = 1"
        ));
        return appState(database);
    }

    if (!isUuid(collectionId)) {
        throw CoreError(QStringLiteral("Invalid selected Collection."));
    }

    const QVariantMap current = appState(database);
    const QVariantMap scope = CollectionRepository::scope(database, collectionId);
    const QStringList libraryIds = toStringList(scope.value(QStringLiteral("libraryIds")));
    const QStringList chatIds = toStringList(scope.value(QStringLiteral("chatIds")));
    const QStringList directChatIds = toStringList(scope.value(QStringLiteral("directChatIds")));
    const QString currentLibraryId = current.value(QStringLiteral("selectedLibraryId")).toString();
    const QString currentChatId = current.value(QStringLiteral("selectedChatId")).toString();

    const QString selectedLibraryId = !currentLibraryId.isEmpty() && libraryIds.contains(currentLibraryId)
        ? currentLibraryId
        : libraryIds.value(0);
    const QString selectedChatId = !currentChatId.isEmpty() && chatIds.contains(currentChatId)
        ? currentChatId
        : (!directChatIds.isEmpty() ? directChatIds.first() : chatIds.value(0));

    database
        .prepare(QStringLiteral(
            "UPDATE app_settings SET selected_collection_id = ?, selected_library_id = ?,"
            " selected_chat_id = ?, updated_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') WHERE id = 1"
        ))
        .bind(1, collectionId)
        .bind(2, nullableString(selectedLibraryId))
        .bind(3, nullableString(selectedChatId))
        .run();

    return appState(database);
}

QVariantMap setSelectedLibrary(CoreDatabase &database, const QString &libraryId)
{
    if (!libraryId.isEmpty()) {
        if (!isUuid(libraryId)) {
            throw CoreError(QStringLiteral("Invalid selected Library."));
        }

        const bool exists = database
            .prepare(QStringLiteral("SELECT EXISTS(SELECT 1 FROM libraries WHERE id = ?)"))
            .bind(1, libraryId)
            .scalar()
            .toInt() == 1;

        if (!exists) {
            throw CoreError(QStringLiteral("Library not found."));
        }
    }

    database
        .prepare(QStringLiteral(
            "UPDATE app_settings SET selected_library_id = ?,"
            " updated_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') WHERE id = 1"
        ))
        .bind(1, nullableString(libraryId))
        .run();

    return appState(database);
}
}

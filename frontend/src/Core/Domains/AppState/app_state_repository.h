#pragma once

#include "Core/Database/core_database.h"

#include <QVariantMap>

// Native port of backend/src/api/appState/models/AppState.ts.
// Results are {selectedCollectionId, selectedLibraryId, selectedChatId}.
namespace AppStateRepository
{
[[nodiscard]] QVariantMap appState(CoreDatabase &database);
QVariantMap setSelectedCollection(CoreDatabase &database, const QString &collectionId);
QVariantMap setSelectedLibrary(CoreDatabase &database, const QString &libraryId);
}

#pragma once

#include "Core/Database/core_database.h"

#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <optional>

// Native port of backend/src/api/libraries/models/Library.ts (the catalog:
// list, create, rename, archive, restore). File scanning, reading and
// indexing still run in the Node backend.
// Libraries are {id, name, description, rootPath, archivedAt, createdAt, updatedAt,
// worktreeOfLibraryId, worktreeRepositoryKey}; the last two are set only for a
// linked Git worktree that Archivist opened from another Library.
namespace LibraryRepository
{
[[nodiscard]] QVariantList activeLibraries(CoreDatabase &database);
[[nodiscard]] QVariantList archivedLibraries(CoreDatabase &database);
[[nodiscard]] std::optional<QVariantMap> libraryById(CoreDatabase &database, const QString &libraryId);

// input: rootPath, optional name and description. Returns
// {library, selectedLibraryId}. A folder that is already a Library throws a
// CoreError whose details are {code: "LIBRARY_EXISTS" | "LIBRARY_ARCHIVED", libraryId}.
QVariantMap createLibrary(CoreDatabase &database, const QVariantMap &input);
// input: name and/or description (null clears the description).
QVariantMap updateLibrary(CoreDatabase &database, const QString &libraryId, const QVariantMap &input);
// Returns {library, selectedLibraryId}.
QVariantMap archiveLibrary(CoreDatabase &database, const QString &libraryId);
QVariantMap restoreLibrary(CoreDatabase &database, const QString &libraryId);

// The real, absolute folder path the way Node's normalizeRootPath produced it.
// Throws CoreError when the folder is missing, not a folder, or unreadable.
[[nodiscard]] QString normalizeRootPath(const QString &rootPath);
}

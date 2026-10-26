#pragma once

#include "Core/Database/core_database.h"

#include <QString>
#include <QVariantMap>

// Native port of the file-changing services in backend/src/api/libraries:
// LibraryFileWriter.ts, LibraryFileMover.ts and LibraryEntryOperations.ts.
// They touch the disk, so callers run them through CoreRuntime::run().
namespace LibraryFileOperations
{
// Saves text atomically when the file still has `expectedModifiedAt`.
// Returns a preview: {file, content, encoding, lineCount, readAt}.
QVariantMap writeText(
    CoreDatabase &database,
    const QString &libraryId,
    const QString &fileId,
    const QString &content,
    const QString &expectedModifiedAt
);

// Returns the moved file.
QVariantMap moveFile(CoreDatabase &database, const QString &libraryId, const QString &fileId, const QString &targetDirectory);

// Returns {kind, relativePath, name, file} (file is null for folders).
QVariantMap createEntry(
    CoreDatabase &database,
    const QString &libraryId,
    const QString &parentDirectory,
    const QString &name,
    bool directory
);

QVariantMap renameFile(CoreDatabase &database, const QString &libraryId, const QString &fileId, const QString &name);
QVariantMap duplicateFile(CoreDatabase &database, const QString &libraryId, const QString &fileId, const QString &name);

// Shows the entry in Finder (or the platform file manager).
void revealEntry(CoreDatabase &database, const QString &libraryId, const QString &relativePath);
}

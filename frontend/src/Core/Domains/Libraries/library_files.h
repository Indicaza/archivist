#pragma once

#include "Core/Database/core_database.h"

#include <QString>
#include <QStringList>
#include <QVariantMap>

// Native port of the filesystem half of backend/src/api/libraries: the file
// reader, directory catalog, scanner, text indexer and Git status.
//
// These touch the disk, so callers run them through CoreRuntime::run() (a
// background thread). Database changes happen in short transactions inside.
namespace LibraryFiles
{
// {file, content, encoding: "utf-8", lineCount, readAt}
[[nodiscard]] QVariantMap readText(CoreDatabase &database, const QString &libraryId, const QString &fileId);

// Relative folder paths under the Library root, sorted case-insensitively.
[[nodiscard]] QStringList directories(CoreDatabase &database, const QString &libraryId);

// {files, latestScan, directories}
[[nodiscard]] QVariantMap catalogWithDirectories(CoreDatabase &database, const QString &libraryId);

// Scans the Library folder, updates the catalog and the text index.
// Returns {files, latestScan, scan, issues, index}.
QVariantMap scan(CoreDatabase &database, const QString &libraryId);

// Re-indexes the supported text files in the current catalog.
QVariantMap indexCatalog(CoreDatabase &database, const QString &libraryId, const QVariantList &files);

// {repository, repositoryRoot, branch, detached, upstream, ahead, behind,
//  dirty, entries, counts}
[[nodiscard]] QVariantMap gitStatus(CoreDatabase &database, const QString &libraryId);
}

#pragma once

#include "Core/Database/core_database.h"

#include <QString>

// Native port of backend/src/api/libraries/services/LibraryEditInspection.ts.
// Each check throws CoreError with the Node message when the target is unsafe.
namespace LibraryEditInspection
{
struct CreateTarget
{
    QString relativePath;
    QString parentRelativePath;
    QString name;
    QString expectedState; // "absent"
};

struct TextFile
{
    QString relativePath;
    QString content;
    QString sha256;
    qint64 sizeBytes = 0;
    QString modifiedAt;
};

struct EmptyDirectory
{
    QString relativePath;
    QString name;
    QString expectedState; // "empty-directory"
};

// "AI edit paths must be safe Library-relative paths." when unsafe.
[[nodiscard]] QString normalizeRelativePath(const QString &value, const QString &message);
// path.posix.isAbsolute || path.win32.isAbsolute
[[nodiscard]] bool isAbsoluteLike(const QString &value);
[[nodiscard]] bool isInsideRoot(const QString &rootPath, const QString &candidatePath);
[[nodiscard]] QString canonicalLibraryRoot(CoreDatabase &database, const QString &libraryId);

[[nodiscard]] CreateTarget createTarget(CoreDatabase &database, const QString &libraryId, const QString &relativePath);
[[nodiscard]] CreateTarget directoryCreateTarget(CoreDatabase &database, const QString &libraryId, const QString &relativePath);
[[nodiscard]] EmptyDirectory emptyDirectory(CoreDatabase &database, const QString &libraryId, const QString &relativePath);
[[nodiscard]] TextFile textFile(CoreDatabase &database, const QString &libraryId, const QString &relativePath);
}

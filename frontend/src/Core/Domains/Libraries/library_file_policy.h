#pragma once

#include <QString>

// Native copy of backend/src/api/libraries/services/LibraryFilePolicy.ts plus
// the ignore lists the scanner and directory catalog share.
namespace LibraryFilePolicy
{
constexpr qint64 maxTextPreviewBytes = 512 * 1024;

[[nodiscard]] bool isCatalogableFile(const QString &fileName);
[[nodiscard]] bool isManagedPhotoLibraryDirectory(const QString &directoryName);
[[nodiscard]] bool isCatalogableDirectory(const QString &directoryName);
[[nodiscard]] bool isSupportedTextFile(const QString &fileName, const QString &extension);

// Directory names the scanner skips (compared lowercased). The directory
// catalog also skips .archivist-trash.
[[nodiscard]] bool isIgnoredScanDirectory(const QString &directoryName);
[[nodiscard]] bool isIgnoredCatalogDirectory(const QString &directoryName);
// File names the scanner skips (exact match).
[[nodiscard]] bool isIgnoredFile(const QString &fileName);

// path.extname(name).toLowerCase()
[[nodiscard]] QString extensionOf(const QString &fileName);
}

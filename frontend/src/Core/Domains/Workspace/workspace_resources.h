#pragma once

#include "Core/Domains/Workspace/workspace_types.h"

#include <QString>
#include <QVariantMap>

// Building resource identities outside the docking UI (agent tools, tests).
//
// The QML workspace builds the same shapes for files picked in the Explorer
// (DockWorkspace.documentResource), so a file an agent opens and the same
// file opened by hand are one resource and share one view and one buffer.
namespace Workspace::Resources
{
struct FileView
{
    QString typeId;       // archivist.editor or archivist.preview
    bool textual = false; // the preview shows the document buffer
};

// Which view a file opens in by default: code and data in the editor,
// Markdown, text, images and documents in the preview.
[[nodiscard]] FileView viewForFile(const QString &name, const QString &extension, const QString &mimeType = {});

// `file` is a Library catalog row: {id, name, relativePath, extension,
// mimeType, languageId, sizeBytes, ...}. The id is
// collectionId:libraryId:fileId, as everywhere else.
[[nodiscard]] ResourceRef document(const QString &collectionId, const QString &libraryId, const QVariantMap &file);
[[nodiscard]] ResourceRef terminal(const QString &collectionId, const QString &libraryId, const QString &sessionId);
[[nodiscard]] ResourceRef chat(const QString &chatId);
} // namespace Workspace::Resources

#pragma once

#include "Core/Domains/Workspace/workspace_model.h"

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// The workspace as a set of named commands, for callers that are not the
// docking UI: a future agent tool, a command palette, scripted tests.
//
// A command is a map: {"command": "anchor.rename", "anchorId": "...", ...}.
// Every command runs through the same WorkspaceModel operations the UI uses,
// so layout and content locks apply exactly as they do to a person, given
// the caller's Origin. Nothing here duplicates a rule.
//
// Commands:
//
//   anchor.list                                    -> {anchors: [description]}
//   anchor.describe        anchorId                -> description
//   anchor.rename          anchorId, name          ("" resets to automatic)
//   anchor.setLayoutLock   anchorId, locked
//   anchor.setContentLock  anchorId, locked
//   anchor.setSwitchable   anchorId, switchable
//   anchor.setWorktree     anchorId, libraryId     ("" clears)
//   anchor.addResource     anchorId, typeId, resource, title, activate
//   anchor.removeResource  viewId
//   anchor.selectResource  viewId
//   anchor.reorderResource viewId, index
//   view.open              typeId, resource, title, anchorId (optional)
//   view.move              viewId, anchorId, index
//
// A description is {anchorId, name, displayName, automaticName, pinned,
// layoutLocked, contentLocked, switchable, libraryId, surfaceId,
// activeViewId, focused, target, views: [...], capabilities: {...},
// restrictions: {capability: reason}}.
//
// `resource` is {kind, id, collectionId, libraryId, details}.
namespace Workspace::Commands
{
struct Result
{
    OpResult op;
    QVariantMap data; // command-specific payload (descriptions, ids)
};

[[nodiscard]] QStringList names();
// Commands that only read.
[[nodiscard]] bool isQuery(const QString &name);
Result execute(WorkspaceModel &model, const QVariantMap &command, Origin origin);

[[nodiscard]] QVariantMap describeAnchor(const WorkspaceModel &model, const QString &anchorId, Origin origin);
[[nodiscard]] QVariantMap capabilitiesMap(const AnchorCapabilities &capabilities);
[[nodiscard]] ResourceRef resourceFromMap(const QVariantMap &map);
[[nodiscard]] QVariantMap resourceToMap(const ResourceRef &resource);
[[nodiscard]] QVariantMap resultMap(const Result &result);
} // namespace Workspace::Commands

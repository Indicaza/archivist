#pragma once

#include "Core/Domains/Workspace/view_type_registry.h"
#include "Core/Domains/Workspace/workspace_model.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

// Saving and loading the workspace.
//
// Two documents, kept apart on purpose:
//
//   layout   panel structure: windows, the split tree, sizes, which anchors
//            exist (pinned or not) and what each prefers to hold. Named
//            layouts and presets are layouts. Applying one never opens or
//            closes content.
//   session  what is open: views, their resources (and worktrees), view
//            state, and which anchor each view sits in.
//
// A workspace snapshot (what is written for restart and crash recovery) is
// one layout plus one session. Loading is forgiving: views pointing at
// anchors that no longer exist land in the target anchor, unknown view types
// are kept so a missing plugin never costs the user their tabs, and a
// snapshot from a newer Archivist is refused rather than half-read.
namespace Workspace::Codec
{
inline constexpr int kSnapshotVersion = 1;
inline constexpr int kLayoutVersion = 1;

inline const QString SnapshotFormat = QStringLiteral("archivist.workspace");
inline const QString LayoutFormat = QStringLiteral("archivist.layout");

struct LoadReport
{
    bool ok = false;
    QString error;        // set when ok is false
    QStringList warnings; // recovered problems (relocated or unknown views)
    int migratedViews = 0;
};

[[nodiscard]] QJsonObject encodeSnapshot(const WorkspaceModel &model);
// Replaces `model` only when the snapshot is usable.
LoadReport decodeSnapshot(const QJsonObject &snapshot, const ViewTypeRegistry &registry, WorkspaceModel &model);

// The current arrangement as a reusable layout. Each anchor remembers the
// view types and resources it held, so switching back puts things where
// they were.
[[nodiscard]] QJsonObject captureLayout(const WorkspaceModel &model, const QString &name);
// Rebuilds windows and anchors from `layout` and re-homes every existing
// view into them. Views, resources and view state are untouched; nothing is
// opened or closed.
LoadReport applyLayout(const QJsonObject &layout, WorkspaceModel &model);
[[nodiscard]] bool isLayout(const QJsonObject &object);

[[nodiscard]] QJsonObject encodeResource(const ResourceRef &resource);
[[nodiscard]] ResourceRef decodeResource(const QJsonObject &object);
[[nodiscard]] QJsonObject encodeView(const ViewRecord &view);
[[nodiscard]] ViewRecord decodeView(const QJsonObject &object);
} // namespace Workspace::Codec

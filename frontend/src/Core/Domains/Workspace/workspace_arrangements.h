#pragma once

#include "Core/Domains/Workspace/workspace_model.h"

#include <QHash>
#include <QSet>
#include <QSizeF>
#include <QString>
#include <QStringList>

// Putting an arrangement back.
//
// An arrangement is the model's data at one moment (WorkspaceModel::Data).
// Restoring one rebuilds its windows, split tree, sizes and panel contents,
// while keeping everything that has to survive since then:
//
//   - every view that still exists, with its current state and title (a
//     view opened since lands in its current panel if that panel is part of
//     the arrangement, else in the arrangement's target panel);
//   - views closed since stay closed;
//   - panel configuration the person changed since (names, locks, shelves,
//     worktree bindings) and where floating windows are on screen;
//   - keyboard focus, unless the focused panel is gone.
//
// Only views the caller lists in `closable` go away: the caller decides that
// they were opened for the arrangement and hold nothing unsaved. Views listed
// in `reopen` that the arrangement shows but are closed now come back as
// they were (same id and resource): redo uses this for views an undo closed.
// Resources (buffers, chats, terminal sessions) are never touched here.
//
// A panel whose layout is locked now must end up exactly where it is now;
// otherwise the restore is refused and nothing changes.
namespace Workspace::Arrangements
{
struct Result
{
    bool ok = false;
    QString code;
    QString message;
    QStringList closedViews;   // views removed (from `closable`)
    QStringList reopenedViews; // views brought back (from `reopen`)
};

Result restore(
    WorkspaceModel &model,
    const WorkspaceModel::Data &arrangement,
    const QSet<QString> &closable,
    const QHash<QString, QSizeF> &surfaceSizes = {},
    const LayoutMetrics &metrics = {},
    const QSet<QString> &reopen = {}
);
} // namespace Workspace::Arrangements

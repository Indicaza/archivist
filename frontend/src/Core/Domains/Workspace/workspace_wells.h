#pragma once

#include "Core/Domains/Workspace/workspace_model.h"

#include <QString>
#include <QVariantMap>

// Gravity Wells: putting work away and getting it back.
//
// The Well records live in the model (WorkspaceModel::wells); this is the
// planning on top of them. Minimizing remembers where a view was the way a
// Magnet pickup does (Magnets::placeOf) and restoring puts it back the way
// Return to Previous Position does (Magnets::planReturn), with a predictable
// fallback when that place is gone. Every function here commits all of its
// change or none of it.
namespace Workspace::Wells
{
// What a Well entry is right now, which decides what a click does.
enum class EntryState
{
    Minimized,        // put away (pinned or not): click restores it
    PinnedOpen,       // pinned and shown in a panel: click brings it forward
    PinnedClosed,     // pinned, nothing shows it: click opens it
};

[[nodiscard]] QString entryStateName(EntryState state);
[[nodiscard]] EntryState entryState(const WorkspaceModel &model, const WellEntry &entry);
// The views of the entry's resource that are in panels, tree order.
[[nodiscard]] QStringList openViews(const WorkspaceModel &model, const WellEntry &entry);

// Puts a view away in `wellId` (empty: the main Well).
OpResult minimize(WorkspaceModel &model, const QString &viewId, const QString &wellId = {}, Origin origin = Origin::User);

// How a restore went.
struct Restored
{
    OpResult result;
    QString viewId;
    QString anchorId;
    // original: its old panel and tab position; returned: its panel was gone,
    // so it came back where that panel sat (beside a neighbour, along the
    // window edge, or as its own floating panel or window); fallback: that
    // place is gone too, so it joined a panel showing the same kind of
    // content, or the panel new content goes to.
    QString how;
};

// Puts a minimized view back.
Restored restore(WorkspaceModel &model, const QString &viewId);

// What clicking an entry does, by its state: restore its most recently
// minimized view, bring forward the panel showing it, or open it again (as
// a view of the type it was pinned with, in the panel new content goes to).
Restored activate(WorkspaceModel &model, const QString &entryId);

// The panel a view lands in when its own place is gone: the first panel in
// the main window whose active view has the same type, else the panel new
// content goes to.
[[nodiscard]] QString fallbackAnchor(const WorkspaceModel &model, const QString &typeId);

// Compact maps for QML.
[[nodiscard]] QVariantList wellsList(const WorkspaceModel &model);
} // namespace Workspace::Wells

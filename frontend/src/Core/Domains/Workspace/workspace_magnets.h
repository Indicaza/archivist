#pragma once

#include "Core/Domains/Workspace/workspace_model.h"

#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QVariantMap>

// Magnets: picking up a view (or a whole anchor) and putting it somewhere.
//
// A magnet is the handle a person drags; it is never a copy of the view or
// its resource. Everything here is read-only over the model: a drag's
// preview is worked out on a copy, so nothing is committed, recorded or
// saved until the drop. The workspace store runs the drag and owns the one
// commit; this file decides what a drop would do, whether it's acceptable,
// and where a magnet came from so it can go back.
namespace Workspace::Magnets
{
// Where a drag is, for the visual layer (and the physics that will drive
// attraction and settling from it).
enum class Phase
{
    Idle,     // no drag
    Pickup,   // picked up, the pointer hasn't found anything yet
    Free,     // over no dock target: a drop floats it as a panel
    Aiming,   // held at a dock zone, not yet long enough to dock there
    Invalid,  // over a target that refuses it (see the reason)
    Preview,  // over a valid target: the proposed layout is showing
    Detached, // outside every window: a drop opens a window of its own
};

[[nodiscard]] QString phaseName(Phase phase);

// What a drop onto `target` would do, judged against the committed model.
struct Preview
{
    OpResult result;            // ok, or why not (code + message)
    LayoutGeometry geometry;    // the target window as it would be
    QRectF landingRect;         // where the magnet lands in it
    QString landingAnchorId;    // the anchor it lands in
};

// Validates with canDrop, then applies the drop to a copy and checks the
// result: refused with NO_CHANGE when nothing would move, and TOO_SMALL
// when a panel would end up under the minimum size (unless the window is
// already that cramped).
[[nodiscard]] Preview preview(
    const WorkspaceModel &model,
    const DragSource &source,
    const DropTarget &target,
    const QSizeF &surfaceSize,
    const LayoutMetrics &metrics
);

// Docking is opt-in: a drag only docks where the person is clearly aiming.
// Inside a panel that is a band along each of its edges (split there), and
// a thin band along the window border (dock along the whole side); an
// empty panel takes a drop anywhere. The rest of a panel is open space: a
// drop there floats. Tab strips (merge) are found by the caller.
struct DockZones
{
    double windowBand = 18;   // along the window border
    double edgeShare = 0.18;  // of the panel's width or height...
    double edgeMinimum = 32;  // ...but never thinner than this
    double edgeMaximum = 72;  // ...or thicker than this
    // Once a zone is chosen it holds until the pointer is this far beyond
    // it, so two zones that meet never flicker between each other.
    double hold = 14;
};

// `held` is the zone already chosen (aimed at or previewed), if any: it
// stays chosen while the pointer is within `hold` of it.
[[nodiscard]] DropTarget dockTargetAt(
    const WorkspaceModel &model,
    const QString &surfaceId,
    const QPointF &point,
    const QSizeF &surfaceSize,
    const LayoutMetrics &metrics,
    const DockZones &zones = {},
    const DropTarget &held = {}
);

// The tab strip under a point, to join that panel: `localX` is from the
// panel's left edge. A strip takes the pointer exactly where it is drawn
// (the top `stripHeight` of a panel). Once chosen (`held` is a tab drop on
// it) it keeps the pointer up to `zones.hold` beyond, so drifting onto the
// divider or the panel above doesn't drop the join; a strip drawn under
// the pointer still wins at once. A split or window edge already chosen
// keeps the pointer over a strip close by in the same way.
struct StripHit
{
    QString anchorId;
    double localX = 0;
};
[[nodiscard]] StripHit tabStripAt(
    const WorkspaceModel &model,
    const QString &surfaceId,
    const QPointF &point,
    const QSizeF &surfaceSize,
    const LayoutMetrics &metrics,
    double stripHeight,
    const DockZones &zones = {},
    const DropTarget &held = {}
);

// Whether `source` can float as a panel inside the main window (judged on a
// copy: locks, and nothing that would leave the main window without a
// panel).
[[nodiscard]] OpResult canFloat(const WorkspaceModel &model, const DragSource &source);

// Where a magnet sits now, recorded at pickup so it can be put back.
struct Place
{
    DragSource source;
    QString surfaceId;
    QString anchorId;     // views: the anchor it was a tab of
    int index = -1;       // views: its tab position
    // Where its anchor sat in the split tree, as a drop target that
    // recreates it: beside a neighbour, or along the window's edge.
    DropTarget beside;
    // Its share of that split (0 when it filled the window), so it comes
    // back the same size.
    double share = 0;
    Orientation splitOrientation = Orientation::Horizontal;
    // The anchor was alone in a floating window: going back reopens one.
    bool aloneInFloatingWindow = false;
    bool inMainWindow = false; // that floating surface was a panel in the main window
    QString sidebarSlot;       // ...or docked in the sidebar
    QRectF windowGeometry;
    QString screenName;

    [[nodiscard]] bool isValid() const { return source.kind != DragSource::Kind::None && !surfaceId.isEmpty(); }
};

[[nodiscard]] Place placeOf(const WorkspaceModel &model, const DragSource &source);

// How to put a magnet back at `place`: a drop, or a new floating window.
struct ReturnPlan
{
    OpResult result;
    DropTarget target;
    bool detach = false;
    bool inMainWindow = false;
    QString sidebarSlot; // detach into this sidebar slot
    QRectF geometry;
    QString screenName;
};

[[nodiscard]] ReturnPlan planReturn(const WorkspaceModel &model, const Place &place);

// After a return that recreated the anchor beside a neighbour: gives it
// back its old share of the split it landed in (when that split runs the
// same way). Returns whether sizes changed.
bool restoreShare(WorkspaceModel &model, const QString &anchorId, const Place &place);

// Compact maps for QML.
[[nodiscard]] QVariantMap rectMap(const QRectF &rect);
[[nodiscard]] QVariantMap targetMap(const DropTarget &target);
} // namespace Workspace::Magnets

#include "Core/Domains/Workspace/workspace_magnets.h"

#include <algorithm>
#include <cmath>

namespace Workspace::Magnets
{
namespace
{
// Windows other than the target are compared at a typical size: only
// whether they changed matters, not their exact geometry.
const QSizeF kTypicalSurface(1440, 900);

bool sameRect(const QRectF &a, const QRectF &b)
{
    return std::abs(a.x() - b.x()) < 0.5 && std::abs(a.y() - b.y()) < 0.5
        && std::abs(a.width() - b.width()) < 0.5 && std::abs(a.height() - b.height()) < 0.5;
}

// Same windows, same panels in the same places, same tabs in the same order.
bool sameArrangement(const WorkspaceModel &a, const WorkspaceModel &b, const QString &targetSurfaceId,
                     const QSizeF &targetSize, const LayoutMetrics &metrics)
{
    if (a.surfaceIds() != b.surfaceIds()) {
        return false;
    }
    for (const QString &surfaceId : a.surfaceIds()) {
        const QStringList anchors = a.anchorIds(surfaceId);
        if (anchors != b.anchorIds(surfaceId)) {
            return false;
        }
        for (const QString &anchorId : anchors) {
            if (a.anchor(anchorId)->views != b.anchor(anchorId)->views) {
                return false;
            }
        }
        const QSizeF size = surfaceId == targetSurfaceId ? targetSize : kTypicalSurface;
        const LayoutGeometry before = a.computeGeometry(surfaceId, size, metrics);
        const LayoutGeometry after = b.computeGeometry(surfaceId, size, metrics);
        for (auto it = before.anchors.cbegin(); it != before.anchors.cend(); ++it) {
            if (!sameRect(it.value(), after.anchors.value(it.key()))) {
                return false;
            }
        }
    }
    return true;
}

bool cramped(const LayoutGeometry &geometry, const LayoutMetrics &metrics)
{
    for (const QRectF &rect : geometry.anchors) {
        if (rect.width() < metrics.minimumAnchorWidth - 0.5 || rect.height() < metrics.minimumAnchorHeight - 0.5) {
            return true;
        }
    }
    return false;
}

QString edgeLeaf(const WorkspaceModel &model, const QString &nodeId, bool last)
{
    const SplitNode *node = model.node(nodeId);
    while (node && !node->leaf && !node->children.isEmpty()) {
        node = model.node(last ? node->children.constLast() : node->children.constFirst());
    }
    return node && node->leaf ? node->anchorId : QString();
}

// A drop target that would put `anchorId` back where it is now.
DropTarget besideOf(const WorkspaceModel &model, const QString &anchorId, const QString &surfaceId)
{
    DropTarget target;
    target.surfaceId = surfaceId;
    const SplitNode *leaf = model.node(model.leafOfAnchor(anchorId));
    if (!leaf) {
        return {};
    }
    if (leaf->parentId.isEmpty()) {
        // It filled the window.
        target.edge = Edge::Left;
        return target;
    }

    const SplitNode *parent = model.node(leaf->parentId);
    const qsizetype index = parent->children.indexOf(leaf->id);
    const bool horizontal = parent->orientation == Orientation::Horizontal;
    const Edge leading = horizontal ? Edge::Left : Edge::Top;
    const Edge trailing = horizontal ? Edge::Right : Edge::Bottom;

    // A child of the window's own split: along the window's edge spans it
    // exactly as before.
    if (parent->parentId.isEmpty() && (index == 0 || index == parent->children.size() - 1)) {
        target.edge = index == 0 ? leading : trailing;
        return target;
    }

    if (index > 0) {
        target.anchorId = edgeLeaf(model, parent->children.at(index - 1), true);
        target.edge = trailing;
    } else {
        target.anchorId = edgeLeaf(model, parent->children.at(index + 1), false);
        target.edge = leading;
    }
    return target;
}

bool sameTarget(const DropTarget &a, const DropTarget &b)
{
    return a.surfaceId == b.surfaceId && a.anchorId == b.anchorId && a.edge == b.edge;
}

bool sourceExists(const WorkspaceModel &model, const DragSource &source)
{
    return source.kind == DragSource::Kind::View ? model.view(source.id) != nullptr
        : source.kind == DragSource::Kind::Anchor ? model.anchor(source.id) != nullptr
        : false;
}
} // namespace

QString phaseName(Phase phase)
{
    switch (phase) {
    case Phase::Idle: return QStringLiteral("idle");
    case Phase::Pickup: return QStringLiteral("pickup");
    case Phase::Free: return QStringLiteral("free");
    case Phase::Aiming: return QStringLiteral("aiming");
    case Phase::Invalid: return QStringLiteral("invalid");
    case Phase::Preview: return QStringLiteral("preview");
    case Phase::Detached: return QStringLiteral("detached");
    }
    return QStringLiteral("idle");
}

Preview preview(const WorkspaceModel &model, const DragSource &source, const DropTarget &target,
                const QSizeF &surfaceSize, const LayoutMetrics &metrics)
{
    Preview out;
    out.result = model.canDrop(source, target);
    if (!out.result.ok) {
        return out;
    }

    WorkspaceModel after = model;
    const OpResult applied = after.applyDrop(source, target);
    if (!applied.ok) {
        out.result = applied;
        return out;
    }
    if (sameArrangement(model, after, target.surfaceId, surfaceSize, metrics)) {
        out.result = OpResult::failure(QStringLiteral("NO_CHANGE"), QStringLiteral("It's already there."));
        return out;
    }

    const LayoutGeometry geometry = after.computeGeometry(target.surfaceId, surfaceSize, metrics);
    // Joining a panel's tabs: the panels don't move while aiming (the strip
    // stays under the pointer); the layout settles once it is dropped.
    const bool joins = target.edge == Edge::Center && !target.anchorId.isEmpty();
    // A window that is already cramped can't get "worse" by this measure;
    // otherwise nothing may end up under the minimum.
    if (cramped(geometry, metrics) && !cramped(model.computeGeometry(target.surfaceId, surfaceSize, metrics), metrics)) {
        out.result = OpResult::failure(
            QStringLiteral("TOO_SMALL"),
            QStringLiteral("Not enough room: a panel would be smaller than %1 × %2.")
                .arg(qRound(metrics.minimumAnchorWidth))
                .arg(qRound(metrics.minimumAnchorHeight)));
        return out;
    }

    out.geometry = joins ? model.computeGeometry(target.surfaceId, surfaceSize, metrics) : geometry;
    out.landingAnchorId = source.kind == DragSource::Kind::View
        ? after.anchorOfView(source.id)
        : (target.edge == Edge::Center && !target.anchorId.isEmpty() ? target.anchorId : source.id);
    out.landingRect = out.geometry.anchors.value(out.landingAnchorId);
    return out;
}

namespace
{
// Whether `point` is still within `held`'s zone, grown by zones.hold.
bool stillHeld(const WorkspaceModel &model, const QString &surfaceId, const QPointF &point, const QSizeF &surfaceSize,
               const LayoutMetrics &metrics, const DockZones &zones, const DropTarget &held)
{
    if (!held.isValid() || held.surfaceId != surfaceId) {
        return false;
    }
    if (held.anchorId.isEmpty()) {
        // Along the window border.
        if (model.anchorIds(surfaceId).size() <= 1) {
            return false;
        }
        const double reach = zones.windowBand + zones.hold;
        switch (held.edge) {
        case Edge::Left: return point.x() <= reach;
        case Edge::Right: return surfaceSize.width() - point.x() <= reach;
        case Edge::Top: return point.y() <= reach;
        case Edge::Bottom: return surfaceSize.height() - point.y() <= reach;
        default: return false;
        }
    }
    const AnchorRecord *anchor = model.anchor(held.anchorId);
    if (!anchor || model.surfaceOfAnchor(held.anchorId) != surfaceId) {
        return false;
    }
    const QRectF rect = model.computeGeometry(surfaceId, surfaceSize, metrics).anchors.value(held.anchorId);
    const QRectF grown = rect.adjusted(-zones.hold, -zones.hold, zones.hold, zones.hold);
    if (rect.isEmpty() || !grown.contains(point)) {
        return false;
    }
    if (held.edge == Edge::Center) {
        return anchor->views.isEmpty();
    }
    const auto band = [&zones](double length) {
        return std::clamp(length * zones.edgeShare, zones.edgeMinimum, std::max(zones.edgeMinimum, zones.edgeMaximum));
    };
    const double reachX = std::min(band(rect.width()), rect.width() / 2) + zones.hold;
    const double reachY = std::min(band(rect.height()), rect.height() / 2) + zones.hold;
    switch (held.edge) {
    case Edge::Left: return point.x() - rect.left() <= reachX;
    case Edge::Right: return rect.right() - point.x() <= reachX;
    case Edge::Top: return point.y() - rect.top() <= reachY;
    case Edge::Bottom: return rect.bottom() - point.y() <= reachY;
    default: return false;
    }
}
} // namespace

StripHit tabStripAt(const WorkspaceModel &model, const QString &surfaceId, const QPointF &point,
                    const QSizeF &surfaceSize, const LayoutMetrics &metrics, double stripHeight,
                    const DockZones &zones, const DropTarget &held)
{
    if (stripHeight <= 0 || !model.surface(surfaceId)) {
        return {};
    }
    const LayoutGeometry geometry = model.computeGeometry(surfaceId, surfaceSize, metrics);
    const auto stripOf = [&](const QString &anchorId) {
        const QRectF rect = geometry.anchors.value(anchorId);
        return QRectF(rect.left(), rect.top(), rect.width(), std::min(stripHeight, rect.height()));
    };

    // Exactly where a strip is drawn.
    QString drawn;
    for (auto it = geometry.anchors.cbegin(); it != geometry.anchors.cend(); ++it) {
        if (stripOf(it.key()).contains(point)) {
            drawn = it.key();
            break;
        }
    }

    const bool heldHere = held.isValid() && held.surfaceId == surfaceId;
    const bool heldStrip = heldHere && held.edge == Edge::Center && !held.anchorId.isEmpty()
        && geometry.anchors.contains(held.anchorId);
    if (heldHere && !heldStrip && held.edge != Edge::Center
        && stillHeld(model, surfaceId, point, surfaceSize, metrics, zones, held)) {
        return {}; // a split or window edge still has it
    }
    if (!drawn.isEmpty()) {
        return {drawn, point.x() - geometry.anchors.value(drawn).left()};
    }
    if (heldStrip) {
        const QRectF strip = stripOf(held.anchorId);
        // Above and below, a little; across, as far as the panel goes (the
        // divider beside it included).
        const QRectF kept = strip.adjusted(-metrics.splitterThickness, -zones.hold, metrics.splitterThickness, zones.hold);
        if (kept.contains(point)) {
            return {held.anchorId, point.x() - strip.left()};
        }
    }
    return {};
}

DropTarget dockTargetAt(const WorkspaceModel &model, const QString &surfaceId, const QPointF &point,
                        const QSizeF &surfaceSize, const LayoutMetrics &metrics, const DockZones &zones,
                        const DropTarget &held)
{
    if (stillHeld(model, surfaceId, point, surfaceSize, metrics, zones, held)) {
        DropTarget kept = held;
        kept.index = -1;
        return kept;
    }
    if (!model.surface(surfaceId) || !QRectF(QPointF(0, 0), surfaceSize).contains(point)) {
        return {};
    }

    DropTarget target;
    target.surfaceId = surfaceId;

    const double left = point.x();
    const double right = surfaceSize.width() - point.x();
    const double top = point.y();
    const double bottom = surfaceSize.height() - point.y();
    const double nearest = std::min({left, right, top, bottom});
    if (model.anchorIds(surfaceId).size() > 1 && nearest <= zones.windowBand) {
        target.edge = nearest == left ? Edge::Left : nearest == right ? Edge::Right : nearest == top ? Edge::Top : Edge::Bottom;
        return target;
    }

    const LayoutGeometry geometry = model.computeGeometry(surfaceId, surfaceSize, metrics);
    for (auto it = geometry.anchors.cbegin(); it != geometry.anchors.cend(); ++it) {
        const QRectF &rect = it.value();
        if (!rect.contains(point)) {
            continue;
        }
        target.anchorId = it.key();
        const AnchorRecord *anchor = model.anchor(it.key());
        if (anchor && anchor->views.isEmpty()) {
            // An empty panel is asking to be filled.
            target.edge = Edge::Center;
            return target;
        }

        const auto band = [&zones](double length) {
            return std::clamp(length * zones.edgeShare, zones.edgeMinimum, std::max(zones.edgeMinimum, zones.edgeMaximum));
        };
        const double bandX = std::min(band(rect.width()), rect.width() / 2);
        const double bandY = std::min(band(rect.height()), rect.height() / 2);
        // How far into its band the point is, 0 at the edge: the deepest
        // claim wins at the corners.
        struct Candidate { Edge edge; double depth; };
        const Candidate candidates[] = {
            {Edge::Left, (point.x() - rect.left()) / bandX},
            {Edge::Right, (rect.right() - point.x()) / bandX},
            {Edge::Top, (point.y() - rect.top()) / bandY},
            {Edge::Bottom, (rect.bottom() - point.y()) / bandY},
        };
        const Candidate *best = nullptr;
        for (const Candidate &candidate : candidates) {
            if (candidate.depth <= 1 && (!best || candidate.depth < best->depth)) {
                best = &candidate;
            }
        }
        if (!best) {
            return {}; // open space in the middle of a panel
        }
        target.edge = best->edge;
        return target;
    }
    // On a divider: the nearest edge beside it (dividers are only a few
    // pixels wide, so this is the edge the pointer is plainly at).
    QString nearestId;
    Edge nearestEdge = Edge::Center;
    double nearestDistance = metrics.splitterThickness + 1;
    for (auto it = geometry.anchors.cbegin(); it != geometry.anchors.cend(); ++it) {
        const QRectF &rect = it.value();
        if (rect.isEmpty()) {
            continue;
        }
        const bool acrossY = point.y() >= rect.top() && point.y() <= rect.bottom();
        const bool acrossX = point.x() >= rect.left() && point.x() <= rect.right();
        const struct { Edge edge; double distance; bool facing; } sides[] = {
            {Edge::Right, point.x() - rect.right(), acrossY},
            {Edge::Left, rect.left() - point.x(), acrossY},
            {Edge::Bottom, point.y() - rect.bottom(), acrossX},
            {Edge::Top, rect.top() - point.y(), acrossX},
        };
        for (const auto &side : sides) {
            if (side.facing && side.distance >= 0 && side.distance < nearestDistance) {
                nearestDistance = side.distance;
                nearestId = it.key();
                nearestEdge = side.edge;
            }
        }
    }
    const AnchorRecord *beside = nearestId.isEmpty() ? nullptr : model.anchor(nearestId);
    if (!beside || beside->views.isEmpty()) {
        return {};
    }
    target.anchorId = nearestId;
    target.edge = nearestEdge;
    return target;
}

OpResult canFloat(const WorkspaceModel &model, const DragSource &source)
{
    if (!sourceExists(model, source)) {
        return OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("It's no longer open."));
    }
    WorkspaceModel copy = model;
    const QRectF probe(40, 40, 480, 320);
    return source.kind == DragSource::Kind::View
        ? copy.detachView(source.id, probe, {}, Origin::User, true)
        : copy.detachAnchor(source.id, probe, {}, true);
}

Place placeOf(const WorkspaceModel &model, const DragSource &source)
{
    Place place;
    if (!sourceExists(model, source)) {
        return place;
    }
    const QString anchorId = source.kind == DragSource::Kind::View ? model.anchorOfView(source.id) : source.id;
    const AnchorRecord *anchor = model.anchor(anchorId);
    if (!anchor) {
        return place;
    }

    place.source = source;
    place.surfaceId = model.surfaceOfAnchor(anchorId);
    if (source.kind == DragSource::Kind::View) {
        place.anchorId = anchorId;
        place.index = int(anchor->views.indexOf(source.id));
    }
    place.beside = besideOf(model, anchorId, place.surfaceId);
    if (const SplitNode *leaf = model.node(model.leafOfAnchor(anchorId)); leaf && !leaf->parentId.isEmpty()) {
        const SplitNode *parent = model.node(leaf->parentId);
        const qsizetype index = parent->children.indexOf(leaf->id);
        place.share = parent->sizes.value(index, 0);
        place.splitOrientation = parent->orientation;
    }

    const SurfaceRecord *surface = model.surface(place.surfaceId);
    const bool anchorLeaves = source.kind == DragSource::Kind::Anchor || anchor->views.size() == 1;
    if (surface && !surface->primary && anchorLeaves && model.anchorIds(place.surfaceId).size() == 1) {
        place.aloneInFloatingWindow = true;
        place.inMainWindow = surface->inMainWindow;
        place.sidebarSlot = surface->sidebarSlot;
        place.windowGeometry = surface->geometry;
        place.screenName = surface->screenName;
    }
    return place;
}

ReturnPlan planReturn(const WorkspaceModel &model, const Place &place)
{
    ReturnPlan plan;
    if (!place.isValid()) {
        plan.result = OpResult::failure(QStringLiteral("NO_PLACE"), QStringLiteral("There's nowhere to put it back."));
        return plan;
    }
    if (!sourceExists(model, place.source)) {
        plan.result = OpResult::failure(QStringLiteral("NOT_FOUND"), QStringLiteral("It's no longer open."));
        return plan;
    }

    const Place now = placeOf(model, place.source);
    const bool isView = place.source.kind == DragSource::Kind::View;

    // A tab whose panel is still there goes back into it, at its old position.
    // (A tab alone in its panel moved the whole panel: that is put back below.)
    const bool movedItsPanel = place.aloneInFloatingWindow && now.anchorId == place.anchorId;
    if (isView && model.anchor(place.anchorId) && !movedItsPanel) {
        if (now.anchorId == place.anchorId && now.index == place.index) {
            plan.result = OpResult::failure(QStringLiteral("NO_CHANGE"), QStringLiteral("It's already there."));
            return plan;
        }
        plan.target.surfaceId = model.surfaceOfAnchor(place.anchorId);
        plan.target.anchorId = place.anchorId;
        plan.target.edge = Edge::Center;
        plan.target.index = place.index;
        plan.result = model.canDrop(place.source, plan.target);
        return plan;
    }

    // It had a floating window to itself: it gets one again.
    if (place.aloneInFloatingWindow) {
        const SurfaceRecord *nowSurface = model.surface(now.surfaceId);
        if (now.aloneInFloatingWindow && nowSurface && nowSurface->inMainWindow == place.inMainWindow
            && nowSurface->sidebarSlot == place.sidebarSlot
            && (!place.sidebarSlot.isEmpty() || nowSurface->geometry == place.windowGeometry)) {
            plan.result = OpResult::failure(QStringLiteral("NO_CHANGE"), QStringLiteral("It's already there."));
            return plan;
        }
        const QString holder = model.surfaceInSidebarSlot(place.sidebarSlot);
        if (!holder.isEmpty() && holder != now.surfaceId) {
            plan.result = OpResult::failure(QStringLiteral("SLOT_TAKEN"), QStringLiteral("Something else is docked there now."));
            return plan;
        }
        plan.detach = true;
        plan.inMainWindow = place.inMainWindow;
        plan.sidebarSlot = place.sidebarSlot;
        plan.geometry = place.windowGeometry;
        plan.screenName = place.screenName;
        plan.result = OpResult::success();
        return plan;
    }

    // Beside the neighbour it had, or along the window edge it had.
    DropTarget target = place.beside;
    if (!target.anchorId.isEmpty()) {
        if (!model.anchor(target.anchorId)) {
            plan.result = OpResult::failure(QStringLiteral("GONE"), QStringLiteral("The panel it was beside is gone."));
            return plan;
        }
        target.surfaceId = model.surfaceOfAnchor(target.anchorId);
    } else if (!model.surface(target.surfaceId)) {
        plan.result = OpResult::failure(QStringLiteral("GONE"), QStringLiteral("The window it was in is gone."));
        return plan;
    }

    const bool aloneNow = !isView || model.anchor(now.anchorId)->views.size() == 1;
    if (aloneNow && sameTarget(now.beside, target)) {
        plan.result = OpResult::failure(QStringLiteral("NO_CHANGE"), QStringLiteral("It's already there."));
        return plan;
    }
    plan.target = target;
    plan.result = model.canDrop(place.source, target);
    return plan;
}

bool restoreShare(WorkspaceModel &model, const QString &anchorId, const Place &place)
{
    if (place.share <= 0 || place.share >= 1) {
        return false;
    }
    const SplitNode *leaf = model.node(model.leafOfAnchor(anchorId));
    if (!leaf || leaf->parentId.isEmpty()) {
        return false;
    }
    const SplitNode *parent = model.node(leaf->parentId);
    if (parent->orientation != place.splitOrientation || parent->children.size() < 2) {
        return false;
    }
    const qsizetype index = parent->children.indexOf(leaf->id);
    const double others = 1.0 - parent->sizes.value(index, 0);
    QList<double> sizes = parent->sizes;
    for (qsizetype i = 0; i < sizes.size(); ++i) {
        sizes[i] = i == index ? place.share : (others > 0 ? sizes.at(i) / others * (1.0 - place.share) : (1.0 - place.share) / (sizes.size() - 1));
    }
    return model.setSplitSizes(parent->id, sizes).ok;
}

QVariantMap rectMap(const QRectF &rect)
{
    if (rect.isNull()) {
        return {};
    }
    return {
        {QStringLiteral("x"), rect.x()},
        {QStringLiteral("y"), rect.y()},
        {QStringLiteral("width"), rect.width()},
        {QStringLiteral("height"), rect.height()},
    };
}

QVariantMap targetMap(const DropTarget &target)
{
    if (!target.isValid()) {
        return {};
    }
    return {
        {QStringLiteral("surfaceId"), target.surfaceId},
        {QStringLiteral("anchorId"), target.anchorId},
        {QStringLiteral("edge"), edgeName(target.edge)},
        {QStringLiteral("index"), target.index},
        {QStringLiteral("surfaceEdge"), target.isSurfaceEdge()},
    };
}
} // namespace Workspace::Magnets

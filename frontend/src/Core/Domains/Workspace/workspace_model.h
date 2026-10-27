#pragma once

#include "Core/Domains/Workspace/workspace_types.h"

#include <QHash>
#include <QList>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QStringList>

#include <functional>

// The docking workspace: surfaces (windows), each with a recursive split tree
// whose leaves are anchors (tab groups) holding views (tabs).
//
// Pure data and rules, no Qt Quick. Every mutation either succeeds and leaves
// the model valid, or fails and leaves it untouched. A view is never dropped
// by a layout operation: the only ways a view leaves the model are
// closeView/closeAnchor, which the caller guards against unsaved work.
//
// Invariants (checked by validate()):
//   - every view is in exactly one anchor;
//   - every anchor is in exactly one leaf of exactly one surface tree;
//   - split nodes have at least two children and sizes summing to 1, and no
//     split has a child split with the same orientation;
//   - there is exactly one primary surface and it is never empty;
//   - an anchor's active view is one of its views (or empty when it has none).
namespace Workspace
{
class WorkspaceModel
{
public:
    using IdGenerator = std::function<QString(const QString &prefix)>;

    // A fresh workspace: one primary surface with one pinned, empty anchor.
    WorkspaceModel();
    explicit WorkspaceModel(IdGenerator generator);

    // ---- reading ---------------------------------------------------------

    [[nodiscard]] QStringList surfaceIds() const { return m_surfaceOrder; }
    [[nodiscard]] QString primarySurfaceId() const;
    [[nodiscard]] const SurfaceRecord *surface(const QString &surfaceId) const;
    [[nodiscard]] const AnchorRecord *anchor(const QString &anchorId) const;
    [[nodiscard]] const ViewRecord *view(const QString &viewId) const;
    [[nodiscard]] const SplitNode *node(const QString &nodeId) const;
    [[nodiscard]] QStringList anchorIds() const; // every anchor, tree order
    [[nodiscard]] QStringList anchorIds(const QString &surfaceId) const;
    [[nodiscard]] QStringList viewIds() const;   // every view, tree order
    [[nodiscard]] QString anchorOfView(const QString &viewId) const;
    [[nodiscard]] QString surfaceOfAnchor(const QString &anchorId) const;
    [[nodiscard]] QString leafOfAnchor(const QString &anchorId) const;
    [[nodiscard]] QStringList viewsForResource(const QString &kind, const QString &id) const;
    [[nodiscard]] QString focusedAnchorId() const { return m_focusedAnchorId; }
    // Where newly opened content goes: the chosen target, else the focused
    // anchor, else the first anchor of the primary surface.
    [[nodiscard]] QString targetAnchorId() const;
    [[nodiscard]] QString explicitTargetAnchorId() const { return m_targetAnchorId; }
    [[nodiscard]] int revision() const { return m_revision; }

    // ---- views -----------------------------------------------------------

    // Adds a view to `anchorId` (empty: the target anchor) at `index`
    // (-1: after the active tab). Returns the new view id in createdId.
    OpResult openView(ViewRecord view, const QString &anchorId = {}, int index = -1, bool activate = true);
    // Removes a view. The caller has already made sure nothing is lost.
    OpResult closeView(const QString &viewId);
    OpResult activateView(const QString &viewId);
    OpResult updateViewState(const QString &viewId, const QVariantMap &state);
    OpResult setViewTitle(const QString &viewId, const QString &title);
    // Rebinds the view to another resource (for example after a rename).
    OpResult setViewResource(const QString &viewId, const ResourceRef &resource);
    // Adds a second view of the same resource next to the original (or in
    // `anchorId`). The copy shares the resource, never its canonical state.
    OpResult duplicateView(const QString &viewId, const QString &anchorId = {}, Edge edge = Edge::Center);

    // Reorders within an anchor or moves to another anchor (tab drag).
    OpResult moveView(const QString &viewId, const QString &targetAnchorId, int index = -1);
    // Moves a view into a new anchor beside `targetAnchorId`.
    OpResult splitView(const QString &viewId, const QString &targetAnchorId, Edge edge);
    // Moves a view into a new anchor along an outer edge of a surface.
    OpResult dockViewAtSurfaceEdge(const QString &viewId, const QString &surfaceId, Edge edge);
    // Moves a view into a new floating surface.
    OpResult detachView(const QString &viewId, const QRectF &geometry, const QString &screenName = {});

    // ---- anchors ---------------------------------------------------------

    // A new pinned, empty anchor beside `targetAnchorId` (empty: target).
    OpResult createAnchor(const QString &targetAnchorId = {}, Edge edge = Edge::Right);
    // Closes an anchor and every view in it. Guard first.
    OpResult closeAnchor(const QString &anchorId);
    // Moves a whole anchor beside another, or merges into it (Center).
    OpResult moveAnchor(const QString &anchorId, const QString &targetAnchorId, Edge edge);
    OpResult mergeAnchor(const QString &anchorId, const QString &targetAnchorId);
    OpResult dockAnchorAtSurfaceEdge(const QString &anchorId, const QString &surfaceId, Edge edge);
    OpResult detachAnchor(const QString &anchorId, const QRectF &geometry, const QString &screenName = {});
    OpResult setAnchorPinned(const QString &anchorId, bool pinned);
    OpResult setAnchorRoles(const QString &anchorId, const QStringList &roles);
    OpResult focusAnchor(const QString &anchorId);
    OpResult setTargetAnchor(const QString &anchorId); // empty clears
    OpResult maximizeAnchor(const QString &anchorId);
    OpResult restoreAnchor(const QString &surfaceId);

    // ---- splits ----------------------------------------------------------

    OpResult setSplitSizes(const QString &nodeId, const QList<double> &sizes);
    // Evenly redistributes a split (double-click on a splitter).
    OpResult equalizeSplit(const QString &nodeId);

    // ---- surfaces --------------------------------------------------------

    OpResult setSurfaceGeometry(const QString &surfaceId, const QRectF &geometry, const QString &screenName, bool userPlaced = true);
    OpResult setSurfaceWindowMaximized(const QString &surfaceId, bool maximized);
    // Closes a floating window. Its views move into the primary surface's
    // target anchor; nothing is closed. The primary surface cannot close.
    OpResult closeSurface(const QString &surfaceId);
    // Puts surfaces whose monitor is gone onto a screen that exists, and
    // returns surfaces to their preferred monitor when it is back.
    // Returns the ids of surfaces that moved.
    QStringList recoverSurfaces(const QList<ScreenInfo> &screens);

    // ---- drag and drop ---------------------------------------------------

    [[nodiscard]] OpResult canDrop(const DragSource &source, const DropTarget &target) const;
    OpResult applyDrop(const DragSource &source, const DropTarget &target);
    // The layout the surface would have after the drop: every anchor's rect,
    // so the UI can ease panels into their proposed positions. `*landingRect`
    // receives the rect the dragged content would occupy.
    [[nodiscard]] LayoutGeometry previewDrop(
        const DragSource &source,
        const DropTarget &target,
        const QSizeF &surfaceSize,
        const LayoutMetrics &metrics,
        QRectF *landingRect = nullptr
    ) const;
    // Which target is under `point` (surface coordinates).
    [[nodiscard]] DropTarget dropTargetAt(
        const QString &surfaceId,
        const QPointF &point,
        const QSizeF &surfaceSize,
        const LayoutMetrics &metrics,
        double surfaceEdgeBand = 18
    ) const;

    // ---- geometry and navigation ----------------------------------------

    [[nodiscard]] LayoutGeometry computeGeometry(const QString &surfaceId, const QSizeF &size, const LayoutMetrics &metrics) const;
    // The anchor in `direction` from `anchorId` within its surface, by
    // geometry, or empty at the edge.
    [[nodiscard]] QString neighborAnchor(const QString &anchorId, Edge direction, const QSizeF &surfaceSize, const LayoutMetrics &metrics) const;
    // Next/previous anchor in reading order across all surfaces (wraps).
    [[nodiscard]] QString cycleAnchor(const QString &anchorId, int step) const;

    // ---- whole-model -----------------------------------------------------

    [[nodiscard]] QStringList validate() const;
    void setIdGenerator(IdGenerator generator) { m_generateId = std::move(generator); }

    // Raw access for the codec. Prefer the operations above.
    struct Data
    {
        QHash<QString, SurfaceRecord> surfaces;
        QStringList surfaceOrder;
        QHash<QString, SplitNode> nodes;
        QHash<QString, AnchorRecord> anchors;
        QHash<QString, ViewRecord> views;
        QString focusedAnchorId;
        QString targetAnchorId;
    };
    [[nodiscard]] Data data() const;
    // Replaces the whole model when `data` is valid; otherwise leaves the
    // model untouched and returns the problems.
    QStringList setData(const Data &data);

private:
    template<typename Work>
    OpResult transact(Work work);
    QString newId(const QString &prefix);
    void bump() { ++m_revision; }

    QString rootOfNode(const QString &nodeId) const;
    QString surfaceOfNode(const QString &nodeId) const;
    QString createLeaf(const QString &anchorId);
    void insertBeside(const QString &targetNodeId, const QString &newNodeId, Edge edge);
    void insertAtSurfaceEdge(const QString &surfaceId, const QString &newNodeId, Edge edge);
    void detachNode(const QString &nodeId); // unlinks from tree, keeps record
    void replaceChild(const QString &parentId, const QString &oldId, const QString &newId);
    void flattenInto(const QString &parentId);
    void removeAnchorRecord(const QString &anchorId); // unlinks + deletes
    void collapseIfIncidental(const QString &anchorId);
    void removeViewFromAnchor(const QString &viewId);
    void insertViewIntoAnchor(const QString &viewId, const QString &anchorId, int index, bool activate);
    QString createSurface(const QRectF &geometry, const QString &screenName);
    void dropSurfaceIfEmpty(const QString &surfaceId);
    void ensurePrimaryHasAnchor();
    void repairPointers();
    void layoutNode(const QString &nodeId, const QRectF &rect, const LayoutMetrics &metrics, LayoutGeometry &out) const;
    QSizeF minimumSize(const QString &nodeId, const LayoutMetrics &metrics) const;
    void collectAnchors(const QString &nodeId, QStringList &out) const;
    QString anchorForDropSimulation(const DragSource &source, const DropTarget &target, const WorkspaceModel &after) const;

    QHash<QString, SurfaceRecord> m_surfaces;
    QStringList m_surfaceOrder;
    QHash<QString, SplitNode> m_nodes;
    QHash<QString, AnchorRecord> m_anchors;
    QHash<QString, ViewRecord> m_views;
    QHash<QString, QString> m_anchorOfView;
    QString m_focusedAnchorId;
    QString m_targetAnchorId;
    int m_revision = 0;
    IdGenerator m_generateId;
};
} // namespace Workspace

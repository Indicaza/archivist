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
//   - every view is in exactly one anchor, or minimized in exactly one
//     Gravity Well entry (never both);
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
    // Every view in a panel, tree order (minimized views are not: see
    // minimizedViewIds).
    [[nodiscard]] QStringList viewIds() const;
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
    OpResult openView(ViewRecord view, QString anchorId = {}, int index = -1, bool activate = true, Origin origin = Origin::User);
    // Removes a view. The caller has already made sure nothing is lost.
    OpResult closeView(QString viewId, Origin origin = Origin::User);
    OpResult activateView(QString viewId);
    OpResult updateViewState(QString viewId, const QVariantMap &state);
    OpResult setViewTitle(QString viewId, QString title);
    // Rebinds the view to another resource (for example after a rename).
    // Refused for automation and agents when the anchor's content is locked.
    OpResult setViewResource(QString viewId, const ResourceRef &resource, Origin origin = Origin::User);
    // Changes how a view presents its resource (rendered Markdown -> source
    // editor) without moving it or changing its id.
    OpResult setViewType(QString viewId, QString typeId, int stateVersion = 1);
    // Adds a second view of the same resource next to the original (or in
    // `anchorId`). The copy shares the resource, never its canonical state.
    OpResult duplicateView(QString viewId, QString anchorId = {}, Edge edge = Edge::Center, Origin origin = Origin::User);

    // Reorders within an anchor or moves to another anchor (tab drag).
    OpResult moveView(QString viewId, QString targetAnchorId, int index = -1, Origin origin = Origin::User);
    // Moves a view into a new anchor beside `targetAnchorId`.
    OpResult splitView(QString viewId, QString targetAnchorId, Edge edge, Origin origin = Origin::User);
    // Moves a view into a new anchor along an outer edge of a surface.
    OpResult dockViewAtSurfaceEdge(QString viewId, QString surfaceId, Edge edge, Origin origin = Origin::User);
    // Moves a view into a new floating surface: a window of its own, or a
    // floating panel inside the main window (`inMainWindow`, geometry in the
    // main window's content). A view that is already alone in a
    // floating surface just moves that surface.
    OpResult detachView(QString viewId, const QRectF &geometry, QString screenName = {}, Origin origin = Origin::User,
                        bool inMainWindow = false);

    // ---- anchors ---------------------------------------------------------

    // A new pinned, empty anchor beside `targetAnchorId` (empty: target).
    OpResult createAnchor(QString targetAnchorId = {}, Edge edge = Edge::Right);
    // A new pinned, empty anchor along an outer edge of a surface (the whole
    // side of the window), squeezing the anchors already there.
    OpResult createAnchorAtSurfaceEdge(QString surfaceId, Edge edge);
    // Closes an anchor and every view in it. Guard first.
    OpResult closeAnchor(QString anchorId, Origin origin = Origin::User);
    // Moves a whole anchor beside another, or merges into it (Center).
    OpResult moveAnchor(QString anchorId, QString targetAnchorId, Edge edge, Origin origin = Origin::User);
    // `index` puts the tabs in at that position (in their order); -1 at the end.
    OpResult mergeAnchor(QString anchorId, QString targetAnchorId, Origin origin = Origin::User, int index = -1);
    OpResult dockAnchorAtSurfaceEdge(QString anchorId, QString surfaceId, Edge edge);
    OpResult detachAnchor(QString anchorId, const QRectF &geometry, QString screenName = {}, bool inMainWindow = false);
    OpResult setAnchorPinned(QString anchorId, bool pinned);
    OpResult setAnchorRoles(QString anchorId, const QStringList &roles);
    OpResult focusAnchor(QString anchorId);
    OpResult setTargetAnchor(QString anchorId); // empty clears
    OpResult maximizeAnchor(QString anchorId);
    OpResult restoreAnchor(QString surfaceId);

    // ---- anchor identity and configuration -------------------------------
    //
    // These are the operations a future agent tool calls too (see
    // workspace_commands.h). Locks are enforced here, not in the UI.

    // Empty (or blank) resets to the automatic name.
    OpResult renameAnchor(QString anchorId, QString name, Origin origin = Origin::User);
    OpResult setAnchorLayoutLocked(QString anchorId, bool locked, Origin origin = Origin::User);
    OpResult setAnchorContentLocked(QString anchorId, bool locked, Origin origin = Origin::User);
    OpResult setAnchorSwitchable(QString anchorId, bool switchable, Origin origin = Origin::User);
    // Empty clears the binding. Existing views keep their own worktree.
    OpResult setAnchorLibrary(QString anchorId, QString libraryId, Origin origin = Origin::User);
    // The anchor's name, or an automatic one ("Editor", "Chat 2", ...).
    [[nodiscard]] QString anchorDisplayName(const QString &anchorId) const;
    [[nodiscard]] QString automaticAnchorName(const QString &anchorId) const;
    [[nodiscard]] AnchorCapabilities anchorCapabilities(const QString &anchorId, Origin origin = Origin::User) const;
    // Which worktree a new resource opened in `anchorId` belongs to:
    //   1. the anchor's own binding;
    //   2. the worktree of the anchor's active view;
    //   3. `explorerLibraryId` (the Explorer's selection);
    //   4. `defaultLibraryId`.
    // When `knownLibraryIds` is not empty, ids outside it are skipped (a
    // binding to a Library that no longer exists falls through).
    [[nodiscard]] LibraryResolution resolveLibrary(
        const QString &anchorId,
        const QString &explorerLibraryId,
        const QString &defaultLibraryId,
        const QStringList &knownLibraryIds = {}
    ) const;
    // True when moving the divider between children[index] and
    // children[index + 1] of `nodeId` would resize a layout-locked anchor.
    [[nodiscard]] bool isSplitBoundaryLocked(const QString &nodeId, int index) const;
    // Anchors that are layout locked, in tree order.
    [[nodiscard]] QStringList layoutLockedAnchorIds() const;

    // ---- splits ----------------------------------------------------------

    OpResult setSplitSizes(QString nodeId, const QList<double> &sizes);
    // Evenly redistributes a split (double-click on a splitter).
    OpResult equalizeSplit(QString nodeId);
    // Moves the boundary between children[index] and children[index + 1] of
    // a split to `position` (surface coordinates along the split's axis),
    // honouring both neighbours' minimum sizes.
    OpResult resizeSplit(
        QString surfaceId,
        QString nodeId,
        int index,
        double position,
        const QSizeF &surfaceSize,
        const LayoutMetrics &metrics
    );

    // ---- surfaces --------------------------------------------------------

    OpResult setSurfaceGeometry(QString surfaceId, const QRectF &geometry, QString screenName, bool userPlaced = true);
    OpResult setSurfaceWindowMaximized(QString surfaceId, bool maximized);
    // Docks a floating surface into a sidebar slot (empty: out of the
    // sidebar, as a floating panel). A slot holds one surface.
    OpResult setSurfaceSidebarSlot(QString surfaceId, QString slot);
    // The surface docked in a sidebar slot, if any.
    [[nodiscard]] QString surfaceInSidebarSlot(const QString &slot) const;
    // Closes a floating window. Its views move into the primary surface's
    // target anchor; nothing is closed. The primary surface cannot close.
    OpResult closeSurface(QString surfaceId);
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

    // ---- Gravity Wells ---------------------------------------------------
    //
    // A Well holds references: minimized views (still views of this model,
    // same id and resource, parked instead of in a panel) and pinned
    // resources. Planning where a restore goes is Wells::restore
    // (workspace_wells.h); these are the primitives it commits.

    // The main window's Well always exists (id kMainWellId).
    static inline const QString kMainWellId = QStringLiteral("well.main");
    [[nodiscard]] QList<WellRecord> wells() const { return m_wells; }
    [[nodiscard]] const WellRecord *well(const QString &wellId) const;
    [[nodiscard]] const WellEntry *wellEntry(const QString &entryId) const;
    [[nodiscard]] QString wellOfEntry(const QString &entryId) const;
    // The entry a minimized view is parked in (empty when it is in a panel).
    [[nodiscard]] QString entryOfMinimizedView(const QString &viewId) const;
    [[nodiscard]] bool isMinimized(const QString &viewId) const { return !entryOfMinimizedView(viewId).isEmpty(); }
    [[nodiscard]] QStringList minimizedViewIds() const;
    // The entry that stands for `resource` in `wellId` (empty: none).
    [[nodiscard]] QString wellEntryForResource(const QString &wellId, const ResourceRef &resource) const;
    [[nodiscard]] const MinimizedView *minimizedView(const QString &viewId) const;

    // Puts a view away: out of its panel (an incidental panel left empty
    // folds, as when its last tab is closed), into the entry for its
    // resource in `wellId` (empty: the main Well), remembering `place`.
    // Refused for automation and agents when the anchor's content is locked.
    OpResult minimizeView(QString viewId, const Place &place, QString wellId = {}, Origin origin = Origin::User);
    // Takes a minimized view back into `anchorId` at `index` (-1: after the
    // active tab). Its entry goes when it is neither pinned nor holding
    // another view.
    OpResult unminimizeView(QString viewId, QString anchorId, int index = -1, bool activate = true);
    // Pins (or unpins) the resource a view shows, in the Well it is in or
    // the main Well. Unpinning an entry with no minimized view removes it.
    OpResult setViewPinned(QString viewId, bool pinned, QString wellId = {});
    OpResult setWellEntryPinned(QString entryId, bool pinned);
    OpResult moveWellEntry(QString entryId, int index);
    // Moves a Well within its window (`position` as a share, clamped 0..1).
    OpResult setWellPosition(QString wellId, const QPointF &position);

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
        QList<WellRecord> wells;
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
    QString createSurface(const QRectF &geometry, const QString &screenName, bool inMainWindow = false);
    // Moves a floating surface that a drag or return re-creates as itself
    // (it leaves the sidebar if it was there).
    void placeSurface(const QString &surfaceId, const QRectF &geometry, const QString &screenName, bool inMainWindow);
    void dropSurfaceIfEmpty(const QString &surfaceId);
    void ensurePrimaryHasAnchor();
    void ensureMainWell();
    WellRecord *mutableWell(const QString &wellId);
    WellEntry *mutableEntry(const QString &entryId);
    void dropEntryIfUnused(const QString &entryId);
    void repairPointers();
    void layoutNode(const QString &nodeId, const QRectF &rect, const LayoutMetrics &metrics, LayoutGeometry &out) const;
    QSizeF minimumSize(const QString &nodeId, const LayoutMetrics &metrics) const;
    void collectAnchors(const QString &nodeId, QStringList &out) const;
    QString anchorForDropSimulation(const DragSource &source, const DropTarget &target, const WorkspaceModel &after) const;
    // Lock checks shared by the operations and canDrop.
    OpResult layoutLockFailure(const QString &anchorId, const QString &action) const;
    OpResult contentLockFailure(QString anchorId, Origin origin) const;
    // Docking along a window edge squeezes the panels already there.
    OpResult surfaceEdgeLockFailure(QString surfaceId, Edge edge) const;
    [[nodiscard]] bool isLayoutLocked(const QString &anchorId) const;
    [[nodiscard]] bool containsLayoutLocked(const QString &nodeId) const;
    // Gives `share` of a split to (or takes it from) the children that are
    // not layout locked, so a locked anchor keeps its size when a sibling
    // comes or goes. Falls back to every child when all are locked.
    void rebalance(SplitNode &split, double freed, qsizetype skipIndex = -1);

    QHash<QString, SurfaceRecord> m_surfaces;
    QStringList m_surfaceOrder;
    QHash<QString, SplitNode> m_nodes;
    QHash<QString, AnchorRecord> m_anchors;
    QHash<QString, ViewRecord> m_views;
    QHash<QString, QString> m_anchorOfView;
    QString m_focusedAnchorId;
    QString m_targetAnchorId;
    QList<WellRecord> m_wells;
    int m_revision = 0;
    IdGenerator m_generateId;
};
} // namespace Workspace

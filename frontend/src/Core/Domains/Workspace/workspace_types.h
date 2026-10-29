#pragma once

#include <QHash>
#include <QList>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// Value types of the docking workspace.
//
// Four identities, each with its own lifecycle:
//
//   resource  the thing being looked at: a document buffer, a Chat, a
//             terminal session, a diff. Owned by its domain store, never by
//             the layout. Its worktree (Library) belongs to it.
//   view      one presentation of a resource inside an Anchor (a tab). Two
//             views may show the same resource; closing a view never closes
//             the resource while another view still shows it.
//   anchor    a dockable tab group: an ordered list of views, one active.
//   surface   a top-level window holding a recursive split tree of anchors.
//
// Moving, splitting or detaching only ever changes view -> anchor ->
// surface membership. Resources are untouched, which is what keeps unsaved
// buffers, running agents and terminal processes alive across layout changes.
namespace Workspace
{
enum class Edge
{
    Center,
    Left,
    Right,
    Top,
    Bottom,
};

enum class Orientation
{
    Horizontal, // children left to right
    Vertical,   // children top to bottom
};

[[nodiscard]] QString edgeName(Edge edge);
[[nodiscard]] bool parseEdge(const QString &name, Edge *edge);
[[nodiscard]] QString orientationName(Orientation orientation);
[[nodiscard]] bool parseOrientation(const QString &name, Orientation *orientation);
[[nodiscard]] Orientation orientationForEdge(Edge edge);
// True when the edge puts the new item before the existing one.
[[nodiscard]] bool edgeLeads(Edge edge);

// Who asked for an operation. Layout locks stop everyone; content locks stop
// automatic behaviour (selection following, a chat switching underneath) and
// agents, but not an explicit user action. Agents and automation may lock an
// Anchor but never unlock one.
enum class Origin
{
    User,
    Automatic,
    Agent,
};

[[nodiscard]] QString originName(Origin origin);
[[nodiscard]] bool parseOrigin(const QString &name, Origin *origin);

// What a view shows. `kind` + `id` is the canonical identity; two views with
// the same pair share one underlying resource.
struct ResourceRef
{
    QString kind; // document, chat, terminal, preview, diff, ... (empty: none)
    QString id;   // documents: collectionId:libraryId:fileId
    QString collectionId;
    QString libraryId; // the worktree the resource belongs to
    QVariantMap details; // path, title hints, ... (never canonical state)

    [[nodiscard]] bool isEmpty() const { return kind.isEmpty() && id.isEmpty(); }
    [[nodiscard]] QString key() const { return kind + QLatin1Char('|') + id; }
    friend bool operator==(const ResourceRef &a, const ResourceRef &b)
    {
        return a.kind == b.kind && a.id == b.id && a.collectionId == b.collectionId
            && a.libraryId == b.libraryId && a.details == b.details;
    }
};

struct ViewRecord
{
    QString id;
    QString typeId;
    int stateVersion = 1;
    ResourceRef resource;
    QString title;
    // View-only state: cursor, scroll, zoom, fold state. Never the document
    // text, the conversation or the terminal scrollback.
    QVariantMap state;
};

struct AnchorRecord
{
    QString id;
    QStringList views; // ordered view ids
    QString activeViewId;
    // Created on purpose (toolbar, palette, preset). A pinned anchor survives
    // losing its last tab; an incidental one collapses.
    bool pinned = false;
    // Layout hint: view types this anchor prefers when a layout is applied.
    QStringList roles;
    // What the user calls this anchor ("Reference Shelf"). Empty: an
    // automatic name from its content (WorkspaceModel::anchorDisplayName).
    QString name;
    // Layout lock: the anchor can't be moved, closed, resized, merged away or
    // floated, by anyone, until it is unlocked. Its content stays usable.
    bool layoutLocked = false;
    // Content lock: automation and agents can't add, remove, move or rebind
    // its views. The user still can, explicitly.
    bool contentLocked = false;
    // Shows one view at a time with a compact picker instead of tabs. The
    // views are ordinary views, so each keeps its own state while hidden.
    bool switchable = false;
    // Default worktree (Library) for new resources opened here. Empty:
    // follow context. Never changes the views already in the anchor.
    QString libraryId;
};

// What an anchor allows right now, for a given origin. `reasons` maps each
// refused capability to a short explanation.
struct AnchorCapabilities
{
    bool canMove = true;
    bool canClose = true;
    bool canResize = true;
    bool canMerge = true;
    bool canFloat = true;
    bool canDockBeside = true;
    bool canAddViews = true;
    bool canRemoveViews = true;
    bool canReplaceContent = true;
    bool canRename = true;
    bool canUnlock = true;
    bool canSetWorktree = true;
    QHash<QString, QString> reasons;
};

// Where a new resource opened in an anchor belongs, and which rule chose it.
struct LibraryResolution
{
    enum class Source
    {
        None,
        AnchorBinding,  // the anchor's own default worktree
        ActiveView,     // the worktree of the anchor's active view
        Explorer,       // the worktree selected in the Explorer
        Default,        // the Library/project default
    };
    QString libraryId;
    Source source = Source::None;
};

[[nodiscard]] QString librarySourceName(LibraryResolution::Source source);

struct SplitNode
{
    QString id;
    QString parentId;
    bool leaf = true;
    QString anchorId; // leaf only
    Orientation orientation = Orientation::Horizontal;
    QStringList children; // split only
    QList<double> sizes;  // split only, fractions summing to 1
};

struct ScreenInfo
{
    QString name;
    QRectF geometry;          // virtual desktop coordinates
    QRectF availableGeometry; // minus menu bar, dock, task bar
    double devicePixelRatio = 1.0;
    bool primary = false;
};

struct SurfaceRecord
{
    QString id;
    bool primary = false;
    QString rootNodeId;
    QString maximizedAnchorId;
    // Where the window is now, and where the user last put it. When a monitor
    // disappears the window is recovered onto another screen, but `preferred*`
    // keeps the original so it can go back when the monitor returns.
    QRectF geometry;
    QString screenName;
    QRectF preferredGeometry;
    QString preferredScreenName;
    bool windowMaximized = false;
    // A floating panel drawn inside the main window rather than a window of
    // its own. Its geometry is then in the main window's content (not the
    // desktop), and it has no screen.
    bool inMainWindow = false;
    // Docked in the sidebar: the rail tab it fills ("navigation.archive",
    // a person's own tab), or "sidebar.bottom" under whichever tab shows.
    // At most one surface per slot. Its geometry only keeps a size.
    QString sidebarSlot;
};

struct OpResult
{
    bool ok = true;
    QString code;    // machine readable: NOT_FOUND, INVALID_DROP, ...
    QString message; // human readable
    QString createdId; // anchor/view/surface created by the operation, if any

    static OpResult success(const QString &createdId = {})
    {
        OpResult result;
        result.createdId = createdId;
        return result;
    }
    static OpResult failure(const QString &code, const QString &message)
    {
        OpResult result;
        result.ok = false;
        result.code = code;
        result.message = message;
        return result;
    }
};

// A drag in progress: either one view (a tab) or a whole anchor.
struct DragSource
{
    enum class Kind
    {
        None,
        View,
        Anchor,
    };
    Kind kind = Kind::None;
    QString id;

    static DragSource view(const QString &viewId) { return {Kind::View, viewId}; }
    static DragSource anchor(const QString &anchorId) { return {Kind::Anchor, anchorId}; }
};

// Where a drag would land. `anchorId` empty with a non-center edge means the
// outer edge of the surface (dock along the whole window side).
struct DropTarget
{
    QString surfaceId;
    QString anchorId;
    Edge edge = Edge::Center;
    int index = -1; // tab insertion index for center drops, -1 = end

    [[nodiscard]] bool isValid() const { return !surfaceId.isEmpty(); }
    [[nodiscard]] bool isSurfaceEdge() const { return anchorId.isEmpty() && edge != Edge::Center; }
};

struct SplitterHandle
{
    QString nodeId;
    int index = 0; // between children[index] and children[index + 1]
    Orientation orientation = Orientation::Horizontal;
    QRectF rect;
};

struct LayoutGeometry
{
    QHash<QString, QRectF> anchors; // anchor id -> rect in surface coordinates
    QHash<QString, QRectF> nodes;   // split node id -> rect
    QList<SplitterHandle> splitters;
};

struct LayoutMetrics
{
    double minimumAnchorWidth = 180;
    double minimumAnchorHeight = 120;
    double splitterThickness = 6;
};
} // namespace Workspace

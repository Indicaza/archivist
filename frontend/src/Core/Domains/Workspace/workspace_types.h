#pragma once

#include <QHash>
#include <QList>
#include <QPointF>
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
    // A floating panel in the main window normally tucks itself into the
    // Well when the person works somewhere else; this keeps it out.
    bool leaveOnTop = false;
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

// Where a magnet sits now: recorded at pickup so it can be put back
// (Magnets::placeOf, Magnets::planReturn), and kept for a minimized view so
// it can be restored.
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

// ---- Gravity Wells -----------------------------------------------------
//
// A Gravity Well is a small collection of references kept within reach: views
// put away (minimized) and resources pinned for quick access. It owns no
// resource and no view record of its own: a minimized view stays a view of
// the model, with the same id and resource, only parked in a Well instead of
// a panel. One entry stands for one resource, so a pinned resource and its
// minimized view never show as two icons.

// A view put away in a Well, and where it came from.
struct MinimizedView
{
    QString viewId;
    Place place;
};

// A whole panel put away as one entry: what it was, so it comes back the
// same (tab order and the active tab come from the entry's views).
struct MinimizedPanel
{
    QString anchorId; // the panel it was (it may be gone by the time it's back)
    QString name;     // the name the user gave it, if any
    QString displayName; // what it was called when put away
    QString activeViewId;
    QStringList roles;
    QString libraryId;
    bool pinned = false;
    bool contentLocked = false;
    bool switchable = false;
};

struct WellEntry
{
    QString id;
    // What it stands for. Empty for a view with no resource (a welcome page):
    // that entry is the view's alone.
    ResourceRef resource;
    // How to open it again when nothing shows it (a pinned resource).
    QString typeId;
    QString title;
    bool pinned = false;
    // Its views put away here, oldest first. An entry that is not pinned
    // always has at least one.
    QList<MinimizedView> minimized;
    // A whole panel: its views (in tab order, any resources) come back
    // together, into one panel where it was. Never pinned, no resource.
    bool panel = false;
    MinimizedPanel panelInfo;
};

// How a Well lays out its entries. Only Linear is drawn today; the rest are
// room to grow (the geometry lives in QML, the model only names it).
enum class WellForm
{
    Linear,
};

[[nodiscard]] QString wellFormName(WellForm form);
[[nodiscard]] bool parseWellForm(const QString &name, WellForm *form);

struct WellRecord
{
    QString id;
    // The window it floats in; empty is the main window, whatever its
    // surface id (applying a layout gives the main window a new one).
    QString surfaceId;
    // Where it sits, as a share of its window: the middle of its long side,
    // so it stays put relative to the window as the window resizes. The
    // default is the bottom centre.
    QPointF position{0.5, 1.0};
    Orientation orientation = Orientation::Horizontal;
    WellForm form = WellForm::Linear;
    QList<WellEntry> entries;
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

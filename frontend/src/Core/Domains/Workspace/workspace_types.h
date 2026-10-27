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
};

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
    QList<SplitterHandle> splitters;
};

struct LayoutMetrics
{
    double minimumAnchorWidth = 180;
    double minimumAnchorHeight = 120;
    double splitterThickness = 6;
};
} // namespace Workspace

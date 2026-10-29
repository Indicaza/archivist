#include "Core/Domains/Workspace/view_type_registry.h"
#include "Core/Domains/Workspace/workspace_codec.h"
#include "Core/Domains/Workspace/workspace_commands.h"
#include "Core/Domains/Workspace/workspace_model.h"
#include "Core/Domains/Workspace/workspace_presets.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QSet>
#include <QtTest>

using namespace Workspace;

namespace
{
WorkspaceModel::IdGenerator counter()
{
    auto next = std::make_shared<int>(0);
    return [next](const QString &prefix) { return prefix + QString::number(++*next); };
}

ViewRecord editorView(const QString &fileId, const QString &libraryId = QStringLiteral("lib-main"))
{
    ViewRecord view;
    view.typeId = ViewTypes::Editor;
    view.resource.kind = QStringLiteral("document");
    view.resource.collectionId = QStringLiteral("col");
    view.resource.libraryId = libraryId;
    view.resource.id = QStringLiteral("col:") + libraryId + QLatin1Char(':') + fileId;
    view.title = fileId;
    return view;
}

ViewRecord chatView(const QString &chatId)
{
    ViewRecord view;
    view.typeId = ViewTypes::Chat;
    view.resource.kind = QStringLiteral("chat");
    view.resource.id = chatId;
    view.title = chatId;
    return view;
}

ViewRecord terminalView(const QString &sessionId, const QString &libraryId = QStringLiteral("lib-main"))
{
    ViewRecord view;
    view.typeId = ViewTypes::Terminal;
    view.resource.kind = QStringLiteral("terminal");
    view.resource.libraryId = libraryId;
    view.resource.id = libraryId + QLatin1Char(':') + sessionId;
    return view;
}

QByteArray snapshotBytes(const WorkspaceModel &model)
{
    return QJsonDocument(Codec::encodeSnapshot(model)).toJson(QJsonDocument::Compact);
}

QSet<QString> viewSet(const WorkspaceModel &model)
{
    const QStringList ids = model.viewIds();
    return QSet<QString>(ids.begin(), ids.end());
}

const QSizeF kSurface(1200, 800);
const LayoutMetrics kMetrics;
} // namespace

class CoreWorkspaceTest final : public QObject
{
    Q_OBJECT

private:
    // chat | (editor a, editor b) / terminal
    struct Arranged
    {
        WorkspaceModel model{counter()};
        QString first;  // anchor with chat
        QString editors;
        QString terminal;
        QString chat;
        QString a;
        QString b;
        QString term;
    };

    Arranged arranged()
    {
        Arranged result;
        result.first = result.model.anchorIds().first();
        result.chat = result.model.openView(chatView(QStringLiteral("chat-1")), result.first).createdId;
        result.a = result.model.openView(editorView(QStringLiteral("a.cpp")), result.first).createdId;
        result.b = result.model.openView(editorView(QStringLiteral("b.cpp")), result.first).createdId;
        result.term = result.model.openView(terminalView(QStringLiteral("t1")), result.first).createdId;
        result.editors = result.model.splitView(result.a, result.first, Edge::Right).createdId;
        result.model.moveView(result.b, result.editors, -1);
        result.terminal = result.model.splitView(result.term, result.editors, Edge::Bottom).createdId;
        return result;
    }

private slots:
    void freshWorkspaceIsValid()
    {
        WorkspaceModel model(counter());
        QVERIFY2(model.validate().isEmpty(), qPrintable(model.validate().join(QLatin1Char('\n'))));
        QCOMPARE(model.surfaceIds().size(), 1);
        QCOMPARE(model.anchorIds().size(), 1);
        const AnchorRecord *anchor = model.anchor(model.anchorIds().first());
        QVERIFY(anchor->pinned);
        QVERIFY(anchor->views.isEmpty());
        QCOMPARE(model.targetAnchorId(), anchor->id);
    }

    void openAndReorderTabs()
    {
        WorkspaceModel model(counter());
        const QString anchorId = model.anchorIds().first();
        const QString a = model.openView(editorView(QStringLiteral("a")), anchorId).createdId;
        const QString b = model.openView(editorView(QStringLiteral("b")), anchorId).createdId;
        const QString c = model.openView(editorView(QStringLiteral("c")), anchorId).createdId;
        QCOMPARE(model.anchor(anchorId)->views, (QStringList{a, b, c}));
        QCOMPARE(model.anchor(anchorId)->activeViewId, c);

        QVERIFY(model.moveView(c, anchorId, 0).ok);
        QCOMPARE(model.anchor(anchorId)->views, (QStringList{c, a, b}));
        QVERIFY(model.moveView(c, anchorId, 2).ok);
        QCOMPARE(model.anchor(anchorId)->views, (QStringList{a, b, c}));

        // Opening "after the active tab".
        QVERIFY(model.activateView(a).ok);
        const QString d = model.openView(editorView(QStringLiteral("d")), anchorId).createdId;
        QCOMPARE(model.anchor(anchorId)->views, (QStringList{a, d, b, c}));
        QVERIFY(model.validate().isEmpty());
    }

    void movingTheLastTabCollapsesIncidentalAnchors()
    {
        Arranged s = arranged();
        QCOMPARE(s.model.anchorIds().size(), 3);
        QVERIFY(!s.model.anchor(s.terminal)->pinned);

        QVERIFY(s.model.moveView(s.term, s.editors, 0).ok);
        QVERIFY(!s.model.anchor(s.terminal));
        QCOMPARE(s.model.anchorIds().size(), 2);
        QCOMPARE(s.model.anchor(s.editors)->views.first(), s.term);
        QVERIFY(s.model.validate().isEmpty());
    }

    void pinnedAnchorsSurviveLosingTheirLastTab()
    {
        WorkspaceModel model(counter());
        const QString first = model.anchorIds().first();
        const QString view = model.openView(chatView(QStringLiteral("c")), first).createdId;
        const QString empty = model.createAnchor(first, Edge::Right).createdId;
        QVERIFY(model.anchor(empty)->pinned);
        QVERIFY(model.moveView(view, empty, -1).ok);
        QVERIFY(model.anchor(first)); // pinned, kept although empty
        QCOMPARE(model.anchorIds().size(), 2);
        QVERIFY(model.validate().isEmpty());
    }

    void splitsNestAndFlatten()
    {
        Arranged s = arranged();
        // Root: horizontal [first, vertical[editors, terminal]]
        const SurfaceRecord *surface = s.model.surface(s.model.primarySurfaceId());
        const SplitNode *root = s.model.node(surface->rootNodeId);
        QVERIFY(!root->leaf);
        QCOMPARE(root->orientation, Orientation::Horizontal);
        QCOMPARE(root->children.size(), 2);
        const SplitNode *right = s.model.node(root->children.at(1));
        QCOMPARE(right->orientation, Orientation::Vertical);

        // Splitting `first` to the right again inserts into the existing row
        // rather than nesting a second horizontal split.
        const QString extra = s.model.openView(chatView(QStringLiteral("chat-2")), s.first).createdId;
        const QString created = s.model.splitView(extra, s.first, Edge::Right).createdId;
        root = s.model.node(s.model.surface(s.model.primarySurfaceId())->rootNodeId);
        QCOMPARE(root->children.size(), 3);
        QCOMPARE(s.model.leafOfAnchor(created), root->children.at(1));

        // Removing the middle column of a vertical split collapses it.
        QVERIFY(s.model.moveView(s.term, s.editors, -1).ok);
        root = s.model.node(s.model.surface(s.model.primarySurfaceId())->rootNodeId);
        for (const QString &child : root->children) {
            QVERIFY(s.model.node(child)->leaf);
        }
        QVERIFY(s.model.validate().isEmpty());
    }

    void invalidOperationsLeaveTheModelUntouched()
    {
        Arranged s = arranged();
        const QByteArray before = snapshotBytes(s.model);
        const int revision = s.model.revision();

        QCOMPARE(s.model.splitView(s.term, s.terminal, Edge::Left).code, QStringLiteral("INVALID_DROP"));
        QCOMPARE(s.model.mergeAnchor(s.editors, s.editors).code, QStringLiteral("INVALID_DROP"));
        QCOMPARE(s.model.moveAnchor(s.editors, s.editors, Edge::Top).code, QStringLiteral("INVALID_DROP"));
        QCOMPARE(s.model.moveView(QStringLiteral("nope"), s.editors).code, QStringLiteral("NOT_FOUND"));
        QCOMPARE(s.model.moveView(s.a, QStringLiteral("nope")).code, QStringLiteral("NOT_FOUND"));
        QCOMPARE(s.model.openView(ViewRecord(), s.first).code, QStringLiteral("INVALID_VIEW"));
        QCOMPARE(s.model.closeSurface(s.model.primarySurfaceId()).code, QStringLiteral("PRIMARY_SURFACE"));
        QCOMPARE(s.model.setSplitSizes(s.model.surface(s.model.primarySurfaceId())->rootNodeId, {1.0}).code, QStringLiteral("INVALID_SIZES"));
        QCOMPARE(s.model.setSplitSizes(s.model.surface(s.model.primarySurfaceId())->rootNodeId, {1.0, -1.0}).code, QStringLiteral("INVALID_SIZES"));
        QCOMPARE(s.model.dockViewAtSurfaceEdge(s.a, s.model.primarySurfaceId(), Edge::Center).code, QStringLiteral("INVALID_DROP"));

        ViewRecord duplicateId = chatView(QStringLiteral("x"));
        duplicateId.id = s.a;
        QCOMPARE(s.model.openView(duplicateId, s.first).code, QStringLiteral("DUPLICATE_ID"));

        QCOMPARE(snapshotBytes(s.model), before);
        QCOMPARE(s.model.revision(), revision);
    }

    void movingAndMergingAnchorsKeepsEveryView()
    {
        Arranged s = arranged();
        const QSet<QString> views = viewSet(s.model);
        const QStringList editorTabs = s.model.anchor(s.editors)->views;

        QVERIFY(s.model.moveAnchor(s.editors, s.first, Edge::Left).ok);
        QCOMPARE(s.model.anchor(s.editors)->views, editorTabs); // tabs travel together
        QCOMPARE(viewSet(s.model), views);

        QVERIFY(s.model.mergeAnchor(s.terminal, s.editors).ok);
        QVERIFY(!s.model.anchor(s.terminal));
        QCOMPARE(s.model.anchor(s.editors)->views, editorTabs + QStringList{s.term});
        QCOMPARE(s.model.anchor(s.editors)->activeViewId, s.term);
        QCOMPARE(viewSet(s.model), views);
        QVERIFY(s.model.validate().isEmpty());
    }

    void duplicatesShareTheResource()
    {
        Arranged s = arranged();
        const QString copy = s.model.duplicateView(s.a, QString(), Edge::Right).createdId;
        QVERIFY(!copy.isEmpty());
        QVERIFY(copy != s.a);
        const ViewRecord *original = s.model.view(s.a);
        const ViewRecord *duplicate = s.model.view(copy);
        QCOMPARE(duplicate->resource, original->resource);
        QVERIFY(s.model.anchorOfView(copy) != s.model.anchorOfView(s.a));
        QCOMPARE(s.model.viewsForResource(QStringLiteral("document"), original->resource.id).size(), 2);
        const QString resourceId = duplicate->resource.id; // the records move when views close

        // Closing one view leaves the other (and the resource) alone.
        QVERIFY(s.model.closeView(s.a).ok);
        QCOMPARE(s.model.viewsForResource(QStringLiteral("document"), resourceId), QStringList{copy});
        QVERIFY(s.model.validate().isEmpty());
    }

    void worktreeBelongsToTheView()
    {
        WorkspaceModel model(counter());
        const QString first = model.anchorIds().first();
        const QString mainView = model.openView(editorView(QStringLiteral("x.cpp"), QStringLiteral("lib-main")), first).createdId;
        const QString agentView = model.openView(editorView(QStringLiteral("x.cpp"), QStringLiteral("lib-agent-a")), first).createdId;
        QVERIFY(model.splitView(agentView, first, Edge::Right).ok);
        QVERIFY(model.focusAnchor(model.anchorOfView(agentView)).ok);
        // Focus moving never rewrites which worktree a view shows.
        QCOMPARE(model.view(mainView)->resource.libraryId, QStringLiteral("lib-main"));
        QCOMPARE(model.view(agentView)->resource.libraryId, QStringLiteral("lib-agent-a"));
        QVERIFY(model.view(mainView)->resource.id != model.view(agentView)->resource.id);
    }

    void closingViewsAndAnchors()
    {
        Arranged s = arranged();
        QVERIFY(s.model.closeAnchor(s.terminal).ok);
        QVERIFY(!s.model.view(s.term));
        QVERIFY(!s.model.anchor(s.terminal));

        QVERIFY(s.model.closeAnchor(s.editors).ok);
        QVERIFY(s.model.closeAnchor(s.first).ok);
        // The main window keeps one (empty) anchor.
        QCOMPARE(s.model.anchorIds().size(), 1);
        QVERIFY(s.model.viewIds().isEmpty());
        QVERIFY(s.model.validate().isEmpty());
    }

    void detachingAndClosingWindowsNeverLosesViews()
    {
        Arranged s = arranged();
        const QSet<QString> views = viewSet(s.model);

        const QString floating = s.model.detachAnchor(s.terminal, QRectF(100, 100, 600, 400), QStringLiteral("DELL")).createdId;
        QCOMPARE(s.model.surfaceIds().size(), 2);
        QCOMPARE(s.model.surfaceOfAnchor(s.terminal), floating);
        QVERIFY(!s.model.surface(floating)->primary);

        const QString another = s.model.detachView(s.b, QRectF(300, 300, 500, 300), QString()).createdId;
        QCOMPARE(s.model.surfaceIds().size(), 3);
        QCOMPARE(s.model.surfaceOfAnchor(s.model.anchorOfView(s.b)), another);
        QCOMPARE(viewSet(s.model), views);

        // Docking back between windows.
        QVERIFY(s.model.moveView(s.b, s.terminal, -1).ok);
        QVERIFY(!s.model.surface(another)); // emptied floating window goes away
        QCOMPARE(s.model.surfaceOfAnchor(s.model.anchorOfView(s.b)), floating);

        QVERIFY(s.model.closeSurface(floating).ok);
        QCOMPARE(s.model.surfaceIds().size(), 1);
        QCOMPARE(viewSet(s.model), views);
        QVERIFY(s.model.validate().isEmpty());
    }

    void movingTheLastPrimaryAnchorAwayLeavesAnEmptyAnchor()
    {
        WorkspaceModel model(counter());
        const QString first = model.anchorIds().first();
        model.openView(chatView(QStringLiteral("c")), first);
        const QString floating = model.detachAnchor(first, QRectF(0, 0, 400, 300)).createdId;
        QVERIFY(!floating.isEmpty());
        QCOMPARE(model.anchorIds(model.primarySurfaceId()).size(), 1);
        QVERIFY(model.anchor(model.anchorIds(model.primarySurfaceId()).first())->views.isEmpty());
        QVERIFY(model.validate().isEmpty());
    }

    void dropRulesMatchOperations()
    {
        Arranged s = arranged();
        const QString primary = s.model.primarySurfaceId();

        QVERIFY(!s.model.canDrop(DragSource::view(s.term), {primary, s.terminal, Edge::Left}).ok);
        QVERIFY(s.model.canDrop(DragSource::view(s.term), {primary, s.terminal, Edge::Center}).ok);
        QVERIFY(!s.model.canDrop(DragSource::anchor(s.editors), {primary, s.editors, Edge::Center}).ok);
        QVERIFY(s.model.canDrop(DragSource::anchor(s.editors), {primary, QString(), Edge::Left}).ok);
        QVERIFY(!s.model.canDrop(DragSource::anchor(s.editors), {QStringLiteral("nope"), s.first, Edge::Left}).ok);

        QVERIFY(s.model.applyDrop(DragSource::anchor(s.terminal), {primary, QString(), Edge::Bottom}).ok);
        const SplitNode *root = s.model.node(s.model.surface(primary)->rootNodeId);
        QCOMPARE(root->orientation, Orientation::Vertical);
        QCOMPARE(s.model.leafOfAnchor(s.terminal), root->children.last());
        QVERIFY(s.model.validate().isEmpty());
    }

    void previewDoesNotMutate()
    {
        Arranged s = arranged();
        const QByteArray before = snapshotBytes(s.model);
        QRectF landing;
        const LayoutGeometry preview = s.model.previewDrop(
            DragSource::view(s.chat),
            {s.model.primarySurfaceId(), s.editors, Edge::Top},
            kSurface,
            kMetrics,
            &landing
        );
        QCOMPARE(snapshotBytes(s.model), before);
        QVERIFY(landing.isValid());
        QVERIFY(landing.width() > 0 && landing.height() > 0);
        // The editors anchor moves down to make room for the dropped tab.
        const LayoutGeometry now = s.model.computeGeometry(s.model.primarySurfaceId(), kSurface, kMetrics);
        QVERIFY(preview.anchors.value(s.editors).top() > now.anchors.value(s.editors).top());
        QVERIFY(qAbs(landing.top() - now.anchors.value(s.editors).top()) < 1);

        // Invalid previews land nowhere and change nothing.
        QRectF nowhere(1, 1, 1, 1);
        static_cast<void>(s.model.previewDrop(DragSource::view(s.term), {s.model.primarySurfaceId(), s.terminal, Edge::Left}, kSurface, kMetrics, &nowhere));
        QVERIFY(!nowhere.isValid());
        QCOMPARE(snapshotBytes(s.model), before);
    }

    void dropZones()
    {
        Arranged s = arranged();
        const QString primary = s.model.primarySurfaceId();
        const LayoutGeometry geometry = s.model.computeGeometry(primary, kSurface, kMetrics);
        const QRectF editors = geometry.anchors.value(s.editors);

        DropTarget center = s.model.dropTargetAt(primary, editors.center(), kSurface, kMetrics);
        QCOMPARE(center.anchorId, s.editors);
        QCOMPARE(center.edge, Edge::Center);

        DropTarget top = s.model.dropTargetAt(primary, QPointF(editors.center().x(), editors.top() + 25), kSurface, kMetrics);
        QCOMPARE(top.anchorId, s.editors);
        QCOMPARE(top.edge, Edge::Top);

        DropTarget leftOfEditors = s.model.dropTargetAt(primary, QPointF(editors.left() + 20, editors.center().y()), kSurface, kMetrics);
        QCOMPARE(leftOfEditors.edge, Edge::Left);

        DropTarget windowEdge = s.model.dropTargetAt(primary, QPointF(4, 400), kSurface, kMetrics);
        QVERIFY(windowEdge.isSurfaceEdge());
        QCOMPARE(windowEdge.edge, Edge::Left);

        QVERIFY(!s.model.dropTargetAt(primary, QPointF(-5, 10), kSurface, kMetrics).isValid());
    }

    void geometryHonoursMinimumsAndMaximize()
    {
        Arranged s = arranged();
        const QString primary = s.model.primarySurfaceId();
        const QString root = s.model.surface(primary)->rootNodeId;
        QVERIFY(s.model.setSplitSizes(root, {0.98, 0.02}).ok);

        LayoutGeometry geometry = s.model.computeGeometry(primary, kSurface, kMetrics);
        QCOMPARE(geometry.anchors.size(), 3);
        QVERIFY(geometry.anchors.value(s.editors).width() >= kMetrics.minimumAnchorWidth - 0.01);
        QCOMPARE(geometry.splitters.size(), 2);

        // Everything still fits the window exactly.
        double width = geometry.anchors.value(s.first).width() + geometry.anchors.value(s.editors).width() + kMetrics.splitterThickness;
        QVERIFY(qAbs(width - kSurface.width()) < 0.01);

        // Too small for minimums: shrink proportionally, never overflow.
        geometry = s.model.computeGeometry(primary, QSizeF(200, 150), kMetrics);
        for (const QRectF &rect : geometry.anchors) {
            QVERIFY(rect.right() <= 200.01);
            QVERIFY(rect.bottom() <= 150.01);
        }

        QVERIFY(s.model.maximizeAnchor(s.terminal).ok);
        geometry = s.model.computeGeometry(primary, kSurface, kMetrics);
        QCOMPARE(geometry.anchors.size(), 1);
        QCOMPARE(geometry.anchors.value(s.terminal), QRectF(QPointF(0, 0), kSurface));
        QVERIFY(s.model.restoreAnchor(primary).ok);
        QCOMPARE(s.model.computeGeometry(primary, kSurface, kMetrics).anchors.size(), 3);

        // Closing the maximized anchor clears maximize.
        QVERIFY(s.model.maximizeAnchor(s.terminal).ok);
        QVERIFY(s.model.closeAnchor(s.terminal).ok);
        QVERIFY(s.model.surface(primary)->maximizedAnchorId.isEmpty());
    }

    void resizingSplitsHonoursNeighbourMinimums()
    {
        Arranged s = arranged();
        const QString primary = s.model.primarySurfaceId();
        const QString root = s.model.surface(primary)->rootNodeId;
        const int before = s.model.revision();

        // Drag the vertical divider to x = 400.
        QVERIFY(s.model.resizeSplit(primary, root, 0, 400, kSurface, kMetrics).ok);
        LayoutGeometry geometry = s.model.computeGeometry(primary, kSurface, kMetrics);
        QVERIFY2(qAbs(geometry.anchors.value(s.first).width() - 400) < 1.0, qPrintable(QString::number(geometry.anchors.value(s.first).width())));
        QVERIFY(s.model.revision() > before);

        // Far left: clamps to the left anchor's minimum width.
        QVERIFY(s.model.resizeSplit(primary, root, 0, 5, kSurface, kMetrics).ok);
        geometry = s.model.computeGeometry(primary, kSurface, kMetrics);
        QVERIFY(geometry.anchors.value(s.first).width() >= kMetrics.minimumAnchorWidth - 0.5);

        // Far right: clamps to the right side's minimum.
        QVERIFY(s.model.resizeSplit(primary, root, 0, 5000, kSurface, kMetrics).ok);
        geometry = s.model.computeGeometry(primary, kSurface, kMetrics);
        QVERIFY(geometry.anchors.value(s.editors).width() >= kMetrics.minimumAnchorWidth - 0.5);
        QVERIFY(s.model.validate().isEmpty());

        // Bad requests change nothing.
        const QByteArray snapshot = snapshotBytes(s.model);
        QVERIFY(!s.model.resizeSplit(primary, root, 3, 400, kSurface, kMetrics).ok);
        QVERIFY(!s.model.resizeSplit(primary, QStringLiteral("nope"), 0, 400, kSurface, kMetrics).ok);
        QCOMPARE(snapshotBytes(s.model), snapshot);
    }

    void changingViewTypeKeepsPlaceAndResource()
    {
        Arranged s = arranged();
        const ResourceRef resource = s.model.view(s.a)->resource;
        const int index = int(s.model.anchor(s.editors)->views.indexOf(s.a));

        QVERIFY(s.model.setViewType(s.a, ViewTypes::Preview).ok);
        QCOMPARE(s.model.view(s.a)->typeId, ViewTypes::Preview);
        QCOMPARE(s.model.view(s.a)->resource, resource);
        QCOMPARE(s.model.anchorOfView(s.a), s.editors);
        QCOMPARE(int(s.model.anchor(s.editors)->views.indexOf(s.a)), index);

        QVERIFY(!s.model.setViewType(s.a, QString()).ok);
        QVERIFY(!s.model.setViewType(QStringLiteral("missing"), ViewTypes::Editor).ok);
        QVERIFY(s.model.validate().isEmpty());
    }

    void keyboardNavigation()
    {
        Arranged s = arranged();
        QCOMPARE(s.model.neighborAnchor(s.first, Edge::Right, kSurface, kMetrics), s.editors);
        QCOMPARE(s.model.neighborAnchor(s.editors, Edge::Bottom, kSurface, kMetrics), s.terminal);
        QCOMPARE(s.model.neighborAnchor(s.terminal, Edge::Top, kSurface, kMetrics), s.editors);
        QCOMPARE(s.model.neighborAnchor(s.terminal, Edge::Left, kSurface, kMetrics), s.first);
        QVERIFY(s.model.neighborAnchor(s.first, Edge::Left, kSurface, kMetrics).isEmpty());

        const QStringList order = s.model.anchorIds();
        QCOMPARE(s.model.cycleAnchor(order.last(), 1), order.first());
        QCOMPARE(s.model.cycleAnchor(order.first(), -1), order.last());
    }

    void targetAnchorForNewContent()
    {
        Arranged s = arranged();
        QVERIFY(s.model.setTargetAnchor(s.terminal).ok);
        QVERIFY(s.model.focusAnchor(s.first).ok);
        const QString opened = s.model.openView(editorView(QStringLiteral("new.cpp"))).createdId;
        QCOMPARE(s.model.anchorOfView(opened), s.terminal);

        QVERIFY(s.model.closeAnchor(s.terminal).ok);
        QVERIFY(s.model.explicitTargetAnchorId().isEmpty());
        QCOMPARE(s.model.targetAnchorId(), s.model.focusedAnchorId());
    }

    void monitorRecovery()
    {
        Arranged s = arranged();
        const QString floating = s.model.detachAnchor(s.terminal, QRectF(2200, 100, 800, 600), QStringLiteral("Studio Display")).createdId;

        ScreenInfo laptop{QStringLiteral("Built-in"), QRectF(0, 0, 1512, 982), QRectF(0, 25, 1512, 957), 2.0, true};
        ScreenInfo studio{QStringLiteral("Studio Display"), QRectF(1512, 0, 2560, 1440), QRectF(1512, 25, 2560, 1415), 2.0, false};

        // Both connected: nothing moves.
        QVERIFY(s.model.recoverSurfaces({laptop, studio}).isEmpty());

        // The external monitor goes away.
        QCOMPARE(s.model.recoverSurfaces({laptop}), QStringList{floating});
        const SurfaceRecord *record = s.model.surface(floating);
        QVERIFY(laptop.availableGeometry.contains(record->geometry));
        QCOMPARE(record->screenName, QStringLiteral("Built-in"));
        QCOMPARE(record->preferredScreenName, QStringLiteral("Studio Display"));

        // It comes back: the window returns to where the user put it.
        QCOMPARE(s.model.recoverSurfaces({laptop, studio}), QStringList{floating});
        QCOMPARE(s.model.surface(floating)->geometry, QRectF(2200, 100, 800, 600));
        QCOMPARE(s.model.surface(floating)->screenName, QStringLiteral("Studio Display"));

        // A huge window is shrunk to fit a small screen.
        QVERIFY(s.model.setSurfaceGeometry(floating, QRectF(5000, 0, 3000, 2000), QStringLiteral("Gone")).ok);
        s.model.recoverSurfaces({laptop});
        record = s.model.surface(floating);
        QVERIFY(record->geometry.width() <= laptop.availableGeometry.width());
        QVERIFY(laptop.availableGeometry.contains(record->geometry));
    }

    void snapshotRoundTrip()
    {
        Arranged s = arranged();
        s.model.detachView(s.b, QRectF(50, 60, 700, 500), QStringLiteral("Built-in"));
        ViewRecord withState = *s.model.view(s.a);
        s.model.updateViewState(s.a, QVariantMap{{QStringLiteral("cursor"), QVariantMap{{QStringLiteral("line"), 42}}}});
        s.model.setTargetAnchor(s.editors);
        s.model.maximizeAnchor(s.terminal);

        const QJsonObject snapshot = Codec::encodeSnapshot(s.model);
        WorkspaceModel restored(counter());
        const Codec::LoadReport report = Codec::decodeSnapshot(snapshot, ViewTypeRegistry::withBuiltIns(), restored);
        QVERIFY2(report.ok, qPrintable(report.error));
        QVERIFY(report.warnings.isEmpty());
        QCOMPARE(snapshotBytes(restored), snapshotBytes(s.model));
        QCOMPARE(restored.view(s.a)->state.value(QStringLiteral("cursor")).toMap().value(QStringLiteral("line")).toInt(), 42);
        QCOMPARE(restored.surfaceIds().size(), 2);
        QCOMPARE(restored.explicitTargetAnchorId(), s.editors);
        QVERIFY(restored.validate().isEmpty());
    }

    void forgivingLoads()
    {
        Arranged s = arranged();
        QJsonObject snapshot = Codec::encodeSnapshot(s.model);

        // A plugin view whose plugin is gone is kept, not dropped.
        QJsonObject session = snapshot.value(QStringLiteral("session")).toObject();
        QJsonArray views = session.value(QStringLiteral("views")).toArray();
        QJsonObject plugin = Codec::encodeView(chatView(QStringLiteral("graph")));
        plugin.insert(QStringLiteral("id"), QStringLiteral("plugin-view"));
        plugin.insert(QStringLiteral("type"), QStringLiteral("plugin.acme.graph"));
        views.append(plugin);
        session.insert(QStringLiteral("views"), views);

        // A view whose anchor vanished goes to the target anchor.
        QJsonArray anchors = session.value(QStringLiteral("anchors")).toArray();
        anchors.append(QJsonObject{
            {QStringLiteral("id"), QStringLiteral("ghost-anchor")},
            {QStringLiteral("views"), QJsonArray{QStringLiteral("plugin-view")}},
        });
        session.insert(QStringLiteral("anchors"), anchors);
        snapshot.insert(QStringLiteral("session"), session);

        WorkspaceModel restored(counter());
        const Codec::LoadReport report = Codec::decodeSnapshot(snapshot, ViewTypeRegistry::withBuiltIns(), restored);
        QVERIFY2(report.ok, qPrintable(report.error));
        QVERIFY(restored.view(QStringLiteral("plugin-view")));
        QVERIFY(!report.warnings.isEmpty());
        QCOMPARE(restored.viewIds().size(), s.model.viewIds().size() + 1);
    }

    void refusedLoadsKeepTheCurrentWorkspace()
    {
        Arranged s = arranged();
        const QByteArray before = snapshotBytes(s.model);
        QJsonObject newer = Codec::encodeSnapshot(s.model);
        newer.insert(QStringLiteral("version"), Codec::kSnapshotVersion + 1);
        Codec::LoadReport report = Codec::decodeSnapshot(newer, ViewTypeRegistry::withBuiltIns(), s.model);
        QVERIFY(!report.ok);
        QCOMPARE(snapshotBytes(s.model), before);

        report = Codec::decodeSnapshot(QJsonObject{{QStringLiteral("format"), QStringLiteral("nonsense")}}, ViewTypeRegistry::withBuiltIns(), s.model);
        QVERIFY(!report.ok);

        QJsonObject empty = Codec::encodeSnapshot(s.model);
        empty.insert(QStringLiteral("layout"), QJsonObject{{QStringLiteral("surfaces"), QJsonArray()}});
        report = Codec::decodeSnapshot(empty, ViewTypeRegistry::withBuiltIns(), s.model);
        QVERIFY(!report.ok);
        QCOMPARE(snapshotBytes(s.model), before);
    }

    void stateMigration()
    {
        ViewTypeRegistry registry = ViewTypeRegistry::withBuiltIns();
        ViewTypeDescriptor graph;
        graph.typeId = QStringLiteral("plugin.acme.graph");
        graph.stateVersion = 3;
        graph.migrateState = [](const QVariantMap &state, int from) {
            QVariantMap upgraded = state;
            if (from < 2) {
                upgraded.insert(QStringLiteral("zoom"), state.value(QStringLiteral("scale"), 1.0));
                upgraded.remove(QStringLiteral("scale"));
            }
            upgraded.insert(QStringLiteral("grid"), true);
            return upgraded;
        };
        QVERIFY(registry.registerType(graph).ok);

        ViewRecord old;
        old.id = QStringLiteral("v");
        old.typeId = graph.typeId;
        old.stateVersion = 1;
        old.state = {{QStringLiteral("scale"), 2.5}};
        bool migrated = false;
        const ViewRecord upgraded = registry.migrate(old, &migrated);
        QVERIFY(migrated);
        QCOMPARE(upgraded.stateVersion, 3);
        QCOMPARE(upgraded.state.value(QStringLiteral("zoom")).toDouble(), 2.5);
        QVERIFY(upgraded.state.value(QStringLiteral("grid")).toBool());

        // Newer than we know: left alone.
        ViewRecord future = old;
        future.stateVersion = 9;
        QCOMPARE(registry.migrate(future, &migrated).state, future.state);
        QVERIFY(!migrated);
    }

    void registryContract()
    {
        ViewTypeRegistry registry = ViewTypeRegistry::withBuiltIns();
        QVERIFY(registry.contains(ViewTypes::Editor));
        QVERIFY(registry.find(ViewTypes::Editor)->worktreeBound);
        QVERIFY(!registry.find(ViewTypes::Chat)->worktreeBound);

        ViewTypeDescriptor bad;
        bad.typeId = QStringLiteral("Not An Id");
        QCOMPARE(registry.registerType(bad).code, QStringLiteral("INVALID_TYPE_ID"));

        ViewTypeDescriptor duplicate;
        duplicate.typeId = ViewTypes::Chat;
        QCOMPARE(registry.registerType(duplicate).code, QStringLiteral("DUPLICATE_TYPE"));

        ViewTypeDescriptor future;
        future.typeId = QStringLiteral("plugin.acme.future");
        future.contractVersion = kViewContractVersion + 1;
        QCOMPARE(registry.registerType(future).code, QStringLiteral("CONTRACT_VERSION"));

        ViewTypeDescriptor plugin;
        plugin.typeId = QStringLiteral("plugin.acme.theater");
        QVERIFY(registry.registerType(plugin).ok);
        QVERIFY(registry.unregisterType(plugin.typeId));
        QVERIFY(!registry.contains(plugin.typeId));
    }

    void presetsArrangeWithoutLosingAnything()
    {
        const QList<Presets::Preset> presets = Presets::all();
        QCOMPARE(presets.size(), 6);

        for (const Presets::Preset &preset : presets) {
            Arranged s = arranged();
            const QSet<QString> views = viewSet(s.model);
            const Codec::LoadReport report = Codec::applyLayout(preset.layout, s.model);
            QVERIFY2(report.ok, qPrintable(preset.id + QStringLiteral(": ") + report.error));
            QCOMPARE(viewSet(s.model), views);
            QVERIFY2(s.model.validate().isEmpty(), qPrintable(preset.id));

            // Terminals go where the preset asked for terminals.
            const QString terminalAnchor = s.model.anchorOfView(s.term);
            QVERIFY2(s.model.anchor(terminalAnchor)->roles.contains(ViewTypes::Terminal), qPrintable(preset.id));

            // Presets also apply to an empty workspace; every slot stays.
            WorkspaceModel empty(counter());
            QVERIFY(Codec::applyLayout(preset.layout, empty).ok);
            QVERIFY(empty.anchorIds().size() >= 3);
            QVERIFY(!empty.explicitTargetAnchorId().isEmpty());
        }
    }

    void observatorySpreadsChats()
    {
        WorkspaceModel model(counter());
        const QString first = model.anchorIds().first();
        QStringList chats;
        for (int i = 0; i < 3; ++i) {
            chats.append(model.openView(chatView(QStringLiteral("agent-%1").arg(i)), first).createdId);
        }
        QVERIFY(Codec::applyLayout(Presets::layout(QStringLiteral("agent-observatory")), model).ok);
        QSet<QString> anchors;
        for (const QString &chat : chats) {
            anchors.insert(model.anchorOfView(chat));
        }
        QCOMPARE(anchors.size(), 3);
    }

    void switchingLayoutsAndBack()
    {
        Arranged s = arranged();
        const QString aAnchor = s.model.anchorOfView(s.a);
        const QString chatAnchor = s.model.anchorOfView(s.chat);
        const QJsonObject mine = Codec::captureLayout(s.model, QStringLiteral("Mine"));
        QVERIFY(Codec::isLayout(mine));

        QVERIFY(Codec::applyLayout(Presets::layout(QStringLiteral("code-review")), s.model).ok);
        QVERIFY(Codec::applyLayout(Presets::layout(QStringLiteral("research")), s.model).ok);
        QVERIFY(Codec::applyLayout(mine, s.model).ok);

        QCOMPARE(s.model.anchorOfView(s.a), aAnchor);
        QCOMPARE(s.model.anchorOfView(s.chat), chatAnchor);
        QCOMPARE(s.model.anchorOfView(s.a), s.model.anchorOfView(s.b));
        QVERIFY(s.model.validate().isEmpty());

        // Floating windows in a layout come back too.
        s.model.detachAnchor(s.terminal, QRectF(10, 20, 640, 480), QStringLiteral("Built-in"));
        const QJsonObject withWindow = Codec::captureLayout(s.model, QStringLiteral("Window"));
        QVERIFY(Codec::applyLayout(Presets::layout(QStringLiteral("development")), s.model).ok);
        QCOMPARE(s.model.surfaceIds().size(), 1);
        QVERIFY(Codec::applyLayout(withWindow, s.model).ok);
        QCOMPARE(s.model.surfaceIds().size(), 2);
        QCOMPARE(s.model.surfaceOfAnchor(s.model.anchorOfView(s.term)), s.model.surfaceIds().last());
    }

    void deeplyNestedSplits()
    {
        WorkspaceModel model(counter());
        QString anchorId = model.anchorIds().first();
        model.openView(chatView(QStringLiteral("root")), anchorId);

        for (int i = 0; i < 40; ++i) {
            const QString view = model.openView(editorView(QStringLiteral("f%1").arg(i)), anchorId).createdId;
            const OpResult split = model.splitView(view, anchorId, i % 2 == 0 ? Edge::Right : Edge::Bottom);
            QVERIFY2(split.ok, qPrintable(split.message));
            anchorId = split.createdId;
        }

        QCOMPARE(model.anchorIds().size(), 41);
        QVERIFY(model.validate().isEmpty());
        const LayoutGeometry geometry = model.computeGeometry(model.primarySurfaceId(), QSizeF(2560, 1440), kMetrics);
        QCOMPARE(geometry.anchors.size(), 41);

        // Round-trips through the codec.
        WorkspaceModel restored(counter());
        QVERIFY(Codec::decodeSnapshot(Codec::encodeSnapshot(model), ViewTypeRegistry::withBuiltIns(), restored).ok);
        QCOMPARE(snapshotBytes(restored), snapshotBytes(model));
    }

    void rapidDockingDoesNotLeak()
    {
        Arranged s = arranged();
        const qsizetype nodes = s.model.data().nodes.size();
        const QString primary = s.model.primarySurfaceId();

        for (int i = 0; i < 500; ++i) {
            const Edge edge = Edge(1 + i % 4);
            QVERIFY(s.model.moveAnchor(s.terminal, i % 2 ? s.first : s.editors, edge).ok);
        }

        QVERIFY(s.model.applyDrop(DragSource::anchor(s.terminal), {primary, s.editors, Edge::Bottom}).ok);
        QVERIFY(s.model.applyDrop(DragSource::anchor(s.editors), {primary, s.first, Edge::Right}).ok);
        QVERIFY(s.model.validate().isEmpty());
        QVERIFY(s.model.data().nodes.size() <= nodes + 1);
    }

    void randomOperationsKeepInvariants()
    {
        QRandomGenerator random(20261027);
        WorkspaceModel model(counter());
        int opened = 0;
        int closed = 0;

        for (int step = 0; step < 2500; ++step) {
            const QStringList anchors = model.anchorIds();
            const QStringList views = model.viewIds();
            const QString anyAnchor = anchors.at(random.bounded(int(anchors.size())));
            const QString anyView = views.isEmpty() ? QString() : views.at(random.bounded(int(views.size())));
            const Edge edge = Edge(random.bounded(5));
            OpResult result;

            switch (random.bounded(15)) {
            case 0:
            case 1:
                result = model.openView(editorView(QStringLiteral("f%1").arg(step)), anyAnchor);
                opened += result.ok ? 1 : 0;
                break;
            case 2: result = model.moveView(anyView, anyAnchor, random.bounded(4) - 1); break;
            case 3: result = model.splitView(anyView, anyAnchor, edge); break;
            case 4: result = model.moveAnchor(anchors.at(random.bounded(int(anchors.size()))), anyAnchor, edge); break;
            case 5: result = model.detachView(anyView, QRectF(0, 0, 400, 300)); break;
            case 6: result = model.detachAnchor(anyAnchor, QRectF(0, 0, 400, 300)); break;
            case 7: {
                const QStringList surfaces = model.surfaceIds();
                result = model.closeSurface(surfaces.at(random.bounded(int(surfaces.size()))));
                break;
            }
            case 8: result = model.dockAnchorAtSurfaceEdge(anyAnchor, model.surfaceIds().first(), edge); break;
            case 9: result = model.createAnchor(anyAnchor, edge); break;
            case 10:
                if (!anyView.isEmpty() && random.bounded(4) == 0) {
                    result = model.closeView(anyView);
                    closed += result.ok ? 1 : 0;
                }
                break;
            case 11: result = model.duplicateView(anyView, anyAnchor, edge); opened += result.ok ? 1 : 0; break;
            case 12: result = model.applyDrop(DragSource::anchor(anyAnchor), model.dropTargetAt(model.surfaceIds().first(), QPointF(random.bounded(1200), random.bounded(800)), kSurface, kMetrics)); break;
            // Locks come and go; every operation above must keep the model
            // valid around them.
            case 13: result = model.setAnchorLayoutLocked(anyAnchor, random.bounded(3) == 0); break;
            case 14: result = model.setAnchorContentLocked(anyAnchor, random.bounded(3) == 0); break;
            }

            const QStringList problems = model.validate();
            QVERIFY2(problems.isEmpty(), qPrintable(QStringLiteral("step %1: %2").arg(step).arg(problems.join(QStringLiteral("; ")))));
            QCOMPARE(model.viewIds().size(), opened - closed);
        }

        WorkspaceModel restored(counter());
        QVERIFY(Codec::decodeSnapshot(Codec::encodeSnapshot(model), ViewTypeRegistry::withBuiltIns(), restored).ok);
        QCOMPARE(viewSet(restored), viewSet(model));
    }

    // ---- anchor identity, locks, shelves, worktree context -------------

    void anchorNamesSurviveRearrangementAndRestore()
    {
        Arranged s = arranged();
        QVERIFY(s.model.renameAnchor(s.editors, QStringLiteral("  Primary   Editor ")).ok);
        QCOMPARE(s.model.anchor(s.editors)->name, QStringLiteral("Primary Editor"));
        QCOMPARE(s.model.anchorDisplayName(s.editors), QStringLiteral("Primary Editor"));

        // Docking, splitting, floating and returning keep the identity.
        QVERIFY(s.model.moveAnchor(s.editors, s.first, Edge::Left).ok);
        QVERIFY(s.model.splitView(s.b, s.editors, Edge::Bottom).ok);
        const QString window = s.model.detachAnchor(s.editors, QRectF(50, 50, 600, 400)).createdId;
        QVERIFY(!window.isEmpty());
        QCOMPARE(s.model.anchorDisplayName(s.editors), QStringLiteral("Primary Editor"));
        QVERIFY(s.model.moveAnchor(s.editors, s.first, Edge::Right).ok);
        QVERIFY(s.model.maximizeAnchor(s.editors).ok);
        QCOMPARE(s.model.anchor(s.editors)->name, QStringLiteral("Primary Editor"));

        // Moving its last tab away keeps a named anchor (it was made on purpose).
        QVERIFY(s.model.moveView(s.a, s.first).ok);
        QVERIFY(s.model.anchor(s.editors));

        // Restart.
        WorkspaceModel restored(counter());
        QVERIFY(Codec::decodeSnapshot(Codec::encodeSnapshot(s.model), ViewTypeRegistry::withBuiltIns(), restored).ok);
        QCOMPARE(restored.anchor(s.editors)->name, QStringLiteral("Primary Editor"));

        // Saved layouts carry names; applying one brings them back.
        const QJsonObject layout = Codec::captureLayout(s.model, QStringLiteral("named"));
        QVERIFY(Codec::applyLayout(Presets::layout(QStringLiteral("development")), s.model).ok);
        QVERIFY(Codec::applyLayout(layout, s.model).ok);
        QCOMPARE(s.model.anchorDisplayName(s.editors), QStringLiteral("Primary Editor"));

        // Blank resets to the automatic name; overlong names are bounded.
        QVERIFY(s.model.renameAnchor(s.editors, QStringLiteral("   ")).ok);
        QVERIFY(s.model.anchor(s.editors)->name.isEmpty());
        QVERIFY(s.model.renameAnchor(s.editors, QString(200, QLatin1Char('x'))).ok);
        QCOMPARE(s.model.anchor(s.editors)->name.size(), 80);
    }

    void automaticNamesFollowContent()
    {
        WorkspaceModel model(counter());
        const QString first = model.anchorIds().first();
        QCOMPARE(model.anchorDisplayName(first), QStringLiteral("Panel"));

        model.openView(editorView(QStringLiteral("a")), first);
        QCOMPARE(model.anchorDisplayName(first), QStringLiteral("Editor"));

        const QString second = model.createAnchor(first, Edge::Right).createdId;
        model.openView(editorView(QStringLiteral("b")), second);
        QCOMPARE(model.anchorDisplayName(second), QStringLiteral("Editor 2"));

        const QString third = model.createAnchor(second, Edge::Bottom).createdId;
        model.openView(terminalView(QStringLiteral("t")), third);
        QCOMPARE(model.anchorDisplayName(third), QStringLiteral("Terminal"));
        model.openView(chatView(QStringLiteral("c")), third);
        QCOMPARE(model.anchorDisplayName(third), QStringLiteral("Panel"));

        QVERIFY(model.setAnchorSwitchable(third, true).ok);
        QCOMPARE(model.anchorDisplayName(third), QStringLiteral("Shelf"));

        // A custom name takes a panel out of the numbering.
        QVERIFY(model.renameAnchor(first, QStringLiteral("Main")).ok);
        QCOMPARE(model.anchorDisplayName(second), QStringLiteral("Editor"));

        // An empty panel waiting for editors, earlier in the layout, is
        // numbered after the one that holds an editor.
        const QString waiting = model.createAnchor(second, Edge::Left).createdId;
        QVERIFY(model.setAnchorRoles(waiting, {QStringLiteral("archivist.editor")}).ok);
        QCOMPARE(model.anchorDisplayName(second), QStringLiteral("Editor"));
        QCOMPARE(model.anchorDisplayName(waiting), QStringLiteral("Editor 2"));
    }

    void layoutLockRejectsStructuralChanges()
    {
        Arranged s = arranged();
        const QString primary = s.model.primarySurfaceId();
        QVERIFY(s.model.renameAnchor(s.editors, QStringLiteral("Reference Shelf")).ok);
        QVERIFY(s.model.setAnchorLayoutLocked(s.editors, true).ok);
        const QByteArray before = snapshotBytes(s.model);
        const LayoutGeometry geometryBefore = s.model.computeGeometry(primary, kSurface, kMetrics);

        auto refused = [&](const OpResult &result) {
            return !result.ok && result.code == QStringLiteral("LAYOUT_LOCKED");
        };

        QVERIFY(refused(s.model.moveAnchor(s.editors, s.first, Edge::Left)));
        QVERIFY(refused(s.model.mergeAnchor(s.editors, s.first)));
        QVERIFY(refused(s.model.closeAnchor(s.editors)));
        QVERIFY(refused(s.model.detachAnchor(s.editors, QRectF(0, 0, 400, 300))));
        QVERIFY(refused(s.model.dockAnchorAtSurfaceEdge(s.editors, primary, Edge::Left)));
        QVERIFY(refused(s.model.splitView(s.chat, s.editors, Edge::Right)));
        QVERIFY(refused(s.model.moveAnchor(s.first, s.editors, Edge::Bottom)));
        QVERIFY(refused(s.model.createAnchor(s.editors, Edge::Right)));
        QVERIFY(refused(s.model.duplicateView(s.chat, s.editors, Edge::Left)));
        QVERIFY(!s.model.canDrop(DragSource::anchor(s.editors), {primary, s.first, Edge::Left}).ok);
        QVERIFY(!s.model.canDrop(DragSource::view(s.chat), {primary, s.editors, Edge::Top}).ok);
        QVERIFY(s.model.moveAnchor(s.editors, s.first, Edge::Left).message.contains(QStringLiteral("Reference Shelf")));

        // Dragging the splitter beside it would resize it.
        const LayoutGeometry geometry = s.model.computeGeometry(primary, kSurface, kMetrics);
        QVERIFY(!geometry.splitters.isEmpty());
        for (const SplitterHandle &handle : geometry.splitters) {
            const double position = handle.orientation == Orientation::Horizontal ? handle.rect.center().x() + 60 : handle.rect.center().y() + 40;
            const OpResult result = s.model.resizeSplit(primary, handle.nodeId, handle.index, position, kSurface, kMetrics);
            QVERIFY(!result.ok);
        }

        QCOMPARE(snapshotBytes(s.model), before);

        // Applying a layout would rebuild it.
        const Codec::LoadReport report = Codec::applyLayout(Presets::layout(QStringLiteral("development")), s.model);
        QVERIFY(!report.ok);
        QVERIFY(report.error.contains(QStringLiteral("Reference Shelf")));
        QCOMPARE(snapshotBytes(s.model), before);

        // Still usable: its tabs work, things can be added, and docking on
        // the far side of a neighbour leaves it alone.
        QVERIFY(s.model.activateView(s.a).ok);
        QVERIFY(s.model.openView(editorView(QStringLiteral("c.cpp")), s.editors).ok);
        QVERIFY(s.model.maximizeAnchor(s.editors).ok);
        QVERIFY(s.model.restoreAnchor(primary).ok);
        QVERIFY(s.model.renameAnchor(s.editors, QStringLiteral("Shelf")).ok);

        // Moving every tab out leaves the locked anchor in place.
        const QStringList tabs = s.model.anchor(s.editors)->views; // a copy: moving changes the list
        for (const QString &viewId : tabs) {
            QVERIFY(s.model.moveView(viewId, s.first).ok);
        }
        QVERIFY(s.model.anchor(s.editors));
        QVERIFY(s.model.anchor(s.editors)->views.isEmpty());
        QCOMPARE(s.model.computeGeometry(primary, kSurface, kMetrics).anchors.value(s.editors), geometryBefore.anchors.value(s.editors));

        // Closing a neighbour gives its room to unlocked panels only.
        const QRectF lockedRect = s.model.computeGeometry(primary, kSurface, kMetrics).anchors.value(s.editors);
        const QString extra = s.model.splitView(s.chat, s.first, Edge::Right).createdId;
        QVERIFY(!extra.isEmpty());
        // (Within a splitter's width: one more splitter takes a few pixels.)
        auto sameSize = [&](const QRectF &rect) {
            return std::abs(rect.width() - lockedRect.width()) <= kMetrics.splitterThickness
                && std::abs(rect.height() - lockedRect.height()) <= kMetrics.splitterThickness;
        };
        QVERIFY(sameSize(s.model.computeGeometry(primary, kSurface, kMetrics).anchors.value(s.editors)));
        QVERIFY(s.model.moveView(s.chat, s.first).ok); // collapses `extra`
        QCOMPARE(s.model.computeGeometry(primary, kSurface, kMetrics).anchors.value(s.editors), lockedRect);

        // Unlocking restores every operation.
        QVERIFY(s.model.setAnchorLayoutLocked(s.editors, false).ok);
        QVERIFY(s.model.moveAnchor(s.editors, s.first, Edge::Left).ok);
        QVERIFY(Codec::applyLayout(Presets::layout(QStringLiteral("development")), s.model).ok);
        QVERIFY(s.model.validate().isEmpty());
    }

    void layoutLockedFloatingWindowsStay()
    {
        Arranged s = arranged();
        const QString window = s.model.detachAnchor(s.terminal, QRectF(0, 0, 400, 300)).createdId;
        QVERIFY(s.model.setAnchorLayoutLocked(s.terminal, true).ok);
        QCOMPARE(s.model.closeSurface(window).code, QStringLiteral("LAYOUT_LOCKED"));
        QVERIFY(s.model.surface(window));
    }

    void contentLockStopsAutomationNotThePerson()
    {
        Arranged s = arranged();
        QVERIFY(s.model.renameAnchor(s.first, QStringLiteral("Architecture Chat")).ok);
        QVERIFY(s.model.setAnchorContentLocked(s.first, true).ok);

        ResourceRef developer;
        developer.kind = QStringLiteral("chat");
        developer.id = QStringLiteral("developer-chat");

        // Another agent changing the selected conversation can't repoint it.
        const OpResult automatic = s.model.setViewResource(s.chat, developer, Origin::Automatic);
        QCOMPARE(automatic.code, QStringLiteral("CONTENT_LOCKED"));
        QVERIFY(automatic.message.contains(QStringLiteral("Architecture Chat")));
        QCOMPARE(s.model.setViewResource(s.chat, developer, Origin::Agent).code, QStringLiteral("CONTENT_LOCKED"));
        QCOMPARE(s.model.view(s.chat)->resource.id, QStringLiteral("chat-1"));

        // Nor add, remove or take its views.
        QVERIFY(!s.model.openView(chatView(QStringLiteral("x")), s.first, -1, true, Origin::Automatic).ok);
        QVERIFY(!s.model.closeView(s.chat, Origin::Agent).ok);
        QVERIFY(!s.model.moveView(s.chat, s.editors, -1, Origin::Agent).ok);
        QVERIFY(!s.model.moveView(s.a, s.first, -1, Origin::Agent).ok);
        QVERIFY(!s.model.detachView(s.chat, QRectF(0, 0, 300, 200), {}, Origin::Agent).ok);
        QVERIFY(!s.model.closeAnchor(s.first, Origin::Agent).ok);
        QVERIFY(!s.model.mergeAnchor(s.editors, s.first, Origin::Agent).ok);

        // Agents may lock, never unlock.
        QCOMPARE(s.model.setAnchorContentLocked(s.first, false, Origin::Agent).code, QStringLiteral("UNLOCK_REFUSED"));
        QVERIFY(s.model.setAnchorLayoutLocked(s.editors, true, Origin::Agent).ok);
        QCOMPARE(s.model.setAnchorLayoutLocked(s.editors, false, Origin::Agent).code, QStringLiteral("UNLOCK_REFUSED"));
        QVERIFY(s.model.anchor(s.first)->contentLocked);

        // Navigation stays free, and the person can still change things.
        QVERIFY(s.model.activateView(s.chat).ok);
        QVERIFY(s.model.openView(chatView(QStringLiteral("notes")), s.first).ok);
        QVERIFY(s.model.setViewResource(s.chat, developer, Origin::User).ok);
        QVERIFY(s.model.setAnchorContentLocked(s.first, false).ok);
        QVERIFY(s.model.setViewResource(s.chat, developer, Origin::Automatic).ok);
        QVERIFY(s.model.validate().isEmpty());
    }

    void switchableAnchorsKeepEachViewsState()
    {
        WorkspaceModel model(counter());
        const QString shelf = model.anchorIds().first();
        QVERIFY(model.renameAnchor(shelf, QStringLiteral("Reference Shelf")).ok);
        QVERIFY(model.setAnchorSwitchable(shelf, true).ok);
        QVERIFY(model.setAnchorLayoutLocked(shelf, true).ok);

        const QString model1 = model.openView(editorView(QStringLiteral("workspace_model.cpp"), QStringLiteral("wt-a")), shelf).createdId;
        const QString dock = model.openView(editorView(QStringLiteral("DockWorkspace.qml")), shelf).createdId;
        const QString notes = model.openView(chatView(QStringLiteral("architecture")), shelf).createdId;
        const QString term = model.openView(terminalView(QStringLiteral("build"), QStringLiteral("wt-b")), shelf).createdId;

        QVERIFY(model.updateViewState(model1, {{QStringLiteral("line"), 120}}).ok);
        QVERIFY(model.updateViewState(dock, {{QStringLiteral("line"), 7}}).ok);
        QVERIFY(model.updateViewState(term, {{QStringLiteral("scroll"), 900}}).ok);

        // Rotating through the shelf changes only which view shows.
        for (int round = 0; round < 3; ++round) {
            for (const QString &viewId : {model1, dock, notes, term}) {
                QVERIFY(model.activateView(viewId).ok);
                QCOMPARE(model.anchor(shelf)->activeViewId, viewId);
            }
        }
        QCOMPARE(model.view(model1)->state.value(QStringLiteral("line")).toInt(), 120);
        QCOMPARE(model.view(dock)->state.value(QStringLiteral("line")).toInt(), 7);
        QCOMPARE(model.view(term)->state.value(QStringLiteral("scroll")).toInt(), 900);
        QCOMPARE(model.view(model1)->resource.libraryId, QStringLiteral("wt-a"));
        QCOMPARE(model.view(term)->resource.libraryId, QStringLiteral("wt-b"));

        // Reorder; the shelf is still where it was.
        QVERIFY(model.moveView(term, shelf, 0).ok);
        QCOMPARE(model.anchor(shelf)->views, (QStringList{term, model1, dock, notes}));

        // The choice and the order survive a restart.
        QVERIFY(model.activateView(dock).ok);
        WorkspaceModel restored(counter());
        QVERIFY(Codec::decodeSnapshot(Codec::encodeSnapshot(model), ViewTypeRegistry::withBuiltIns(), restored).ok);
        const AnchorRecord *back = restored.anchor(shelf);
        QVERIFY(back->switchable);
        QVERIFY(back->layoutLocked);
        QCOMPARE(back->activeViewId, dock);
        QCOMPARE(back->views, (QStringList{term, model1, dock, notes}));
        QCOMPARE(restored.view(model1)->state.value(QStringLiteral("line")).toInt(), 120);

        // Conventional tabs stay the default.
        WorkspaceModel fresh(counter());
        QVERIFY(!fresh.anchor(fresh.anchorIds().first())->switchable);
    }

    void newResourcesResolveTheirWorktree()
    {
        Arranged s = arranged();
        const QStringList known{QStringLiteral("lib-main"), QStringLiteral("wt-a"), QStringLiteral("wt-b")};

        // Terminal anchor: its active view (a terminal in lib-main) decides.
        LibraryResolution resolved = s.model.resolveLibrary(s.terminal, QStringLiteral("wt-b"), QStringLiteral("lib-main"), known);
        QCOMPARE(resolved.libraryId, QStringLiteral("lib-main"));
        QCOMPARE(resolved.source, LibraryResolution::Source::ActiveView);

        // An explicit binding beats everything, and doesn't touch existing views.
        QVERIFY(s.model.setAnchorLibrary(s.terminal, QStringLiteral("wt-a")).ok);
        resolved = s.model.resolveLibrary(s.terminal, QStringLiteral("wt-b"), QStringLiteral("lib-main"), known);
        QCOMPARE(resolved.libraryId, QStringLiteral("wt-a"));
        QCOMPARE(resolved.source, LibraryResolution::Source::AnchorBinding);
        QCOMPARE(s.model.view(s.term)->resource.libraryId, QStringLiteral("lib-main"));

        // A binding to a worktree that is gone falls through.
        QVERIFY(s.model.setAnchorLibrary(s.terminal, QStringLiteral("deleted")).ok);
        QCOMPARE(s.model.resolveLibrary(s.terminal, QStringLiteral("wt-b"), QStringLiteral("lib-main"), known).source, LibraryResolution::Source::ActiveView);

        // A chat has no worktree: the Explorer's selection decides, then the default.
        resolved = s.model.resolveLibrary(s.first, QStringLiteral("wt-b"), QStringLiteral("lib-main"), known);
        QCOMPARE(resolved.libraryId, QStringLiteral("wt-b"));
        QCOMPARE(resolved.source, LibraryResolution::Source::Explorer);
        resolved = s.model.resolveLibrary(s.first, QString(), QStringLiteral("lib-main"), known);
        QCOMPARE(resolved.source, LibraryResolution::Source::Default);

        // Switching worktrees later changes nothing that already exists.
        const QByteArray before = snapshotBytes(s.model);
        static_cast<void>(s.model.resolveLibrary(s.editors, QStringLiteral("wt-a"), QStringLiteral("lib-main"), known));
        QCOMPARE(snapshotBytes(s.model), before);
        for (const QString &viewId : s.model.viewIds()) {
            const ViewRecord *view = s.model.view(viewId);
            if (view->resource.kind != QStringLiteral("chat")) {
                QCOMPARE(view->resource.libraryId, QStringLiteral("lib-main"));
            }
        }

        // Automatic redirection never touches a configured panel.
        QVERIFY(s.model.anchor(s.first)->pinned);
        QCOMPARE(s.model.setAnchorLibrary(s.first, QStringLiteral("wt-b"), Origin::Automatic).code, QStringLiteral("CONTENT_LOCKED"));
        QVERIFY(s.model.setAnchorLayoutLocked(s.editors, true).ok);
        QVERIFY(!s.model.setAnchorLibrary(s.editors, QStringLiteral("wt-b"), Origin::Automatic).ok);
        QVERIFY(s.model.anchor(s.first)->libraryId.isEmpty());
        QVERIFY(s.model.anchor(s.editors)->libraryId.isEmpty());
    }

    void commandsShareTheModelsRules()
    {
        Arranged s = arranged();
        QVERIFY(Commands::names().contains(QStringLiteral("anchor.rename")));

        Commands::Result result = Commands::execute(s.model, {{"command", "anchor.rename"}, {"anchorId", s.editors}, {"name", "API Debugging"}}, Origin::Agent);
        QVERIFY(result.op.ok);
        QCOMPARE(s.model.anchorDisplayName(s.editors), QStringLiteral("API Debugging"));

        result = Commands::execute(s.model, {{"command", "anchor.setLayoutLock"}, {"anchorId", s.editors}, {"locked", true}}, Origin::Agent);
        QVERIFY(result.op.ok);
        result = Commands::execute(s.model, {{"command", "anchor.setLayoutLock"}, {"anchorId", s.editors}, {"locked", false}}, Origin::Agent);
        QCOMPARE(result.op.code, QStringLiteral("UNLOCK_REFUSED"));

        // An agent rearranging meets the same lock the person does.
        result = Commands::execute(s.model, {{"command", "view.move"}, {"viewId", s.chat}, {"anchorId", s.editors}}, Origin::Agent);
        QVERIFY(result.op.ok); // adding a tab is not structural
        QVERIFY(s.model.setAnchorContentLocked(s.editors, true).ok);
        result = Commands::execute(s.model, {{"command", "view.move"}, {"viewId", s.chat}, {"anchorId", s.first}}, Origin::Agent);
        QCOMPARE(result.op.code, QStringLiteral("CONTENT_LOCKED"));
        result = Commands::execute(s.model, {{"command", "anchor.addResource"}, {"anchorId", s.editors}, {"typeId", "archivist.chat"}, {"resource", QVariantMap{{"kind", "chat"}, {"id", "x"}}}}, Origin::Agent);
        QCOMPARE(result.op.code, QStringLiteral("CONTENT_LOCKED"));

        // Selecting within a locked anchor is navigation, and keeps focus.
        QVERIFY(s.model.focusAnchor(s.terminal).ok);
        result = Commands::execute(s.model, {{"command", "anchor.selectResource"}, {"viewId", s.a}}, Origin::Agent);
        QVERIFY(result.op.ok);
        QCOMPARE(s.model.anchor(s.editors)->activeViewId, s.a);
        QCOMPARE(s.model.focusedAnchorId(), s.terminal);

        // Queries describe capabilities and why.
        result = Commands::execute(s.model, {{"command", "anchor.describe"}, {"anchorId", s.editors}}, Origin::Agent);
        QVERIFY(result.op.ok);
        const QVariantMap caps = result.data.value("capabilities").toMap();
        QVERIFY(!caps.value("move").toBool());
        QVERIFY(!caps.value("replaceContent").toBool());
        QVERIFY(!caps.value("unlock").toBool());
        QVERIFY(result.data.value("restrictions").toMap().value("move").toString().contains(QStringLiteral("API Debugging")));
        QCOMPARE(result.data.value("views").toList().size(), 3);

        result = Commands::execute(s.model, {{"command", "anchor.list"}}, Origin::Agent);
        QCOMPARE(result.data.value("anchors").toList().size(), s.model.anchorIds().size());

        QCOMPARE(Commands::execute(s.model, {{"command", "anchor.explode"}}, Origin::Agent).op.code, QStringLiteral("UNKNOWN_COMMAND"));
        QVERIFY(s.model.validate().isEmpty());
    }

    void olderSnapshotsReadWithDefaults()
    {
        // A snapshot written before anchors had names or locks.
        const QByteArray old = R"({"format":"archivist.workspace","version":1,
            "layout":{"surfaces":[{"id":"w1","primary":true,"root":{"anchor":{"id":"a1","pinned":true}}}]},
            "session":{"views":[{"id":"v1","type":"archivist.chat","resource":{"kind":"chat","id":"c"}}],
                       "anchors":[{"id":"a1","views":["v1"],"active":"v1"}]}})";
        WorkspaceModel model(counter());
        const Codec::LoadReport report = Codec::decodeSnapshot(QJsonDocument::fromJson(old).object(), ViewTypeRegistry::withBuiltIns(), model);
        QVERIFY2(report.ok, qPrintable(report.error));
        const AnchorRecord *anchor = model.anchor(QStringLiteral("a1"));
        QVERIFY(anchor);
        QVERIFY(anchor->name.isEmpty());
        QVERIFY(!anchor->layoutLocked);
        QVERIFY(!anchor->contentLocked);
        QVERIFY(!anchor->switchable);
        QCOMPARE(model.anchorDisplayName(QStringLiteral("a1")), QStringLiteral("Chat"));
    }
};

QTEST_GUILESS_MAIN(CoreWorkspaceTest)
#include "core_workspace_test.moc"

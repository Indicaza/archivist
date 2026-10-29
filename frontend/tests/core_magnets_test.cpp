#include "Core/Domains/Workspace/view_type_registry.h"
#include "Core/Domains/Workspace/workspace_magnets.h"
#include "Core/Domains/Workspace/workspace_model.h"

#include <QtTest>

#include <memory>

using namespace Workspace;

namespace
{
WorkspaceModel::IdGenerator counter()
{
    auto next = std::make_shared<int>(0);
    return [next](const QString &prefix) { return prefix + QString::number(++*next); };
}

ViewRecord editorView(const QString &fileId)
{
    ViewRecord view;
    view.typeId = ViewTypes::Editor;
    view.resource.kind = QStringLiteral("document");
    view.resource.collectionId = QStringLiteral("col");
    view.resource.libraryId = QStringLiteral("lib");
    view.resource.id = QStringLiteral("col:lib:") + fileId;
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

ViewRecord terminalView(const QString &sessionId)
{
    ViewRecord view;
    view.typeId = ViewTypes::Terminal;
    view.resource.kind = QStringLiteral("terminal");
    view.resource.libraryId = QStringLiteral("lib");
    view.resource.id = QStringLiteral("lib:") + sessionId;
    return view;
}

const QSizeF kSurface(1200, 800);
const LayoutMetrics kMetrics;

DropTarget onto(const WorkspaceModel &model, const QString &anchorId, Edge edge, int index = -1)
{
    DropTarget target;
    target.surfaceId = model.surfaceOfAnchor(anchorId);
    target.anchorId = anchorId;
    target.edge = edge;
    target.index = index;
    return target;
}

DropTarget alongWindow(const QString &surfaceId, Edge edge)
{
    DropTarget target;
    target.surfaceId = surfaceId;
    target.edge = edge;
    return target;
}

QRectF rectOf(const WorkspaceModel &model, const QString &anchorId, const QSizeF &size = kSurface)
{
    return model.computeGeometry(model.surfaceOfAnchor(anchorId), size, kMetrics).anchors.value(anchorId);
}
} // namespace

class CoreMagnetsTest final : public QObject
{
    Q_OBJECT

private:
    // chat | (editor a, editor b) / terminal
    struct Arranged
    {
        WorkspaceModel model{counter()};
        QString first;
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
    void phasesHaveStableNames()
    {
        QCOMPARE(Magnets::phaseName(Magnets::Phase::Idle), QStringLiteral("idle"));
        QCOMPARE(Magnets::phaseName(Magnets::Phase::Pickup), QStringLiteral("pickup"));
        QCOMPARE(Magnets::phaseName(Magnets::Phase::Free), QStringLiteral("free"));
        QCOMPARE(Magnets::phaseName(Magnets::Phase::Invalid), QStringLiteral("invalid"));
        QCOMPARE(Magnets::phaseName(Magnets::Phase::Preview), QStringLiteral("preview"));
        QCOMPARE(Magnets::phaseName(Magnets::Phase::Detached), QStringLiteral("detached"));
    }

    // The preview is worked out on a copy: the model itself never changes.
    void previewLeavesTheModelAlone()
    {
        Arranged s = arranged();
        const int revision = s.model.revision();
        const QStringList anchors = s.model.anchorIds();

        const Magnets::Preview tab = Magnets::preview(s.model, DragSource::view(s.b), onto(s.model, s.terminal, Edge::Center), kSurface, kMetrics);
        QVERIFY2(tab.result.ok, qPrintable(tab.result.message));
        QCOMPARE(tab.landingAnchorId, s.terminal);
        QCOMPARE(tab.landingRect, rectOf(s.model, s.terminal));

        const Magnets::Preview split = Magnets::preview(s.model, DragSource::view(s.b), onto(s.model, s.first, Edge::Bottom), kSurface, kMetrics);
        QVERIFY(split.result.ok);
        // A new panel under the chat: the chat gets shorter to make room.
        QVERIFY(!split.landingAnchorId.isEmpty());
        QVERIFY(split.geometry.anchors.value(s.first).height() < rectOf(s.model, s.first).height());
        QVERIFY(split.landingRect.top() > split.geometry.anchors.value(s.first).top());

        QCOMPARE(s.model.revision(), revision);
        QCOMPARE(s.model.anchorIds(), anchors);
        QCOMPARE(s.model.anchorOfView(s.b), s.editors);
    }

    void droppingWhereItIsChangesNothing()
    {
        Arranged s = arranged();
        const Magnets::Preview same = Magnets::preview(s.model, DragSource::view(s.a), onto(s.model, s.editors, Edge::Center, 0), kSurface, kMetrics);
        QVERIFY(!same.result.ok);
        QCOMPARE(same.result.code, QStringLiteral("NO_CHANGE"));
        QVERIFY(!same.result.message.isEmpty());

        // Reordering is a change.
        const Magnets::Preview reorder = Magnets::preview(s.model, DragSource::view(s.a), onto(s.model, s.editors, Edge::Center, 2), kSurface, kMetrics);
        QVERIFY2(reorder.result.ok, qPrintable(reorder.result.message));
    }

    // Splitting where there's no room would squeeze a panel under the
    // minimum: refused with a reason, not committed as something else.
    void tooSmallIsRefused()
    {
        Arranged s = arranged();
        const QSizeF narrow(500, 800);
        const Magnets::Preview cramped = Magnets::preview(s.model, DragSource::view(s.a), onto(s.model, s.terminal, Edge::Right), narrow, kMetrics);
        QVERIFY(!cramped.result.ok);
        QCOMPARE(cramped.result.code, QStringLiteral("TOO_SMALL"));
        QVERIFY(cramped.result.message.contains(QStringLiteral("180")));

        // The same drop fits in a wider window.
        QVERIFY(Magnets::preview(s.model, DragSource::view(s.a), onto(s.model, s.terminal, Edge::Right), kSurface, kMetrics).result.ok);
        // A tab drop never squeezes anything.
        QVERIFY(Magnets::preview(s.model, DragSource::view(s.a), onto(s.model, s.terminal, Edge::Center), narrow, kMetrics).result.ok);
    }

    void locksComeThroughWithTheirReason()
    {
        Arranged s = arranged();
        QVERIFY(s.model.setAnchorLayoutLocked(s.terminal, true).ok);
        const Magnets::Preview locked = Magnets::preview(s.model, DragSource::view(s.a), onto(s.model, s.terminal, Edge::Right), kSurface, kMetrics);
        QVERIFY(!locked.result.ok);
        QVERIFY(!locked.result.code.isEmpty());
        QVERIFY(!locked.result.message.isEmpty());
    }

    // A tab goes back into the panel it came from, at its old position.
    void tabReturnsToItsPanel()
    {
        Arranged s = arranged();
        const Magnets::Place place = Magnets::placeOf(s.model, DragSource::view(s.b));
        QVERIFY(place.isValid());
        QCOMPARE(place.anchorId, s.editors);
        QCOMPARE(place.index, 1);
        QVERIFY(!place.aloneInFloatingWindow);

        QVERIFY(s.model.applyDrop(DragSource::view(s.b), onto(s.model, s.terminal, Edge::Center)).ok);
        QCOMPARE(s.model.anchorOfView(s.b), s.terminal);

        const Magnets::ReturnPlan plan = Magnets::planReturn(s.model, place);
        QVERIFY2(plan.result.ok, qPrintable(plan.result.message));
        QVERIFY(!plan.detach);
        QVERIFY(s.model.applyDrop(DragSource::view(s.b), plan.target).ok);
        QCOMPARE(s.model.anchor(s.editors)->views, (QStringList{s.a, s.b}));
        QVERIFY(s.model.validate().isEmpty());

        // Already back: nothing to do.
        QCOMPARE(Magnets::planReturn(s.model, place).result.code, QStringLiteral("NO_CHANGE"));
    }

    // A tab that was alone took its panel with it: going back recreates a
    // panel in the same spot of the split.
    void loneTabReturnsBesideItsNeighbour()
    {
        Arranged s = arranged();
        const QRectF before = rectOf(s.model, s.terminal);
        const Magnets::Place place = Magnets::placeOf(s.model, DragSource::view(s.term));
        QCOMPARE(place.beside.anchorId, s.editors);
        QCOMPARE(place.beside.edge, Edge::Bottom);

        QVERIFY(s.model.applyDrop(DragSource::view(s.term), onto(s.model, s.first, Edge::Center)).ok);
        QVERIFY(!s.model.anchor(s.terminal));

        const Magnets::ReturnPlan plan = Magnets::planReturn(s.model, place);
        QVERIFY2(plan.result.ok, qPrintable(plan.result.message));
        QVERIFY(s.model.applyDrop(DragSource::view(s.term), plan.target).ok);
        const QString back = s.model.anchorOfView(s.term);
        QVERIFY(back != s.first);
        QCOMPARE(rectOf(s.model, back), before);
        QVERIFY(s.model.validate().isEmpty());
    }

    // A whole panel along the window's edge goes back along the same edge.
    void panelReturnsAlongTheWindowEdge()
    {
        Arranged s = arranged();
        const QRectF before = rectOf(s.model, s.first);
        const Magnets::Place place = Magnets::placeOf(s.model, DragSource::anchor(s.first));
        QVERIFY(place.beside.isSurfaceEdge());
        QCOMPARE(place.beside.edge, Edge::Left);

        QVERIFY(s.model.applyDrop(DragSource::anchor(s.first), alongWindow(s.model.primarySurfaceId(), Edge::Right)).ok);
        QVERIFY(rectOf(s.model, s.first).left() > 0);

        const Magnets::ReturnPlan plan = Magnets::planReturn(s.model, place);
        QVERIFY2(plan.result.ok, qPrintable(plan.result.message));
        QVERIFY(s.model.applyDrop(DragSource::anchor(s.first), plan.target).ok);
        Magnets::restoreShare(s.model, s.first, place);
        QCOMPARE(rectOf(s.model, s.first), before);
        QCOMPARE(Magnets::planReturn(s.model, place).result.code, QStringLiteral("NO_CHANGE"));
    }

    void goneNeighbourIsReported()
    {
        Arranged s = arranged();
        const Magnets::Place place = Magnets::placeOf(s.model, DragSource::view(s.term));
        QVERIFY(s.model.applyDrop(DragSource::view(s.term), onto(s.model, s.first, Edge::Center)).ok);
        QVERIFY(s.model.closeAnchor(s.editors).ok);
        const Magnets::ReturnPlan plan = Magnets::planReturn(s.model, place);
        QVERIFY(!plan.result.ok);
        QCOMPARE(plan.result.code, QStringLiteral("GONE"));

        QCOMPARE(Magnets::planReturn(s.model, Magnets::Place{}).result.code, QStringLiteral("NO_PLACE"));
    }

    // Alone in a floating window: going back opens one again, same frame.
    void floatingWindowComesBack()
    {
        Arranged s = arranged();
        const QRectF frame(200, 150, 640, 420);
        QVERIFY(s.model.detachView(s.a, frame, QStringLiteral("Screen 1")).ok);
        const Magnets::Place place = Magnets::placeOf(s.model, DragSource::view(s.a));
        QVERIFY(place.aloneInFloatingWindow);
        QCOMPARE(place.windowGeometry, frame);

        QVERIFY(s.model.applyDrop(DragSource::view(s.a), onto(s.model, s.editors, Edge::Center)).ok);
        QCOMPARE(s.model.surfaceIds().size(), 1);

        const Magnets::ReturnPlan plan = Magnets::planReturn(s.model, place);
        QVERIFY2(plan.result.ok, qPrintable(plan.result.message));
        QVERIFY(plan.detach);
        QCOMPARE(plan.geometry, frame);
        QCOMPARE(plan.screenName, QStringLiteral("Screen 1"));
    }
};

QTEST_GUILESS_MAIN(CoreMagnetsTest)
#include "core_magnets_test.moc"

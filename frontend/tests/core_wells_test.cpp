#include "Core/Domains/Workspace/view_type_registry.h"
#include "Core/Domains/Workspace/workspace_arrangements.h"
#include "Core/Domains/Workspace/workspace_codec.h"
#include "Core/Domains/Workspace/workspace_model.h"
#include "Core/Domains/Workspace/workspace_wells.h"

#include <QJsonArray>
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

ViewRecord terminalView(const QString &sessionId)
{
    ViewRecord view;
    view.typeId = ViewTypes::Terminal;
    view.resource.kind = QStringLiteral("terminal");
    view.resource.libraryId = QStringLiteral("lib");
    view.resource.id = QStringLiteral("lib:") + sessionId;
    view.title = sessionId;
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

const QSizeF kSurface(1200, 800);
const LayoutMetrics kMetrics;

QRectF rectOf(const WorkspaceModel &model, const QString &anchorId)
{
    return model.computeGeometry(model.surfaceOfAnchor(anchorId), kSurface, kMetrics).anchors.value(anchorId);
}

const WellEntry *onlyEntry(const WorkspaceModel &model)
{
    const WellRecord *well = model.well(WorkspaceModel::kMainWellId);
    return well && well->entries.size() == 1 ? &well->entries.first() : nullptr;
}

struct Setup
{
    WorkspaceModel model{counter()};
    QString main;

    Setup() { main = model.anchorIds().first(); }

    QString open(const ViewRecord &view, const QString &anchorId = {})
    {
        const OpResult result = model.openView(view, anchorId.isEmpty() ? main : anchorId);
        return result.createdId;
    }
};
} // namespace

class CoreWellsTest final : public QObject
{
    Q_OBJECT

private slots:
    void freshWorkspaceHasAnEmptyMainWellAtTheBottomCentre()
    {
        Setup s;
        const WellRecord *well = s.model.well(WorkspaceModel::kMainWellId);
        QVERIFY(well);
        QVERIFY(well->entries.isEmpty());
        QCOMPARE(well->position, QPointF(0.5, 1.0));
        QVERIFY(s.model.validate().isEmpty());
    }

    void minimizingKeepsTheViewAndItsResource()
    {
        Setup s;
        const QString a = s.open(editorView(QStringLiteral("a")));
        const QString b = s.open(editorView(QStringLiteral("b")));
        s.model.updateViewState(b, {{QStringLiteral("cursor"), 42}});
        const ViewRecord before = *s.model.view(b);

        const OpResult result = Wells::minimize(s.model, b);
        QVERIFY2(result.ok, qPrintable(result.message));
        QVERIFY(s.model.validate().isEmpty());

        // Out of its panel, still the same view of the same resource.
        QVERIFY(!s.model.viewIds().contains(b));
        QVERIFY(s.model.isMinimized(b));
        QCOMPARE(s.model.anchorOfView(b), QString());
        QCOMPARE(s.model.view(b)->id, before.id);
        QCOMPARE(s.model.view(b)->resource, before.resource);
        QCOMPARE(s.model.view(b)->state, before.state);
        QCOMPARE(s.model.anchor(s.main)->views, QStringList{a});
        QCOMPARE(s.model.anchor(s.main)->activeViewId, a);

        const WellEntry *entry = onlyEntry(s.model);
        QVERIFY(entry);
        QCOMPARE(entry->resource, before.resource);
        QCOMPARE(entry->minimized.size(), 1);
        QCOMPARE(entry->minimized.first().viewId, b);
        QCOMPARE(Wells::entryState(s.model, *entry), Wells::EntryState::Minimized);

        // Twice is refused, not doubled.
        QCOMPARE(Wells::minimize(s.model, b).code, QStringLiteral("NO_CHANGE"));
        // Panel operations don't reach into the Well.
        QCOMPARE(s.model.activateView(b).code, QStringLiteral("MINIMIZED"));
        QVERIFY(!s.model.moveView(b, s.main).ok);
        QVERIFY(s.model.validate().isEmpty());
    }

    void restoringPutsItBackInItsTab()
    {
        Setup s;
        const QString a = s.open(editorView(QStringLiteral("a")));
        const QString b = s.open(editorView(QStringLiteral("b")));
        const QString c = s.open(editorView(QStringLiteral("c")));
        s.model.activateView(a);

        QVERIFY(Wells::minimize(s.model, b).ok);
        QCOMPARE(s.model.anchor(s.main)->views, (QStringList{a, c}));

        const Wells::Restored restored = Wells::restore(s.model, b);
        QVERIFY2(restored.result.ok, qPrintable(restored.result.message));
        QCOMPARE(restored.how, QStringLiteral("original"));
        QCOMPARE(restored.anchorId, s.main);
        QCOMPARE(s.model.anchor(s.main)->views, (QStringList{a, b, c}));
        QCOMPARE(s.model.anchor(s.main)->activeViewId, b);
        QCOMPARE(s.model.focusedAnchorId(), s.main);
        QVERIFY(!s.model.isMinimized(b));
        // Not pinned: the entry goes with it.
        QVERIFY(s.model.well(WorkspaceModel::kMainWellId)->entries.isEmpty());
        QVERIFY(s.model.validate().isEmpty());
    }

    void aPanelThatFoldedComesBackWhereItWas()
    {
        Setup s;
        s.open(editorView(QStringLiteral("a")));
        const QString term = s.open(terminalView(QStringLiteral("t1")));
        QVERIFY(s.model.splitView(term, s.main, Edge::Right).ok);
        const QString side = s.model.anchorOfView(term);
        QVERIFY(s.model.setSplitSizes(s.model.node(s.model.leafOfAnchor(side))->parentId, {0.7, 0.3}).ok);
        const QRectF was = rectOf(s.model, side);

        QVERIFY(Wells::minimize(s.model, term).ok);
        QVERIFY(!s.model.anchor(side)); // incidental: it folded
        QCOMPARE(s.model.anchorIds().size(), 1);

        const Wells::Restored restored = Wells::restore(s.model, term);
        QVERIFY2(restored.result.ok, qPrintable(restored.result.message));
        QCOMPARE(restored.how, QStringLiteral("returned"));
        QVERIFY(restored.anchorId != s.main);
        const QRectF now = rectOf(s.model, restored.anchorId);
        QVERIFY2(qAbs(now.x() - was.x()) < 1 && qAbs(now.width() - was.width()) < 1,
                 qPrintable(QStringLiteral("was %1+%2, now %3+%4").arg(was.x()).arg(was.width()).arg(now.x()).arg(now.width())));
        QVERIFY(s.model.validate().isEmpty());
    }

    void withItsPlaceGoneItJoinsAPanelOfItsKind()
    {
        Setup s;
        s.open(editorView(QStringLiteral("a")));
        const QString term = s.open(terminalView(QStringLiteral("t1")));
        const QString chat = s.open(chatView(QStringLiteral("c1")));
        QVERIFY(s.model.splitView(chat, s.main, Edge::Right).ok);
        const QString chatPanel = s.model.anchorOfView(chat);
        QVERIFY(s.model.splitView(term, chatPanel, Edge::Bottom).ok);
        const QString termPanel = s.model.anchorOfView(term);
        const QString otherTerm = s.open(terminalView(QStringLiteral("t2")));
        QVERIFY(s.model.splitView(otherTerm, s.main, Edge::Left).ok);
        const QString otherTermPanel = s.model.anchorOfView(otherTerm);

        QVERIFY(Wells::minimize(s.model, term).ok);
        QVERIFY(!s.model.anchor(termPanel));
        // The panel it sat under is closed too: its place is gone.
        QVERIFY(s.model.closeAnchor(chatPanel).ok);

        const Wells::Restored restored = Wells::restore(s.model, term);
        QVERIFY2(restored.result.ok, qPrintable(restored.result.message));
        QCOMPARE(restored.how, QStringLiteral("fallback"));
        QCOMPARE(restored.anchorId, otherTermPanel); // a panel showing a terminal
        QCOMPARE(s.model.anchor(otherTermPanel)->activeViewId, term);
        QVERIFY(s.model.validate().isEmpty());
    }

    void aFloatingPanelOfItsOwnFloatsAgain()
    {
        Setup s;
        s.open(editorView(QStringLiteral("a")));
        const QString b = s.open(editorView(QStringLiteral("b")));
        const QRectF frame(80, 60, 420, 300);
        QVERIFY(s.model.detachView(b, frame, {}, Origin::User, true).ok);
        const QString panelSurface = s.model.surfaceOfAnchor(s.model.anchorOfView(b));
        QVERIFY(s.model.surface(panelSurface)->inMainWindow);

        QVERIFY(Wells::minimize(s.model, b).ok);
        QVERIFY(!s.model.surface(panelSurface)); // the empty panel went away
        QCOMPARE(s.model.surfaceIds().size(), 1);

        const Wells::Restored restored = Wells::restore(s.model, b);
        QVERIFY2(restored.result.ok, qPrintable(restored.result.message));
        QCOMPARE(restored.how, QStringLiteral("returned"));
        const SurfaceRecord *surface = s.model.surface(s.model.surfaceOfAnchor(restored.anchorId));
        QVERIFY(surface && !surface->primary && surface->inMainWindow);
        QCOMPARE(surface->geometry, frame);
        QVERIFY(s.model.validate().isEmpty());
    }

    void pinnedAndMinimizedIsOneEntry()
    {
        Setup s;
        s.open(editorView(QStringLiteral("a")));
        const QString b = s.open(editorView(QStringLiteral("b")));

        QVERIFY(s.model.setViewPinned(b, true).ok);
        const WellEntry *entry = onlyEntry(s.model);
        QVERIFY(entry && entry->pinned);
        const QString entryId = entry->id;
        QCOMPARE(Wells::entryState(s.model, *entry), Wells::EntryState::PinnedOpen);
        QCOMPARE(s.model.setViewPinned(b, true).code, QStringLiteral("NO_CHANGE"));

        // Minimized, it joins the pinned entry instead of adding one.
        QVERIFY(Wells::minimize(s.model, b).ok);
        entry = onlyEntry(s.model);
        QVERIFY(entry);
        QCOMPARE(entry->id, entryId);
        QCOMPARE(Wells::entryState(s.model, *entry), Wells::EntryState::Minimized);

        // Clicking restores it; pinned, the entry stays.
        Wells::Restored clicked = Wells::activate(s.model, entryId);
        QVERIFY(clicked.result.ok);
        QCOMPARE(clicked.how, QStringLiteral("original"));
        entry = onlyEntry(s.model);
        QVERIFY(entry && entry->minimized.isEmpty());
        QCOMPARE(Wells::entryState(s.model, *entry), Wells::EntryState::PinnedOpen);

        // Clicking again brings the open one forward.
        s.model.activateView(s.model.anchor(s.main)->views.first());
        clicked = Wells::activate(s.model, entryId);
        QVERIFY(clicked.result.ok);
        QCOMPARE(clicked.how, QStringLiteral("revealed"));
        QCOMPARE(s.model.anchor(s.main)->activeViewId, b);

        // Closed, the pin remains and opens it again.
        const ResourceRef resource = s.model.view(b)->resource;
        QVERIFY(s.model.closeView(b).ok);
        entry = onlyEntry(s.model);
        QVERIFY(entry);
        QCOMPARE(Wells::entryState(s.model, *entry), Wells::EntryState::PinnedClosed);
        clicked = Wells::activate(s.model, entryId);
        QVERIFY(clicked.result.ok);
        QCOMPARE(clicked.how, QStringLiteral("opened"));
        QCOMPARE(s.model.view(clicked.viewId)->resource, resource);
        QCOMPARE(s.model.view(clicked.viewId)->typeId, ViewTypes::Editor);
        QCOMPARE(s.model.well(WorkspaceModel::kMainWellId)->entries.size(), 1);
        QVERIFY(s.model.validate().isEmpty());
    }

    void unpinningKeepsWhatIsStillPutAway()
    {
        Setup s;
        s.open(editorView(QStringLiteral("a")));
        const QString b = s.open(editorView(QStringLiteral("b")));
        QVERIFY(s.model.setViewPinned(b, true).ok);
        QVERIFY(Wells::minimize(s.model, b).ok);
        const QString entryId = onlyEntry(s.model)->id;

        QVERIFY(s.model.setWellEntryPinned(entryId, false).ok);
        QVERIFY(onlyEntry(s.model)); // still holds the minimized view
        QVERIFY(Wells::restore(s.model, b).result.ok);
        QVERIFY(s.model.well(WorkspaceModel::kMainWellId)->entries.isEmpty());

        // Pinned and closed, unpinning removes it.
        QVERIFY(s.model.setViewPinned(b, true).ok);
        QVERIFY(s.model.closeView(b).ok);
        QVERIFY(s.model.setWellEntryPinned(onlyEntry(s.model)->id, false).ok);
        QVERIFY(s.model.well(WorkspaceModel::kMainWellId)->entries.isEmpty());
        QVERIFY(s.model.validate().isEmpty());
    }

    void twoViewsOfOneResourceShareAnEntry()
    {
        Setup s;
        s.open(editorView(QStringLiteral("a")));
        const QString b1 = s.open(editorView(QStringLiteral("b")));
        QVERIFY(s.model.duplicateView(b1).ok);
        const QString b2 = s.model.anchor(s.main)->activeViewId;
        QVERIFY(b2 != b1);

        QVERIFY(Wells::minimize(s.model, b1).ok);
        QVERIFY(Wells::minimize(s.model, b2).ok);
        const WellEntry *entry = onlyEntry(s.model);
        QVERIFY(entry);
        QCOMPARE(entry->minimized.size(), 2);

        // A click brings back the latest one first.
        const Wells::Restored clicked = Wells::activate(s.model, entry->id);
        QCOMPARE(clicked.viewId, b2);
        QVERIFY(s.model.isMinimized(b1));
        QVERIFY(s.model.validate().isEmpty());
    }

    void closingAMinimizedViewTakesItOutOfTheWell()
    {
        Setup s;
        s.open(editorView(QStringLiteral("a")));
        const QString b = s.open(editorView(QStringLiteral("b")));
        QVERIFY(Wells::minimize(s.model, b).ok);

        QVERIFY(s.model.closeView(b).ok);
        QVERIFY(!s.model.view(b));
        QVERIFY(s.model.well(WorkspaceModel::kMainWellId)->entries.isEmpty());
        QCOMPARE(Wells::restore(s.model, b).result.code, QStringLiteral("NOT_MINIMIZED"));
        QVERIFY(s.model.validate().isEmpty());
    }

    void aRenameCarriesThePinAlong()
    {
        Setup s;
        s.open(editorView(QStringLiteral("a")));
        const QString b = s.open(editorView(QStringLiteral("b")));
        QVERIFY(s.model.setViewPinned(b, true).ok);
        QVERIFY(Wells::minimize(s.model, b).ok);

        ResourceRef renamed = s.model.view(b)->resource;
        renamed.id = QStringLiteral("col:lib:b-renamed");
        QVERIFY(s.model.setViewResource(b, renamed).ok);
        QCOMPARE(onlyEntry(s.model)->resource, renamed);
        QVERIFY(s.model.validate().isEmpty());
    }

    void agentsCantEmptyALockedPanel()
    {
        Setup s;
        s.open(editorView(QStringLiteral("a")));
        const QString b = s.open(editorView(QStringLiteral("b")));
        QVERIFY(s.model.setAnchorContentLocked(s.main, true).ok);

        QCOMPARE(Wells::minimize(s.model, b, {}, Origin::Agent).code, QStringLiteral("CONTENT_LOCKED"));
        QVERIFY(!s.model.isMinimized(b));
        QVERIFY(Wells::minimize(s.model, b, {}, Origin::User).ok);
    }

    void theWellMovesWithinItsWindow()
    {
        Setup s;
        QVERIFY(s.model.setWellPosition(WorkspaceModel::kMainWellId, QPointF(0.25, 0.4)).ok);
        QCOMPARE(s.model.well(WorkspaceModel::kMainWellId)->position, QPointF(0.25, 0.4));
        QVERIFY(s.model.setWellPosition(WorkspaceModel::kMainWellId, QPointF(-3, 7)).ok);
        QCOMPARE(s.model.well(WorkspaceModel::kMainWellId)->position, QPointF(0, 1));
        QCOMPARE(s.model.setWellPosition(QStringLiteral("nope"), QPointF()).code, QStringLiteral("NOT_FOUND"));
    }

    void aWellIsReactiveOrFixedAndKeepsIt()
    {
        Setup s;
        // Reactive by default: it follows the layout.
        QVERIFY(!s.model.well(WorkspaceModel::kMainWellId)->fixed);
        QVERIFY(!Wells::wellsList(s.model).first().toMap().value("fixed").toBool());
        const int before = s.model.revision();
        QVERIFY(s.model.setWellFixed(WorkspaceModel::kMainWellId, true).ok);
        QVERIFY(s.model.revision() != before);
        QVERIFY(Wells::wellsList(s.model).first().toMap().value("fixed").toBool());
        // Saying it again changes nothing.
        const int same = s.model.revision();
        QVERIFY(s.model.setWellFixed(WorkspaceModel::kMainWellId, true).ok);
        QCOMPARE(s.model.revision(), same);
        QCOMPARE(s.model.setWellFixed(QStringLiteral("nope"), true).code, QStringLiteral("NOT_FOUND"));

        // Saved and read back; a snapshot without the flag is reactive.
        const QJsonObject snapshot = Codec::encodeSnapshot(s.model);
        WorkspaceModel restored(counter());
        QVERIFY(Codec::decodeSnapshot(snapshot, ViewTypeRegistry::withBuiltIns(), restored).ok);
        QVERIFY(restored.well(WorkspaceModel::kMainWellId)->fixed);
        QVERIFY(s.model.setWellFixed(WorkspaceModel::kMainWellId, false).ok);
        WorkspaceModel reactive(counter());
        QVERIFY(Codec::decodeSnapshot(Codec::encodeSnapshot(s.model), ViewTypeRegistry::withBuiltIns(), reactive).ok);
        QVERIFY(!reactive.well(WorkspaceModel::kMainWellId)->fixed);
    }

    void goingBackToAnEarlierStateUndoesAMinimize()
    {
        Setup s;
        s.open(editorView(QStringLiteral("a")));
        const QString b = s.open(editorView(QStringLiteral("b")));
        const WorkspaceModel::Data before = s.model.data();

        QVERIFY(Wells::minimize(s.model, b).ok);
        const WorkspaceModel::Data minimized = s.model.data();

        // What the store's undo and redo do: put a whole state back.
        QVERIFY(s.model.setData(before).isEmpty());
        QCOMPARE(s.model.anchorOfView(b), s.main);
        QVERIFY(s.model.well(WorkspaceModel::kMainWellId)->entries.isEmpty());
        QVERIFY(s.model.setData(minimized).isEmpty());
        QVERIFY(s.model.isMinimized(b));
        QVERIFY(s.model.validate().isEmpty());
    }

    void saveAndLoadKeepsTheWell()
    {
        Setup s;
        const QString a = s.open(editorView(QStringLiteral("a")));
        const QString b = s.open(editorView(QStringLiteral("b")));
        const QString c = s.open(editorView(QStringLiteral("c")));
        const QString pinnedOnly = s.open(chatView(QStringLiteral("c1")));
        QVERIFY(s.model.setViewPinned(pinnedOnly, true).ok);
        QVERIFY(s.model.closeView(pinnedOnly).ok);
        s.model.updateViewState(b, {{QStringLiteral("scroll"), 120}});
        QVERIFY(Wells::minimize(s.model, b).ok);
        QVERIFY(s.model.setWellPosition(WorkspaceModel::kMainWellId, QPointF(0.3, 0.9)).ok);

        const QJsonObject snapshot = Codec::encodeSnapshot(s.model);
        WorkspaceModel restored(counter());
        const Codec::LoadReport report = Codec::decodeSnapshot(snapshot, ViewTypeRegistry::withBuiltIns(), restored);
        QVERIFY2(report.ok, qPrintable(report.error));
        QVERIFY(restored.validate().isEmpty());

        QVERIFY(restored.isMinimized(b));
        QVERIFY(!restored.viewIds().contains(b));
        QCOMPARE(restored.view(b)->state, s.model.view(b)->state);
        QCOMPARE(restored.anchor(s.main)->views, (QStringList{a, c}));
        const WellRecord *well = restored.well(WorkspaceModel::kMainWellId);
        QCOMPARE(well->position, QPointF(0.3, 0.9));
        QCOMPARE(well->entries.size(), 2);
        QCOMPARE(Wells::entryState(restored, well->entries.at(0)), Wells::EntryState::PinnedClosed);
        QCOMPARE(well->entries.at(0).typeId, ViewTypes::Chat);

        // After the restart it still goes back to its own tab.
        const Wells::Restored back = Wells::restore(restored, b);
        QVERIFY2(back.result.ok, qPrintable(back.result.message));
        QCOMPARE(back.how, QStringLiteral("original"));
        QCOMPARE(restored.anchor(s.main)->views, (QStringList{a, b, c}));
    }

    void anOlderArchivistStillOpensMinimizedViewsAsTabs()
    {
        Setup s;
        s.open(editorView(QStringLiteral("a")));
        const QString b = s.open(editorView(QStringLiteral("b")));
        QVERIFY(Wells::minimize(s.model, b).ok);

        // A snapshot read without Wells (as an older version does): nothing lost.
        QJsonObject snapshot = Codec::encodeSnapshot(s.model);
        QJsonObject session = snapshot.value(QStringLiteral("session")).toObject();
        session.remove(QStringLiteral("wells"));
        snapshot.insert(QStringLiteral("session"), session);
        WorkspaceModel older(counter());
        QVERIFY(Codec::decodeSnapshot(snapshot, ViewTypeRegistry::withBuiltIns(), older).ok);
        QVERIFY(older.viewIds().contains(b));
        QVERIFY(older.validate().isEmpty());

        // A damaged Well entry: its view opens as a tab instead of vanishing.
        snapshot = Codec::encodeSnapshot(s.model);
        session = snapshot.value(QStringLiteral("session")).toObject();
        QJsonArray wells = session.value(QStringLiteral("wells")).toArray();
        QJsonObject well = wells.at(0).toObject();
        QJsonArray entries = well.value(QStringLiteral("entries")).toArray();
        QJsonObject entry = entries.at(0).toObject();
        entry.remove(QStringLiteral("id"));
        entries.replace(0, entry);
        well.insert(QStringLiteral("entries"), entries);
        wells.replace(0, well);
        session.insert(QStringLiteral("wells"), wells);
        snapshot.insert(QStringLiteral("session"), session);
        WorkspaceModel damaged(counter());
        QVERIFY(Codec::decodeSnapshot(snapshot, ViewTypeRegistry::withBuiltIns(), damaged).ok);
        QVERIFY(damaged.viewIds().contains(b));
        QVERIFY(damaged.validate().isEmpty());
    }

    void switchingLayoutsLeavesTheWellAlone()
    {
        Setup s;
        s.open(editorView(QStringLiteral("a")));
        const QString b = s.open(editorView(QStringLiteral("b")));
        const QJsonObject layout = Codec::captureLayout(s.model, QStringLiteral("Mine"));
        QVERIFY(Wells::minimize(s.model, b).ok);

        const Codec::LoadReport report = Codec::applyLayout(layout, s.model);
        QVERIFY2(report.ok, qPrintable(report.error));
        QVERIFY(s.model.isMinimized(b));
        QVERIFY(!s.model.viewIds().contains(b));
        QVERIFY(s.model.validate().isEmpty());
        QVERIFY(Wells::restore(s.model, b).result.ok);
    }

    void goingBackToAnArrangementBringsItsWellBack()
    {
        Setup s;
        s.open(editorView(QStringLiteral("a")));
        const QString b = s.open(editorView(QStringLiteral("b")));
        const WorkspaceModel::Data arrangement = s.model.data();
        QVERIFY(Wells::minimize(s.model, b).ok);

        // What undo does (Arrangements::restore): the view is back in its tab.
        Arrangements::Result result = Arrangements::restore(s.model, arrangement, {});
        QVERIFY2(result.ok, qPrintable(result.message));
        QCOMPARE(s.model.anchorOfView(b), s.main);
        QVERIFY(s.model.well(WorkspaceModel::kMainWellId)->entries.isEmpty());
        QVERIFY(s.model.validate().isEmpty());

        // ...and redo puts it away again.
        QVERIFY(Wells::minimize(s.model, b).ok);
        const WorkspaceModel::Data minimized = s.model.data();
        QVERIFY(Wells::restore(s.model, b).result.ok);
        result = Arrangements::restore(s.model, minimized, {});
        QVERIFY2(result.ok, qPrintable(result.message));
        QVERIFY(s.model.isMinimized(b));
        QVERIFY(s.model.validate().isEmpty());

        // Opened and put away since the arrangement: it stays put away.
        const WorkspaceModel::Data earlier = s.model.data();
        const QString c = s.open(editorView(QStringLiteral("c")));
        QVERIFY(Wells::minimize(s.model, c).ok);
        result = Arrangements::restore(s.model, earlier, {});
        QVERIFY2(result.ok, qPrintable(result.message));
        QVERIFY(s.model.isMinimized(c));
        QVERIFY(s.model.isMinimized(b));
        QVERIFY(s.model.validate().isEmpty());

        // A view that step opened is closed, even from the Well.
        result = Arrangements::restore(s.model, earlier, {c});
        QVERIFY2(result.ok, qPrintable(result.message));
        QVERIFY(!s.model.view(c));
        QVERIFY(s.model.isMinimized(b));
        QVERIFY(s.model.validate().isEmpty());
    }

    void aCrowdedWellStaysConsistent()
    {
        Setup s;
        QStringList ids;
        for (int i = 0; i < 40; ++i) {
            ids.append(s.open(editorView(QStringLiteral("f%1").arg(i))));
        }
        const QStringList tabs = s.model.anchor(s.main)->views;
        for (const QString &id : tabs) {
            QVERIFY(Wells::minimize(s.model, id).ok);
        }
        QCOMPARE(s.model.well(WorkspaceModel::kMainWellId)->entries.size(), 40);
        QVERIFY(s.model.anchor(s.main)->views.isEmpty());
        QVERIFY(s.model.validate().isEmpty());
        // Brought back last-out-first-in, every tab is where it was.
        for (qsizetype i = tabs.size() - 1; i >= 0; --i) {
            QVERIFY(Wells::restore(s.model, tabs.at(i)).result.ok);
        }
        QCOMPARE(s.model.anchor(s.main)->views, tabs);
        QVERIFY(s.model.validate().isEmpty());
    }

    // ---- whole panels ------------------------------------------------------

    void aWholePanelGoesAwayAsOneAndComesBackTheSame()
    {
        Setup s;
        s.open(editorView(QStringLiteral("a")));
        const QString b = s.open(editorView(QStringLiteral("b")));
        QVERIFY(s.model.splitView(b, s.main, Edge::Right).ok);
        const QString panel = s.model.anchorOfView(b);
        const QString c = s.open(terminalView(QStringLiteral("t1")), panel);
        const QString d = s.open(chatView(QStringLiteral("c1")), panel);
        QVERIFY(s.model.activateView(c).ok);
        QVERIFY(s.model.renameAnchor(panel, QStringLiteral("Reference")).ok);
        QVERIFY(s.model.setAnchorSwitchable(panel, true).ok);
        const QStringList tabs = s.model.anchor(panel)->views;
        const QRectF frame = rectOf(s.model, panel);

        const OpResult put = Wells::minimizeAnchor(s.model, panel);
        QVERIFY2(put.ok, qPrintable(put.message));
        QVERIFY(!s.model.anchor(panel));
        QCOMPARE(s.model.anchorIds().size(), 1);
        const WellEntry *entry = onlyEntry(s.model);
        QVERIFY(entry && entry->panel && !entry->pinned);
        QCOMPARE(entry->title, QStringLiteral("Reference"));
        QCOMPARE(entry->minimized.size(), 3);
        for (const QString &id : tabs) {
            QVERIFY(s.model.isMinimized(id));
            QVERIFY(s.model.view(id)); // still the same views
        }
        QVERIFY(s.model.validate().isEmpty());

        // Clicking any of it brings the whole panel back where it was.
        const Wells::Restored back = Wells::activate(s.model, entry->id);
        QVERIFY2(back.result.ok, qPrintable(back.result.message));
        QCOMPARE(back.how, QStringLiteral("returned"));
        const QString again = back.anchorId;
        QCOMPARE(s.model.anchor(again)->views, tabs);
        QCOMPARE(s.model.anchor(again)->activeViewId, c);
        QCOMPARE(s.model.anchor(again)->name, QStringLiteral("Reference"));
        QVERIFY(s.model.anchor(again)->switchable);
        QCOMPARE(rectOf(s.model, again), frame);
        QCOMPARE(s.model.focusedAnchorId(), again);
        QVERIFY(s.model.well(WorkspaceModel::kMainWellId)->entries.isEmpty());
        QVERIFY(s.model.validate().isEmpty());
        Q_UNUSED(d);
    }

    void aFloatingPanelGoesAwayAndFloatsBackWithItsTabs()
    {
        Setup s;
        s.open(editorView(QStringLiteral("a")));
        const QString b = s.open(editorView(QStringLiteral("b")));
        const QRectF frame(120, 80, 420, 300);
        QVERIFY(s.model.detachView(b, frame, {}, Origin::User, true).ok);
        const QString panel = s.model.anchorOfView(b);
        const QString c = s.open(editorView(QStringLiteral("c")), panel);

        QVERIFY(Wells::minimizeAnchor(s.model, panel).ok);
        QCOMPARE(s.model.surfaceIds().size(), 1);

        // Asking for one of its tabs brings the panel.
        const Wells::Restored back = Wells::restore(s.model, c);
        QVERIFY2(back.result.ok, qPrintable(back.result.message));
        const SurfaceRecord *surface = s.model.surface(s.model.surfaceOfAnchor(back.anchorId));
        QVERIFY(surface && surface->inMainWindow);
        QCOMPARE(surface->geometry, frame);
        QCOMPARE(s.model.anchor(back.anchorId)->views, (QStringList{b, c}));
        QVERIFY(s.model.validate().isEmpty());
    }

    void theMainWindowsLastPanelStaysToTakeItsTabsBack()
    {
        Setup s;
        const QString a = s.open(editorView(QStringLiteral("a")));
        const QString b = s.open(editorView(QStringLiteral("b")));
        QVERIFY(Wells::minimizeAnchor(s.model, s.main).ok);
        QVERIFY(s.model.anchor(s.main));
        QVERIFY(s.model.anchor(s.main)->views.isEmpty());
        QVERIFY(s.model.validate().isEmpty());

        const Wells::Restored back = Wells::restore(s.model, a);
        QVERIFY(back.result.ok);
        QCOMPARE(back.how, QStringLiteral("original"));
        QCOMPARE(s.model.anchor(s.main)->views, (QStringList{a, b}));
        QCOMPARE(s.model.anchor(s.main)->activeViewId, b);
    }

    void lockedPanelsStayPut()
    {
        Setup s;
        s.open(editorView(QStringLiteral("a")));
        const QString b = s.open(editorView(QStringLiteral("b")));
        QVERIFY(s.model.splitView(b, s.main, Edge::Right).ok);
        const QString panel = s.model.anchorOfView(b);
        QVERIFY(s.model.setAnchorLayoutLocked(panel, true).ok);
        QCOMPARE(Wells::minimizeAnchor(s.model, panel).code, QStringLiteral("LAYOUT_LOCKED"));
        QVERIFY(s.model.setAnchorLayoutLocked(panel, false).ok);
        QVERIFY(s.model.setAnchorContentLocked(panel, true).ok);
        QVERIFY(!Wells::minimizeAnchor(s.model, panel, {}, Origin::Agent).ok);
        QVERIFY(Wells::minimizeAnchor(s.model, panel).ok); // the person still can
        const WellEntry *entry = onlyEntry(s.model);
        QVERIFY(entry->panelInfo.contentLocked);
        QVERIFY(s.model.setWellEntryPinned(entry->id, true).code == QStringLiteral("NOT_PINNABLE"));
    }

    void aPanelPutAwaySurvivesSaveLoadAndUndo()
    {
        Setup s;
        s.open(editorView(QStringLiteral("a")));
        const QString b = s.open(editorView(QStringLiteral("b")));
        QVERIFY(s.model.splitView(b, s.main, Edge::Right).ok);
        const QString panel = s.model.anchorOfView(b);
        const QString c = s.open(editorView(QStringLiteral("c")), panel);
        QVERIFY(s.model.renameAnchor(panel, QStringLiteral("Notes")).ok);
        const WorkspaceModel::Data before = s.model.data();
        QVERIFY(Wells::minimizeAnchor(s.model, panel).ok);

        const QJsonObject snapshot = Codec::encodeSnapshot(s.model);
        WorkspaceModel loaded(counter());
        const Codec::LoadReport report = Codec::decodeSnapshot(snapshot, ViewTypeRegistry::withBuiltIns(), loaded);
        QVERIFY2(report.ok, qPrintable(report.error));
        QVERIFY(loaded.validate().isEmpty());
        QVERIFY(loaded.isMinimized(b) && loaded.isMinimized(c));
        const WellEntry *entry = onlyEntry(loaded);
        QVERIFY(entry && entry->panel);
        QCOMPARE(entry->panelInfo.name, QStringLiteral("Notes"));
        const Wells::Restored back = Wells::restore(loaded, b);
        QVERIFY(back.result.ok);
        QCOMPARE(loaded.anchor(back.anchorId)->views, (QStringList{b, c}));
        QCOMPARE(loaded.anchor(back.anchorId)->name, QStringLiteral("Notes"));

        // Undo puts the panel itself back, same id.
        QVERIFY(s.model.setData(before).isEmpty());
        QCOMPARE(s.model.anchor(panel)->views, (QStringList{b, c}));
        QVERIFY(s.model.well(WorkspaceModel::kMainWellId)->entries.isEmpty());
    }

    void leaveOnTopIsSaved()
    {
        Setup s;
        s.open(editorView(QStringLiteral("a")));
        const QString b = s.open(editorView(QStringLiteral("b")));
        QVERIFY(s.model.detachView(b, QRectF(50, 50, 300, 200), {}, Origin::User, true).ok);
        const QString surfaceId = s.model.surfaceOfAnchor(s.model.anchorOfView(b));
        QVERIFY(!s.model.surface(surfaceId)->leaveOnTop);
        QVERIFY(s.model.setSurfaceLeaveOnTop(surfaceId, true).ok);
        QVERIFY(!s.model.setSurfaceLeaveOnTop(QStringLiteral("nope"), true).ok);

        WorkspaceModel loaded(counter());
        QVERIFY(Codec::decodeSnapshot(Codec::encodeSnapshot(s.model), ViewTypeRegistry::withBuiltIns(), loaded).ok);
        const SurfaceRecord *surface = loaded.surface(loaded.surfaceOfAnchor(loaded.anchorOfView(b)));
        QVERIFY(surface && surface->leaveOnTop);
    }
};

QTEST_GUILESS_MAIN(CoreWellsTest)
#include "core_wells_test.moc"

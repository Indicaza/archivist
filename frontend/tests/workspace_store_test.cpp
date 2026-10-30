#include "App/Domains/Documents/document_store.h"
#include "App/Domains/Documents/unsaved_recovery.h"
#include "App/Domains/Workspace/keyed_list_model.h"
#include "App/Domains/Workspace/workspace_store.h"
#include "Core/Domains/Observatory/live_events.h"
#include "Core/Domains/Observatory/live_session.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

namespace
{
QVariantMap documentResource(const QString &fileId, const QString &libraryId = QStringLiteral("lib-main"))
{
    return {
        {QStringLiteral("kind"), QStringLiteral("document")},
        {QStringLiteral("id"), QStringLiteral("col:") + libraryId + QLatin1Char(':') + fileId},
        {QStringLiteral("collectionId"), QStringLiteral("col")},
        {QStringLiteral("libraryId"), libraryId},
        {QStringLiteral("details"), QVariantMap{{QStringLiteral("fileId"), fileId}}},
    };
}

// Loads from an in-memory "disk" and records saves.
struct FakeDisk
{
    QHash<QString, QString> files; // libraryId:fileId -> text
    QList<QStringList> saves;      // {libraryId, fileId, text}
    QHash<QString, QString> modified; // libraryId:fileId -> modifiedAt
    int loads = 0;

    void attach(DocumentStore &store)
    {
        store.setLoader([this](const QString &libraryId, const QString &fileId, QObject *, const DocumentStore::LoadCallback &done) {
            ++loads;
            const QString key = libraryId + QLatin1Char(':') + fileId;
            if (!files.contains(key)) {
                done(false, QStringLiteral("missing"), {});
                return;
            }
            done(true, {}, {
                {QStringLiteral("content"), files.value(key)},
                {QStringLiteral("file"), QVariantMap{{QStringLiteral("modifiedAt"), modified.value(key, QStringLiteral("t0"))}}},
            });
        });
        store.setSaver([this](const QString &libraryId, const QString &fileId, const QString &text, const QString &) {
            saves.append({libraryId, fileId, text});
        });
    }
};

QString insertAt(int offset, const QString &text)
{
    return QStringLiteral("[{\"offset\":%1,\"length\":0,\"text\":\"%2\"}]").arg(offset).arg(text);
}

QStringList keys(QAbstractItemModel *model, const QByteArray &role)
{
    const int roleId = model->roleNames().key(role);
    QStringList result;
    for (int row = 0; row < model->rowCount(); ++row) {
        result.append(model->data(model->index(row, 0), roleId).toString());
    }
    return result;
}

QVariantMap agentFile(const QString &fileId, const QString &libraryId = QStringLiteral("lib-main"))
{
    return {
        {QStringLiteral("kind"), QStringLiteral("file")},
        {QStringLiteral("libraryId"), libraryId},
        {QStringLiteral("file"), QVariantMap{{QStringLiteral("id"), fileId}, {QStringLiteral("relativePath"), fileId}, {QStringLiteral("name"), fileId}}},
    };
}

QVariantMap agentShow(const QVariantList &items, const QString &where = {}, bool focus = false, bool asked = true)
{
    return {
        {QStringLiteral("action"), QStringLiteral("show")},
        {QStringLiteral("items"), items},
        {QStringLiteral("where"), where},
        {QStringLiteral("purpose"), QString()},
        {QStringLiteral("focus"), focus},
        {QStringLiteral("explicit"), asked},
        {QStringLiteral("chatId"), QStringLiteral("chat-1")},
    };
}

QVariantMap agentRestore(const QString &scope, const QString &layout = {})
{
    return {
        {QStringLiteral("action"), QStringLiteral("restore")},
        {QStringLiteral("scope"), scope},
        {QStringLiteral("layout"), layout},
        {QStringLiteral("explicit"), true},
        {QStringLiteral("chatId"), QStringLiteral("chat-1")},
    };
}

QStringList agentAreas(const WorkspaceStore &store)
{
    QStringList ids;
    for (const QVariant &area : store.describeForAgent().value(QStringLiteral("areas")).toList()) {
        ids.append(area.toMap().value(QStringLiteral("id")).toString());
    }
    return ids;
}

QStringList agentViews(const WorkspaceStore &store)
{
    QStringList ids;
    for (const QVariant &area : store.describeForAgent().value(QStringLiteral("areas")).toList()) {
        for (const QVariant &shown : area.toMap().value(QStringLiteral("shows")).toList()) {
            ids.append(shown.toMap().value(QStringLiteral("viewId")).toString());
        }
    }
    return ids;
}
} // namespace

class WorkspaceStoreTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(m_settingsDir.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
        QCoreApplication::setOrganizationName(QStringLiteral("ArchivistTest"));
        QCoreApplication::setApplicationName(QStringLiteral("workspace-store-test"));
    }

    void keyedModelKeepsRowsAcrossReorders()
    {
        KeyedListModel model(QStringLiteral("id"), {QStringLiteral("id"), QStringLiteral("value")});
        model.setRows({{{"id", "a"}, {"value", 1}}, {{"id", "b"}, {"value", 2}}, {{"id", "c"}, {"value", 3}}});

        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
        QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
        QSignalSpy moved(&model, &QAbstractItemModel::rowsMoved);
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);

        // Reorder plus one value change: no row is recreated.
        model.setRows({{{"id", "c"}, {"value", 3}}, {{"id", "a"}, {"value", 10}}, {{"id", "b"}, {"value", 2}}});
        QCOMPARE(keys(&model, "id"), QStringList({"c", "a", "b"}));
        QCOMPARE(inserted.count(), 0);
        QCOMPARE(removed.count(), 0);
        QVERIFY(moved.count() >= 1);
        QCOMPARE(changed.count(), 1);

        model.setRows({{{"id", "a"}, {"value", 10}}, {{"id", "d"}, {"value", 4}}});
        QCOMPARE(keys(&model, "id"), QStringList({"a", "d"}));
        QCOMPARE(removed.count(), 2);
        QCOMPARE(inserted.count(), 1);
    }

    void openRevealAndClose()
    {
        WorkspaceStore store;
        store.setCollection(QStringLiteral("open-reveal"));

        const QString a = store.openView(QStringLiteral("archivist.editor"), documentResource("a.cpp"), "a.cpp");
        QVERIFY(!a.isEmpty());
        QCOMPARE(store.focusedViewId(), a);

        // Opening the same document again reveals the existing view.
        const QString again = store.openView(QStringLiteral("archivist.editor"), documentResource("a.cpp"), "a.cpp");
        QCOMPARE(again, a);

        // Unless a duplicate is asked for.
        const QString copy = store.openView(
            QStringLiteral("archivist.editor"),
            documentResource("a.cpp"),
            "a.cpp",
            {{QStringLiteral("duplicate"), true}}
        );
        QVERIFY(copy != a);
        QCOMPARE(store.viewsForResource("document", "col:lib-main:a.cpp").size(), 2);
        QCOMPARE(store.views()->rowCount(), 2);

        QSignalSpy closed(&store, &WorkspaceStore::viewClosed);
        QVERIFY(store.closeView(copy).value("ok").toBool());
        QCOMPARE(closed.count(), 1);
        QCOMPARE(store.views()->rowCount(), 1);
    }

    void splitPublishesAnchorsAndSplitters()
    {
        WorkspaceStore store;
        store.setCollection(QStringLiteral("split"));
        const QString surface = store.primarySurfaceId();
        store.setSurfaceSize(surface, 1000, 600);
        QAbstractItemModel *anchors = store.anchors(surface);
        QAbstractItemModel *splitters = store.splitters(surface);

        const QString a = store.openView(QStringLiteral("archivist.editor"), documentResource("a.cpp"), "a");
        store.openView(QStringLiteral("archivist.editor"), documentResource("b.cpp"), "b");
        const QString created = store.splitView(a, {}, QStringLiteral("right"));
        QVERIFY(!created.isEmpty());
        QCOMPARE(anchors->rowCount(), 2);
        QCOMPARE(splitters->rowCount(), 1);

        // The view row carries its anchor's rect, so content can sit in one
        // flat layer and survive moves between anchors.
        const QVariantMap row = static_cast<KeyedListModel *>(store.views())->row(a);
        QCOMPARE(row.value("anchorId").toString(), created);
        QVERIFY(row.value("rectX").toDouble() > 400);

        const QVariantMap splitter = static_cast<KeyedListModel *>(splitters)->get(0);
        QVERIFY(store.resizeSplit(surface, splitter.value("nodeId").toString(), 0, 300));
        QVERIFY(qAbs(static_cast<KeyedListModel *>(anchors)->row(created).value("rectX").toDouble() - 306) < 1.0);
    }

    void dragPreviewsCommitsAndCancels()
    {
        WorkspaceStore store;
        // Docks at once: these are about what a dock does, not the dwell.
        store.setDockDwellMs(0);
        store.setCollection(QStringLiteral("drag"));
        const QString surface = store.primarySurfaceId();
        store.setSurfaceSize(surface, 1000, 600);
        KeyedListModel *anchors = static_cast<KeyedListModel *>(store.anchors(surface));

        const QString a = store.openView(QStringLiteral("archivist.editor"), documentResource("a.cpp"), "a");
        const QString b = store.openView(QStringLiteral("archivist.editor"), documentResource("b.cpp"), "b");
        const QString first = anchors->get(0).value("anchorId").toString();
        const int revision = store.model().revision();

        // Hover the right edge of the only anchor: preview shows a split,
        // the model is unchanged.
        QVERIFY(store.beginDrag(QStringLiteral("view"), b));
        store.updateDrag(surface, 950, 300);
        QVERIFY(store.dropAllowed());
        QCOMPARE(store.dropTarget().value("edge").toString(), QStringLiteral("right"));
        QVERIFY(!store.dragLandingRect().isEmpty());
        QVERIFY(anchors->row(first).value("rectWidth").toDouble() < 900);
        QCOMPARE(store.model().revision(), revision);

        // Escape: back to where it was.
        store.cancelDrag();
        QVERIFY(!store.dragActive());
        QCOMPARE(anchors->rowCount(), 1);
        QCOMPARE(anchors->row(first).value("rectWidth").toDouble(), 1000.0);

        // Drop for real.
        QVERIFY(store.beginDrag(QStringLiteral("view"), b));
        store.updateDrag(surface, 950, 300);
        QVERIFY(store.finishDrag());
        QCOMPARE(anchors->rowCount(), 2);
        QCOMPARE(store.view(b).value("anchorId").toString() == first, false);
        QCOMPARE(store.focusedViewId(), b);

        // A drop that would do nothing is refused, not applied.
        QVERIFY(store.beginDrag(QStringLiteral("anchor"), first));
        store.updateDragOverTabs(surface, first, -1);
        QVERIFY(!store.dropAllowed());
        QVERIFY(!store.finishDrag());
        QCOMPARE(store.view(a).value("anchorId").toString(), first);
    }

    // The drag publishes one state for the visual layer: its phase, the
    // target and whether it's valid (and why not), where it lands and how
    // the other panels would move. Nothing is committed before the drop,
    // and the drop is one undo step.
    void dragStateWalksThePhases()
    {
        WorkspaceStore store;
        // Docks at once: these are about what a dock does, not the dwell.
        store.setDockDwellMs(0);
        store.setCollection(QStringLiteral("magnet-phases"));
        const QString surface = store.primarySurfaceId();
        store.setSurfaceSize(surface, 1000, 600);
        KeyedListModel *anchors = static_cast<KeyedListModel *>(store.anchors(surface));
        store.openView(QStringLiteral("archivist.editor"), documentResource("a.cpp"), "a");
        const QString b = store.openView(QStringLiteral("archivist.editor"), documentResource("b.cpp"), "b");
        const QString first = anchors->get(0).value("anchorId").toString();
        QCOMPARE(store.dragState().value("phase").toString(), QStringLiteral("idle"));
        QVERIFY(!store.canUndo());

        QSignalSpy finished(&store, &WorkspaceStore::dragFinished);
        QVERIFY(store.beginDrag(QStringLiteral("view"), b));
        QVariantMap state = store.dragState();
        QCOMPARE(state.value("phase").toString(), QStringLiteral("pickup"));
        QCOMPARE(state.value("id").toString(), b);
        QCOMPARE(state.value("origin").toMap().value("anchorId").toString(), first);
        QCOMPARE(state.value("origin").toMap().value("rect").toMap().value("width").toDouble(), 1000.0);

        // Over nothing (a splitter or off the panels): free.
        store.updateDrag(surface, -50, -50);
        QCOMPARE(store.dragState().value("phase").toString(), QStringLiteral("free"));

        // Over its own place: refused, with a reason.
        store.updateDragOverTabs(surface, first, 1);
        state = store.dragState();
        QCOMPARE(state.value("phase").toString(), QStringLiteral("invalid"));
        QCOMPARE(state.value("reasonCode").toString(), QStringLiteral("NO_CHANGE"));
        QVERIFY(!store.dropRefusal().isEmpty());
        QVERIFY(!state.value("valid").toBool());

        // The right edge: a valid preview, the panel making room is in the
        // reflow, and the preview counter moves on.
        const int serial = state.value("previewSerial").toInt();
        const int revision = store.model().revision();
        store.updateDrag(surface, 950, 300);
        state = store.dragState();
        QCOMPARE(state.value("phase").toString(), QStringLiteral("preview"));
        QVERIFY(state.value("valid").toBool());
        QVERIFY(store.dropRefusal().isEmpty());
        QVERIFY(state.value("previewSerial").toInt() > serial);
        QVERIFY(state.value("landingRect").toMap().value("x").toDouble() > 400);
        bool firstShrinks = false;
        for (const QVariant &entry : state.value("reflow").toList()) {
            const QVariantMap move = entry.toMap();
            if (move.value("anchorId").toString() == first) {
                firstShrinks = move.value("to").toMap().value("width").toDouble() < move.value("from").toMap().value("width").toDouble();
            }
        }
        QVERIFY(firstShrinks);
        QCOMPARE(store.model().revision(), revision);
        QVERIFY(!store.canUndo());

        // Drop: one step, and undo puts it back.
        QVERIFY(store.finishDrag());
        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.last().at(0).toString(), QStringLiteral("committed"));
        QCOMPARE(store.dragState().value("phase").toString(), QStringLiteral("idle"));
        QCOMPARE(anchors->rowCount(), 2);
        QVERIFY(store.canUndo());
        QVERIFY(store.undoSummary().contains(QStringLiteral("b")));
        QVERIFY(store.undo());
        QCOMPARE(anchors->rowCount(), 1);
        QCOMPARE(store.view(b).value("anchorId").toString(), first);

        // Escape: cancelled, nothing recorded.
        QVERIFY(store.beginDrag(QStringLiteral("view"), b));
        store.updateDrag(surface, 950, 300);
        store.cancelDrag();
        QCOMPARE(finished.last().at(0).toString(), QStringLiteral("cancelled"));
        QCOMPARE(anchors->rowCount(), 1);
        QVERIFY(!store.canUndo());

        // Released on a refused target: refused, with the reason.
        QVERIFY(store.beginDrag(QStringLiteral("view"), b));
        store.updateDragOverTabs(surface, first, 1);
        QVERIFY(!store.finishDrag());
        QCOMPARE(finished.last().at(0).toString(), QStringLiteral("refused"));
        QCOMPARE(finished.last().at(1).toMap().value("code").toString(), QStringLiteral("NO_CHANGE"));
    }

    // A split that would leave a panel unusably small is refused instead of
    // squeezing everything.
    void crampedSplitsAreRefused()
    {
        WorkspaceStore store;
        // Docks at once: these are about what a dock does, not the dwell.
        store.setDockDwellMs(0);
        store.setCollection(QStringLiteral("magnet-cramped"));
        const QString surface = store.primarySurfaceId();
        store.setSurfaceSize(surface, 420, 600);
        KeyedListModel *anchors = static_cast<KeyedListModel *>(store.anchors(surface));
        store.openView(QStringLiteral("archivist.editor"), documentResource("a.cpp"), "a");
        const QString b = store.openView(QStringLiteral("archivist.editor"), documentResource("b.cpp"), "b");
        const QString c = store.openView(QStringLiteral("archivist.editor"), documentResource("c.cpp"), "c");
        const QString first = anchors->get(0).value("anchorId").toString();
        QVERIFY(!store.splitView(b, first, QStringLiteral("right")).isEmpty());

        QVERIFY(store.beginDrag(QStringLiteral("view"), c));
        store.updateDrag(surface, 405, 300);
        QVERIFY(!store.dropAllowed());
        QCOMPARE(store.dragState().value("reasonCode").toString(), QStringLiteral("TOO_SMALL"));
        QVERIFY(!store.finishDrag());
        QCOMPARE(anchors->rowCount(), 2);
    }

    // Return to previous position: a tab goes back into its panel, a
    // panel back beside its neighbour; each is one step, and doing it
    // again goes back to where it was moved.
    void magnetsReturnToTheirPreviousPlace()
    {
        WorkspaceStore store;
        // Docks at once: these are about what a dock does, not the dwell.
        store.setDockDwellMs(0);
        store.setCollection(QStringLiteral("magnet-return"));
        const QString surface = store.primarySurfaceId();
        store.setSurfaceSize(surface, 1200, 700);
        KeyedListModel *anchors = static_cast<KeyedListModel *>(store.anchors(surface));
        const QString a = store.openView(QStringLiteral("archivist.editor"), documentResource("a.cpp"), "a");
        const QString b = store.openView(QStringLiteral("archivist.editor"), documentResource("b.cpp"), "b");
        const QString chat = store.openView(QStringLiteral("archivist.chat"), {{"kind", "chat"}, {"id", "c1"}}, "Chat");
        const QString first = anchors->get(0).value("anchorId").toString();
        QVERIFY(!store.splitView(chat, first, QStringLiteral("right")).isEmpty());
        const QString chatAnchor = store.view(chat).value("anchorId").toString();
        QVERIFY(!store.canReturnToPrevious(QStringLiteral("view"), b));
        QCOMPARE(store.returnToPrevious(QStringLiteral("view"), b).value("code").toString(), QStringLiteral("NO_PLACE"));

        // Drag b into the chat's panel.
        QVERIFY(store.beginDrag(QStringLiteral("view"), b));
        store.updateDragOverTabs(surface, chatAnchor, -1);
        QVERIFY(store.finishDrag());
        QCOMPARE(store.view(b).value("anchorId").toString(), chatAnchor);

        QVERIFY(store.canReturnToPrevious(QStringLiteral("view"), b));
        QVariantMap back = store.returnToPrevious(QStringLiteral("view"), b);
        QVERIFY2(back.value("ok").toBool(), qPrintable(back.value("error").toString()));
        QCOMPARE(store.view(b).value("anchorId").toString(), first);
        QCOMPARE(store.anchor(first).value("views").toStringList(), (QStringList{a, b}));
        QVERIFY(store.undoSummary().startsWith(QStringLiteral("Put")));

        // Again: back to the chat's panel.
        QVERIFY(store.returnToPrevious(QStringLiteral("view"), b).value("ok").toBool());
        QCOMPARE(store.view(b).value("anchorId").toString(), chatAnchor);

        // Undo walks the same steps.
        QVERIFY(store.undo());
        QCOMPARE(store.view(b).value("anchorId").toString(), first);

        // A whole panel moved to the other side goes back.
        const double chatX = anchors->row(chatAnchor).value("rectX").toDouble();
        QVERIFY(store.beginDrag(QStringLiteral("anchor"), chatAnchor));
        store.updateDrag(surface, 30, 350);
        QVERIFY(store.dropAllowed());
        QVERIFY(store.finishDrag());
        QVERIFY(anchors->row(chatAnchor).value("rectX").toDouble() < chatX);
        QVERIFY(store.returnToPrevious(QStringLiteral("anchor"), chatAnchor).value("ok").toBool());
        QCOMPARE(anchors->row(chatAnchor).value("rectX").toDouble(), chatX);

        // Another project starts with no memory of these.
        store.setCollection(QStringLiteral("magnet-return-other"));
        QVERIFY(!store.canReturnToPrevious(QStringLiteral("anchor"), chatAnchor));
    }

    void draggedFromPanelStaysAliveWhileItFolds()
    {
        WorkspaceStore store;
        store.setCollection(QStringLiteral("vacate"));
        const QString surface = store.primarySurfaceId();
        store.setSurfaceSize(surface, 1000, 600);
        KeyedListModel *anchors = static_cast<KeyedListModel *>(store.anchors(surface));

        store.openView(QStringLiteral("archivist.editor"), documentResource("a.cpp"), "a");
        const QString b = store.openView(QStringLiteral("archivist.editor"), documentResource("b.cpp"), "b");
        const QString first = anchors->get(0).value("anchorId").toString();
        QVERIFY(!store.splitView(b, first, QStringLiteral("right")).isEmpty());
        const QString second = store.view(b).value("anchorId").toString();
        const double secondX = anchors->row(second).value("rectX").toDouble();

        // Joining the left panel's tabs moves nothing while aiming: the right
        // panel stays where it is (it folds once dropped).
        QVERIFY(store.beginDrag(QStringLiteral("view"), b));
        store.updateDragOverTabs(surface, first, -1);
        QVERIFY(store.dropAllowed());
        QVERIFY(!anchors->row(second).value("isVacating").toBool());
        QCOMPARE(anchors->row(second).value("rectX").toDouble(), secondX);

        // A split elsewhere folds the right panel away in the preview. Its
        // row stays drawable (it holds the pointer grab) at its old place,
        // marked as vacating.
        store.setDockDwellMs(0);
        store.updateDrag(surface, 10, 300);
        QVERIFY(store.dropAllowed());
        QVERIFY(anchors->row(second).value("isVacating").toBool());
        QVERIFY(!anchors->row(second).value("isHidden").toBool());
        QCOMPARE(anchors->row(second).value("rectX").toDouble(), secondX);

        store.cancelDrag();
        QVERIFY(!anchors->row(second).value("isVacating").toBool());

        // Dragging a whole group onto another's tabs.
        QVERIFY(store.beginDrag(QStringLiteral("anchor"), second));
        store.updateDragOverTabs(surface, first, -1);
        QVERIFY(store.dropAllowed());
        QVERIFY(store.finishDrag());
        QCOMPARE(anchors->rowCount(), 1);
        QCOMPARE(store.view(b).value("anchorId").toString(), first);
    }

    void tearOffOpensAFloatingWindowAndClosingBringsTabsBack()
    {
        WorkspaceStore store;
        // Docks at once: these are about what a dock does, not the dwell.
        store.setDockDwellMs(0);
        store.setScreensForTesting({{QStringLiteral("Main"), QRectF(0, 0, 1920, 1080), QRectF(0, 25, 1920, 1055), 2.0, true}});
        store.setCollection(QStringLiteral("float"));
        const QString surface = store.primarySurfaceId();
        store.setSurfaceSize(surface, 1000, 600);

        const QString a = store.openView(QStringLiteral("archivist.editor"), documentResource("a.cpp"), "a");
        const QString b = store.openView(QStringLiteral("archivist.terminal"), {{"kind", "terminal"}, {"id", "lib:t1"}}, "zsh");
        QSignalSpy surfaces(&store, &WorkspaceStore::surfacesChanged);

        // Drag the terminal tab out of every window: a drop opens a window.
        QVERIFY(store.beginDrag(QStringLiteral("view"), b));
        store.updateDragDetached(1200, 300, 640, 420, QStringLiteral("Main"));
        QVERIFY(store.dragDetached());
        QVERIFY(store.dropAllowed());
        QCOMPARE(store.detachRect().value("width").toDouble(), 640.0);
        // Back over the main window: an ordinary drop target again.
        store.updateDrag(surface, 950, 300);
        QVERIFY(!store.dragDetached());
        store.updateDragDetached(1200, 300, 640, 420, QStringLiteral("Main"));
        QVERIFY(store.finishDrag());

        QCOMPARE(store.floatingSurfaceIds().size(), 1);
        QCOMPARE(surfaces.count(), 1);
        const QString floating = store.floatingSurfaceIds().first();
        QCOMPARE(store.view(b).value("surfaceId").toString(), floating);
        QCOMPARE(store.view(a).value("surfaceId").toString(), surface);
        QCOMPARE(store.surface(floating).value("x").toDouble(), 1200.0);
        QCOMPARE(store.surface(floating).value("viewCount").toInt(), 1);
        QCOMPARE(store.focusedViewId(), b);

        // Its panel models publish like the main window's.
        store.setSurfaceSize(floating, 640, 420);
        KeyedListModel *floatingAnchors = static_cast<KeyedListModel *>(store.anchors(floating));
        QCOMPARE(floatingAnchors->rowCount(), 1);
        QCOMPARE(floatingAnchors->get(0).value("rectWidth").toDouble(), 640.0);

        // Tearing the only tab out of its own window again is refused: the
        // window itself is what moves.
        QVERIFY(store.beginDrag(QStringLiteral("view"), b));
        store.updateDragDetached(100, 100, 640, 420, QStringLiteral("Main"));
        QVERIFY(!store.dropAllowed());
        QVERIFY(!store.finishDrag());
        QCOMPARE(store.floatingSurfaceIds().size(), 1);

        // Dragging it into the main window works across windows.
        QVERIFY(store.beginDrag(QStringLiteral("view"), b));
        store.updateDragOverTabs(surface, store.view(a).value("anchorId").toString(), -1);
        QVERIFY(store.dropAllowed());
        QVERIFY(store.finishDrag());
        QCOMPARE(store.view(b).value("surfaceId").toString(), surface);
        QVERIFY(store.floatingSurfaceIds().isEmpty());

        // Closing a window keeps every tab.
        const QString again = store.detachView(b, 1200, 300, 640, 420, QStringLiteral("Main"));
        QVERIFY(!again.isEmpty());
        QVERIFY(store.setSurfaceGeometry(again, 1300, 320, 700, 500, QStringLiteral("Main"), false));
        QCOMPARE(store.surface(again).value("width").toDouble(), 700.0);
        QVERIFY(store.closeSurface(again));
        QVERIFY(store.floatingSurfaceIds().isEmpty());
        QCOMPARE(store.view(b).value("surfaceId").toString(), surface);
        QVERIFY(!store.closeSurface(surface));
    }

    // A drag floats by default; it docks only where it is aimed: a tab
    // strip at once, a panel edge once held there.
    void draggingFloatsUnlessAimed()
    {
        QString panel;
        QString b;
        {
            WorkspaceStore store;
            store.setCollection(QStringLiteral("float-default"));
            // Several floating panels side by side (none tucks into the Well).
            store.setFloatingPanelsTuckAway(false);
            const QString surface = store.primarySurfaceId();
            store.setSurfaceSize(surface, 1000, 600);
            KeyedListModel *anchors = static_cast<KeyedListModel *>(store.anchors(surface));
            store.openView(QStringLiteral("archivist.editor"), documentResource("a.cpp"), "a");
            b = store.openView(QStringLiteral("archivist.editor"), documentResource("b.cpp"), "b");
            const QString first = anchors->get(0).value("anchorId").toString();
            const int revision = store.model().revision();

            QVERIFY(store.beginDrag(QStringLiteral("view"), b));
            store.updateDragFloat(300, 200, 420, 300);
            store.updateDrag(surface, 500, 300);
            QVariantMap state = store.dragState();
            QCOMPARE(state.value("phase").toString(), QStringLiteral("free"));
            QVERIFY(store.dropFloats());
            QVERIFY(state.value("valid").toBool());
            QCOMPARE(state.value("floatRect").toMap().value("x").toDouble(), 300.0);

            // At the right edge: aiming, not docking yet...
            store.updateDrag(surface, 960, 300);
            state = store.dragState();
            QCOMPARE(state.value("phase").toString(), QStringLiteral("aiming"));
            QCOMPARE(state.value("aim").toMap().value("edge").toString(), QStringLiteral("right"));
            QVERIFY(!store.dropAllowed());
            // ...until it is held there.
            QTRY_COMPARE_WITH_TIMEOUT(store.dragState().value("phase").toString(), QStringLiteral("preview"), 2000);
            QVERIFY(store.dropAllowed());
            QVERIFY(!store.dropFloats());

            // Away from the edge: floating again. A tab strip docks at once.
            store.updateDrag(surface, 500, 300);
            QVERIFY(store.dropFloats());
            store.updateDragOverTabs(surface, first, 0);
            QCOMPARE(store.dragState().value("phase").toString(), QStringLiteral("preview"));
            store.updateDrag(surface, 500, 300);
            QCOMPARE(store.model().revision(), revision);

            // Let go in open space: a panel inside the main window, one step.
            QSignalSpy finished(&store, &WorkspaceStore::dragFinished);
            QVERIFY(store.finishDrag());
            QCOMPARE(finished.first().at(0).toString(), QStringLiteral("committed"));
            QVERIFY(store.floatingSurfaceIds().isEmpty());
            QCOMPARE(store.panelSurfaceIds().size(), 1);
            panel = store.panelSurfaceIds().first();
            QCOMPARE(store.view(b).value("surfaceId").toString(), panel);
            QVERIFY(store.surface(panel).value("inMainWindow").toBool());
            QCOMPARE(store.surface(panel).value("x").toDouble(), 300.0);
            QCOMPARE(store.undoSummary(), QStringLiteral("Floated “b”"));

            QVERIFY(store.undo());
            QVERIFY(store.panelSurfaceIds().isEmpty());
            QVERIFY(store.redo());
            QCOMPARE(store.panelSurfaceIds(), QStringList{panel});

            // Dragged again into open space: the same panel moves, and it
            // may hang partly off the window's left side.
            QVERIFY(store.beginDrag(QStringLiteral("view"), b));
            store.updateDragFloat(-150, 60, 420, 300);
            store.updateDrag(surface, 300, 200);
            QVERIFY(store.finishDrag());
            QCOMPARE(store.panelSurfaceIds(), QStringList{panel});
            QCOMPARE(store.surface(panel).value("x").toDouble(), -150.0);

            // Many at once.
            for (int index = 0; index < 10; index += 1) {
                const QString view = store.openView(QStringLiteral("archivist.editor"),
                    documentResource(QStringLiteral("n%1.cpp").arg(index)), QStringLiteral("n%1").arg(index));
                QVERIFY(store.beginDrag(QStringLiteral("view"), view));
                store.updateDragFloat(20 + index * 30, 20 + index * 20, 360, 240);
                QVERIFY(store.finishDrag());
            }
            QCOMPARE(store.panelSurfaceIds().size(), 11);
            store.saveNow();
        }
        {
            // Restarted: still panels in the main window, untouched by
            // monitors coming and going.
            WorkspaceStore store;
            store.setScreensForTesting({{QStringLiteral("Tiny"), QRectF(5000, 5000, 100, 100), QRectF(5000, 5000, 100, 100), 1.0, true}});
            store.setCollection(QStringLiteral("float-default"));
            QCOMPARE(store.panelSurfaceIds().size(), 11);
            QVERIFY(store.panelSurfaceIds().contains(panel));
            QVERIFY(store.floatingSurfaceIds().isEmpty());
            QCOMPARE(store.view(b).value("surfaceId").toString(), panel);
            QVERIFY(!store.recoverSurfaces().contains(panel));
            QCOMPARE(store.surface(panel).value("x").toDouble(), -150.0);
        }
    }

    void floatingPanelGeometryUpdatesBindings()
    {
        WorkspaceStore store;
        store.setCollection(QStringLiteral("floating-geometry-notify"));
        const QString main = store.primarySurfaceId();
        store.setSurfaceSize(main, 1200, 800);
        const QString view = store.openView(QStringLiteral("archivist.editor"), documentResource("resize.cpp"), "resize");
        QVERIFY(store.beginDrag(QStringLiteral("view"), view));
        store.updateDragFloat(120, 90, 440, 300);
        QVERIFY(store.finishDrag());

        QCOMPARE(store.panelSurfaceIds().size(), 1);
        const QString panel = store.panelSurfaceIds().first();
        QSignalSpy revised(&store, &WorkspaceStore::revisionChanged);
        QSignalSpy recreated(&store, &WorkspaceStore::surfacesChanged);
        const int before = store.revision();

        QVERIFY(store.setSurfaceGeometry(panel, 170, 130, 690, 470, {}, false));
        QCOMPARE(store.surface(panel).value("width").toDouble(), 690.0);
        QCOMPARE(store.surface(panel).value("height").toDouble(), 470.0);
        QCOMPARE(store.revision(), before + 1);
        QCOMPARE(revised.count(), 1);
        QCOMPARE(recreated.count(), 0);

        QVERIFY(store.setSurfaceGeometry(panel, 170, 130, 690, 470, {}, false));
        QCOMPARE(store.revision(), before + 1);
        QCOMPARE(revised.count(), 1);
    }

    // A tab or panel dropped on a sidebar tab (or its bottom) docks there:
    // one surface per spot, one undo step, kept across a restart. When the
    // tab is deleted, what was in it floats instead of disappearing.
    void sidebarSlotsHoldPanels()
    {
        const QString tab = QStringLiteral("navigation.custom.1");
        const QString bottom = QStringLiteral("sidebar.bottom");
        QString b;
        QString c;
        QString inTab;
        {
            WorkspaceStore store;
            store.setCollection(QStringLiteral("sidebar-slots"));
            store.setSidebarSlots({QStringLiteral("navigation.archive"), tab});
            const QString surface = store.primarySurfaceId();
            store.setSurfaceSize(surface, 1000, 600);
            store.openView(QStringLiteral("archivist.editor"), documentResource("a.cpp"), "a");
            b = store.openView(QStringLiteral("archivist.editor"), documentResource("b.cpp"), "b");
            c = store.openView(QStringLiteral("archivist.editor"), documentResource("c.cpp"), "c");
            const int revision = store.model().revision();

            QVERIFY(store.beginDrag(QStringLiteral("view"), b));
            store.updateDragFloat(300, 200, 420, 300);
            store.updateDrag(surface, 500, 300);
            QVERIFY(store.dropFloats());
            store.updateDragOverSlot(tab);
            QVariantMap state = store.dragState();
            QCOMPARE(state.value("slot").toString(), tab);
            QCOMPARE(state.value("phase").toString(), QStringLiteral("preview"));
            QVERIFY(store.dropAllowed());
            QVERIFY(!store.dropFloats());
            QCOMPARE(store.model().revision(), revision);

            QVERIFY(store.finishDrag());
            inTab = store.sidebarSurface(tab);
            QVERIFY(!inTab.isEmpty());
            QCOMPARE(store.view(b).value("surfaceId").toString(), inTab);
            QCOMPARE(store.surface(inTab).value("sidebarSlot").toString(), tab);
            QVERIFY(store.sidebarSurfaceIds().contains(inTab));
            QVERIFY(store.panelSurfaceIds().isEmpty());
            QVERIFY(store.floatingSurfaceIds().isEmpty());
            QCOMPARE(store.undoSummary(), QStringLiteral("Docked “b” in the sidebar"));

            QVERIFY(store.undo());
            QVERIFY(store.sidebarSurface(tab).isEmpty());
            QCOMPARE(store.view(b).value("surfaceId").toString(), surface);
            QVERIFY(store.redo());
            QCOMPARE(store.sidebarSurface(tab), inTab);

            // That spot is taken now.
            QVERIFY(store.beginDrag(QStringLiteral("view"), c));
            store.updateDragOverSlot(tab);
            QVERIFY(!store.dropAllowed());
            QCOMPARE(store.dragState().value("phase").toString(), QStringLiteral("invalid"));
            // The bottom is free.
            store.updateDragOverSlot(bottom);
            QVERIFY(store.dropAllowed());
            QVERIFY(store.finishDrag());
            const QString inBottom = store.sidebarSurface(bottom);
            QVERIFY(!inBottom.isEmpty());
            QCOMPARE(store.view(c).value("surfaceId").toString(), inBottom);

            // Out of the sidebar into open space: it floats, the spot frees.
            QVERIFY(store.beginDrag(QStringLiteral("view"), c));
            store.updateDragFloat(200, 120, 420, 300);
            store.updateDrag(surface, 500, 300);
            QVERIFY(store.finishDrag());
            QVERIFY(store.sidebarSurface(bottom).isEmpty());
            QCOMPARE(store.panelSurfaceIds().size(), 1);
            QCOMPARE(store.view(c).value("surfaceId").toString(), store.panelSurfaceIds().first());

            // Return puts it back in the sidebar.
            QVERIFY(store.returnToPrevious(QStringLiteral("view"), c).value("ok").toBool());
            QCOMPARE(store.view(c).value("surfaceId").toString(), store.sidebarSurface(bottom));
            store.saveNow();
        }
        {
            WorkspaceStore store;
            store.setCollection(QStringLiteral("sidebar-slots"));
            QCOMPARE(store.sidebarSurface(tab), inTab);
            QCOMPARE(store.view(b).value("surfaceId").toString(), inTab);
            QVERIFY(!store.sidebarSurface(bottom).isEmpty());

            // The tab is deleted: its panel floats, nothing is closed.
            store.setSidebarSlots({QStringLiteral("navigation.archive")});
            QVERIFY(store.sidebarSurface(tab).isEmpty());
            QCOMPARE(store.panelSurfaceIds(), QStringList{inTab});
            QCOMPARE(store.view(b).value("surfaceId").toString(), inTab);
            QVERIFY(!store.sidebarSurface(bottom).isEmpty());
        }
    }

    // The odd cases around sidebar spots: a magnet over its own spot, locks,
    // cancelling, a tab deleted mid-drag, the spot's height, closing it.
    void sidebarSlotEdgeCases()
    {
        const QString tab = QStringLiteral("navigation.custom.1");
        const QString other = QStringLiteral("navigation.custom.2");
        const QString bottom = QStringLiteral("sidebar.bottom");
        WorkspaceStore store;
        store.setCollection(QStringLiteral("sidebar-edges"));
        // Several floating panels side by side (none tucks into the Well).
        store.setFloatingPanelsTuckAway(false);
        store.setSidebarSlots({tab, other});
        const QString surface = store.primarySurfaceId();
        store.setSurfaceSize(surface, 1000, 600);
        KeyedListModel *anchors = static_cast<KeyedListModel *>(store.anchors(surface));
        const QString a = store.openView(QStringLiteral("archivist.editor"), documentResource("a.cpp"), "a");
        const QString b = store.openView(QStringLiteral("archivist.editor"), documentResource("b.cpp"), "b");
        const QString c = store.openView(QStringLiteral("archivist.editor"), documentResource("c.cpp"), "c");
        const QString first = anchors->get(0).value("anchorId").toString();

        // Cancelled over a spot: nothing changes, nothing to undo.
        const int revision = store.model().revision();
        const QString undoBefore = store.undoSummary();
        QVERIFY(store.beginDrag(QStringLiteral("view"), b));
        store.updateDragOverSlot(tab);
        QVERIFY(store.dropAllowed());
        store.cancelDrag();
        QCOMPARE(store.model().revision(), revision);
        QCOMPARE(store.undoSummary(), undoBefore);
        QVERIFY(store.sidebarSurface(tab).isEmpty());
        QVERIFY(store.dragState().value("slot").toString().isEmpty());

        // Over a spot, then open space: it floats again, the spot lets go.
        QVERIFY(store.beginDrag(QStringLiteral("view"), b));
        store.updateDragFloat(300, 200, 420, 300);
        store.updateDragOverSlot(tab);
        QVERIFY(!store.dropFloats());
        store.updateDrag(surface, 500, 300);
        QVERIFY(store.dropFloats());
        QVERIFY(store.dragState().value("slot").toString().isEmpty());
        // ...and back over the spot, then a tab strip: it docks in the strip.
        store.updateDragOverSlot(tab);
        store.updateDragOverTabs(surface, first, 0);
        QVERIFY(store.dragState().value("slot").toString().isEmpty());
        store.cancelDrag();

        // Docked; dragged over its own spot it can't go "there" again, and
        // it says so plainly.
        QVERIFY(store.beginDrag(QStringLiteral("view"), b));
        store.updateDragOverSlot(tab);
        QVERIFY(store.finishDrag());
        const QString inTab = store.sidebarSurface(tab);
        QVERIFY(!inTab.isEmpty());
        QVERIFY(store.beginDrag(QStringLiteral("view"), b));
        store.updateDragOverSlot(tab);
        QVERIFY(!store.dropAllowed());
        QCOMPARE(store.dragState().value("phase").toString(), QStringLiteral("invalid"));
        QCOMPARE(store.dropRefusal(), QStringLiteral("It's already there."));
        store.cancelDrag();

        // Alone in its spot, it moves to another spot: the same surface goes.
        QVERIFY(store.beginDrag(QStringLiteral("view"), b));
        store.updateDragOverSlot(other);
        QVERIFY(store.dropAllowed());
        QVERIFY(store.finishDrag());
        QCOMPARE(store.sidebarSurface(other), inTab);
        QVERIFY(store.sidebarSurface(tab).isEmpty());

        // A whole panel docks too; a locked one can't leave its place.
        QVERIFY(store.setAnchorLayoutLocked(first, true));
        if (store.beginDrag(QStringLiteral("anchor"), first)) {
            store.updateDragOverSlot(tab);
            QVERIFY(!store.dropAllowed());
            QVERIFY(!store.dropRefusal().isEmpty());
            store.finishDrag();
        }
        QVERIFY(store.sidebarSurface(tab).isEmpty());
        QCOMPARE(store.view(c).value("anchorId").toString(), first);
        QVERIFY(store.setAnchorLayoutLocked(first, false));

        // The tab is deleted while the drag is over it: the drop floats
        // instead of docking into a spot that is gone.
        QVERIFY(store.beginDrag(QStringLiteral("view"), c));
        store.updateDragFloat(260, 160, 420, 300);
        store.updateDragOverSlot(tab);
        store.setSidebarSlots({other});
        QVERIFY(store.dragState().value("slot").toString().isEmpty());
        QVERIFY(store.finishDrag());
        QVERIFY(store.sidebarSurface(tab).isEmpty());
        QCOMPARE(store.panelSurfaceIds().size(), 1);
        QCOMPARE(store.view(c).value("surfaceId").toString(), store.panelSurfaceIds().first());

        // The bottom spot keeps the height it is given, and stays a spot.
        QVERIFY(store.beginDrag(QStringLiteral("view"), a));
        store.updateDragOverSlot(bottom);
        QVERIFY(store.finishDrag());
        const QString inBottom = store.sidebarSurface(bottom);
        QVERIFY(store.setSurfaceGeometry(inBottom, 0, 0, 280, 330, QString(), false));
        QCOMPARE(store.surface(inBottom).value("height").toDouble(), 330.0);
        QCOMPARE(store.surface(inBottom).value("sidebarSlot").toString(), bottom);
        QVERIFY(!store.surface(inBottom).value("inMainWindow").toBool());
        QVERIFY(store.panelSurfaceIds().size() == 1);

        // Closing a spot's surface sends its tabs back to the main layout.
        QVERIFY(store.closeSurface(inBottom));
        QVERIFY(store.sidebarSurface(bottom).isEmpty());
        QCOMPARE(store.view(a).value("surfaceId").toString(), surface);

        // Nothing was ever closed or copied along the way.
        for (const QString &view : {a, b, c}) {
            QVERIFY(!store.view(view).value("viewId").toString().isEmpty());
        }
        QCOMPARE(store.model().viewIds().size(), qsizetype(3));
    }

    // A drag must never leave anything behind: one drag at a time, no
    // second finish, no late timer after a cancel, and a source closed
    // mid-drag is refused, not crashed on. Layouts leave the sidebar alone.
    void dragsLeaveNothingBehind()
    {
        WorkspaceStore store;
        store.setCollection(QStringLiteral("drag-edges"));
        store.setSidebarSlots({QStringLiteral("navigation.custom.1")});
        const QString surface = store.primarySurfaceId();
        store.setSurfaceSize(surface, 1000, 600);
        const QString a = store.openView(QStringLiteral("archivist.editor"), documentResource("a.cpp"), "a");
        const QString b = store.openView(QStringLiteral("archivist.editor"), documentResource("b.cpp"), "b");
        const QString c = store.openView(QStringLiteral("archivist.editor"), documentResource("c.cpp"), "c");

        // One at a time.
        QVERIFY(store.beginDrag(QStringLiteral("view"), b));
        QVERIFY(!store.beginDrag(QStringLiteral("view"), c));
        store.cancelDrag();
        QVERIFY(!store.finishDrag());
        QVERIFY(!store.dragActive());

        // Cancelled while aiming: the dwell never fires later.
        const int revision = store.model().revision();
        QVERIFY(store.beginDrag(QStringLiteral("view"), b));
        store.updateDrag(surface, 960, 300);
        QCOMPARE(store.dragState().value("phase").toString(), QStringLiteral("aiming"));
        store.cancelDrag();
        QTest::qWait(500);
        QCOMPARE(store.dragState().value("phase").toString(), QStringLiteral("idle"));
        QVERIFY(!store.dragActive());
        QCOMPARE(store.model().revision(), revision);

        // The source is closed while it is being carried: the drop is
        // refused and nothing else moves.
        QVERIFY(store.beginDrag(QStringLiteral("view"), c));
        store.updateDragOverSlot(QStringLiteral("navigation.custom.1"));
        store.closeView(c, true);
        QSignalSpy finished(&store, &WorkspaceStore::dragFinished);
        store.finishDrag();
        QVERIFY(store.sidebarSurface(QStringLiteral("navigation.custom.1")).isEmpty());
        QVERIFY(store.view(c).isEmpty());
        QVERIFY(!store.dragActive());
        if (!finished.isEmpty()) {
            QVERIFY(finished.first().at(0).toString() != QStringLiteral("committed"));
        }

        // Something in the sidebar stays there when a layout is applied.
        QVERIFY(store.beginDrag(QStringLiteral("view"), b));
        store.updateDragOverSlot(QStringLiteral("navigation.custom.1"));
        QVERIFY(store.finishDrag());
        const QString docked = store.sidebarSurface(QStringLiteral("navigation.custom.1"));
        QVERIFY(!docked.isEmpty());
        QVERIFY(store.applyPreset(QStringLiteral("development")));
        QVERIFY(!store.view(b).isEmpty());
        QVERIFY(!store.view(a).isEmpty());
        QCOMPARE(store.sidebarSurface(QStringLiteral("navigation.custom.1")), docked);
        QCOMPARE(store.view(b).value("surfaceId").toString(), docked);
    }

    // Pointer jitter where two zones meet keeps one preview; hundreds of
    // moves over every kind of target commit nothing and record nothing;
    // only the release does, once.
    // Joining a panel's tabs: the caret's slot is what lands, nothing
    // moves while aiming, the strip keeps the pointer a little beyond its
    // edge, and each drop is one undo step that keeps every view's id.
    void tabJoinsCommitWhatTheyShow()
    {
        WorkspaceStore store;
        store.setCollection(QStringLiteral("joins"));
        store.setDockDwellMs(0);
        const QString surface = store.primarySurfaceId();
        store.setSurfaceSize(surface, 1000, 600);
        KeyedListModel *anchors = static_cast<KeyedListModel *>(store.anchors(surface));
        const QString a = store.openView(QStringLiteral("archivist.editor"), documentResource("a.cpp"), "a");
        const QString b = store.openView(QStringLiteral("archivist.editor"), documentResource("b.cpp"), "b");
        const QString c = store.openView(QStringLiteral("archivist.editor"), documentResource("c.cpp"), "c");
        const QString first = anchors->get(0).value("anchorId").toString();
        const QString right = store.splitView(c, first, QStringLiteral("right"));
        QVERIFY(!right.isEmpty());
        const auto viewsOf = [&](const QString &anchorId) { return store.model().anchor(anchorId) ? store.model().anchor(anchorId)->views : QStringList{}; };
        QCOMPARE(viewsOf(first), (QStringList{a, b}));
        const int revision = store.model().revision();

        // c (alone on the right) onto the left panel's strip, between a and b.
        QVERIFY(store.beginDrag(QStringLiteral("view"), c));
        QCOMPARE(store.tabStripAt(surface, 100, 10, 32).value("anchorId").toString(), first);
        QVERIFY(store.tabStripAt(surface, 100, 40, 32).isEmpty()); // not taken on the way in
        store.updateDragOverTabs(surface, first, 1);
        QVERIFY(store.dropAllowed());
        QCOMPARE(store.dragState().value("target").toMap().value("index").toInt(), 1);
        QVERIFY(store.dragState().value("reflow").toList().isEmpty()); // nothing moves
        QCOMPARE(store.model().revision(), revision);
        // Drifting just below the strip keeps the join; well below lets go.
        QCOMPARE(store.tabStripAt(surface, 100, 40, 32).value("anchorId").toString(), first);
        QVERIFY(store.tabStripAt(surface, 100, 70, 32).isEmpty());
        QVERIFY(store.finishDrag());
        QCOMPARE(viewsOf(first), (QStringList{a, c, b}));
        QCOMPARE(store.model().anchorIds(surface).size(), 1);

        QVERIFY(store.undo());
        QCOMPARE(viewsOf(first), (QStringList{a, b}));
        QCOMPARE(store.model().anchorOfView(c) != first, true);
        QVERIFY(store.redo());
        QCOMPARE(viewsOf(first), (QStringList{a, c, b}));

        // Reordering rightwards in its own panel lands at the caret.
        QVERIFY(store.beginDrag(QStringLiteral("view"), a));
        store.updateDragOverTabs(surface, first, 2);
        QVERIFY(store.dropAllowed());
        QVERIFY(store.finishDrag());
        QCOMPARE(viewsOf(first), (QStringList{c, a, b}));
        // Either side of itself: refused, nothing changes.
        const int settled = store.model().revision();
        QVERIFY(store.beginDrag(QStringLiteral("view"), a));
        store.updateDragOverTabs(surface, first, 2);
        QVERIFY(!store.dropAllowed());
        QVERIFY(!store.finishDrag());
        QCOMPARE(store.model().revision(), settled);
        QCOMPARE(viewsOf(first), (QStringList{c, a, b}));
        QCOMPARE(store.model().view(a)->id, a);
    }

    // Cancelling at any stage puts everything back, with no history.
    void cancellingAnywhereRestores()
    {
        WorkspaceStore store;
        store.setCollection(QStringLiteral("cancels"));
        const QString surface = store.primarySurfaceId();
        store.setSurfaceSize(surface, 1000, 600);
        KeyedListModel *anchors = static_cast<KeyedListModel *>(store.anchors(surface));
        store.openView(QStringLiteral("archivist.editor"), documentResource("a.cpp"), "a");
        const QString b = store.openView(QStringLiteral("archivist.editor"), documentResource("b.cpp"), "b");
        const QString c = store.openView(QStringLiteral("archivist.editor"), documentResource("c.cpp"), "c");
        const QString first = anchors->get(0).value("anchorId").toString();
        QVERIFY(!store.splitView(c, first, QStringLiteral("right")).isEmpty());
        const int revision = store.model().revision();
        const QString summary = store.undoSummary();
        const auto unchanged = [&]() {
            return store.model().revision() == revision && store.undoSummary() == summary
                && store.model().anchorOfView(b) == first && !store.dragActive();
        };

        // Picked up only.
        QVERIFY(store.beginDrag(QStringLiteral("view"), b));
        store.cancelDrag();
        QVERIFY(unchanged());
        // Free, floating.
        QVERIFY(store.beginDrag(QStringLiteral("view"), b));
        store.updateDragFloat(200, 200, 400, 300);
        store.updateDrag(surface, 300, 300);
        store.cancelDrag();
        QVERIFY(unchanged());
        // Aiming (the dwell running).
        store.setDockDwellMs(10000);
        QVERIFY(store.beginDrag(QStringLiteral("view"), b));
        store.updateDrag(surface, 990, 300);
        QCOMPARE(store.dragState().value("phase").toString(), QStringLiteral("aiming"));
        store.cancelDrag();
        QVERIFY(unchanged());
        // A split previewed.
        store.setDockDwellMs(0);
        QVERIFY(store.beginDrag(QStringLiteral("view"), b));
        store.updateDrag(surface, 990, 300);
        QCOMPARE(store.dragState().value("phase").toString(), QStringLiteral("preview"));
        store.cancelDrag();
        QVERIFY(unchanged());
        // A tab join previewed, then across to a split, then to open space.
        QVERIFY(store.beginDrag(QStringLiteral("view"), b));
        store.updateDragOverTabs(surface, store.model().anchorOfView(c), 0);
        QVERIFY(store.dropAllowed());
        store.updateDrag(surface, 990, 300);
        store.updateDrag(surface, 750, 300);
        store.cancelDrag();
        QVERIFY(unchanged());
    }

    void previewsHoldSteadyAndNeverCommit()
    {
        WorkspaceStore store;
        store.setCollection(QStringLiteral("steady"));
        store.setDockDwellMs(0);
        store.setSidebarSlots({QStringLiteral("navigation.custom.1")});
        const QString surface = store.primarySurfaceId();
        store.setSurfaceSize(surface, 1000, 600);
        KeyedListModel *anchors = static_cast<KeyedListModel *>(store.anchors(surface));
        store.openView(QStringLiteral("archivist.editor"), documentResource("a.cpp"), "a");
        const QString b = store.openView(QStringLiteral("archivist.editor"), documentResource("b.cpp"), "b");
        const QString c = store.openView(QStringLiteral("archivist.editor"), documentResource("c.cpp"), "c");
        const QString first = anchors->get(0).value("anchorId").toString();
        QVERIFY(!store.splitView(c, first, QStringLiteral("right")).isEmpty());
        const QString summary = store.undoSummary();
        const int revision = store.model().revision();
        const int views = int(store.model().viewIds().size());

        QVERIFY(store.beginDrag(QStringLiteral("view"), b));
        store.updateDragFloat(200, 120, 400, 300);
        // Into the left panel's right band, then jitter around the border
        // between the two panels: one target, one preview.
        store.updateDrag(surface, 480, 300);
        const QVariantMap chosen = store.dragState().value("target").toMap();
        QVERIFY(!chosen.isEmpty());
        const int serial = store.dragState().value("previewSerial").toInt();
        for (int step = 0; step < 40; step += 1) {
            store.updateDrag(surface, 494 + (step % 2 ? 6 : -4), 300 + step % 3);
        }
        QCOMPARE(store.dragState().value("target").toMap().value("anchorId"), chosen.value("anchorId"));
        QCOMPARE(store.dragState().value("target").toMap().value("edge"), chosen.value("edge"));
        QCOMPARE(store.dragState().value("previewSerial").toInt(), serial);

        // Everywhere at once, fast.
        for (int step = 0; step < 300; step += 1) {
            const int kind = step % 5;
            if (kind == 0) {
                store.updateDrag(surface, (step * 37) % 1000, (step * 53) % 600);
            } else if (kind == 1) {
                store.updateDragOverTabs(surface, first, step % 3);
            } else if (kind == 2) {
                store.updateDragOverSlot(QStringLiteral("navigation.custom.1"));
            } else if (kind == 3) {
                store.updateDrag(QString(), 0, 0);
            } else {
                store.updateDrag(surface, 990, (step * 7) % 600);
            }
            QCOMPARE(store.model().revision(), revision);
        }
        QCOMPARE(store.undoSummary(), summary);
        store.cancelDrag();
        QCOMPARE(store.model().revision(), revision);
        QCOMPARE(store.undoSummary(), summary);
        QCOMPARE(int(store.model().viewIds().size()), views);

        // Then a real drop: exactly one step, and Undo puts it all back.
        QVERIFY(store.beginDrag(QStringLiteral("view"), b));
        store.updateDrag(surface, 990, 300);
        QVERIFY(store.dropAllowed());
        QVERIFY(store.finishDrag());
        QVERIFY(store.undoSummary() != summary);
        QVERIFY(store.undo());
        QCOMPARE(store.undoSummary(), summary);
        QCOMPARE(store.view(b).value("anchorId").toString(), first);
    }

    // The tab and panel menus float without a drag, one undo step each.
    void menusFloatTabsAndPanels()
    {
        WorkspaceStore store;
        store.setCollection(QStringLiteral("float-menu"));
        // Several floating panels side by side (none tucks into the Well).
        store.setFloatingPanelsTuckAway(false);
        const QString surface = store.primarySurfaceId();
        store.setSurfaceSize(surface, 1000, 600);
        KeyedListModel *anchors = static_cast<KeyedListModel *>(store.anchors(surface));
        store.openView(QStringLiteral("archivist.editor"), documentResource("a.cpp"), "a");
        const QString b = store.openView(QStringLiteral("archivist.editor"), documentResource("b.cpp"), "b");
        const QString c = store.openView(QStringLiteral("archivist.editor"), documentResource("c.cpp"), "c");
        const QString first = anchors->get(0).value("anchorId").toString();
        const QString split = store.splitView(c, first, QStringLiteral("right"));
        QVERIFY(!split.isEmpty());

        const QString tabPanel = store.floatView(b, 80, 60, 480, 320);
        QVERIFY(!tabPanel.isEmpty());
        QCOMPARE(store.view(b).value("surfaceId").toString(), tabPanel);
        QVERIFY(store.surface(tabPanel).value("inMainWindow").toBool());

        const QString anchorPanel = store.floatAnchor(split, 140, 90, 480, 320);
        QVERIFY(!anchorPanel.isEmpty());
        QCOMPARE(store.panelSurfaceIds(), (QStringList{tabPanel, anchorPanel}));
        QCOMPARE(store.view(c).value("surfaceId").toString(), anchorPanel);

        // Back again, step by step; and Return puts the tab back.
        QVERIFY(store.undo());
        QCOMPARE(store.panelSurfaceIds(), QStringList{tabPanel});
        QVERIFY(store.returnToPrevious(QStringLiteral("view"), b).value("ok").toBool());
        QCOMPARE(store.view(b).value("anchorId").toString(), first);
        QVERIFY(store.panelSurfaceIds().isEmpty());
    }

    void floatingWindowsSurviveRestartAndLostMonitors()
    {
        const QList<Workspace::ScreenInfo> twoScreens = {
            {QStringLiteral("Laptop"), QRectF(0, 0, 1512, 982), QRectF(0, 25, 1512, 957), 2.0, true},
            {QStringLiteral("Studio"), QRectF(1512, -200, 2560, 1440), QRectF(1512, -200, 2560, 1440), 1.0, false},
        };
        const QList<Workspace::ScreenInfo> laptopOnly = {twoScreens.first()};
        QString floating;
        QString view;
        {
            WorkspaceStore store;
            store.setScreensForTesting(twoScreens);
            store.setCollection(QStringLiteral("monitors"));
            store.openView(QStringLiteral("archivist.editor"), documentResource("a.cpp"), "a");
            view = store.openView(QStringLiteral("archivist.editor"), documentResource("b.cpp"), "b");
            floating = store.detachView(view, 2000, 100, 900, 700, QStringLiteral("Studio"));
            QVERIFY(!floating.isEmpty());
            store.saveNow();
        }
        {
            // Restarted with the external monitor unplugged: the window comes
            // back on the laptop, sized to fit, with its tab.
            WorkspaceStore store;
            store.setScreensForTesting(laptopOnly);
            store.setCollection(QStringLiteral("monitors"));
            QCOMPARE(store.floatingSurfaceIds(), QStringList{floating});
            QCOMPARE(store.view(view).value("surfaceId").toString(), floating);
            const QVariantMap rect = store.surface(floating);
            QVERIFY(QRectF(0, 0, 1512, 982).contains(QRectF(rect.value("x").toDouble(), rect.value("y").toDouble(),
                rect.value("width").toDouble(), rect.value("height").toDouble())));

            // Plugged back in: it returns to the monitor the user chose.
            QSignalSpy recovered(&store, &WorkspaceStore::surfaceRecovered);
            store.setScreensForTesting(twoScreens);
            QCOMPARE(store.recoverSurfaces(), QStringList{floating});
            QCOMPARE(recovered.count(), 1);
            QCOMPARE(store.surface(floating).value("x").toDouble(), 2000.0);
            QCOMPARE(store.surface(floating).value("screen").toString(), QStringLiteral("Studio"));
        }
    }

    void snapshotsArePerCollection()
    {
        QString view;
        {
            WorkspaceStore store;
            store.setCollection(QStringLiteral("persist-one"));
            view = store.openView(QStringLiteral("archivist.editor"), documentResource("keep.cpp"), "keep.cpp");
            store.splitView(view, {}, QStringLiteral("bottom"));

            // Switching away saves; the other Collection starts fresh.
            store.setCollection(QStringLiteral("persist-two"));
            QCOMPARE(store.views()->rowCount(), 0);
            store.openView(QStringLiteral("archivist.chat"), {{"kind", "chat"}, {"id", "chat-9"}}, "Chat");
            store.saveNow();
        }

        WorkspaceStore restored;
        QSignalSpy spy(&restored, &WorkspaceStore::workspaceRestored);
        restored.setCollection(QStringLiteral("persist-one"));
        QCOMPARE(spy.count(), 1);
        QVERIFY(spy.at(0).at(1).toBool());
        QCOMPARE(restored.views()->rowCount(), 1);
        QCOMPARE(restored.view(view).value("title").toString(), QStringLiteral("keep.cpp"));
        QVERIFY(restored.hasSavedWorkspace(QStringLiteral("persist-two")));

        restored.setCollection(QStringLiteral("persist-two"));
        QCOMPARE(restored.viewIdsOfType(QStringLiteral("archivist.chat")).size(), 1);
    }

    void presetsKeepEveryView()
    {
        WorkspaceStore store;
        store.setCollection(QStringLiteral("presets"));
        store.openView(QStringLiteral("archivist.editor"), documentResource("a.cpp"), "a");
        store.openView(QStringLiteral("archivist.chat"), {{"kind", "chat"}, {"id", "c1"}}, "Chat");
        store.openView(QStringLiteral("archivist.terminal"), {{"kind", "terminal"}, {"id", "t1"}}, "Terminal");

        for (const QVariant &preset : store.presets()) {
            QVERIFY(store.applyPreset(preset.toMap().value("id").toString()));
            QCOMPARE(store.views()->rowCount(), 3);
            QVERIFY(store.model().validate().isEmpty());
        }
        QVERIFY(!store.applyPreset(QStringLiteral("nope")));
    }

    void editorViewsShareOneBuffer()
    {
        FakeDisk disk;
        disk.files.insert(QStringLiteral("lib-main:a.cpp"), QStringLiteral("hello"));
        DocumentStore documents;
        disk.attach(documents);
        WorkspaceStore store;
        store.setDocumentStore(&documents);
        store.setCollection(QStringLiteral("buffers"));

        const QString docId = QStringLiteral("col:lib-main:a.cpp");
        const QString first = store.openView(QStringLiteral("archivist.editor"), documentResource("a.cpp"), "a.cpp");
        const QString second = store.duplicateView(first, QStringLiteral("right"));
        QVERIFY(!second.isEmpty());
        QCOMPARE(disk.loads, 1);
        QCOMPARE(documents.holders(docId).size(), 2);
        QCOMPARE(documents.snapshot(docId).value("text").toString(), QStringLiteral("hello"));

        // An edit from one page reaches the other through documentEdited.
        QSignalSpy edited(&documents, &DocumentStore::documentEdited);
        QVariantMap result = documents.applyEdits(docId, QStringLiteral("page-1"), 1, insertAt(5, "!"));
        QVERIFY2(result.value("ok").toBool(), qPrintable(result.value("error").toString()));
        QCOMPARE(edited.count(), 1);
        QCOMPARE(documents.snapshot(docId).value("text").toString(), QStringLiteral("hello!"));
        QVERIFY(documents.isDirty(docId));

        // A page that missed that edit is refused and must resync.
        result = documents.applyEdits(docId, QStringLiteral("page-2"), 1, insertAt(0, ">"));
        QVERIFY(!result.value("ok").toBool());
        QVERIFY(result.value("conflict").toBool());

        // Moving and closing one of two views never asks.
        QVERIFY(store.moveView(second, store.view(first).value("anchorId").toString()));
        QVERIFY(store.closeView(second).value("ok").toBool());
        QCOMPARE(documents.holders(docId).size(), 1);

        // Closing the last view of unsaved work asks first.
        QVariantMap close = store.closeView(first);
        QVERIFY(!close.value("ok").toBool());
        QVERIFY(close.value("needsConfirmation").toBool());
        QCOMPARE(close.value("documents").toStringList(), QStringList({docId}));

        // Forced, the view goes but the unsaved text stays recoverable.
        QVERIFY(store.closeView(first, true).value("ok").toBool());
        QVERIFY(documents.dirtyDocuments().contains(docId));
        QCOMPARE(documents.snapshot(docId).value("text").toString(), QStringLiteral("hello!"));

        // Reopening finds the same buffer, not the disk text.
        const QString reopened = store.openView(QStringLiteral("archivist.editor"), documentResource("a.cpp"), "a.cpp");
        QVERIFY(!reopened.isEmpty());
        QCOMPARE(disk.loads, 1);
        QCOMPARE(documents.snapshot(docId).value("text").toString(), QStringLiteral("hello!"));
    }

    void namedLayoutsSaveApplyAndUndo()
    {
        QSettings settings;
        settings.remove(QStringLiteral("workspace/docking/layouts"));
        WorkspaceStore store;
        store.setCollection(QStringLiteral("layouts"));
        const QString surface = store.primarySurfaceId();
        store.setSurfaceSize(surface, 1000, 600);

        const QString a = store.openView(QStringLiteral("archivist.editor"), documentResource("a.cpp"), "a");
        const QString chat = store.openView(QStringLiteral("archivist.chat"), {{QStringLiteral("kind"), QStringLiteral("chat")}, {QStringLiteral("id"), QStringLiteral("c1")}}, "Chat");

        QVERIFY(store.applyPreset(QStringLiteral("research")));
        QCOMPARE(store.currentLayoutId(), QStringLiteral("research"));
        QCOMPARE(store.currentLayoutName(), QStringLiteral("Research"));
        QVERIFY(store.canUndoLayout());
        const QString chatAnchorInResearch = store.view(chat).value("anchorId").toString();
        QCOMPARE(chatAnchorInResearch, QStringLiteral("research.chat"));

        // Thumbnails cover the window and name what each panel is for.
        const QVariantList preview = store.layoutPreview(QStringLiteral("development"));
        QCOMPARE(preview.size(), 3);
        QStringList kinds;
        for (const QVariant &rect : preview) {
            kinds.append(rect.toMap().value("kind").toString());
            QVERIFY(rect.toMap().value("width").toDouble() > 0.1);
        }
        QVERIFY(kinds.contains(QStringLiteral("chat")));
        QVERIFY(kinds.contains(QStringLiteral("terminal")));

        // Save the current arrangement, switch away, come back.
        const QString saved = store.saveLayout(QStringLiteral("  My Research  "));
        QVERIFY(saved.startsWith(QStringLiteral("user.")));
        QCOMPARE(store.savedLayouts().size(), 1);
        QCOMPARE(store.savedLayouts().first().toMap().value("name").toString(), QStringLiteral("My Research"));
        QVERIFY(store.applyPreset(QStringLiteral("development")));
        QCOMPARE(store.view(chat).value("anchorId").toString(), QStringLiteral("dev.chat"));
        QVERIFY(store.applyLayout(saved));
        QCOMPARE(store.view(chat).value("anchorId").toString(), chatAnchorInResearch);
        QCOMPARE(store.layoutPreview(saved).size(), store.layoutPreview(QString()).size());

        // Undo puts back the arrangement before the last change; views stay.
        QVERIFY(store.undoLayout());
        QCOMPARE(store.view(chat).value("anchorId").toString(), QStringLiteral("dev.chat"));
        QCOMPARE(store.currentLayoutId(), QStringLiteral("development"));
        QVERIFY(!store.view(a).isEmpty());
        // One history: redo goes forward again, undo again goes further back.
        QVERIFY(store.canRedo());
        QVERIFY(store.redo());
        QCOMPARE(store.view(chat).value("anchorId").toString(), chatAnchorInResearch);
        QCOMPARE(store.currentLayoutId(), saved);
        QVERIFY(!store.canRedo());
        QVERIFY(store.undo());
        QVERIFY(store.undo());
        QCOMPARE(store.currentLayoutId(), saved); // saved from Research just before
        QCOMPARE(store.view(chat).value("anchorId").toString(), chatAnchorInResearch);
        QVERIFY(store.canUndoLayout()); // switching to Research

        // Saving under the same name replaces; rename and delete.
        QCOMPARE(store.saveLayout(QStringLiteral("my research")), saved);
        QCOMPARE(store.savedLayouts().size(), 1);
        QVERIFY(store.renameLayout(saved, QStringLiteral("Deep Work")));
        QCOMPARE(store.currentLayoutName(), QStringLiteral("Deep Work"));
        QVERIFY(store.deleteLayout(saved));
        QVERIFY(store.savedLayouts().isEmpty());
        QVERIFY(store.currentLayoutId().isEmpty());
        QVERIFY(store.saveLayout(QStringLiteral("   ")).isEmpty());
    }

    void unsavedEditsSurviveACrash()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("recovery/unsaved.json"));
        const QString docId = QStringLiteral("col:lib-main:a.cpp");

        FakeDisk disk;
        disk.files.insert(QStringLiteral("lib-main:a.cpp"), QStringLiteral("hello"));

        {
            DocumentStore documents;
            disk.attach(documents);
            UnsavedRecovery recovery(&documents, path);
            WorkspaceStore store;
            store.setDocumentStore(&documents);
            store.setCollection(QStringLiteral("recovery"));
            store.openView(QStringLiteral("archivist.editor"), documentResource("a.cpp"), "a.cpp");

            // Nothing unsaved: no file.
            recovery.flush();
            QVERIFY(!QFile::exists(path));

            QVERIFY(documents.applyEdits(docId, QStringLiteral("page"), 1, insertAt(5, " world")).value("ok").toBool());
            recovery.flush();
            QVERIFY(QFile::exists(path));
            // The process dies here: no save, no discard.
        }

        {
            DocumentStore documents;
            disk.attach(documents);
            UnsavedRecovery recovery(&documents, path);
            QCOMPARE(recovery.restore(), 1);
            QCOMPARE(documents.recoveredDocuments(), QStringList({docId}));
            QVERIFY(documents.isDirty(docId));
            QCOMPARE(documents.snapshot(docId).value("text").toString(), QStringLiteral("hello world"));
            QVERIFY(documents.snapshot(docId).value("recovered").toBool());
            QVERIFY(!documents.snapshot(docId).value("diskChanged").toBool());

            // A view opening the file gets the recovered text, not the disk.
            WorkspaceStore store;
            store.setDocumentStore(&documents);
            store.setCollection(QStringLiteral("recovery"));
            store.openView(QStringLiteral("archivist.editor"), documentResource("a.cpp"), "a.cpp");
            QCOMPARE(documents.snapshot(docId).value("text").toString(), QStringLiteral("hello world"));

            // Saving clears it, and the recovery file goes away.
            documents.save(docId);
            documents.noteFileSaved(QStringLiteral("lib-main"), QStringLiteral("a.cpp"), {});
            QVERIFY(!documents.isDirty(docId));
            QVERIFY(documents.recoveredDocuments().isEmpty());
            recovery.flush();
            QVERIFY(!QFile::exists(path));
        }
    }

    void editsSurviveTheFileChangingOnDisk()
    {
        const QString docId = QStringLiteral("col:lib-main:a.cpp");
        FakeDisk disk;
        disk.files.insert(QStringLiteral("lib-main:a.cpp"), QStringLiteral("hello"));
        DocumentStore documents;
        disk.attach(documents);
        WorkspaceStore store;
        store.setDocumentStore(&documents);
        store.setCollection(QStringLiteral("conflict"));
        store.openView(QStringLiteral("archivist.editor"), documentResource("a.cpp"), "a.cpp");
        QVERIFY(documents.applyEdits(docId, QStringLiteral("page"), 1, insertAt(5, "!")).value("ok").toBool());

        // Someone else writes the file. Our edits stay; the buffer is flagged.
        disk.files.insert(QStringLiteral("lib-main:a.cpp"), QStringLiteral("HELLO"));
        disk.modified.insert(QStringLiteral("lib-main:a.cpp"), QStringLiteral("t1"));
        documents.reload(docId);
        QVERIFY(documents.isDirty(docId));
        QVERIFY(documents.snapshot(docId).value("diskChanged").toBool());
        QCOMPARE(documents.snapshot(docId).value("text").toString(), QStringLiteral("hello!"));

        // Keep mine: still unsaved, no longer flagged, saves against t1.
        documents.keepUnsavedOverDisk(docId);
        QVERIFY(documents.isDirty(docId));
        QVERIFY(!documents.snapshot(docId).value("diskChanged").toBool());
        QCOMPARE(documents.snapshot(docId).value("modifiedAt").toString(), QStringLiteral("t1"));

        // Or take the file: the disk text, clean.
        disk.files.insert(QStringLiteral("lib-main:a.cpp"), QStringLiteral("HELLO again"));
        documents.reload(docId);
        QVERIFY(documents.snapshot(docId).value("diskChanged").toBool());
        QSignalSpy reset(&documents, &DocumentStore::documentReset);
        documents.takeDiskVersion(docId);
        QCOMPARE(reset.count(), 1);
        QVERIFY(!documents.isDirty(docId));
        QCOMPARE(documents.snapshot(docId).value("text").toString(), QStringLiteral("HELLO again"));
    }

    void savesGoToTheDocumentsOwnWorktree()
    {
        FakeDisk disk;
        disk.files.insert(QStringLiteral("lib-main:x.md"), QStringLiteral("main"));
        disk.files.insert(QStringLiteral("lib-feature:x.md"), QStringLiteral("feature"));
        DocumentStore documents;
        disk.attach(documents);
        WorkspaceStore store;
        store.setDocumentStore(&documents);
        store.setCollection(QStringLiteral("worktrees"));

        // The same path in two worktrees: two documents, side by side.
        const QString mainView = store.openView(QStringLiteral("archivist.editor"), documentResource("x.md"), "x.md");
        const QString featureView = store.openView(
            QStringLiteral("archivist.editor"),
            documentResource("x.md", QStringLiteral("lib-feature")),
            "x.md"
        );
        QVERIFY(mainView != featureView);
        store.splitView(featureView, {}, QStringLiteral("right"));

        const QString featureDoc = QStringLiteral("col:lib-feature:x.md");
        QVERIFY(documents.applyEdits(featureDoc, "p", 1, insertAt(0, "+")).value("ok").toBool());
        documents.save(featureDoc);
        QCOMPARE(disk.saves.size(), 1);
        QCOMPARE(disk.saves.first(), QStringList({"lib-feature", "x.md", "+feature"}));

        // Typing while the save is in flight keeps the newer text dirty.
        QVERIFY(documents.applyEdits(featureDoc, "p", 2, insertAt(0, "+")).value("ok").toBool());
        documents.noteFileSaved(QStringLiteral("lib-feature"), QStringLiteral("x.md"), {});
        QVERIFY(documents.isDirty(featureDoc));
        QVERIFY(!documents.isDirty(QStringLiteral("col:lib-main:x.md")));

        documents.save(featureDoc);
        documents.noteFileSaved(QStringLiteral("lib-feature"), QStringLiteral("x.md"), {});
        QVERIFY(!documents.isDirty(featureDoc));
    }


    // ---- Anchor workstations ---------------------------------------------

    void anchorConfigurationIsPublishedAndPersisted()
    {
        QString shelf;
        {
            WorkspaceStore store;
            store.setCollection(QStringLiteral("anchor-config"));
            shelf = store.focusedAnchorId();
            store.openView(QStringLiteral("archivist.editor"), documentResource("a.cpp"), "a.cpp");

            QVERIFY(store.renameAnchor(shelf, QStringLiteral("Reference Shelf")));
            QVERIFY(store.setAnchorSwitchable(shelf, true));
            QVERIFY(store.setAnchorLayoutLocked(shelf, true));
            QVERIFY(store.setAnchorLibrary(shelf, QStringLiteral("wt-a")));

            QAbstractItemModel *anchors = store.anchors(store.primarySurfaceId());
            QCOMPARE(keys(anchors, "displayName"), QStringList({QStringLiteral("Reference Shelf")}));
            QCOMPARE(keys(anchors, "layoutLocked"), QStringList({QStringLiteral("true")}));
            QCOMPARE(keys(anchors, "switchable"), QStringList({QStringLiteral("true")}));
            QCOMPARE(keys(anchors, "contextLibraryId"), QStringList({QStringLiteral("wt-a")}));

            // The UI asks what is allowed, and why not.
            const QVariantMap caps = store.anchorCapabilities(shelf);
            QVERIFY(!caps.value("close").toBool());
            QVERIFY(caps.value("restrictions").toMap().value("close").toString().contains(QStringLiteral("Reference Shelf")));

            // Closing is refused with the reason, and nothing changes.
            QSignalSpy failed(&store, &WorkspaceStore::operationFailed);
            const QVariantMap closed = store.closeAnchor(shelf, true);
            QVERIFY(!closed.value("ok").toBool());
            QVERIFY(closed.value("error").toString().contains(QStringLiteral("locked")));
            QCOMPARE(store.views()->rowCount(), 1);
            QVERIFY(!store.applyPreset(QStringLiteral("development")));
            QCOMPARE(failed.count(), 1);
            store.saveNow();
        }

        WorkspaceStore restored;
        restored.setCollection(QStringLiteral("anchor-config"));
        const QVariantMap anchor = restored.anchor(shelf);
        QCOMPARE(anchor.value("name").toString(), QStringLiteral("Reference Shelf"));
        QVERIFY(anchor.value("switchable").toBool());
        QVERIFY(anchor.value("layoutLocked").toBool());
        QCOMPARE(anchor.value("libraryId").toString(), QStringLiteral("wt-a"));
    }

    void layoutUndoBringsBackAnchorProperties()
    {
        QSettings().remove(QStringLiteral("workspace/docking/layouts"));
        WorkspaceStore store;
        store.setCollection(QStringLiteral("anchor-undo"));
        const QString chat = store.openView(QStringLiteral("archivist.chat"), {{QStringLiteral("kind"), QStringLiteral("chat")}, {QStringLiteral("id"), QStringLiteral("c1")}}, "Chat");
        const QString anchorId = store.view(chat).value("anchorId").toString();
        QVERIFY(store.renameAnchor(anchorId, QStringLiteral("Architecture Chat")));
        QVERIFY(store.setAnchorContentLocked(anchorId, true));
        QVERIFY(store.setAnchorSwitchable(anchorId, true));

        QVERIFY(store.applyPreset(QStringLiteral("research")));
        QVERIFY(store.anchor(store.view(chat).value("anchorId").toString()).value("name").toString().isEmpty());

        QVERIFY(store.undoLayout());
        const QVariantMap back = store.anchor(store.view(chat).value("anchorId").toString());
        QCOMPARE(back.value("name").toString(), QStringLiteral("Architecture Chat"));
        QVERIFY(back.value("contentLocked").toBool());
        QVERIFY(back.value("switchable").toBool());
    }

    void lockedChatIsNotRepointedAutomatically()
    {
        WorkspaceStore store;
        store.setCollection(QStringLiteral("anchor-chat-lock"));
        const QString architecture = store.openView(QStringLiteral("archivist.chat"), {{QStringLiteral("kind"), QStringLiteral("chat")}, {QStringLiteral("id"), QStringLiteral("architecture")}}, "Architecture");
        const QString locked = store.view(architecture).value("anchorId").toString();
        const QString other = store.createAnchor(locked, QStringLiteral("right"));
        QVERIFY(store.setAnchorContentLocked(locked, true));
        QVERIFY(store.focusAnchor(locked));

        // Another agent changing the selected chat can't repoint the view...
        QSignalSpy failed(&store, &WorkspaceStore::operationFailed);
        QVERIFY(!store.setViewResource(architecture, {{QStringLiteral("kind"), QStringLiteral("chat")}, {QStringLiteral("id"), QStringLiteral("developer")}}, QStringLiteral("automatic")));
        QCOMPARE(store.view(architecture).value("resource").toMap().value("id").toString(), QStringLiteral("architecture"));
        QCOMPARE(failed.count(), 0); // expected, so not reported as an error

        // ...and new content lands somewhere else even though it is focused.
        QCOMPARE(store.preferredAnchorFor(QStringLiteral("archivist.chat")), other);
        QCOMPARE(store.preferredAnchorFor(QStringLiteral("archivist.editor")), other);
        const QString developer = store.openView(QStringLiteral("archivist.chat"), {{QStringLiteral("kind"), QStringLiteral("chat")}, {QStringLiteral("id"), QStringLiteral("developer")}}, "Developer");
        QCOMPARE(store.view(developer).value("anchorId").toString(), other);

        // The person can still put something there on purpose.
        const QString explicitView = store.openView(QStringLiteral("archivist.editor"), documentResource("n.md"), "n.md", {{QStringLiteral("anchorId"), locked}});
        QCOMPARE(store.view(explicitView).value("anchorId").toString(), locked);
        QVERIFY(store.openView(QStringLiteral("archivist.editor"), documentResource("m.md"), "m.md", {{QStringLiteral("anchorId"), locked}, {QStringLiteral("origin"), QStringLiteral("agent")}}).isEmpty());
    }

    void switchingShelfResourcesKeepsUnsavedBuffers()
    {
        FakeDisk disk;
        disk.files.insert(QStringLiteral("lib-main:a.cpp"), QStringLiteral("alpha"));
        disk.files.insert(QStringLiteral("wt-b:b.cpp"), QStringLiteral("beta"));
        DocumentStore documents;
        disk.attach(documents);
        WorkspaceStore store;
        store.setDocumentStore(&documents);
        store.setCollection(QStringLiteral("anchor-shelf"));
        const QString shelf = store.focusedAnchorId();
        QVERIFY(store.setAnchorSwitchable(shelf, true));

        const QString a = store.openView(QStringLiteral("archivist.editor"), documentResource("a.cpp"), "a.cpp");
        const QString b = store.openView(QStringLiteral("archivist.editor"), documentResource("b.cpp", QStringLiteral("wt-b")), "b.cpp");
        const QString term = store.openView(QStringLiteral("archivist.terminal"), {{QStringLiteral("kind"), QStringLiteral("terminal")}, {QStringLiteral("id"), QStringLiteral("wt-b:s1")}, {QStringLiteral("libraryId"), QStringLiteral("wt-b")}}, "Terminal");
        const QString docA = QStringLiteral("col:lib-main:a.cpp");
        QVERIFY(documents.applyEdits(docA, QStringLiteral("page"), 1, insertAt(5, "!")).value("ok").toBool());
        QVERIFY(store.mergeViewState(a, {{QStringLiteral("cursorLine"), 3}}));

        // Rotate through the shelf a few times.
        for (int step = 0; step < 7; ++step) {
            QVERIFY(store.cycleView(step % 2 ? -1 : 1));
        }
        QVERIFY(store.activateView(b));
        QVERIFY(store.activateView(term));

        QVERIFY(documents.isDirty(docA));
        QCOMPARE(documents.snapshot(docA).value("text").toString(), QStringLiteral("alpha!"));
        QCOMPARE(documents.holders(docA).size(), 1); // still held by its hidden view
        QCOMPARE(disk.loads, 2);                     // nothing reloaded
        QCOMPARE(store.view(a).value("state").toMap().value("cursorLine").toInt(), 3);
        QCOMPARE(store.view(b).value("resource").toMap().value("libraryId").toString(), QStringLiteral("wt-b"));
        QCOMPARE(store.view(term).value("resource").toMap().value("id").toString(), QStringLiteral("wt-b:s1"));
        QCOMPARE(store.views()->rowCount(), 3);
    }

    void newTerminalsResolveTheirWorktree()
    {
        WorkspaceStore store;
        store.setCollection(QStringLiteral("anchor-terminals"));
        const QStringList known{QStringLiteral("lib-main"), QStringLiteral("wt-a"), QStringLiteral("wt-b")};
        const QString editors = store.focusedAnchorId();
        store.openView(QStringLiteral("archivist.editor"), documentResource("a.cpp", QStringLiteral("wt-a")), "a.cpp");
        const QString existing = store.openView(QStringLiteral("archivist.terminal"), {{QStringLiteral("kind"), QStringLiteral("terminal")}, {QStringLiteral("id"), QStringLiteral("lib-main:s1")}, {QStringLiteral("libraryId"), QStringLiteral("lib-main")}}, "Terminal", {{QStringLiteral("activate"), false}});

        // The active document's worktree wins over the Explorer's selection.
        QVariantMap resolved = store.resolveLibrary(editors, QStringLiteral("wt-b"), QStringLiteral("lib-main"), known);
        QCOMPARE(resolved.value("libraryId").toString(), QStringLiteral("wt-a"));
        QCOMPARE(resolved.value("source").toString(), QStringLiteral("activeView"));

        // An empty panel follows the Explorer, then the default.
        const QString empty = store.createAnchor(editors, QStringLiteral("bottom"));
        QCOMPARE(store.resolveLibrary(empty, QStringLiteral("wt-b"), QStringLiteral("lib-main"), known).value("libraryId").toString(), QStringLiteral("wt-b"));
        QCOMPARE(store.resolveLibrary(empty, QString(), QStringLiteral("lib-main"), known).value("source").toString(), QStringLiteral("default"));

        // A panel bound to a worktree sends new terminals there.
        QVERIFY(store.setAnchorLibrary(empty, QStringLiteral("wt-a")));
        QCOMPARE(store.resolveLibrary(empty, QStringLiteral("wt-b"), QStringLiteral("lib-main"), known).value("source").toString(), QStringLiteral("anchor"));

        // The terminal that already existed keeps its own worktree throughout.
        QCOMPARE(store.view(existing).value("resource").toMap().value("libraryId").toString(), QStringLiteral("lib-main"));
    }

    void agentCommandsMeetTheSameLocks()
    {
        FakeDisk disk;
        disk.files.insert(QStringLiteral("lib-main:a.cpp"), QStringLiteral("alpha"));
        DocumentStore documents;
        disk.attach(documents);
        WorkspaceStore store;
        store.setDocumentStore(&documents);
        store.setCollection(QStringLiteral("anchor-agent"));
        const QString first = store.focusedAnchorId();
        const QString a = store.openView(QStringLiteral("archivist.editor"), documentResource("a.cpp"), "a.cpp");
        const QString second = store.createAnchor(first, QStringLiteral("right"));

        QVariantMap result = store.runCommand({{"command", "anchor.rename"}, {"anchorId", second}, {"name", "Agent Observatory"}});
        QVERIFY(result.value("ok").toBool());
        QCOMPARE(store.anchorDisplayName(second), QStringLiteral("Agent Observatory"));

        result = store.runCommand({{"command", "anchor.addResource"}, {"anchorId", second}, {"typeId", "archivist.chat"}, {"resource", QVariantMap{{"kind", "chat"}, {"id", "agent-1"}}}, {"title", "Agent 1"}});
        QVERIFY(result.value("ok").toBool());
        QVERIFY(!result.value("viewId").toString().isEmpty());

        // An agent can't throw away the only copy of unsaved edits.
        QVERIFY(documents.applyEdits(QStringLiteral("col:lib-main:a.cpp"), QStringLiteral("p"), 1, insertAt(0, ">")).value("ok").toBool());
        result = store.runCommand({{"command", "anchor.removeResource"}, {"viewId", a}, {"force", true}});
        QCOMPARE(result.value("code").toString(), QStringLiteral("UNSAVED_WORK"));
        QVERIFY(!store.view(a).isEmpty());

        // Locks apply, and only the person unlocks.
        QVERIFY(store.setAnchorLayoutLocked(first, true));
        result = store.runCommand({{"command", "anchor.setLayoutLock"}, {"anchorId", first}, {"locked", false}});
        QCOMPARE(result.value("code").toString(), QStringLiteral("UNLOCK_REFUSED"));
        result = store.runCommand({{"command", "anchor.describe"}, {"anchorId", first}});
        QVERIFY(!result.value("capabilities").toMap().value("move").toBool());
        result = store.runCommand({{"command", "anchor.setLayoutLock"}, {"anchorId", first}, {"locked", false}, {"origin", "user"}});
        QVERIFY(result.value("ok").toBool());
    }

    // Requests from the conversation in front apply at once; anything else
    // waits for the person.
    void agentRequestsApplyOrWaitForThePerson()
    {
        WorkspaceStore store;
        store.setCollection(QStringLiteral("agent-apply"));
        QSignalSpy arranged(&store, &WorkspaceStore::agentArranged);

        QVariantMap result = store.runAgentRequest(agentShow({agentFile("a.cpp")}), true);
        QVERIFY2(result.value("ok").toBool(), qPrintable(result.value("error").toString()));
        QCOMPARE(result.value("status").toString(), QStringLiteral("applied"));
        QCOMPARE(arranged.count(), 1);
        QVERIFY(store.canUndoAgentChange());

        // A background conversation only prepares the change.
        const QString before = store.focusedViewId();
        const qsizetype viewsBefore = agentViews(store).size();
        result = store.runAgentRequest(agentShow({agentFile("b.cpp")}), false);
        QVERIFY(result.value("ok").toBool());
        QCOMPARE(result.value("status").toString(), QStringLiteral("suggested"));
        QCOMPARE(arranged.count(), 1);
        QVERIFY(!store.agentSuggestion().isEmpty());
        QCOMPARE(store.focusedViewId(), before);
        QCOMPARE(agentViews(store).size(), viewsBefore);

        // The person applies it.
        QVERIFY(store.applyAgentSuggestion());
        QVERIFY(store.agentSuggestion().isEmpty());
        QCOMPARE(arranged.count(), 2);

        // Or dismisses one; a request nobody asked for is also only a suggestion.
        result = store.runAgentRequest(agentShow({agentFile("c.cpp")}, {}, false, false), true);
        QCOMPARE(result.value("status").toString(), QStringLiteral("suggested"));
        store.dismissAgentSuggestion();
        QVERIFY(store.agentSuggestion().isEmpty());
        QCOMPARE(arranged.count(), 2);

        // Background agents can't put things back on their own.
        QCOMPARE(store.runAgentRequest(agentRestore("last_change"), false).value("code").toString(), QStringLiteral("NOT_ALLOWED"));

        // Everything is on the record.
        QVERIFY(store.agentActivity().size() >= 4);
    }

    // 6. An agent finishing while the person types never takes focus.
    void typingKeepsFocus()
    {
        WorkspaceStore store;
        store.setCollection(QStringLiteral("agent-typing"));
        qint64 clock = 100000;
        store.setClockForTesting([&clock]() { return clock; });

        const QString typingIn = store.openView(QStringLiteral("archivist.editor"), documentResource("notes.md"), "notes.md");
        const QString side = store.createAnchor(store.focusedAnchorId(), QStringLiteral("right"));
        store.focusAnchor(store.view(typingIn).value("anchorId").toString());
        QCOMPARE(store.focusedViewId(), typingIn);

        store.noteUserInput();
        clock += 300;
        QVariantMap result = store.runAgentRequest(agentShow({agentFile("x.cpp")}, side, true), true);
        QVERIFY2(result.value("ok").toBool(), qPrintable(result.value("error").toString()));
        QCOMPARE(store.focusedViewId(), typingIn);

        // Once the person pauses, an asked-for focus change happens.
        clock += 5000;
        result = store.runAgentRequest(agentShow({agentFile("y.cpp")}, side, true), true);
        QVERIFY(result.value("ok").toBool());
        QCOMPARE(store.focusedAnchorId(), side);
    }

    // 4 and 8. "Put everything back": tabs the agent opened close, unsaved
    // editors and terminals stay open, and no edit is undone.
    void puttingThingsBackKeepsWork()
    {
        FakeDisk disk;
        disk.files.insert(QStringLiteral("lib-main:a.cpp"), QStringLiteral("alpha"));
        disk.files.insert(QStringLiteral("lib-main:b.cpp"), QStringLiteral("beta"));
        DocumentStore documents;
        disk.attach(documents);
        WorkspaceStore store;
        store.setDocumentStore(&documents);
        store.setCollection(QStringLiteral("agent-restore"));
        const QString surface = store.primarySurfaceId();
        store.setSurfaceSize(surface, 1200, 800);

        const QString mine = store.openView(QStringLiteral("archivist.editor"), documentResource("mine.cpp"), "mine.cpp");
        const QStringList anchorsBefore = agentAreas(store);

        QVariantMap result = store.runAgentRequest(agentShow({agentFile("a.cpp"), agentFile("b.cpp")}, QStringLiteral("right")), true);
        QVERIFY2(result.value("ok").toBool(), qPrintable(result.value("error").toString()));
        QVariantMap terminal = agentShow({QVariantMap{{"kind", "terminal"}, {"reference", "new"}, {"libraryId", "lib-main"}}}, QStringLiteral("right"));
        result = store.runAgentRequest(terminal, true);
        QVERIFY2(result.value("ok").toBool(), qPrintable(result.value("error").toString()));
        result = store.runAgentRequest({{"action", "resize"}, {"target", "right"}, {"change", "larger"}, {"explicit", true}}, true);
        QVERIFY2(result.value("ok").toBool(), qPrintable(result.value("error").toString()));

        QString a;
        QString b;
        QString term;
        for (const QString &viewId : agentViews(store)) {
            const QVariantMap view = store.view(viewId);
            const QString fileId = view.value("resource").toMap().value("details").toMap().value("fileId").toString();
            if (fileId == QStringLiteral("a.cpp")) {
                a = view.value("viewId").toString();
            } else if (fileId == QStringLiteral("b.cpp")) {
                b = view.value("viewId").toString();
            } else if (view.value("typeId").toString() == QStringLiteral("archivist.terminal")) {
                term = view.value("viewId").toString();
            }
        }
        QVERIFY(!a.isEmpty() && !b.isEmpty() && !term.isEmpty());

        // The person edits one of the files the agent opened.
        const QString docA = store.view(a).value("resource").toMap().value("id").toString();
        QVERIFY(documents.applyEdits(docA, QStringLiteral("p"), 1, insertAt(0, ">")).value("ok").toBool());

        result = store.runAgentRequest(agentRestore("before_task"), true);
        QVERIFY2(result.value("ok").toBool(), qPrintable(result.value("error").toString()));
        // Putting back is itself a step.
        QCOMPARE(store.undoSummary(), QStringLiteral("Put the workspace back the way it was"));

        QVERIFY(store.view(b).isEmpty());
        QVERIFY(!store.view(a).isEmpty());
        QVERIFY(!store.view(term).isEmpty());
        QVERIFY(!store.view(mine).isEmpty());
        QCOMPARE(documents.snapshot(docA).value("text").toString(), QStringLiteral(">alpha"));
        QVERIFY(documents.isDirty(docA));

        // The panels the person had are back as they were.
        QCOMPARE(agentAreas(store), anchorsBefore);

        // Undoing the put back brings the agent's tabs back (same views and
        // documents) and the temporary arrangement with them; redo puts back again.
        const QStringList putBack = agentAreas(store);
        QVERIFY(store.undo());
        QVERIFY(!store.view(b).isEmpty());
        QVERIFY(!store.view(a).isEmpty());
        QVERIFY(!store.temporaryArrangement().isEmpty());
        QVERIFY(store.redo());
        QVERIFY(store.view(b).isEmpty());
        QVERIFY(!store.view(a).isEmpty()); // edited: never closed
        QCOMPARE(agentAreas(store), putBack);
        QVERIFY(store.temporaryArrangement().isEmpty());
        QCOMPARE(documents.snapshot(docA).value("text").toString(), QStringLiteral(">alpha"));
    }

    // 10. Agent rearrangements never touch named layouts.
    void agentChangesLeaveNamedLayoutsAlone()
    {
        QSettings settings;
        settings.remove(QStringLiteral("workspace/docking/layouts"));
        WorkspaceStore store;
        store.setCollection(QStringLiteral("agent-layouts"));
        store.setSurfaceSize(store.primarySurfaceId(), 1200, 800);
        store.openView(QStringLiteral("archivist.editor"), documentResource("a.cpp"), "a.cpp");
        const QString saved = store.saveLayout(QStringLiteral("Focus"));
        QVERIFY(!saved.isEmpty());
        settings.sync();
        const QVariant stored = settings.value(QStringLiteral("workspace/docking/layouts"));

        QVERIFY(store.runAgentRequest(agentShow({agentFile("b.md"), agentFile("c.md")}, QStringLiteral("right")), true).value("ok").toBool());
        QVERIFY(store.runAgentRequest({{"action", "resize"}, {"target", "right"}, {"change", "larger"}, {"explicit", true}}, true).value("ok").toBool());
        QVERIFY(store.runAgentRequest(agentRestore("before_task"), true).value("ok").toBool());

        settings.sync();
        QCOMPARE(settings.value(QStringLiteral("workspace/docking/layouts")), stored);
        QCOMPARE(store.savedLayouts().size(), 1);

        // Asked by name, the saved layout is applied, and that is undoable too.
        QVariantMap result = store.runAgentRequest(agentRestore("saved_layout", "focus"), true);
        QVERIFY2(result.value("ok").toBool(), qPrintable(result.value("error").toString()));
        QCOMPARE(store.currentLayoutId(), saved);
        QVERIFY(store.canUndoAgentChange());
        QCOMPARE(store.runAgentRequest(agentRestore("saved_layout", "nowhere"), true).value("code").toString(), QStringLiteral("NOT_FOUND"));
        settings.sync();
        QCOMPARE(settings.value(QStringLiteral("workspace/docking/layouts")), stored);
    }

    void agentsSeeTheWorkspaceInWords()
    {
        QSettings settings;
        settings.remove(QStringLiteral("workspace/docking/layouts"));
        WorkspaceStore store;
        store.setCollection(QStringLiteral("agent-describe"));
        store.openView(QStringLiteral("archivist.editor"), documentResource("a.cpp"), "a.cpp");
        store.setCommandSurfaceState(true, QStringLiteral("terminal"));

        const QVariantMap description = store.runAgentRequest({{"action", "describe"}}, false);
        QVERIFY(description.value("ok").toBool());
        QVERIFY(!description.value("areas").toList().isEmpty());
        QVERIFY(description.contains("bottomPanel"));
        QVERIFY(!description.contains("savedLayouts"));
        store.saveLayout(QStringLiteral("Quiet"));
        QCOMPARE(store.describeForAgent().value("savedLayouts").toStringList(), QStringList({QStringLiteral("Quiet")}));
        settings.remove(QStringLiteral("workspace/docking/layouts"));
        QCOMPARE(description.value("canUndoLastChange").toBool(), false);
        QCOMPARE(store.commandSurface().value("collapsed").toBool(), true);

        // "Beside our conversation" opens the bottom chat if it was folded.
        QSignalSpy surface(&store, &WorkspaceStore::commandSurfaceRequested);
        QVERIFY(store.runAgentRequest(agentShow({agentFile("b.cpp")}, QStringLiteral("beside our conversation")), true).value("ok").toBool());
        QCOMPARE(surface.count(), 1);
        QCOMPARE(surface.first().at(0).toString(), QStringLiteral("chat"));
        QCOMPARE(surface.first().at(1).toBool(), true);
    }

    // Phase C. The arrangement before an agent's task is kept apart from
    // named layouts, survives a restart, and comes back in one step.
    void temporaryArrangementsSurviveARestart()
    {
        QSettings settings;
        const QString key = QStringLiteral("workspace/collections/agent-temporary/docking/temporaryArrangement");
        settings.remove(key);
        settings.remove(QStringLiteral("workspace/collections/agent-temporary/docking/snapshot"));
        QStringList areasBefore;
        QString mine;

        {
            WorkspaceStore store;
            store.setCollection(QStringLiteral("agent-temporary"));
            store.setSurfaceSize(store.primarySurfaceId(), 1200, 800);
            mine = store.openView(QStringLiteral("archivist.editor"), documentResource("mine.cpp"), "mine.cpp");
            areasBefore = agentAreas(store);
            QVERIFY(store.temporaryArrangement().isEmpty());

            QVERIFY(store.runAgentRequest(agentShow({agentFile("doc.md")}, QStringLiteral("right")), true).value("ok").toBool());
            QVERIFY(store.runAgentRequest({{"action", "resize"}, {"target", "right"}, {"change", "larger"}, {"explicit", true}}, true).value("ok").toBool());
            QCOMPARE(store.temporaryArrangement().value("changes").toStringList().size(), 2);
            QVERIFY(agentAreas(store).size() > areasBefore.size());

            // Stored on its own, as an arrangement without focus.
            settings.sync();
            const QJsonObject record = QJsonDocument::fromJson(settings.value(key).toString().toUtf8()).object();
            QVERIFY(!record.value("baseline").toObject().isEmpty());
            QVERIFY(!record.value("baseline").toObject().value("session").toObject().contains("focusedAnchor"));
            store.saveNow();
        }

        WorkspaceStore store;
        store.setCollection(QStringLiteral("agent-temporary"));
        store.setSurfaceSize(store.primarySurfaceId(), 1200, 800);
        QVERIFY(!store.temporaryArrangement().isEmpty());
        QVERIFY(store.describeForAgent().contains("temporaryArrangementSince"));
        QVERIFY(!store.canUndoAgentChange());

        QSignalSpy restored(&store, &WorkspaceStore::agentRestored);
        const QVariantMap result = store.runAgentRequest(agentRestore("before_task"), true);
        QVERIFY2(result.value("ok").toBool(), qPrintable(result.value("error").toString()));
        QCOMPARE(restored.count(), 1);
        QCOMPARE(agentAreas(store), areasBefore);
        QVERIFY(!store.view(mine).isEmpty());
        QCOMPARE(agentViews(store), QStringList({mine}));
        QVERIFY(store.temporaryArrangement().isEmpty());
        settings.sync();
        QVERIFY(!settings.contains(key));
    }

    void keepingAnArrangementMakesItTheNormalOne()
    {
        WorkspaceStore store;
        store.setCollection(QStringLiteral("agent-keep"));
        store.setSurfaceSize(store.primarySurfaceId(), 1200, 800);
        store.openView(QStringLiteral("archivist.editor"), documentResource("mine.cpp"), "mine.cpp");

        QVERIFY(store.runAgentRequest(agentShow({agentFile("doc.md")}, QStringLiteral("right")), true).value("ok").toBool());
        const QStringList arranged = agentAreas(store);
        store.keepArrangement();
        QVERIFY(store.temporaryArrangement().isEmpty());
        QVERIFY(store.canUndo()); // keeping ends the task, not the history
        QCOMPARE(store.runAgentRequest(agentRestore("before_task"), true).value("code").toString(), QStringLiteral("NOTHING_TO_RESTORE"));
        QCOMPARE(agentAreas(store), arranged);

        // Undoing a task's only change also ends the temporary arrangement.
        QVERIFY(store.runAgentRequest(agentShow({agentFile("more.md")}, QStringLiteral("left")), true).value("ok").toBool());
        QVERIFY(!store.temporaryArrangement().isEmpty());
        QVERIFY(store.undoAgentChange());
        QVERIFY(store.temporaryArrangement().isEmpty());
        QCOMPARE(agentAreas(store), arranged);

        // And the person can put things back without the agent.
        QVERIFY(store.runAgentRequest(agentShow({agentFile("more.md")}, QStringLiteral("left")), true).value("ok").toBool());
        QVERIFY(store.runAgentRequest(agentShow({agentFile("again.md")}, QStringLiteral("bottom")), true).value("ok").toBool());
        QVERIFY(store.putBackArrangement());
        QCOMPARE(agentAreas(store), arranged);
        QCOMPARE(store.undoSummary(), QStringLiteral("Put the workspace back the way it was"));
    }

    // Phase D. Preferences: lasting ones survive a restart, one-offs don't,
    // and each changes only what nobody spelled out.
    void preferencesSurviveARestart()
    {
        QSettings settings;
        settings.remove(QStringLiteral("workspace/preferences"));
        auto preference = [](const QString &key, const QString &value, bool lasting, bool allowed, const QString &area = {}) {
            return QVariantMap{
                {"action", "preference"}, {"op", "set"}, {"preference", key}, {"value", value}, {"area", area},
                {"lasting", lasting}, {"lastingAllowed", allowed}, {"explicit", true}, {"chatId", "chat-1"},
            };
        };

        {
            WorkspaceStore store;
            store.setCollection(QStringLiteral("prefs"));
            QVariantMap result = store.runAgentRequest(preference("chat_location", "bottom", true, true), true);
            QVERIFY2(result.value("ok").toBool(), qPrintable(result.value("error").toString()));
            // Asked to last, but the message wasn't a standing preference.
            result = store.runAgentRequest(preference("main_editor", "large", true, false), true);
            QVERIFY(result.value("ok").toBool());
            QVERIFY(result.value("notes").toStringList().join(" ").contains("this session"));
            QCOMPARE(store.workspacePreferences().size(), 2);
            // Background conversations can't set preferences.
            QCOMPARE(store.runAgentRequest(preference("rearranging", "only_when_asked", true, true), false).value("code").toString(), QStringLiteral("NOT_ALLOWED"));
        }

        settings.sync();
        QVERIFY(settings.value("workspace/preferences").toString().contains("chat_location"));
        QVERIFY(!settings.value("workspace/preferences").toString().contains("main_editor"));

        WorkspaceStore store;
        store.setCollection(QStringLiteral("prefs"));
        const QVariantList kept = store.workspacePreferences();
        QCOMPARE(kept.size(), 1);
        QCOMPARE(kept.first().toMap().value("sentence").toString(), QStringLiteral("Chat stays in the bottom panel"));
        QCOMPARE(kept.first().toMap().value("duration").toString(), QStringLiteral("always"));
        QVERIFY(store.describeForAgent().value("preferences").toStringList().first().contains("bottom panel"));

        // Easy to revoke.
        QVERIFY(store.forgetPreference(kept.first().toMap().value("id").toString()));
        QVERIFY(store.workspacePreferences().isEmpty());
        settings.sync();
        QVERIFY(!settings.contains("workspace/preferences"));
    }

    void preferencesShapeWhatWasNotSpelledOut()
    {
        QSettings settings;
        settings.remove(QStringLiteral("workspace/preferences"));
        WorkspaceStore store;
        store.setCollection(QStringLiteral("prefs-effects"));
        store.setSurfaceSize(store.primarySurfaceId(), 1200, 800);
        const QString main = store.focusedAnchorId();
        store.openView(QStringLiteral("archivist.editor"), documentResource("main.cpp"), "main.cpp");
        const QString side = store.createAnchor(main, QStringLiteral("right"));
        store.openView(QStringLiteral("archivist.editor"), documentResource("side.cpp"), "side.cpp", {{"anchorId", side}});

        // "Use the right panel for documentation."
        QVERIFY(store.setPreference(QStringLiteral("area_purpose"), QStringLiteral("docs"), side).value("ok").toBool());
        QCOMPARE(store.anchorPurpose(side), QStringLiteral("documentation"));
        QVariantMap docs = agentShow({agentFile("guide.md")});
        docs.insert("purpose", "documentation");
        QVERIFY(store.runAgentRequest(docs, true).value("ok").toBool());
        QCOMPARE(store.view(agentViews(store).last()).value("anchorId").toString(), side);
        QCOMPARE(agentAreas(store).size(), 2);

        // "Don't rearrange my windows unless I ask": nothing new appears for
        // reference material, and unasked requests only suggest.
        QVERIFY(store.setPreference(QStringLiteral("area_purpose"), QStringLiteral("documentation"), main).value("ok").toBool());
        QCOMPARE(store.anchorPurpose(side), QString());
        store.forgetPreference(store.workspacePreferences().last().toMap().value("id").toString());
        QVERIFY(store.setPreference(QStringLiteral("rearranging"), QStringLiteral("only_when_asked"), {}, false).value("ok").toBool());
        QVariantMap reference = agentShow({agentFile("spec.md")});
        reference.insert("purpose", "reference");
        QVERIFY(store.runAgentRequest(reference, true).value("ok").toBool());
        QCOMPARE(agentAreas(store).size(), 2);
        QCOMPARE(store.runAgentRequest(agentShow({agentFile("x.md")}, {}, false, false), true).value("status").toString(), QStringLiteral("suggested"));
        store.dismissAgentSuggestion();
        // An explicit place still wins.
        QVERIFY(store.runAgentRequest(agentShow({agentFile("y.md")}, QStringLiteral("new")), true).value("ok").toBool());
        QCOMPARE(agentAreas(store).size(), 3);
        QVERIFY(store.runAgentRequest(agentRestore("last_change"), true).value("ok").toBool());

        // "Keep my main editor large": others grow only so far.
        QVERIFY(store.setPreference(QStringLiteral("main_editor"), QStringLiteral("large"), {}, false).value("ok").toBool());
        QVERIFY(store.runAgentRequest({{"action", "resize"}, {"target", side}, {"change", "0.3"}, {"explicit", true}}, true).value("ok").toBool());
        for (int step = 0; step < 4; ++step) {
            store.runAgentRequest({{"action", "resize"}, {"target", side}, {"change", "larger"}, {"explicit", true}}, true);
        }
        QVERIFY(store.runAgentRequest({{"action", "resize"}, {"target", side}, {"change", "larger"}, {"explicit", true}}, true).value("error").toString().contains("main editor"));
        const QVariantList areas = store.describeForAgent().value("areas").toList();
        for (const QVariant &area : areas) {
            if (area.toMap().value("id").toString() == side) {
                QVERIFY(area.toMap().value("sizePercent").toInt() <= 46);
            }
        }
        // Unless a size is asked for.
        QVERIFY(store.runAgentRequest({{"action", "resize"}, {"target", side}, {"change", "0.7"}, {"explicit", true}}, true).value("ok").toBool());

        // "I like my chat at the bottom": the conversation isn't docked.
        store.openView(QStringLiteral("archivist.chat"), {{QStringLiteral("kind"), QStringLiteral("chat")}, {QStringLiteral("id"), QStringLiteral("chat-1")}}, "Chat", {{"anchorId", side}});
        QVERIFY(store.setPreference(QStringLiteral("chat_location"), QStringLiteral("bottom"), {}, false).value("ok").toBool());
        QSignalSpy surface(&store, &WorkspaceStore::commandSurfaceRequested);
        store.setCommandSurfaceState(true, QStringLiteral("terminal"));
        QVERIFY(store.runAgentRequest(agentShow({QVariantMap{{"kind", "chat"}, {"reference", ""}}}), true).value("ok").toBool());
        QCOMPARE(surface.count(), 1);
        QCOMPARE(surface.first().at(0).toString(), QStringLiteral("chat"));
    }

    // Definition of done, as the assistant's tool calls would arrive:
    // 1. "Help me understand this subsystem. Open the important files, put
    //    the relevant documentation on the side, keep our conversation
    //    accessible."  2. "Make the code bigger."  3. "Put everything back."
    void understandEnlargeAndPutBack()
    {
        QSettings settings;
        settings.remove(QStringLiteral("workspace/preferences"));
        WorkspaceStore store;
        store.setCollection(QStringLiteral("definition-of-done"));
        store.setSurfaceSize(store.primarySurfaceId(), 1440, 900);
        const QString mine = store.openView(QStringLiteral("archivist.editor"), documentResource("notes.cpp"), "notes.cpp");
        store.setCommandSurfaceState(true, QStringLiteral("terminal"));
        const QStringList areasBefore = agentAreas(store);
        QSignalSpy surface(&store, &WorkspaceStore::commandSurfaceRequested);

        // 1.
        QVERIFY(store.runAgentRequest({{"action", "describe"}}, true).value("ok").toBool());
        QVariantMap code = agentShow({agentFile("src/engine.cpp"), agentFile("src/engine.h"), agentFile("src/scheduler.cpp")});
        QVariantMap result = store.runAgentRequest(code, true);
        QVERIFY2(result.value("ok").toBool(), qPrintable(result.value("error").toString()));
        QVariantMap docs = agentShow({agentFile("docs/engine.md"), agentFile("docs/scheduling.md")});
        docs.insert("purpose", "documentation");
        result = store.runAgentRequest(docs, true);
        QVERIFY2(result.value("ok").toBool(), qPrintable(result.value("error").toString()));
        result = store.runAgentRequest(agentShow({QVariantMap{{"kind", "chat"}, {"reference", ""}}}), true);
        QVERIFY2(result.value("ok").toBool(), qPrintable(result.value("error").toString()));

        const QString codeArea = store.view(mine).value("anchorId").toString();
        QString docsArea;
        for (const QString &viewId : agentViews(store)) {
            const QString path = store.view(viewId).value("resource").toMap().value("details").toMap().value("path").toString();
            if (path.startsWith("src/")) {
                QCOMPARE(store.view(viewId).value("anchorId").toString(), codeArea);
            } else if (path.startsWith("docs/")) {
                docsArea = store.view(viewId).value("anchorId").toString();
            }
        }
        QVERIFY(!docsArea.isEmpty() && docsArea != codeArea);
        QCOMPARE(agentAreas(store).size(), areasBefore.size() + 1);
        QVERIFY(!surface.isEmpty());
        QCOMPARE(surface.last().at(0).toString(), QStringLiteral("chat"));
        QCOMPARE(surface.last().at(1).toBool(), true);

        // 2.
        auto sizeOf = [&store](const QString &anchorId) {
            for (const QVariant &area : store.describeForAgent().value("areas").toList()) {
                if (area.toMap().value("id").toString() == anchorId) {
                    return area.toMap().value("sizePercent").toInt();
                }
            }
            return -1;
        };
        const int codeBefore = sizeOf(codeArea);
        result = store.runAgentRequest({{"action", "resize"}, {"target", "the code"}, {"change", "larger"}, {"explicit", true}, {"chatId", "chat-1"}}, true);
        QVERIFY2(result.value("ok").toBool(), qPrintable(result.value("error").toString()));
        QVERIFY(sizeOf(codeArea) > codeBefore);

        // 3.
        result = store.runAgentRequest(agentRestore("before_task"), true);
        QVERIFY2(result.value("ok").toBool(), qPrintable(result.value("error").toString()));
        QCOMPARE(agentAreas(store), areasBefore);
        QCOMPARE(agentViews(store), QStringList({mine}));
        QCOMPARE(surface.last().at(0).toString(), QStringLiteral("terminal"));
        QCOMPARE(surface.last().at(1).toBool(), false);
        QVERIFY(store.temporaryArrangement().isEmpty());
    }

    // "Make a review layout with docs on the right, save it, switch to
    // Research, then back to mine."
    void layoutsByConversation()
    {
        QSettings settings;
        settings.remove(QStringLiteral("workspace/docking/layouts"));
        settings.remove(QStringLiteral("workspace/preferences"));
        WorkspaceStore store;
        store.setCollection(QStringLiteral("layouts-by-chat"));
        store.setSurfaceSize(store.primarySurfaceId(), 1440, 900);
        const QString mine = store.openView(QStringLiteral("archivist.editor"), documentResource("main.cpp"), "main.cpp");
        auto layout = [](const QString &op, const QString &name, bool replace = false, const QString &newName = {}) {
            return QVariantMap{{"action", "layout"}, {"op", op}, {"name", name}, {"newName", newName}, {"replace", replace}, {"explicit", true}, {"chatId", "chat-1"}};
        };

        // What exists, built-in layouts included.
        const QVariantList listed = store.runAgentRequest(layout("list", {}), true).value("layouts").toList();
        QVERIFY(listed.size() >= 6);
        QVERIFY(store.describeForAgent().contains("layouts"));

        // Build: a named empty panel on the right, docs in it, then save.
        QVariantMap result = store.runAgentRequest({{"action", "add"}, {"target", ""}, {"to", "right"}, {"name", "Docs"}, {"explicit", true}}, true);
        QVERIFY2(result.value("ok").toBool(), qPrintable(result.value("error").toString()));
        QCOMPARE(agentAreas(store).size(), 2);
        QVERIFY(store.runAgentRequest(agentShow({agentFile("guide.md")}, QStringLiteral("Docs")), true).value("ok").toBool());
        result = store.runAgentRequest(layout("save", "Review"), true);
        QVERIFY2(result.value("ok").toBool(), qPrintable(result.value("error").toString()));
        QCOMPARE(store.savedLayouts().size(), 1);
        QVERIFY(store.temporaryArrangement().isEmpty());

        // Never over the person's layout or a built-in one unless told to.
        QCOMPARE(store.runAgentRequest(layout("save", "review"), true).value("code").toString(), QStringLiteral("EXISTS"));
        QVERIFY(store.runAgentRequest(layout("save", "review", true), true).value("ok").toBool());
        QCOMPARE(store.runAgentRequest(layout("save", "Research"), true).value("code").toString(), QStringLiteral("INVALID_ARGUMENT"));

        // Switch to a built-in one and back; tabs come along.
        QVERIFY(store.runAgentRequest(layout("apply", "research"), true).value("ok").toBool());
        QCOMPARE(store.currentLayoutId(), QStringLiteral("research"));
        QVERIFY(!store.view(mine).isEmpty());
        QVERIFY(store.runAgentRequest(layout("apply", "Review"), true).value("ok").toBool());
        QCOMPARE(store.currentLayoutName(), QStringLiteral("review"));
        QCOMPARE(store.runAgentRequest(layout("apply", "Nowhere"), true).value("code").toString(), QStringLiteral("NOT_FOUND"));
        // Background chats only look.
        QCOMPARE(store.runAgentRequest(layout("apply", "research"), false).value("code").toString(), QStringLiteral("NOT_ALLOWED"));

        // Remove a panel: its tabs move, nothing closes.
        const int viewsBefore = agentViews(store).size();
        result = store.runAgentRequest({{"action", "remove"}, {"target", "Docs"}, {"to", ""}, {"explicit", true}}, true);
        QVERIFY2(result.value("ok").toBool(), qPrintable(result.value("error").toString()));
        QCOMPARE(agentAreas(store).size(), 1);
        QCOMPARE(agentViews(store).size(), viewsBefore);

        // Rename and delete only the person's own layouts.
        QVERIFY(store.runAgentRequest(layout("rename", "review", false, "Deep Review"), true).value("ok").toBool());
        QCOMPARE(store.runAgentRequest(layout("delete", "Research"), true).value("code").toString(), QStringLiteral("NOT_FOUND"));
        QVERIFY(store.runAgentRequest(layout("delete", "Deep Review"), true).value("ok").toBool());
        QVERIFY(store.savedLayouts().isEmpty());
    }

    // "Make a layout with a Docs panel on the right and a Notes panel below it,
    // and save it as Writing": one request, one undo step.
    void wholeLayoutInOneRequest()
    {
        QSettings settings;
        settings.remove(QStringLiteral("workspace/docking/layouts"));
        settings.remove(QStringLiteral("workspace/preferences"));
        WorkspaceStore store;
        store.setCollection(QStringLiteral("layout-in-one"));
        store.setSurfaceSize(store.primarySurfaceId(), 1440, 900);
        store.openView(QStringLiteral("archivist.editor"), documentResource("main.cpp"), "main.cpp");
        auto step = [](const QString &action, const QString &target, const QString &change, const QString &to, const QString &name) {
            return QVariantMap{{"action", action}, {"target", target}, {"change", change}, {"to", to}, {"name", name}};
        };

        QVariantMap result = store.runAgentRequest({
            {"action", "steps"},
            {"steps", QVariantList{
                step("add", "", "", "right", "Docs"),
                step("add", "Docs", "", "bottom", "Notes"),
                step("resize", "Docs", "0.3", "", ""),
            }},
            {"saveAs", "Writing"},
            {"explicit", true},
            {"chatId", "chat-1"},
        }, true);
        QVERIFY2(result.value("ok").toBool(), qPrintable(result.value("error").toString()));
        QCOMPARE(agentAreas(store).size(), 3);
        QVERIFY(result.value("layout").toString().contains("Writing"));
        QCOMPARE(store.savedLayouts().size(), 1);
        QCOMPARE(store.currentLayoutName(), QStringLiteral("Writing"));

        // A step that can't happen leaves everything as it was.
        result = store.runAgentRequest({
            {"action", "steps"},
            {"steps", QVariantList{
                step("add", "", "", "left", "Tests"),
                step("resize", "Nowhere at all", "larger", "", ""),
            }},
            {"saveAs", ""},
            {"explicit", true},
        }, true);
        QVERIFY(!result.value("ok").toBool());
        QVERIFY2(result.value("error").toString().startsWith("Step 2"), qPrintable(result.value("error").toString()));
        QCOMPARE(agentAreas(store).size(), 3);
    }

    // "Set me up to review: code on the left, the guide on the right, and keep
    // it as Review". One request, one change, one undo step.
    void composeByConversation()
    {
        QSettings settings;
        settings.remove(QStringLiteral("workspace/docking/layouts"));
        settings.remove(QStringLiteral("workspace/preferences"));
        WorkspaceStore store;
        store.setCollection(QStringLiteral("compose-by-chat"));
        store.setSurfaceSize(store.primarySurfaceId(), 1440, 900);
        store.openView(QStringLiteral("archivist.editor"), documentResource("main.cpp"), "main.cpp");
        const int published = store.revision();
        auto compose = [](bool asked, const QString &saveAs = {}) {
            return QVariantMap{
                {"action", "compose"},
                {"template", "focus_references"},
                {"strategy", ""},
                {"slots", QVariantList{
                    QVariantMap{{"name", "Focus"}, {"holds", QStringList{}}, {"weight", 0}, {"items", QVariantList{agentFile("main.cpp"), agentFile("util.cpp")}}},
                    QVariantMap{{"name", "Docs"}, {"holds", QStringList{"docs"}}, {"weight", 0}, {"items", QVariantList{agentFile("guide.md"), QVariantMap{{"kind", "open_view"}, {"reference", "nothing-like-this"}}}}},
                }},
                {"focus", ""},
                {"saveAs", saveAs},
                {"explicit", asked},
                {"chatId", "chat-1"},
            };
        };

        // A background conversation only stages it.
        QVariantMap result = store.runAgentRequest(compose(true), false);
        QCOMPARE(result.value("status").toString(), QStringLiteral("suggested"));
        QCOMPARE(agentAreas(store).size(), 1);
        QVERIFY(!store.agentSuggestion().isEmpty());
        store.dismissAgentSuggestion();

        // Temporary: one undo puts the old arrangement back.
        result = store.runAgentRequest(compose(true), true);
        QVERIFY2(result.value("ok").toBool(), qPrintable(result.value("error").toString()));
        QCOMPARE(agentAreas(store).size(), 2);
        QCOMPARE(store.revision(), published + 1); // published once
        QCOMPARE(result.value("panels").toList().size(), 2);
        {
            QStringList statuses;
            for (const QVariant &resource : result.value("resources").toList()) {
                statuses.append(resource.toMap().value("status").toString());
            }
            QVERIFY2(statuses.contains("not_found"), qPrintable(statuses.join(",")));
        }
        QVERIFY(!store.temporaryArrangement().isEmpty());
        QVERIFY(store.undoAgentChange());
        QCOMPARE(agentAreas(store).size(), 1);
        QVERIFY(!store.canUndoAgentChange());

        // Kept: saved as a layout, and it becomes the normal arrangement.
        result = store.runAgentRequest(compose(true, "Review"), true);
        QVERIFY2(result.value("ok").toBool(), qPrintable(result.value("error").toString()));
        QCOMPARE(store.savedLayouts().size(), 1);
        QCOMPARE(store.currentLayoutName(), QStringLiteral("Review"));
        QVERIFY(store.temporaryArrangement().isEmpty());
    }

    // Files the model names that are open (from anywhere) are used as they
    // are; unclear or missing names are reported, never swapped for others.
    void composeFindsOpenFilesFirst()
    {
        QSettings settings;
        settings.remove(QStringLiteral("workspace/docking/layouts"));
        settings.remove(QStringLiteral("workspace/preferences"));
        WorkspaceStore store;
        store.setCollection(QStringLiteral("compose-open-files"));
        store.setSurfaceSize(store.primarySurfaceId(), 1440, 900);
        auto pathResource = [](const QString &path) {
            QVariantMap resource = documentResource(path);
            resource.insert("details", QVariantMap{{"fileId", path}, {"relativePath", path}, {"name", path.section('/', -1)}});
            return resource;
        };
        store.openView(QStringLiteral("archivist.editor"), documentResource("main.cpp"), "main.cpp");
        store.openView(QStringLiteral("archivist.preview"), pathResource("art/skull.png"), "skull.png");
        store.openView(QStringLiteral("archivist.editor"), pathResource("a/notes.md"), "notes.md");
        store.openView(QStringLiteral("archivist.editor"), pathResource("b/notes.md"), "notes.md");
        const qsizetype viewsBefore = agentViews(store).size();
        auto named = [](const QString &reference, const QString &lookup) {
            return QVariantMap{{"kind", "file"}, {"reference", reference}, {"libraryId", "lib-main"}, {"lookup", lookup}};
        };
        const QVariantMap request{
            {"action", "compose"},
            {"template", ""},
            {"strategy", "focus_with_sidebar"},
            {"slots", QVariantList{
                QVariantMap{{"name", "Code"}, {"holds", QStringList{"code"}}, {"weight", 10}, {"items", QVariantList{named("main.cpp", "not_found")}}},
                QVariantMap{{"name", "Art"}, {"holds", QStringList{"preview"}}, {"weight", 0}, {"items", QVariantList{named("skull.png", "not_found")}}},
                QVariantMap{{"name", "Notes"}, {"holds", QStringList{"docs"}}, {"weight", 0}, {"items", QVariantList{named("notes.md", "not_found"), named("ghost.png", "not_found")}}},
            }},
            {"focus", ""},
            {"saveAs", ""},
            {"explicit", true},
            {"chatId", "chat-1"},
        };
        const int revision = store.revision();
        const QVariantMap result = store.runAgentRequest(request, true);
        QVERIFY2(result.value("ok").toBool(), qPrintable(result.value("error").toString()));
        QCOMPARE(store.revision(), revision + 1); // one change
        QHash<QString, QString> statuses;
        for (const QVariant &value : result.value("resources").toList()) {
            const QVariantMap resource = value.toMap();
            statuses.insert(resource.value("reference").toString(), resource.value("status").toString());
        }
        QCOMPARE(statuses.value("main.cpp"), QStringLiteral("reused"));
        QCOMPARE(statuses.value("skull.png"), QStringLiteral("reused"));
        QCOMPARE(statuses.value("notes.md"), QStringLiteral("ambiguous"));
        QCOMPARE(statuses.value("ghost.png"), QStringLiteral("not_found"));
        QCOMPARE(agentViews(store).size(), viewsBefore); // nothing opened in their place, nothing lost
        // The open image is the one in "Art".
        bool artHoldsSkull = false;
        for (const QVariant &value : result.value("panels").toList()) {
            const QVariantMap panel = value.toMap();
            if (panel.value("name").toString() == QStringLiteral("Art")) {
                artHoldsSkull = panel.value("shows").toString().contains(QStringLiteral("skull.png"));
            }
        }
        QVERIFY2(artHoldsSkull, qPrintable(QJsonDocument::fromVariant(result.value("panels")).toJson()));
        // One undo puts it all back.
        QVERIFY(store.undoAgentChange());
        QVERIFY(!store.canUndoAgentChange());
        QCOMPARE(agentViews(store).size(), viewsBefore);
    }

    // Patch (the default) keeps the panels that exist and changes only the
    // ones named; a recipe replaces the arrangement.
    void composePatchesByDefault()
    {
        QSettings settings;
        settings.remove(QStringLiteral("workspace/docking/layouts"));
        settings.remove(QStringLiteral("workspace/preferences"));
        WorkspaceStore store;
        store.setCollection(QStringLiteral("compose-patch"));
        store.setSurfaceSize(store.primarySurfaceId(), 1440, 900);
        const QString mine = store.openView(QStringLiteral("archivist.editor"), documentResource("main.cpp"), "main.cpp");
        const QString editorPanel = store.view(mine).value("anchorId").toString();
        auto compose = [](const QString &mode, const QString &strategy, const QVariantList &panelList) {
            return QVariantMap{
                {"action", "compose"}, {"mode", mode}, {"template", ""}, {"strategy", strategy}, {"rows", 0}, {"columns", 0},
                {"slots", panelList}, {"focus", ""}, {"saveAs", ""}, {"explicit", true}, {"chatId", "chat-1"},
            };
        };
        auto slot = [](const QString &name, const QString &position, const QVariantList &items) {
            return QVariantMap{{"name", name}, {"holds", QStringList{}}, {"weight", 0}, {"position", position}, {"items", items}};
        };

        // "Put the guide on the right": one new panel, the editor untouched.
        QVariantMap result = store.runAgentRequest(compose("", "", {slot("Docs", "right", {agentFile("guide.md")})}), true);
        QVERIFY2(result.value("ok").toBool(), qPrintable(result.value("error").toString()));
        QCOMPARE(agentAreas(store).size(), 2);
        QVERIFY(agentAreas(store).contains(editorPanel));
        QCOMPARE(store.view(mine).value("anchorId").toString(), editorPanel);
        const QStringList patched = agentAreas(store);

        // A slot named like an existing panel fills that panel: no new one.
        result = store.runAgentRequest(compose("patch", "", {slot("Docs", "", {agentFile("notes.md")})}), true);
        QVERIFY2(result.value("ok").toBool(), qPrintable(result.value("error").toString()));
        QCOMPARE(agentAreas(store), patched);

        // A recipe replaces the arrangement, even when patch was asked for.
        result = store.runAgentRequest(compose("patch", "columns", {
            slot("Code", "left", {agentFile("main.cpp")}), slot("Docs", "center", {agentFile("guide.md")}), slot("Notes", "right", {agentFile("notes.md")})}), true);
        QVERIFY2(result.value("ok").toBool(), qPrintable(result.value("error").toString()));
        QCOMPARE(agentAreas(store).size(), 3);
        QVERIFY(!store.view(mine).isEmpty());

        // Each was one step: three undos go back to the start.
        QVERIFY(store.undo());
        QCOMPARE(agentAreas(store), patched);
        QVERIFY(store.undo());
        QVERIFY(store.undo());
        QCOMPARE(agentAreas(store), QStringList({editorPanel}));
        QCOMPARE(agentViews(store), QStringList({mine}));
    }

    // The composer can dock into an anchor that shows a chat, the move is
    // an undo step, and it falls back to the bottom when that anchor goes.
    void composerDocksUndoesAndFallsBack()
    {
        WorkspaceStore store;
        store.setCollection(QStringLiteral("composer-dock"));
        store.setSurfaceSize(store.primarySurfaceId(), 1200, 800);
        const QString editor = store.openView(QStringLiteral("archivist.editor"), documentResource("a.cpp"), "a.cpp");
        const QString chat = store.openView(QStringLiteral("archivist.chat"), {{"kind", "chat"}, {"id", "c1"}}, "Chat");
        const QString chatAnchor = store.view(chat).value("anchorId").toString();
        const QString editorAnchor = store.view(editor).value("anchorId").toString();
        QVERIFY(!chatAnchor.isEmpty());
        QCOMPARE(store.composerPlacement(QStringLiteral("compact")).value("kind").toString(), QStringLiteral("bottom"));

        QSignalSpy changed(&store, &WorkspaceStore::composerChanged);
        if (editorAnchor != chatAnchor) {
            // An anchor with no chat in it isn't a place for the composer.
            QVERIFY(!store.dockComposer(editorAnchor));
        }
        QVERIFY(!store.dockComposer(QStringLiteral("no-such-anchor")));
        // A locked anchor is respected.
        QVERIFY(store.setAnchorContentLocked(chatAnchor, true));
        QVERIFY(!store.dockComposer(chatAnchor));
        QVERIFY(store.setAnchorContentLocked(chatAnchor, false));
        QCOMPARE(changed.count(), 0);

        const bool couldUndo = store.canUndo();
        QVERIFY(store.dockComposer(chatAnchor));
        QCOMPARE(changed.count(), 1);
        QVERIFY(store.canUndo());
        QVERIFY(couldUndo || !store.undoSummary().isEmpty());
        QVariantMap placement = store.composerPlacement(QStringLiteral("compact"));
        QCOMPARE(placement.value("kind").toString(), QStringLiteral("anchor"));
        QCOMPARE(placement.value("anchorId").toString(), chatAnchor);
        // The workbench lives at the bottom; the docked spot is kept for
        // when the chat collapses again.
        QCOMPARE(store.composerPlacement(QStringLiteral("workbench")).value("kind").toString(), QStringLiteral("bottom"));
        QVERIFY(!store.composerModeAllows(QStringLiteral("workbench"), QStringLiteral("anchor")));
        QVERIFY(store.composerModeAllows(QStringLiteral("compact"), QStringLiteral("anchor")));

        // Undo puts it back at the bottom; redo docks it again.
        QVERIFY(store.undo());
        QCOMPARE(store.composerPlacement(QStringLiteral("compact")).value("kind").toString(), QStringLiteral("bottom"));
        QVERIFY(store.redo());
        QCOMPARE(store.composerPlacement(QStringLiteral("compact")).value("anchorId").toString(), chatAnchor);

        // Closing the chat leaves no chat anchor: the composer goes back to
        // the bottom rather than pointing at nothing.
        QVERIFY(store.closeView(chat, true).value("ok").toBool());
        QCOMPARE(store.composerPlacement(QStringLiteral("compact")).value("kind").toString(), QStringLiteral("bottom"));
    }

    void composerModesPersistPerProject()
    {
        {
            WorkspaceStore store;
            store.setCollection(QStringLiteral("composer-saved"));
            QCOMPARE(store.composerMode(QString()), QStringLiteral("compact"));
            QVERIFY(store.setComposerMode(QStringLiteral("workbench")));
            QVERIFY(!store.setComposerMode(QStringLiteral("no-such-mode")));
            QVERIFY(store.setConversationComposerMode(QStringLiteral("c2"), QStringLiteral("compact")));
            QCOMPARE(store.composerMode(QStringLiteral("c1")), QStringLiteral("workbench"));
            QCOMPARE(store.composerMode(QStringLiteral("c2")), QStringLiteral("compact"));
            // Mode switches aren't undo steps: only placement is.
            QVERIFY(!store.canUndo());

            // Another project has its own state.
            store.setCollection(QStringLiteral("composer-other"));
            QCOMPARE(store.composerMode(QStringLiteral("c1")), QStringLiteral("compact"));
            store.setCollection(QStringLiteral("composer-saved"));
            QCOMPARE(store.composerMode(QStringLiteral("c1")), QStringLiteral("workbench"));
        }

        WorkspaceStore reopened;
        reopened.setCollection(QStringLiteral("composer-saved"));
        QCOMPARE(reopened.composerMode(QStringLiteral("c1")), QStringLiteral("workbench"));
        QCOMPARE(reopened.composerMode(QStringLiteral("c2")), QStringLiteral("compact"));

        // A deleted chat drops its override.
        reopened.forgetComposerConversation(QStringLiteral("c2"));
        QCOMPARE(reopened.composerMode(QStringLiteral("c2")), QStringLiteral("workbench"));

        // A space can default to the workbench; an explicit choice still wins.
        QVERIFY(reopened.restoreComposer());
        QVERIFY(reopened.setComposerSpaceDefault(QStringLiteral("workbench")));
        QCOMPARE(reopened.composerMode(QStringLiteral("c1")), QStringLiteral("workbench"));
        QVERIFY(reopened.setComposerMode(QStringLiteral("compact")));
        QCOMPARE(reopened.composerMode(QStringLiteral("c1")), QStringLiteral("compact"));
    }

    void composerFloatsAndReturns()
    {
        WorkspaceStore store;
        store.setCollection(QStringLiteral("composer-float"));
        store.setSurfaceSize(store.primarySurfaceId(), 1200, 800);
        const QString chat = store.openView(QStringLiteral("archivist.chat"), {{"kind", "chat"}, {"id", "c1"}}, "Chat");
        const QString anchor = store.view(chat).value("anchorId").toString();
        QVERIFY(store.dockComposer(anchor));

        QVERIFY(store.floatComposer(100, 120, 520, 140));
        QCOMPARE(store.composerPlacement(QStringLiteral("compact")).value("kind").toString(), QStringLiteral("floating"));
        // Moving while floating doesn't pile up undo steps.
        const QString summary = store.undoSummary();
        QVERIFY(store.floatComposer(140, 160, 520, 140));
        QCOMPARE(store.undoSummary(), summary);
        // Too small to use is refused.
        QVERIFY(!store.floatComposer(0, 0, 10, 10));

        QVERIFY(store.returnComposerFromFloating());
        QCOMPARE(store.composerPlacement(QStringLiteral("compact")).value("anchorId").toString(), anchor);

        QVERIFY(store.restoreComposer());
        QCOMPARE(store.composerPlacement(QStringLiteral("compact")).value("kind").toString(), QStringLiteral("bottom"));
        QVERIFY(store.undo());
        QCOMPARE(store.composerPlacement(QStringLiteral("compact")).value("anchorId").toString(), anchor);
    }

    // Undo and redo walk one history: views an undo closed come back as
    // they were on redo, edits are never touched, and a new change clears
    // redo.
    void undoAndRedoKeepDocuments()
    {
        FakeDisk disk;
        disk.files.insert(QStringLiteral("lib-main:a.cpp"), QStringLiteral("alpha"));
        disk.files.insert(QStringLiteral("lib-main:b.cpp"), QStringLiteral("beta"));
        DocumentStore documents;
        disk.attach(documents);
        WorkspaceStore store;
        store.setDocumentStore(&documents);
        store.setCollection(QStringLiteral("history-redo"));
        store.setSurfaceSize(store.primarySurfaceId(), 1200, 800);
        const QString mine = store.openView(QStringLiteral("archivist.editor"), documentResource("mine.cpp"), "mine.cpp");
        QVERIFY(!store.canUndo());
        QVERIFY(!store.canRedo());

        QVariantMap result = store.runAgentRequest(agentShow({agentFile("a.cpp"), agentFile("b.cpp")}, QStringLiteral("right")), true);
        QVERIFY2(result.value("ok").toBool(), qPrintable(result.value("error").toString()));
        const QStringList arranged = agentAreas(store);
        QString a;
        QString b;
        for (const QString &viewId : agentViews(store)) {
            const QString fileId = store.view(viewId).value("resource").toMap().value("details").toMap().value("fileId").toString();
            if (fileId == QStringLiteral("a.cpp")) {
                a = viewId;
            } else if (fileId == QStringLiteral("b.cpp")) {
                b = viewId;
            }
        }
        QVERIFY(!a.isEmpty() && !b.isEmpty());
        const QVariantMap resourceB = store.view(b).value("resource").toMap();
        const QString docA = store.view(a).value("resource").toMap().value("id").toString();
        QVERIFY(documents.applyEdits(docA, QStringLiteral("p"), 1, insertAt(0, ">")).value("ok").toBool());

        // Undo: b (clean) closes, a (edited) stays with its edit.
        QVERIFY(store.undo());
        QVERIFY(store.view(b).isEmpty());
        QVERIFY(!store.view(a).isEmpty());
        QVERIFY(store.canRedo());
        QVERIFY(!store.redoSummary().isEmpty());

        // Redo: the same arrangement, b back with the same id and resource.
        QSignalSpy opened(&store, &WorkspaceStore::viewOpened);
        QVERIFY(store.redo());
        QCOMPARE(agentAreas(store), arranged);
        QVERIFY(!store.view(b).isEmpty());
        QCOMPARE(store.view(b).value("resource").toMap(), resourceB);
        QCOMPARE(opened.count(), 1);
        QCOMPARE(documents.snapshot(docA).value("text").toString(), QStringLiteral(">alpha"));
        QVERIFY(documents.isDirty(docA));
        QVERIFY(!store.canRedo());

        // Undo, then a new change: redo is gone.
        QVERIFY(store.undo());
        QVERIFY(store.canRedo());
        QVERIFY(store.runAgentRequest(agentShow({agentFile("c.md")}, QStringLiteral("left")), true).value("ok").toBool());
        QVERIFY(!store.canRedo());
        QVERIFY(!store.redo());

        // The agent can redo too ("redo").
        QVERIFY(store.undo());
        result = store.runAgentRequest({{"action", "restore"}, {"scope", "redo"}, {"layout", ""}, {"explicit", true}, {"chatId", "chat-1"}}, true);
        QVERIFY2(result.value("ok").toBool(), qPrintable(result.value("error").toString()));
        QVERIFY(!store.canRedo());
        QVERIFY(!store.view(mine).isEmpty());
    }

    // A 2 x 2 grid with this chat in a panel: the bottom chat (the same
    // conversation) folds away, and undo brings it back. A refused
    // arrangement changes and announces nothing.
    void gridWithChatHidesTheBottomChat()
    {
        QSettings settings;
        settings.remove(QStringLiteral("workspace/docking/layouts"));
        settings.remove(QStringLiteral("workspace/preferences"));
        WorkspaceStore store;
        store.setCollection(QStringLiteral("grid-bottom-chat"));
        store.setSurfaceSize(store.primarySurfaceId(), 1440, 900);
        store.openView(QStringLiteral("archivist.editor"), documentResource("main.cpp"), "main.cpp");
        store.setCommandSurfaceState(false, QStringLiteral("chat"));
        QSignalSpy surface(&store, &WorkspaceStore::commandSurfaceRequested);
        QSignalSpy arranged(&store, &WorkspaceStore::agentArranged);
        auto grid = [](int rows, int columns) {
            return QVariantMap{
                {"action", "compose"}, {"template", ""}, {"strategy", "grid"}, {"rows", rows}, {"columns", columns},
                {"slots", QVariantList{QVariantMap{{"name", "Chat"}, {"holds", QStringList{"chat"}}, {"weight", 0}, {"position", "bottom-left"},
                                                   {"items", QVariantList{QVariantMap{{"kind", "chat"}, {"reference", ""}}}}}}},
                {"focus", ""}, {"saveAs", ""}, {"explicit", true}, {"chatId", "chat-1"},
            };
        };

        // Refused before anything shows (a 3 x 3 grid is more than six panels).
        int revision = store.revision();
        QVariantMap result = store.runAgentRequest(grid(3, 3), true);
        QVERIFY(!result.value("ok").toBool());
        QCOMPARE(store.revision(), revision);
        QCOMPARE(arranged.count(), 0);
        QCOMPARE(surface.count(), 0);

        revision = store.revision();
        result = store.runAgentRequest(grid(2, 2), true);
        QVERIFY2(result.value("ok").toBool(), qPrintable(result.value("error").toString()));
        QCOMPARE(store.revision(), revision + 1);
        QCOMPARE(arranged.count(), 1);
        QCOMPARE(agentAreas(store).size(), 4);
        QVERIFY(store.commandSurface().value("collapsed").toBool());
        QCOMPARE(surface.count(), 1);
        QCOMPARE(surface.last().at(1).toBool(), false);
        QVERIFY(result.contains("bottomPanel"));
        // The editor kept its content in a cell.
        QVERIFY(agentViews(store).size() >= 2);

        // One undo: the panels and the bottom chat come back.
        QVERIFY(store.undoAgentChange());
        QCOMPARE(surface.last().at(0).toString(), QStringLiteral("chat"));
        QCOMPARE(surface.last().at(1).toBool(), true);
    }

    // Authorisation is the workspace's: a request from the person in the
    // conversation in front applies, however it was phrased. Only the
    // "unless I ask" preference looks at whether it read as a request.
    void foregroundRequestsApply()
    {
        QSettings settings;
        settings.remove(QStringLiteral("workspace/preferences"));
        WorkspaceStore store;
        store.setCollection(QStringLiteral("foreground-requests"));
        store.setSurfaceSize(store.primarySurfaceId(), 1440, 900);
        store.openView(QStringLiteral("archivist.editor"), documentResource("main.cpp"), "main.cpp");
        auto add = [](const QString &name, bool asked) {
            return QVariantMap{{"action", "add"}, {"target", ""}, {"to", "right"}, {"name", name}, {"explicit", true}, {"asked", asked}, {"chatId", "chat-1"}};
        };

        // "try another" didn't read as a workspace request, but the person asked in front.
        QCOMPARE(store.runAgentRequest(add("One", false), true).value("status").toString(), QStringLiteral("applied"));
        // A background conversation is staged.
        QCOMPARE(store.runAgentRequest(add("Two", true), false).value("status").toString(), QStringLiteral("suggested"));
        store.dismissAgentSuggestion();
        // "Don't rearrange unless I ask" holds back what wasn't a request.
        QVERIFY(store.setPreference(QStringLiteral("rearranging"), QStringLiteral("only_when_asked"), {}, false).value("ok").toBool());
        QCOMPARE(store.runAgentRequest(add("Three", false), true).value("status").toString(), QStringLiteral("suggested"));
        store.dismissAgentSuggestion();
        QCOMPARE(store.runAgentRequest(add("Four", true), true).value("status").toString(), QStringLiteral("applied"));
        QCOMPARE(agentAreas(store).size(), 3);
    }

    // ---- Gravity Wells -------------------------------------------------

    void minimizingKeepsWorkAliveAndUndoes()
    {
        FakeDisk disk;
        disk.files.insert(QStringLiteral("lib-main:a.cpp"), QStringLiteral("alpha"));
        disk.files.insert(QStringLiteral("lib-main:b.cpp"), QStringLiteral("beta"));
        DocumentStore documents;
        disk.attach(documents);
        WorkspaceStore store;
        store.setDocumentStore(&documents);
        store.setCollection(QStringLiteral("wells-minimize"));
        store.setSurfaceSize(store.primarySurfaceId(), 1200, 800);
        const QString a = store.openView(QStringLiteral("archivist.editor"), documentResource("a.cpp"), "a.cpp");
        const QString b = store.openView(QStringLiteral("archivist.editor"), documentResource("b.cpp"), "b.cpp");
        const QString docB = store.view(b).value("resource").toMap().value("id").toString();
        QVERIFY(documents.applyEdits(docB, b, 1, insertAt(0, ">")).value("ok").toBool());
        QVERIFY(documents.isDirty(docB));

        const QVariantMap minimized = store.minimizeView(b);
        QVERIFY2(minimized.value("ok").toBool(), qPrintable(minimized.value("error").toString()));
        QVERIFY(store.isViewMinimized(b));
        QCOMPARE(store.undoSummary(), QStringLiteral("Minimized “b.cpp”"));

        // Still a view (hidden, in no window), its buffer and edit kept.
        const QVariantMap row = store.viewRow(b);
        QCOMPARE(row.value("isHidden").toBool(), true);
        QCOMPARE(row.value("surfaceId").toString(), QString());
        QVERIFY(documents.holders(docB).contains(b));
        QCOMPARE(documents.snapshot(docB).value("text").toString(), QStringLiteral(">beta"));
        QVERIFY(store.view(b).value("minimized").toBool());

        const QVariantList wells = store.wells();
        QCOMPARE(wells.size(), 1);
        const QVariantList entries = wells.first().toMap().value("entries").toList();
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first().toMap().value("state").toString(), QStringLiteral("minimized"));
        QCOMPARE(entries.first().toMap().value("title").toString(), QStringLiteral("b.cpp"));

        // Undo puts it back in its tab; redo puts it away again.
        QVERIFY(store.undo());
        QVERIFY(!store.isViewMinimized(b));
        QCOMPARE(store.view(b).value("anchorId").toString(), store.view(a).value("anchorId").toString());
        QVERIFY(store.redo());
        QVERIFY(store.isViewMinimized(b));
        QCOMPARE(documents.snapshot(docB).value("text").toString(), QStringLiteral(">beta"));

        // Restoring is a step of its own.
        const QVariantMap restored = store.restoreMinimizedView(b);
        QVERIFY2(restored.value("ok").toBool(), qPrintable(restored.value("error").toString()));
        QCOMPARE(restored.value("how").toString(), QStringLiteral("original"));
        QCOMPARE(store.undoSummary(), QStringLiteral("Restored “b.cpp”"));
        QVERIFY(store.viewRow(b).value("isActive").toBool());
        QVERIFY(documents.isDirty(docB));
    }

    // While the Observatory live recorder runs, operations report spans with
    // their outcome and publish reports its stages; never titles or paths.
    void liveRecorderSeesOperations()
    {
        WorkspaceStore store;
        store.setCollection(QStringLiteral("wells-live"));
        store.setSurfaceSize(store.primarySurfaceId(), 1200, 800);
        const QString a = store.openView(QStringLiteral("archivist.editor"), documentResource("a.cpp"), "secret-name.cpp");
        const QString b = store.openView(QStringLiteral("archivist.editor"), documentResource("b.cpp"), "b.cpp");

        Live::Session session;
        session.reset(LiveEvents::nowNs());
        LiveEvents::setSink(&session);
        QVERIFY(store.minimizeView(b).value("ok").toBool());
        QVERIFY(store.restoreMinimizedView(b).value("ok").toBool());
        QVERIFY(store.activateView(a));
        QVERIFY(!store.minimizeView(QStringLiteral("no-such-view")).value("ok").toBool());
        QVERIFY(store.beginDrag(QStringLiteral("view"), b));
        store.cancelDrag();
        store.setResizing(true);
        store.setResizing(false);
        LiveEvents::setSink(nullptr);

        const QJsonArray events = Live::eventsJson(session.snapshot()).value(QStringLiteral("events")).toArray();
        QHash<QString, QList<QJsonObject>> byName;
        for (const QJsonValue &value : events) {
            const QJsonObject event = value.toObject();
            byName[event.value("category").toString() + "." + event.value("name").toString()].append(event);
        }
        QCOMPARE(byName.value("wells.minimize").size(), 2);
        QCOMPARE(byName.value("wells.minimize").at(0).value("ok").toBool(), true);
        QCOMPARE(byName.value("wells.minimize").at(1).value("ok").toBool(), false);
        QCOMPARE(byName.value("wells.restore").size(), 1);
        QVERIFY(byName.value("wells.restore").at(0).value("durationMs").isDouble());
        QVERIFY(!byName.value("workspace.publish").isEmpty());
        const QJsonObject stages = byName.value("workspace.publish").at(0).value("fields").toObject();
        QVERIFY(stages.contains("geometry") && stages.contains("composer"));
        QCOMPARE(byName.value("workspace.switch-tab").at(0).value("fields").toObject().value("viewType").toString(), QStringLiteral("archivist.editor"));
        QCOMPARE(byName.value("workspace.drag").size(), 1);
        QCOMPARE(byName.value("workspace.drag").at(0).value("fields").toObject().value("outcome").toString(), QStringLiteral("cancelled"));
        QCOMPARE(byName.value("workspace.drag").at(0).value("ok").toBool(), false);
        QCOMPARE(byName.value("workspace.resize-split").size(), 1);
        QVERIFY(!QJsonDocument(events).toJson().contains("secret-name"));

        // Not recording: nothing is collected.
        const qsizetype before = session.snapshot().events.size();
        QVERIFY(store.minimizeView(b).value("ok").toBool());
        QCOMPARE(session.snapshot().events.size(), before);
    }

    void openingSomethingPutAwayBringsItBack()
    {
        WorkspaceStore store;
        store.setCollection(QStringLiteral("wells-open"));
        store.setSurfaceSize(store.primarySurfaceId(), 1200, 800);
        store.openView(QStringLiteral("archivist.editor"), documentResource("a.cpp"), "a.cpp");
        const QString b = store.openView(QStringLiteral("archivist.editor"), documentResource("b.cpp"), "b.cpp");
        QVERIFY(store.minimizeView(b).value("ok").toBool());

        // The same resource asked for again: that view, not a copy.
        QCOMPARE(store.viewsForResource(QStringLiteral("document"), documentResource("b.cpp").value("id").toString()), QStringList{b});
        QCOMPARE(store.openView(QStringLiteral("archivist.editor"), documentResource("b.cpp"), "b.cpp"), b);
        QVERIFY(!store.isViewMinimized(b));

        // Activating a minimized view restores it too.
        QVERIFY(store.minimizeView(b).value("ok").toBool());
        QVERIFY(store.activateView(b));
        QVERIFY(!store.isViewMinimized(b));
        QVERIFY(store.viewRow(b).value("isActive").toBool());
    }

    void closingSomethingPutAwayStillAsksAboutUnsavedWork()
    {
        FakeDisk disk;
        disk.files.insert(QStringLiteral("lib-main:a.cpp"), QStringLiteral("alpha"));
        DocumentStore documents;
        disk.attach(documents);
        WorkspaceStore store;
        store.setDocumentStore(&documents);
        store.setCollection(QStringLiteral("wells-close"));
        store.setSurfaceSize(store.primarySurfaceId(), 1200, 800);
        const QString a = store.openView(QStringLiteral("archivist.editor"), documentResource("a.cpp"), "a.cpp");
        const QString docA = store.view(a).value("resource").toMap().value("id").toString();
        QVERIFY(documents.applyEdits(docA, a, 1, insertAt(0, "!")).value("ok").toBool());
        QVERIFY(store.minimizeView(a).value("ok").toBool());

        QVariantMap closed = store.closeView(a, false);
        QVERIFY(!closed.value("ok").toBool());
        QVERIFY(store.isViewMinimized(a));
        closed = store.closeView(a, true);
        QVERIFY(closed.value("ok").toBool());
        QVERIFY(store.view(a).isEmpty());
        QVERIFY(store.wells().first().toMap().value("entries").toList().isEmpty());
    }

    void pinsAndClicksDoWhatTheEntryShows()
    {
        WorkspaceStore store;
        store.setCollection(QStringLiteral("wells-pins"));
        store.setSurfaceSize(store.primarySurfaceId(), 1200, 800);
        store.openView(QStringLiteral("archivist.editor"), documentResource("a.cpp"), "a.cpp");
        const QString b = store.openView(QStringLiteral("archivist.editor"), documentResource("b.cpp"), "b.cpp");
        const QVariantMap pinned = store.setViewPinned(b, true);
        QVERIFY(pinned.value("ok").toBool());
        const QString entryId = pinned.value("entryId").toString();
        QCOMPARE(store.undoSummary(), QStringLiteral("Pinned “b.cpp”"));

        QVERIFY(store.closeView(b, true).value("ok").toBool());
        QVariantMap entry = store.wells().first().toMap().value("entries").toList().first().toMap();
        QCOMPARE(entry.value("state").toString(), QStringLiteral("pinnedClosed"));

        QSignalSpy opened(&store, &WorkspaceStore::viewOpened);
        const QVariantMap clicked = store.activateWellEntry(entryId);
        QVERIFY2(clicked.value("ok").toBool(), qPrintable(clicked.value("error").toString()));
        QCOMPARE(clicked.value("how").toString(), QStringLiteral("opened"));
        QCOMPARE(opened.count(), 1);
        entry = store.wells().first().toMap().value("entries").toList().first().toMap();
        QCOMPARE(entry.value("state").toString(), QStringLiteral("pinnedOpen"));
        QCOMPARE(store.wells().first().toMap().value("entries").toList().size(), 1);

        QVERIFY(store.setWellEntryPinned(entryId, false).value("ok").toBool());
        QVERIFY(store.wells().first().toMap().value("entries").toList().isEmpty());
    }

    // ---- Gravity Wells: panels, drops and floating panels -----------------

    void aWholePanelGoesIntoTheWellAndUndoes()
    {
        WorkspaceStore store;
        store.setCollection(QStringLiteral("wells-panel"));
        store.setSurfaceSize(store.primarySurfaceId(), 1200, 800);
        store.openView(QStringLiteral("archivist.editor"), documentResource("a.cpp"), "a.cpp");
        const QString b = store.openView(QStringLiteral("archivist.editor"), documentResource("b.cpp"), "b.cpp");
        const QString panel = store.splitView(b, store.view(b).value("anchorId").toString(), QStringLiteral("right"));
        QVERIFY(!panel.isEmpty());
        const QVariantMap put = store.minimizeAnchor(panel);
        QVERIFY2(put.value("ok").toBool(), qPrintable(put.value("error").toString()));
        QVERIFY(store.isViewMinimized(b));
        const QVariantMap entry = store.wells().first().toMap().value("entries").toList().first().toMap();
        QVERIFY(entry.value("panel").toBool());
        QCOMPARE(entry.value("viewTitles").toStringList(), QStringList{QStringLiteral("b.cpp")});

        QVERIFY(store.undo());
        QVERIFY(!store.isViewMinimized(b));
        QCOMPARE(store.view(b).value("anchorId").toString(), panel);
        QVERIFY(store.redo());
        QVERIFY(store.isViewMinimized(b));
        QVERIFY(store.activateWellEntry(entry.value("entryId").toString()).value("ok").toBool());
        QVERIFY(!store.isViewMinimized(b));
    }

    void droppingOnTheWellPutsItAway()
    {
        WorkspaceStore store;
        store.setCollection(QStringLiteral("wells-drop"));
        store.setSurfaceSize(store.primarySurfaceId(), 1200, 800);
        store.openView(QStringLiteral("archivist.editor"), documentResource("a.cpp"), "a.cpp");
        const QString b = store.openView(QStringLiteral("archivist.editor"), documentResource("b.cpp"), "b.cpp");
        const QString c = store.openView(QStringLiteral("archivist.editor"), documentResource("c.cpp"), "c.cpp");
        const QString panel = store.splitView(c, store.view(c).value("anchorId").toString(), QStringLiteral("right"));

        // A tab: over the Well, then away again, nothing changes...
        QVERIFY(store.beginDrag(QStringLiteral("view"), b));
        store.updateDragOverWell(QStringLiteral("well.main"));
        QCOMPARE(store.dragWellId(), QStringLiteral("well.main"));
        QVERIFY(!store.dropFloats());
        QVERIFY(!store.isViewMinimized(b)); // nothing happens before the drop
        store.updateDrag(store.primarySurfaceId(), 5, 5);
        QVERIFY(store.dragWellId().isEmpty());
        store.cancelDrag();
        QVERIFY(!store.isViewMinimized(b));

        // ...let go over it, and it's put away, one step.
        QSignalSpy finished(&store, &WorkspaceStore::dragFinished);
        QVERIFY(store.beginDrag(QStringLiteral("view"), b));
        store.updateDragOverWell(QStringLiteral("well.main"));
        QVERIFY(store.finishDrag());
        QVERIFY(store.isViewMinimized(b));
        QVERIFY(!store.dragActive());
        QCOMPARE(finished.last().at(0).toString(), QStringLiteral("committed"));
        QCOMPARE(store.undoSummary(), QStringLiteral("Minimized “b.cpp”"));

        // A panel goes as one.
        QVERIFY(store.beginDrag(QStringLiteral("anchor"), panel));
        store.updateDragOverWell(QStringLiteral("well.main"));
        QVERIFY(store.finishDrag());
        QVERIFY(store.isViewMinimized(c));
        QCOMPARE(store.wells().first().toMap().value("entries").toList().size(), 2);

        QVERIFY(store.undo());
        QVERIFY(!store.isViewMinimized(c));
        QVERIFY(store.undo());
        QVERIFY(!store.isViewMinimized(b));
    }

    void floatingPanelsTuckAwayUnlessLeftOnTop()
    {
        WorkspaceStore store;
        store.setCollection(QStringLiteral("wells-tuck"));
        store.setSurfaceSize(store.primarySurfaceId(), 1200, 800);
        const QString a = store.openView(QStringLiteral("archivist.editor"), documentResource("a.cpp"), "a.cpp");
        const QString main = store.view(a).value("anchorId").toString();
        const QString b = store.openView(QStringLiteral("archivist.editor"), documentResource("b.cpp"), "b.cpp");
        const QString c = store.openView(QStringLiteral("archivist.editor"), documentResource("c.cpp"), "c.cpp");
        QVERIFY(!store.floatView(b, 100, 100, 400, 300).isEmpty());
        const QString bPanel = store.view(b).value("anchorId").toString();
        QVERIFY(store.focusAnchor(bPanel));

        // Working somewhere else: it goes into the Well, one undoable step.
        QVERIFY(store.focusAnchor(main));
        QVERIFY(store.isViewMinimized(b));
        QVERIFY(store.panelSurfaceIds().isEmpty());
        QVERIFY(store.undoSummary().startsWith(QStringLiteral("Tucked")));

        // Clicking it brings it back floating where it was.
        const QString entryId = store.wells().first().toMap().value("entries").toList().first().toMap().value("entryId").toString();
        QVERIFY(store.activateWellEntry(entryId).value("ok").toBool());
        QVERIFY(!store.isViewMinimized(b));
        QCOMPARE(store.panelSurfaceIds().size(), 1);
        const QString bSurface = store.panelSurfaceIds().first();
        QCOMPARE(store.surface(bSurface).value("x").toDouble(), 100.0);

        // A second floating panel brought out sends the first one back.
        QVERIFY(!store.floatView(c, 600, 100, 400, 300).isEmpty());
        QVERIFY(store.focusAnchor(store.view(c).value("anchorId").toString()));
        QVERIFY(store.isViewMinimized(b));

        // Left on top, it stays out.
        const QString cSurface = store.panelSurfaceIds().first();
        QVERIFY(store.setSurfaceLeaveOnTop(cSurface, true));
        QVERIFY(store.surface(cSurface).value("leaveOnTop").toBool());
        QVERIFY(store.focusAnchor(main));
        QVERIFY(!store.isViewMinimized(c));
        QVERIFY(store.view(c).value("anchorId").toString() != main);
    }

    // Animated tucking: the store asks first, the UI tucks after its
    // picture; nothing tucks if the reason went away meanwhile.
    void animatedTuckAsksFirst()
    {
        WorkspaceStore store;
        store.setCollection(QStringLiteral("wells-tuck-animated"));
        store.setSurfaceSize(store.primarySurfaceId(), 1200, 800);
        store.setTuckAnimated(true);
        const QString a = store.openView(QStringLiteral("archivist.editor"), documentResource("a.cpp"), "a.cpp");
        const QString main = store.view(a).value("anchorId").toString();
        const QString b = store.openView(QStringLiteral("archivist.editor"), documentResource("b.cpp"), "b.cpp");
        QVERIFY(!store.floatView(b, 100, 100, 400, 300).isEmpty());
        const QString bPanel = store.view(b).value("anchorId").toString();
        const QString bSurface = store.panelSurfaceIds().first();
        QVERIFY(store.focusAnchor(bPanel));

        QSignalSpy asked(&store, &WorkspaceStore::tuckRequested);
        QVERIFY(store.focusAnchor(main));
        QCOMPARE(asked.size(), 1);
        QCOMPARE(asked.first().at(0).toString(), bSurface);
        QVERIFY(!store.isViewMinimized(b));

        // Back on it before the picture came: it stays.
        QVERIFY(store.focusAnchor(bPanel));
        QVERIFY(store.tuckAway(bSurface).isEmpty());
        QVERIFY(!store.isViewMinimized(b));

        // Away again: the UI tucks it, one step, and learns the entry.
        QVERIFY(store.focusAnchor(main));
        const QVariantList tucked = store.tuckAway(bSurface);
        QCOMPARE(tucked.size(), 1);
        QCOMPARE(tucked.first().toMap().value("anchorId").toString(), bPanel);
        QVERIFY(!tucked.first().toMap().value("entryId").toString().isEmpty());
        QVERIFY(store.isViewMinimized(b));
        QVERIFY(store.undoSummary().startsWith(QStringLiteral("Tucked")));
        // Already gone: asking again does nothing.
        QVERIFY(store.tuckAway(bSurface).isEmpty());
    }

    // What the Well bar and the tab menu read.
    void theWellShowsWhatItsIconsNeed()
    {
        WorkspaceStore store;
        store.setCollection(QStringLiteral("wells-icons"));
        store.setSurfaceSize(store.primarySurfaceId(), 1200, 800);
        store.openView(QStringLiteral("archivist.editor"), documentResource("a.cpp"), "a.cpp");
        const QString b = store.openView(QStringLiteral("archivist.editor"), documentResource("notes.md"), "notes.md");
        QVERIFY(!store.isViewPinned(b));
        QVERIFY(store.setViewPinned(b, true).value("ok").toBool());
        QVERIFY(store.isViewPinned(b));

        QVERIFY(store.minimizeView(b).value("ok").toBool());
        const QVariantMap entry = store.wells().first().toMap().value("entries").toList().first().toMap();
        const QVariantMap resource = entry.value("resource").toMap();
        QCOMPARE(resource.value("kind").toString(), QStringLiteral("document"));
        QCOMPARE(resource.value("id").toString(), documentResource("notes.md").value("id").toString());
        QCOMPARE(entry.value("minimizedViewIds").toStringList(), QStringList{b});

        QVERIFY(store.setViewPinned(b, false).value("ok").toBool());
        QVERIFY(!store.isViewPinned(b));
        QVERIFY(store.isViewMinimized(b));
        QVERIFY(!store.isViewPinned(QStringLiteral("no-such-view")));
    }

    void theWellSurvivesARestart()
    {
        QSettings settings;
        settings.remove(QStringLiteral("workspace/collections/wells-restart/docking/snapshot"));
        QString b;
        {
            WorkspaceStore store;
            store.setCollection(QStringLiteral("wells-restart"));
            store.setSurfaceSize(store.primarySurfaceId(), 1200, 800);
            store.openView(QStringLiteral("archivist.editor"), documentResource("a.cpp"), "a.cpp");
            b = store.openView(QStringLiteral("archivist.editor"), documentResource("b.cpp"), "b.cpp");
            QVERIFY(store.setViewPinned(b, true).value("ok").toBool());
            QVERIFY(store.minimizeView(b).value("ok").toBool());
            QVERIFY(store.setWellPosition(QStringLiteral("well.main"), 0.2, 0.75));
            store.saveNow();
        }

        WorkspaceStore store;
        store.setCollection(QStringLiteral("wells-restart"));
        store.setSurfaceSize(store.primarySurfaceId(), 1200, 800);
        QVERIFY(store.isViewMinimized(b));
        const QVariantMap well = store.wells().first().toMap();
        QCOMPARE(well.value("x").toDouble(), 0.2);
        QCOMPARE(well.value("y").toDouble(), 0.75);
        const QVariantMap entry = well.value("entries").toList().first().toMap();
        QVERIFY(entry.value("pinned").toBool());
        QCOMPARE(entry.value("state").toString(), QStringLiteral("minimized"));
        QCOMPARE(store.restoreMinimizedView(b).value("how").toString(), QStringLiteral("original"));
    }

private:
    QTemporaryDir m_settingsDir;
};

QTEST_GUILESS_MAIN(WorkspaceStoreTest)
#include "workspace_store_test.moc"

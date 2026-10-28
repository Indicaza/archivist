#include "App/Domains/Documents/document_store.h"
#include "App/Domains/Documents/unsaved_recovery.h"
#include "App/Domains/Workspace/keyed_list_model.h"
#include "App/Domains/Workspace/workspace_store.h"

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

        // Moving the only tab of the right panel into the left one folds the
        // right panel away in the preview. Its row stays drawable (it holds
        // the pointer grab) at its old place, marked as vacating.
        QVERIFY(store.beginDrag(QStringLiteral("view"), b));
        store.updateDragOverTabs(surface, first, -1);
        QVERIFY(store.dropAllowed());
        QVERIFY(anchors->row(second).value("isVacating").toBool());
        QVERIFY(!anchors->row(second).value("isHidden").toBool());
        QCOMPARE(anchors->row(second).value("rectX").toDouble(), secondX);

        store.cancelDrag();
        QVERIFY(!anchors->row(second).value("isVacating").toBool());

        // Same for dragging a whole group onto another.
        QVERIFY(store.beginDrag(QStringLiteral("anchor"), second));
        store.updateDragOverTabs(surface, first, -1);
        QVERIFY(store.dropAllowed());
        QVERIFY(anchors->row(second).value("isVacating").toBool());
        QVERIFY(store.finishDrag());
        QCOMPARE(anchors->rowCount(), 1);
        QCOMPARE(store.view(b).value("anchorId").toString(), first);
    }

    void tearOffOpensAFloatingWindowAndClosingBringsTabsBack()
    {
        WorkspaceStore store;
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
        QVERIFY(!store.canUndoLayout());
        QVERIFY(!store.view(a).isEmpty());

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
        QVERIFY(!store.canUndoAgentChange());

        QVERIFY(store.view(b).isEmpty());
        QVERIFY(!store.view(a).isEmpty());
        QVERIFY(!store.view(term).isEmpty());
        QVERIFY(!store.view(mine).isEmpty());
        QCOMPARE(documents.snapshot(docA).value("text").toString(), QStringLiteral(">alpha"));
        QVERIFY(documents.isDirty(docA));

        // The panels the person had are back as they were.
        QCOMPARE(agentAreas(store), anchorsBefore);

        QCOMPARE(store.runAgentRequest(agentRestore("last_change"), true).value("code").toString(), QStringLiteral("NOTHING_TO_RESTORE"));
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
        QVERIFY(!store.canUndoAgentChange());
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
        QVERIFY(!store.canUndoAgentChange());
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

private:
    QTemporaryDir m_settingsDir;
};

QTEST_GUILESS_MAIN(WorkspaceStoreTest)
#include "workspace_store_test.moc"

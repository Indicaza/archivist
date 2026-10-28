#include "App/Domains/Documents/document_store.h"
#include "App/Domains/Documents/unsaved_recovery.h"
#include "App/Domains/Workspace/keyed_list_model.h"
#include "App/Domains/Workspace/workspace_store.h"

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

private:
    QTemporaryDir m_settingsDir;
};

QTEST_GUILESS_MAIN(WorkspaceStoreTest)
#include "workspace_store_test.moc"

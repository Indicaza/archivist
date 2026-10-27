#include "Core/Domains/Documents/document_buffers.h"

#include <QtTest>

using namespace Documents;

namespace
{
DocumentSeed seed(const QString &text = QStringLiteral("hello world"))
{
    DocumentSeed value;
    value.documentId = QStringLiteral("col:lib-a:file-1");
    value.collectionId = QStringLiteral("col");
    value.libraryId = QStringLiteral("lib-a");
    value.fileId = QStringLiteral("file-1");
    value.path = QStringLiteral("src/main.cpp");
    value.language = QStringLiteral("cpp");
    value.diskText = text;
    value.diskModifiedAt = QStringLiteral("t0");
    return value;
}

const QString kId = QStringLiteral("col:lib-a:file-1");
} // namespace

class CoreDocumentBuffersTest final : public QObject
{
    Q_OBJECT

private slots:
    void viewsShareOneBuffer()
    {
        DocumentBuffers buffers;
        buffers.open(seed(), QStringLiteral("view-1"));
        QVERIFY(buffers.applyEdit(kId, 1, {{5, 6, QStringLiteral(" there")}}).ok);

        // A second view (a duplicate, or the same view rebuilt in another
        // window) gets the unsaved text, not the disk text.
        const Buffer &second = buffers.open(seed(QStringLiteral("stale disk text")), QStringLiteral("view-2"));
        QCOMPARE(second.text, QStringLiteral("hello there"));
        QCOMPARE(second.holders.size(), 2);
        QVERIFY(buffers.isDirty(kId));
    }

    void editsAreVersioned()
    {
        DocumentBuffers buffers;
        buffers.open(seed(QStringLiteral("abc")), QStringLiteral("v"));
        EditResult result = buffers.applyEdit(kId, 1, {{3, 0, QStringLiteral("d")}});
        QVERIFY(result.ok);
        QCOMPARE(result.version, 2);

        // A view still on version 1 is told to resync.
        result = buffers.applyEdit(kId, 1, {{0, 0, QStringLiteral("x")}});
        QVERIFY(!result.ok);
        QVERIFY(result.conflict);
        QCOMPARE(buffers.find(kId)->text, QStringLiteral("abcd"));

        // Several changes against one base apply as Monaco reports them.
        result = buffers.applyEdit(kId, 2, {{0, 1, QStringLiteral("A")}, {3, 1, QStringLiteral("D")}});
        QVERIFY(result.ok);
        QCOMPARE(buffers.find(kId)->text, QStringLiteral("AbcD"));

        // Overlapping or out-of-range changes are refused.
        QVERIFY(buffers.applyEdit(kId, 3, {{0, 3, QStringLiteral("")}, {1, 1, QStringLiteral("")}}).conflict);
        QVERIFY(buffers.applyEdit(kId, 3, {{2, 10, QStringLiteral("")}}).conflict);
        QCOMPARE(buffers.find(kId)->text, QStringLiteral("AbcD"));

        QVERIFY(buffers.replaceText(kId, 3, QStringLiteral("whole")).ok);
        QCOMPARE(buffers.find(kId)->version, 4);
    }

    void unicodeOffsetsAreUtf16()
    {
        DocumentBuffers buffers;
        buffers.open(seed(QStringLiteral("a😀b")), QStringLiteral("v"));
        // The emoji is two UTF-16 units, as in JavaScript.
        QVERIFY(buffers.applyEdit(kId, 1, {{3, 1, QStringLiteral("c")}}).ok);
        QCOMPARE(buffers.find(kId)->text, QStringLiteral("a😀c"));
    }

    void dirtyBuffersOutliveTheirViews()
    {
        DocumentBuffers buffers;
        buffers.open(seed(), QStringLiteral("v1"));
        QVERIFY(buffers.applyEdit(kId, 1, {{0, 0, QStringLiteral(">")}}).ok);

        // Closing the only view must not lose the edit.
        QVERIFY(!buffers.release(kId, QStringLiteral("v1")));
        QVERIFY(buffers.contains(kId));
        QCOMPARE(buffers.dirtyDocuments(), QStringList{kId});

        // Saving lets it go.
        QVERIFY(buffers.markSaved(kId, 2, QStringLiteral(">hello world"), QStringLiteral("t1")));
        QVERIFY(!buffers.contains(kId));
    }

    void cleanBuffersAreDroppedWithTheirLastView()
    {
        DocumentBuffers buffers;
        buffers.open(seed(), QStringLiteral("v1"));
        buffers.open(seed(), QStringLiteral("v2"));
        QVERIFY(!buffers.release(kId, QStringLiteral("v1")));
        QVERIFY(buffers.release(kId, QStringLiteral("v2")));
        QVERIFY(!buffers.contains(kId));
    }

    void typingBackToTheSavedTextIsClean()
    {
        DocumentBuffers buffers;
        buffers.open(seed(QStringLiteral("x")), QStringLiteral("v"));
        QVERIFY(buffers.applyEdit(kId, 1, {{1, 0, QStringLiteral("y")}}).ok);
        QVERIFY(buffers.isDirty(kId));
        QVERIFY(buffers.applyEdit(kId, 2, {{1, 1, QString()}}).ok);
        QVERIFY(!buffers.isDirty(kId));
    }

    void saveRacesWithTyping()
    {
        DocumentBuffers buffers;
        buffers.open(seed(QStringLiteral("a")), QStringLiteral("v"));
        QVERIFY(buffers.applyEdit(kId, 1, {{1, 0, QStringLiteral("b")}}).ok); // v2 "ab"
        // Save of v2 starts, user keeps typing.
        QVERIFY(buffers.applyEdit(kId, 2, {{2, 0, QStringLiteral("c")}}).ok); // v3 "abc"
        QVERIFY(buffers.markSaved(kId, 2, QStringLiteral("ab"), QStringLiteral("t1")));
        QVERIFY(buffers.isDirty(kId)); // "c" is still unsaved
        QVERIFY(!buffers.markSaved(kId, 9, QStringLiteral("?"), QStringLiteral("t2"))); // future version refused
    }

    void diskChangesRespectUnsavedWork()
    {
        DocumentBuffers buffers;
        buffers.open(seed(QStringLiteral("one")), QStringLiteral("v"));
        QVERIFY(buffers.reloadFromDisk(kId, QStringLiteral("two"), QStringLiteral("t1")));
        QCOMPARE(buffers.find(kId)->text, QStringLiteral("two"));
        QVERIFY(!buffers.isDirty(kId));

        QVERIFY(buffers.applyEdit(kId, buffers.find(kId)->version, {{0, 0, QStringLiteral("my ")}}).ok);
        QVERIFY(!buffers.reloadFromDisk(kId, QStringLiteral("three"), QStringLiteral("t2")));
        QCOMPARE(buffers.find(kId)->text, QStringLiteral("my two"));
        QVERIFY(buffers.find(kId)->diskChanged);

        QVERIFY(buffers.discard(kId));
        QCOMPARE(buffers.find(kId)->text, QStringLiteral("two"));
        QVERIFY(!buffers.isDirty(kId));
    }

    void dirtyDocumentsHeldOnlyByAWindow()
    {
        DocumentBuffers buffers;
        buffers.open(seed(), QStringLiteral("floating-view"));
        buffers.applyEdit(kId, 1, {{0, 0, QStringLiteral("!")}});
        QCOMPARE(buffers.dirtyDocumentsHeldOnlyBy({QStringLiteral("floating-view")}), QStringList{kId});
        buffers.open(seed(), QStringLiteral("main-view"));
        QVERIFY(buffers.dirtyDocumentsHeldOnlyBy({QStringLiteral("floating-view")}).isEmpty());
    }

    void unsavedBuffersSurviveACrash()
    {
        DocumentBuffers before;
        before.open(seed(), QStringLiteral("v"));
        before.applyEdit(kId, 1, {{11, 0, QStringLiteral("!")}});
        const QJsonArray unsaved = before.encodeUnsaved();
        QCOMPARE(unsaved.size(), 1);

        DocumentBuffers after;
        QCOMPARE(after.restoreUnsaved(unsaved), 1);
        QVERIFY(after.isDirty(kId));
        QCOMPARE(after.find(kId)->text, QStringLiteral("hello world!"));
        QCOMPARE(after.find(kId)->savedText, QStringLiteral("hello world"));
        QCOMPARE(after.find(kId)->libraryId, QStringLiteral("lib-a"));

        // Reopening the file after recovery keeps the recovered text.
        QCOMPARE(after.open(seed(), QStringLiteral("v2")).text, QStringLiteral("hello world!"));
    }
};

QTEST_GUILESS_MAIN(CoreDocumentBuffersTest)
#include "core_document_buffers_test.moc"

#include "App/Domains/Chat/chat_draft_store.h"
#include "Core/Domains/Chats/chat_drafts.h"

#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using Chats::DraftBook;

class CoreChatDraftsTest : public QObject
{
    Q_OBJECT

private slots:
    void keepsOneDraftPerConversation()
    {
        DraftBook book;
        QVERIFY(book.set("a", "hello a"));
        QVERIFY(book.set("b", "hello b"));
        QCOMPARE(book.text("a"), QString("hello a"));
        QCOMPARE(book.text("b"), QString("hello b"));
        QCOMPARE(book.text("c"), QString());
    }

    void blankTextClearsTheDraft()
    {
        DraftBook book;
        book.set("a", "something");
        QVERIFY(book.set("a", "   \n"));
        QVERIFY(!book.has("a"));
        QVERIFY(!book.set("a", ""));
    }

    void keepsWhitespaceInsideRealText()
    {
        DraftBook book;
        book.set("a", "  line one\n\nline two  ");
        QCOMPARE(book.text("a"), QString("  line one\n\nline two  "));
    }

    void unchangedTextIsNotAChange()
    {
        DraftBook book;
        QVERIFY(book.set("a", "same"));
        QVERIFY(!book.set("a", "same"));
    }

    void ignoresEmptyKeys()
    {
        DraftBook book;
        QVERIFY(!book.set("", "x"));
        QVERIFY(!book.set("  ", "x"));
        QVERIFY(book.drafts().isEmpty());
    }

    void dropsTheOldestBeyondCapacity()
    {
        DraftBook book(3);
        book.set("a", "1", 1);
        book.set("b", "2", 2);
        book.set("c", "3", 3);
        book.set("a", "1 again", 4); // a is now the newest
        book.set("d", "4", 5);
        QCOMPARE(book.drafts().size(), 3);
        QVERIFY(!book.has("b"));
        QVERIFY(book.has("a"));
        QCOMPARE(book.drafts().first().key, QString("d"));
    }

    void capsVeryLongText()
    {
        DraftBook book;
        book.set("a", QString(DraftBook::MaxLength + 50, QChar('x')));
        QCOMPARE(book.text("a").size(), DraftBook::MaxLength);
    }

    void renameMovesTheDraft()
    {
        DraftBook book;
        book.set("none", "for a new chat");
        QVERIFY(book.rename("none", "chat-1"));
        QVERIFY(!book.has("none"));
        QCOMPARE(book.text("chat-1"), QString("for a new chat"));
    }

    void renameKeepsAnExistingTargetDraft()
    {
        DraftBook book;
        book.set("chat-1", "already here");
        book.set("none", "other");
        QVERIFY(book.rename("none", "chat-1"));
        QCOMPARE(book.text("chat-1"), QString("already here"));
        QVERIFY(!book.has("none"));
    }

    void roundTripsAndSkipsBadEntries()
    {
        DraftBook book;
        book.set("a", "one", 10);
        book.set("b", "two", 20);
        QVariantList saved = book.toVariant();
        saved.append(QVariantMap{{"key", ""}, {"text", "no key"}});
        saved.append(QVariantMap{{"key", "c"}, {"text", "   "}});
        saved.append(QVariantMap{{"key", "a"}, {"text", "duplicate"}});
        saved.append(QString("not a map"));

        const DraftBook loaded = DraftBook::fromVariant(saved);
        QCOMPARE(loaded.drafts().size(), 2);
        QCOMPARE(loaded.text("a"), QString("one"));
        QCOMPARE(loaded.text("b"), QString("two"));
        QCOMPARE(loaded.drafts().first().key, QString("b"));
        QCOMPARE(loaded.drafts().first().updatedAt, qint64(20));
    }

    void storePersistsAcrossRestarts()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("drafts.ini");
        {
            QSettings settings(path, QSettings::IniFormat);
            ChatDraftStore store(&settings);
            store.setDraft("chat-1", "half a thought");
            store.setDraft("", "typed with no chat");
        } // destruction flushes the pending save
        {
            QSettings settings(path, QSettings::IniFormat);
            ChatDraftStore store(&settings);
            QCOMPARE(store.draft("chat-1"), QString("half a thought"));
            QCOMPARE(store.draft(""), QString("typed with no chat"));
            QCOMPARE(store.draft(ChatDraftStore::NoChatKey), QString("typed with no chat"));
            QCOMPARE(store.count(), 2);
        }
    }

    void storeSignalsAndClears()
    {
        QTemporaryDir dir;
        QSettings settings(dir.filePath("drafts.ini"), QSettings::IniFormat);
        ChatDraftStore store(&settings);
        QSignalSpy changed(&store, &ChatDraftStore::draftChanged);
        QSignalSpy counted(&store, &ChatDraftStore::draftsChanged);

        store.setDraft("chat-1", "a");
        store.setDraft("chat-1", "ab");
        store.setDraft("chat-1", "ab"); // no change, no signal
        QCOMPARE(changed.count(), 2);
        QCOMPARE(counted.count(), 1);

        store.clearDraft("chat-1");
        QVERIFY(!store.hasDraft("chat-1"));
        QCOMPARE(counted.count(), 2);
        store.flush();
        QVERIFY(!settings.contains("chat/drafts"));
    }

    void storeIgnoresCorruptSettings()
    {
        QTemporaryDir dir;
        QSettings settings(dir.filePath("drafts.ini"), QSettings::IniFormat);
        settings.setValue("chat/drafts", "{not json");
        ChatDraftStore store(&settings);
        QCOMPARE(store.count(), 0);
        store.setDraft("chat-1", "fine");
        QCOMPARE(store.draft("chat-1"), QString("fine"));
    }

    void storeAdoptsTheNoChatDraft()
    {
        QTemporaryDir dir;
        QSettings settings(dir.filePath("drafts.ini"), QSettings::IniFormat);
        ChatDraftStore store(&settings);
        store.setDraft("", "start of a chat");
        store.adoptDraft("", "chat-9");
        QCOMPARE(store.draft("chat-9"), QString("start of a chat"));
        QVERIFY(!store.hasDraft(""));
    }
};

QTEST_GUILESS_MAIN(CoreChatDraftsTest)
#include "core_chat_drafts_test.moc"

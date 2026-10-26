#include "Core/Domains/Agents/agent_defaults.h"
#include "Core/Domains/Agents/agent_repository.h"
#include "Core/Domains/Chats/chat_repository.h"
#include "Core/Domains/Libraries/library_files.h"
#include "Core/Domains/Libraries/library_file_repository.h"
#include "Core/Domains/Libraries/library_repository.h"
#include "Core/Support/core_values.h"
#include "Core/core_error.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace CoreValues;

class CoreChatsTest final : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    CoreDatabase m_database;
    QString m_libraryId;
    QString m_root;

    template<typename Work>
    QString errorFrom(Work &&work)
    {
        try {
            work();
        } catch (const CoreError &error) {
            return error.message();
        }

        return {};
    }

    QString newChat(const QString &title = {})
    {
        QVariantMap input{{QStringLiteral("libraryId"), m_libraryId}};

        if (!title.isEmpty()) {
            input.insert(QStringLiteral("title"), title);
        }

        return ChatRepository::createChat(m_database, input).value(QStringLiteral("id")).toString();
    }

    void message(const QString &chatId, const QString &content)
    {
        ChatRepository::createMessage(m_database, chatId, {{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), content}});
    }

private slots:
    void init()
    {
        QVERIFY(m_directory.isValid());
        m_root = m_directory.filePath(newId());
        QDir().mkpath(m_root);
        m_database.open(m_directory.filePath(newId() + QStringLiteral(".db")));
        m_database.runMigrations();
        m_libraryId = LibraryRepository::createLibrary(m_database, {{QStringLiteral("rootPath"), m_root}})
                          .value(QStringLiteral("library")).toMap().value(QStringLiteral("id")).toString();
    }

    void cleanup()
    {
        m_database.close();
    }

    void createsListsAndSelects()
    {
        const QVariantMap chat = ChatRepository::createChat(
            m_database, {{QStringLiteral("libraryId"), m_libraryId}, {QStringLiteral("title"), QStringLiteral("  Plans  ")}}
        );
        QCOMPARE(chat.value(QStringLiteral("title")).toString(), QStringLiteral("Plans"));
        QCOMPARE(chat.value(QStringLiteral("agentId")).toString(), AgentDefaults::defaultAgentId);
        QCOMPARE(chat.value(QStringLiteral("agentIds")).toList(), (QVariantList{AgentDefaults::defaultAgentId}));
        QVERIFY(chat.value(QStringLiteral("archivedAt")).isNull());
        QCOMPARE(ChatRepository::selectedChatId(m_database).toString(), chat.value(QStringLiteral("id")).toString());

        const QString untitled = newChat();
        QCOMPARE(ChatRepository::chatById(m_database, untitled)->value(QStringLiteral("title")).toString(), QStringLiteral("New Chat"));
        QCOMPARE(ChatRepository::activeChats(m_database).size(), 2);

        QCOMPARE(
            errorFrom([&]() { ChatRepository::createChat(m_database, {{QStringLiteral("libraryId"), m_libraryId}, {QStringLiteral("title"), QStringLiteral("  ")}}); }),
            QStringLiteral("Invalid Chat data.")
        );
        QCOMPARE(
            errorFrom([&]() { ChatRepository::createChat(m_database, {{QStringLiteral("libraryId"), newId()}}); }),
            QStringLiteral("Library not found.")
        );
        QCOMPARE(
            errorFrom([&]() { ChatRepository::selectChat(m_database, newId()); }),
            QStringLiteral("Chat not found.")
        );
        QVERIFY(ChatRepository::selectChat(m_database, {}).isNull());
    }

    void archivesRestoresAndDeletes()
    {
        const QString older = newChat(QStringLiteral("Older"));
        QTest::qWait(5);
        const QString newer = newChat(QStringLiteral("Newer"));

        const QVariantMap archived = ChatRepository::archiveChat(m_database, newer);
        QVERIFY(!archived.value(QStringLiteral("chat")).toMap().value(QStringLiteral("archivedAt")).isNull());
        QCOMPARE(archived.value(QStringLiteral("selectedChatId")).toString(), older);
        QCOMPARE(ChatRepository::archivedChats(m_database).size(), 1);
        QCOMPARE(errorFrom([&]() { ChatRepository::archiveChat(m_database, newer); }), QStringLiteral("Chat is already archived."));
        QCOMPARE(
            errorFrom([&]() { (void)ChatRepository::messages(m_database, newer); }),
            QStringLiteral("This chat is archived. Restore it before continuing the conversation.")
        );

        QVERIFY(ChatRepository::restoreChat(m_database, newer).value(QStringLiteral("archivedAt")).isNull());
        QCOMPARE(errorFrom([&]() { ChatRepository::restoreChat(m_database, newer); }), QStringLiteral("Chat is already active."));

        // The foreign key clears the selection on delete, so Node also returned null here.
        QVERIFY(ChatRepository::deleteChat(m_database, older).isNull());
        QVERIFY(ChatRepository::deleteChat(m_database, newer).isNull());
        QCOMPARE(errorFrom([&]() { ChatRepository::deleteChat(m_database, newer); }), QStringLiteral("Chat not found."));
    }

    void updatesTitleAndAgents()
    {
        const QString chatId = newChat();
        const QString agentId = AgentRepository::createAgent(m_database, {{QStringLiteral("name"), QStringLiteral("Helper")}})
                                    .value(QStringLiteral("id")).toString();

        QCOMPARE(errorFrom([&]() { ChatRepository::updateChat(m_database, chatId, {}); }), QStringLiteral("Invalid Chat data."));

        QVariantMap chat = ChatRepository::updateChat(m_database, chatId, {{QStringLiteral("agentId"), agentId}});
        QCOMPARE(chat.value(QStringLiteral("agentId")).toString(), agentId);
        QCOMPARE(chat.value(QStringLiteral("agentIds")).toList(), (QVariantList{AgentDefaults::defaultAgentId, agentId}));

        QCOMPARE(
            errorFrom([&]() { ChatRepository::detachAgent(m_database, chatId, agentId); }),
            QStringLiteral("Select another active Agent before detaching this one.")
        );
        chat = ChatRepository::detachAgent(m_database, chatId, AgentDefaults::defaultAgentId);
        QCOMPARE(chat.value(QStringLiteral("agentIds")).toList(), (QVariantList{agentId}));
        QCOMPARE(
            errorFrom([&]() { ChatRepository::detachAgent(m_database, chatId, AgentDefaults::defaultAgentId); }),
            QStringLiteral("This Agent is not attached to the Chat.")
        );

        chat = ChatRepository::attachAgent(m_database, chatId, AgentDefaults::defaultAgentId);
        QCOMPARE(chat.value(QStringLiteral("agentIds")).toList(), (QVariantList{agentId, AgentDefaults::defaultAgentId}));

        chat = ChatRepository::updateChat(m_database, chatId, {{QStringLiteral("title"), QStringLiteral(" Renamed ")}});
        QCOMPARE(chat.value(QStringLiteral("title")).toString(), QStringLiteral("Renamed"));
        QCOMPARE(chat.value(QStringLiteral("agentId")).toString(), agentId);
    }

    void pagesMessages()
    {
        const QString chatId = newChat();

        for (int index = 1; index <= 5; ++index) {
            message(chatId, QStringLiteral(" m%1 ").arg(index));
        }

        QCOMPARE(ChatRepository::messages(m_database, chatId).size(), 5);
        QCOMPARE(ChatRepository::messages(m_database, chatId).first().toMap().value(QStringLiteral("content")).toString(), QStringLiteral("m1"));

        const QVariantMap first = ChatRepository::messagePage(m_database, chatId, 2);
        const QVariantList firstMessages = first.value(QStringLiteral("messages")).toList();
        QCOMPARE(firstMessages.size(), 2);
        QCOMPARE(firstMessages.at(0).toMap().value(QStringLiteral("content")).toString(), QStringLiteral("m4"));
        QCOMPARE(firstMessages.at(1).toMap().value(QStringLiteral("content")).toString(), QStringLiteral("m5"));
        QVariantMap pagination = first.value(QStringLiteral("pagination")).toMap();
        QVERIFY(pagination.value(QStringLiteral("hasMore")).toBool());
        QCOMPARE(pagination.value(QStringLiteral("nextBeforeMessageId")), firstMessages.at(0).toMap().value(QStringLiteral("id")));

        const QVariantMap last = ChatRepository::messagePage(
            m_database, chatId, 3, pagination.value(QStringLiteral("nextBeforeMessageId")).toString()
        );
        QCOMPARE(last.value(QStringLiteral("messages")).toList().size(), 3);
        pagination = last.value(QStringLiteral("pagination")).toMap();
        QVERIFY(!pagination.value(QStringLiteral("hasMore")).toBool());
        QVERIFY(pagination.value(QStringLiteral("nextBeforeMessageId")).isNull());

        QCOMPARE(errorFrom([&]() { (void)ChatRepository::messagePage(m_database, chatId, 251); }), QStringLiteral("Invalid message page query."));
        QCOMPARE(
            errorFrom([&]() { (void)ChatRepository::messagePage(m_database, chatId, 2, newId()); }),
            QStringLiteral("Message cursor not found for this Chat.")
        );
        QCOMPARE(
            errorFrom([&]() { ChatRepository::createMessage(m_database, chatId, {{QStringLiteral("role"), QStringLiteral("tool")}, {QStringLiteral("content"), QStringLiteral("x")}}); }),
            QStringLiteral("Invalid message data.")
        );
    }

    void managesAttachments()
    {
        const QString chatId = newChat();

        for (int index = 0; index < 9; ++index) {
            QFile file(QDir(m_root).filePath(QStringLiteral("f%1.md").arg(index)));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("x");
        }

        (void)LibraryFiles::scan(m_database, m_libraryId);
        auto fileId = [&](int index) {
            return LibraryFileRepository::fileByRelativePath(m_database, m_libraryId, QStringLiteral("f%1.md").arg(index))
                ->value(QStringLiteral("id")).toString();
        };

        const QVariantMap attachment = ChatRepository::createAttachment(m_database, chatId, m_libraryId, fileId(0));
        QCOMPARE(attachment.value(QStringLiteral("fileName")).toString(), QStringLiteral("f0.md"));
        QCOMPARE(attachment.value(QStringLiteral("fileStatus")).toString(), QStringLiteral("available"));
        QCOMPARE(
            ChatRepository::createAttachment(m_database, chatId, m_libraryId, fileId(0)).value(QStringLiteral("id")),
            attachment.value(QStringLiteral("id"))
        );

        for (int index = 1; index < 8; ++index) {
            (void)ChatRepository::createAttachment(m_database, chatId, m_libraryId, fileId(index));
        }

        QCOMPARE(ChatRepository::attachments(m_database, chatId).size(), 8);
        QCOMPARE(
            errorFrom([&]() { ChatRepository::createAttachment(m_database, chatId, m_libraryId, fileId(8)); }),
            QStringLiteral("A Chat can have at most 8 attached files.")
        );

        ChatRepository::deleteAttachment(m_database, chatId, attachment.value(QStringLiteral("id")).toString());
        QCOMPARE(ChatRepository::attachments(m_database, chatId).size(), 7);
        QCOMPARE(
            errorFrom([&]() { ChatRepository::deleteAttachment(m_database, chatId, attachment.value(QStringLiteral("id")).toString()); }),
            QStringLiteral("Chat attachment not found.")
        );
    }

    void readsContextRuns()
    {
        const QString chatId = newChat();
        message(chatId, QStringLiteral("hi"));
        const QVariantList messages = ChatRepository::messages(m_database, chatId);
        const QString userId = messages.first().toMap().value(QStringLiteral("id")).toString();
        const QString assistantId = ChatRepository::createMessage(
            m_database, chatId, {{QStringLiteral("role"), QStringLiteral("assistant")}, {QStringLiteral("content"), QStringLiteral("hello")}}
        ).value(QStringLiteral("id")).toString();

        QCOMPARE(
            errorFrom([&]() { (void)ChatRepository::contextRun(m_database, chatId, assistantId); }),
            QStringLiteral("No stored context is available for this response.")
        );

        m_database
            .prepare(QStringLiteral(
                "INSERT INTO context_runs (id, chat_id, user_message_id, assistant_message_id, provider, model, agent_id, snapshot_json) "
                "VALUES (?, ?, ?, ?, 'openai', 'gpt-test', ?, ?)"
            ))
            .bind(1, newId())
            .bind(2, chatId)
            .bind(3, userId)
            .bind(4, assistantId)
            .bind(5, AgentDefaults::defaultAgentId)
            .bind(6, QStringLiteral(R"({"compiler":{"id":"recent-history","version":1},"manifest":{"tokens":3},"warnings":[],"sources":[{"kind":"message"}]})"))
            .run();

        const QVariantMap run = ChatRepository::contextRun(m_database, chatId, assistantId);
        QCOMPARE(run.value(QStringLiteral("model")).toString(), QStringLiteral("gpt-test"));
        QCOMPARE(run.value(QStringLiteral("compiler")).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("recent-history"));
        QCOMPARE(run.value(QStringLiteral("sources")).toList().size(), 1);
        QCOMPARE(run.value(QStringLiteral("userMessageId")).toString(), userId);
    }
};

QTEST_GUILESS_MAIN(CoreChatsTest)
#include "core_chats_test.moc"

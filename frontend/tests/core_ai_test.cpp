#include "Core/Domains/AI/ai_run_repository.h"
#include "Core/Domains/AI/ai_tools.h"
#include "Core/Domains/AI/context_compilers.h"
#include "Core/Domains/AI/edit_proposals.h"
#include "Core/Domains/AI/edit_transactions.h"
#include "Core/Domains/Libraries/library_files.h"
#include "Core/Domains/Agents/agent_defaults.h"
#include "Core/Domains/Chats/chat_repository.h"
#include "Core/Domains/Libraries/library_repository.h"
#include "Core/Support/core_values.h"
#include "Core/Support/json_values.h"
#include "Core/core_error.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace CoreValues;
using ContextCompilers::SourceMessage;

class CoreAITest final : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    CoreDatabase m_database;
    QString m_libraryId;
    QString m_chatId;
    QString m_root;
    QStringList m_events;

    void writeFile(const QString &relativePath, const QByteArray &content)
    {
        QFile file(QDir(m_root).filePath(relativePath));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(content);
    }

    QByteArray readFile(const QString &relativePath)
    {
        QFile file(QDir(m_root).filePath(relativePath));
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray("<missing>");
    }

    AITools::Context toolContext(const QString &runId)
    {
        AITools::Context context;
        context.database = &m_database;
        context.runId = runId;
        context.chatId = m_chatId;
        context.libraryId = m_libraryId;
        context.emitEvent = [this](const QString &type, const QVariantMap &) { m_events.append(type); };
        return context;
    }

    QVariantMap tool(const QString &runId, const QString &toolId, const QVariantMap &input)
    {
        return AITools::execute(toolContext(runId), toolId, input);
    }

    QString toolErrorCode(const QString &runId, const QString &toolId, const QVariantMap &input)
    {
        try {
            tool(runId, toolId, input);
        } catch (const AITools::ToolError &error) {
            return error.code();
        }

        return {};
    }

    AIEditTransactions::EventSink sink()
    {
        return [this](const QString &, const QString &type, const QVariantMap &) { m_events.append(type); };
    }

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

    QString message(const QString &role, const QString &content, const QString &status = QStringLiteral("complete"))
    {
        return ChatRepository::createMessage(
                   m_database,
                   m_chatId,
                   {{QStringLiteral("role"), role}, {QStringLiteral("content"), content}, {QStringLiteral("status"), status}}
        )
            .value(QStringLiteral("id"))
            .toString();
    }

    QVariantMap newRun(const QString &assistantContent = QStringLiteral("…"), const QString &status = QStringLiteral("streaming"))
    {
        const QString userId = message(QStringLiteral("user"), QStringLiteral("Hello"));
        const QString assistantId = message(QStringLiteral("assistant"), assistantContent, status);
        return AIRunRepository::createRun(
            m_database,
            {
                {QStringLiteral("chatId"), m_chatId},
                {QStringLiteral("libraryId"), m_libraryId},
                {QStringLiteral("agentId"), AgentDefaults::defaultAgentId},
                {QStringLiteral("userMessageId"), userId},
                {QStringLiteral("assistantMessageId"), assistantId},
                {QStringLiteral("contextCompiler"), QVariantMap{{QStringLiteral("id"), QStringLiteral("recent-history")}, {QStringLiteral("version"), 1}}},
                {QStringLiteral("provider"), QStringLiteral("openai")},
                {QStringLiteral("model"), QStringLiteral("gpt-5-mini")},
            }
        );
    }

    static QList<SourceMessage> conversation(int count, int length)
    {
        QList<SourceMessage> messages;

        for (int index = 0; index < count; ++index) {
            messages.append({
                QStringLiteral("m%1").arg(index),
                index % 2 == 0 ? QStringLiteral("user") : QStringLiteral("assistant"),
                QString(length, QLatin1Char('a' + index % 26)),
                QStringLiteral("2026-01-01T00:00:%1.000Z").arg(index, 2, 10, QLatin1Char('0')),
            });
        }

        return messages;
    }

    static QVariantMap budget(int total, int reserve)
    {
        return {{QStringLiteral("totalTokens"), total}, {QStringLiteral("responseTokenReserve"), reserve}};
    }

private slots:
    void init()
    {
        QVERIFY(m_directory.isValid());
        m_root = m_directory.filePath(newId());
        QDir().mkpath(m_root);
        const QString root = m_root;
        m_events.clear();
        m_database.open(m_directory.filePath(newId() + QStringLiteral(".db")));
        m_database.runMigrations();
        m_libraryId = LibraryRepository::createLibrary(m_database, {{QStringLiteral("rootPath"), root}})
                          .value(QStringLiteral("library")).toMap().value(QStringLiteral("id")).toString();
        m_chatId = ChatRepository::createChat(m_database, {{QStringLiteral("libraryId"), m_libraryId}})
                       .value(QStringLiteral("id")).toString();
    }

    void cleanup()
    {
        m_database.close();
    }

    void runsTrackEventsAndPhases()
    {
        const QVariantMap run = newRun();
        const QString runId = run.value(QStringLiteral("id")).toString();
        QCOMPARE(run.value(QStringLiteral("status")).toString(), QStringLiteral("running"));
        QCOMPARE(run.value(QStringLiteral("phase")).toString(), QStringLiteral("run.started"));
        QCOMPARE(run.value(QStringLiteral("contextCompiler")).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("recent-history"));
        QCOMPARE(AIRunRepository::activeRunByChat(m_database, m_chatId)->value(QStringLiteral("id")).toString(), runId);

        QCOMPARE(
            errorFrom([&]() { newRun(); }),
            QStringLiteral("This Chat already has an active AI Run.")
        );

        const QVariantMap first = AIRunRepository::appendEvent(m_database, runId, QStringLiteral("run.started"), {{QStringLiteral("chatId"), m_chatId}});
        QCOMPARE(first.value(QStringLiteral("sequence")).toInt(), 1);
        QCOMPARE(first.value(QStringLiteral("payload")).toMap().value(QStringLiteral("chatId")).toString(), m_chatId);
        AIRunRepository::appendEvent(m_database, runId, QStringLiteral("model.started"));
        AIRunRepository::appendEvent(m_database, runId, QStringLiteral("model.delta"), {{QStringLiteral("delta"), QStringLiteral("Hi")}});
        QCOMPARE(AIRunRepository::requireRun(m_database, runId).value(QStringLiteral("phase")).toString(), QStringLiteral("model.started"));
        QCOMPARE(AIRunRepository::events(m_database, runId).size(), 3);
        const QVariantList after = AIRunRepository::events(m_database, runId, 2);
        QCOMPARE(after.size(), 1);
        QCOMPARE(after.first().toMap().value(QStringLiteral("eventType")).toString(), QStringLiteral("model.delta"));

        const QVariantMap completed = AIRunRepository::completeRun(m_database, runId, QStringLiteral("Hi"), QVariant());
        QCOMPARE(completed.value(QStringLiteral("status")).toString(), QStringLiteral("completed"));
        QVERIFY(AIRunRepository::isTerminal(QStringLiteral("completed")));
        QVERIFY(!AIRunRepository::activeRunByChat(m_database, m_chatId));
        QCOMPARE(AIRunRepository::runsByChat(m_database, m_chatId).size(), 1);
        QCOMPARE(errorFrom([&]() { AIRunRepository::requireRun(m_database, newId()); }), QStringLiteral("AI Run not found."));
    }

    void appendEventJoinsAnOpenTransaction()
    {
        const QString runId = newRun().value(QStringLiteral("id")).toString();
        m_database.transaction([&]() {
            AIRunRepository::appendEvent(m_database, runId, QStringLiteral("context.started"));
            return QVariant();
        });
        QCOMPARE(AIRunRepository::events(m_database, runId).size(), 1);
    }

    void recoversInterruptedRuns()
    {
        const QString runId = newRun(QStringLiteral("Saved text")).value(QStringLiteral("id")).toString();
        AIRunRepository::appendEvent(m_database, runId, QStringLiteral("model.delta"), {{QStringLiteral("delta"), QStringLiteral("Part ")}});
        AIRunRepository::appendEvent(m_database, runId, QStringLiteral("model.delta"), {{QStringLiteral("delta"), QStringLiteral("two")}});

        QCOMPARE(AIRunRepository::recoverInterruptedRuns(m_database), 1);
        const QVariantMap run = AIRunRepository::requireRun(m_database, runId);
        QCOMPARE(run.value(QStringLiteral("status")).toString(), QStringLiteral("failed"));
        QCOMPARE(run.value(QStringLiteral("errorCode")).toString(), QStringLiteral("process_interrupted"));
        QCOMPARE(run.value(QStringLiteral("finalResponse")).toString(), QStringLiteral("Part two"));
        const QVariantMap assistant = *ChatRepository::messageById(m_database, run.value(QStringLiteral("assistantMessageId")).toString());
        QCOMPARE(assistant.value(QStringLiteral("content")).toString(), QStringLiteral("Part two"));
        QCOMPARE(assistant.value(QStringLiteral("status")).toString(), QStringLiteral("failed"));
        QCOMPARE(AIRunRepository::events(m_database, runId).last().toMap().value(QStringLiteral("eventType")).toString(), QStringLiteral("run.failed"));
        QCOMPARE(AIRunRepository::recoverInterruptedRuns(m_database), 0);

        const QString completeId = newRun(QStringLiteral("Done"), QStringLiteral("complete")).value(QStringLiteral("id")).toString();
        QCOMPARE(AIRunRepository::recoverInterruptedRuns(m_database), 1);
        QCOMPARE(AIRunRepository::requireRun(m_database, completeId).value(QStringLiteral("status")).toString(), QStringLiteral("completed"));
    }

    void estimatesTokensAndDescribesCompilers()
    {
        QCOMPARE(ContextCompilers::estimateTokens({}), 0);
        QCOMPARE(ContextCompilers::estimateTokens(QStringLiteral("a")), 1);
        QCOMPARE(ContextCompilers::estimateTokens(QStringLiteral("abcde")), 2);
        QCOMPARE(ContextCompilers::descriptor(QStringLiteral("auto-balanced"), 2).value(QStringLiteral("name")).toString(), QStringLiteral("Auto Balanced"));
        QCOMPARE(
            errorFrom([&]() { (void)ContextCompilers::descriptor(QStringLiteral("nope"), 1); }),
            QStringLiteral("Context Compiler \"nope:v1\" is not registered.")
        );

        QVariantMap config = budget(8000, 1000);
        config.insert({
            {QStringLiteral("recentHistoryTokens"), 2000},
            {QStringLiteral("retrievalTokens"), 1000},
            {QStringLiteral("retrievalResultLimit"), 8},
            {QStringLiteral("minimumSearchTermLength"), 3},
            {QStringLiteral("searchScope"), QStringLiteral("current-chat")},
        });
        const QVariantMap parsed = ContextCompilers::parseConfig(QStringLiteral("auto-balanced"), 2, config);
        QCOMPARE(parsed.value(QStringLiteral("maxEvidenceItems")).toInt(), 6);
        QCOMPARE(parsed.value(QStringLiteral("minimumTermMatches")).toInt(), 1);
    }

    void recentHistoryKeepsNewestWithinBudget()
    {
        // 10 messages of 400 chars = 100 tokens each; 350 tokens available.
        const QList<SourceMessage> messages = conversation(10, 400);
        const auto compiled = ContextCompilers::compile(
            m_database, QStringLiteral("recent-history"), 1, m_chatId, QStringLiteral("m9"), messages, budget(1350, 1000)
        );
        QCOMPARE(compiled.providerMessages.size(), 3);
        QCOMPARE(compiled.manifest.value(QStringLiteral("includedMessageIds")).toList(), (QVariantList{QStringLiteral("m7"), QStringLiteral("m8"), QStringLiteral("m9")}));
        QCOMPARE(compiled.manifest.value(QStringLiteral("omittedMessageCount")).toInt(), 7);
        QCOMPARE(compiled.manifest.value(QStringLiteral("estimatedInputTokens")).toInt(), 300);
        QVERIFY(compiled.warnings.isEmpty());

        const auto over = ContextCompilers::compile(
            m_database, QStringLiteral("recent-history"), 1, m_chatId, QStringLiteral("m9"), conversation(10, 4000), budget(1350, 1000)
        );
        QCOMPARE(over.providerMessages.size(), 1);
        QCOMPARE(over.warnings.size(), 1);

        QVERIFY(!errorFrom([&]() {
            (void)ContextCompilers::compile(m_database, QStringLiteral("recent-history"), 1, m_chatId, QStringLiteral("missing"), messages, budget(1350, 1000));
        }).isEmpty());
    }

    void otherCompilersStayWithinBudget()
    {
        const QList<SourceMessage> messages = conversation(12, 400);
        QVariantMap keyword = budget(1600, 1000);
        keyword.insert({
            {QStringLiteral("recentMessageFloor"), 2},
            {QStringLiteral("candidateLimit"), 50},
            {QStringLiteral("keywordWeight"), 70},
            {QStringLiteral("recencyWeight"), 30},
        });
        const QList<QPair<QString, QVariantMap>> cases{
            {QStringLiteral("contiguous-history"), budget(1600, 1000)},
            {QStringLiteral("turn-pairs"), budget(1600, 1000)},
            {QStringLiteral("keyword-recency"), keyword},
        };

        for (const auto &[id, config] : cases) {
            const auto compiled = ContextCompilers::compile(m_database, id, 1, m_chatId, QStringLiteral("m11"), messages, config);
            QVERIFY2(!compiled.providerMessages.isEmpty(), qPrintable(id));
            QVERIFY2(compiled.manifest.value(QStringLiteral("estimatedInputTokens")).toInt() <= 600, qPrintable(id));
            QCOMPARE(compiled.manifest.value(QStringLiteral("includedMessageIds")).toList().last().toString(), QStringLiteral("m11"));
        }
    }

    void searchesChatMessagesAndAddsEvidence()
    {
        message(QStringLiteral("user"), QStringLiteral("The lighthouse keeper paints the stairs blue."));
        message(QStringLiteral("assistant"), QStringLiteral("Noted, blue stairs."));

        const QVariantMap found = ContextCompilers::searchChatMessages(m_database, QStringLiteral("lighthouse"), m_chatId, 5);
        QCOMPARE(found.value(QStringLiteral("candidates")).toList().size(), 1);
        QCOMPARE(ContextCompilers::searchChatMessages(m_database, QStringLiteral("lighthouse"), {}, 5).value(QStringLiteral("candidates")).toList().size(), 1);
        QCOMPARE(
            ContextCompilers::searchChatMessages(m_database, QStringLiteral("   "), m_chatId, 5).value(QStringLiteral("warnings")).toList(),
            (QVariantList{QStringLiteral("The query did not contain any searchable terms.")})
        );

        QList<SourceMessage> messages = conversation(8, 400);
        messages.prepend({newId(), QStringLiteral("user"), QStringLiteral("The lighthouse keeper paints the stairs blue."), QStringLiteral("2025-12-31T00:00:00.000Z")});
        messages.append({QStringLiteral("current"), QStringLiteral("user"), QStringLiteral("What colour did the lighthouse keeper paint?"), QStringLiteral("2099-01-01T00:00:00.000Z")});
        QVariantMap config = budget(8000, 1000);
        config.insert({
            {QStringLiteral("recentHistoryTokens"), 300},
            {QStringLiteral("retrievalTokens"), 1000},
            {QStringLiteral("retrievalResultLimit"), 8},
            {QStringLiteral("minimumSearchTermLength"), 3},
            {QStringLiteral("searchScope"), QStringLiteral("all-chats")},
        });

        for (int version : {1, 2}) {
            const QVariantMap parsed = ContextCompilers::parseConfig(QStringLiteral("auto-balanced"), version, config);
            const auto compiled = ContextCompilers::compile(m_database, QStringLiteral("auto-balanced"), version, m_chatId, QStringLiteral("current"), messages, parsed);
            QString all;

            for (const auto &provider : compiled.providerMessages) {
                all += provider.content;
            }

            // v1 orders retrieved rows by their real created_at, so the current message must be newest.
            QVERIFY2(all.contains(QStringLiteral("stairs blue")), qPrintable(QString::number(version)));
            QCOMPARE(compiled.providerMessages.last().content, QStringLiteral("What colour did the lighthouse keeper paint?"));
        }
    }

    void toolsRecordExecutionsAndEvents()
    {
        writeFile(QStringLiteral("alpha.txt"), "line one\nline two\n");
        LibraryFiles::scan(m_database, m_libraryId);
        const QString runId = newRun().value(QStringLiteral("id")).toString();

        const QVariantMap listed = tool(runId, QStringLiteral("list_directory"), {{QStringLiteral("path"), QString()}, {QStringLiteral("limit"), 20}});
        QCOMPARE(listed.value(QStringLiteral("status")).toString(), QStringLiteral("completed"));
        QCOMPARE(m_events, (QStringList{QStringLiteral("tool.requested"), QStringLiteral("tool.started"), QStringLiteral("tool.completed")}));

        const QVariantMap found = tool(runId, QStringLiteral("search_filenames"), {{QStringLiteral("query"), QStringLiteral("alpha")}, {QStringLiteral("limit"), 5}});
        QVERIFY(JsonValues::toJsonString(found.value(QStringLiteral("output"))).contains(QStringLiteral("alpha.txt")));

        QCOMPARE(toolErrorCode(runId, QStringLiteral("list_directory"), {{QStringLiteral("limit"), 0}}), QStringLiteral("invalid_input"));
        QCOMPARE(m_events.last(), QStringLiteral("tool.failed"));
        QCOMPARE(toolErrorCode(runId, QStringLiteral("delete_everything"), {}), QStringLiteral("tool_not_found"));
        QCOMPARE(AITools::executionsByRun(m_database, runId).size(), 3);

        QVERIFY(AITools::providerTools({}).size() == 6);
        AITools::Availability withEdits;
        withEdits.mutationProposals = true;
        QCOMPARE(AITools::providerTools(withEdits).size(), 11);
    }

    void approvesAndUndoesProposedEdits()
    {
        writeFile(QStringLiteral("alpha.txt"), "original\n");
        LibraryFiles::scan(m_database, m_libraryId);
        const QString runId = newRun().value(QStringLiteral("id")).toString();

        const QVariantMap created = tool(runId, QStringLiteral("create_file"), {{QStringLiteral("path"), QStringLiteral("beta.txt")}, {QStringLiteral("content"), QStringLiteral("new file\n")}});
        QCOMPARE(m_events.last(), QStringLiteral("tool.completed"));
        QVERIFY(m_events.contains(QStringLiteral("edit.proposed")));
        tool(runId, QStringLiteral("patch_file"), {{QStringLiteral("path"), QStringLiteral("alpha.txt")}, {QStringLiteral("content"), QStringLiteral("changed\n")}});
        const QString proposalId = created.value(QStringLiteral("output")).toMap().value(QStringLiteral("proposalId")).toString();
        QCOMPARE(AIEditProposals::requireProposal(m_database, proposalId).value(QStringLiteral("operations")).toList().size(), 2);
        QCOMPARE(readFile(QStringLiteral("beta.txt")), QByteArray("<missing>"));

        AIRunRepository::completeRun(m_database, runId, QStringLiteral("Done"), {});
        const QVariantMap approved = AIEditTransactions::approve(m_database, proposalId, std::nullopt, sink());
        QCOMPARE(approved.value(QStringLiteral("proposal")).toMap().value(QStringLiteral("status")).toString(), QStringLiteral("completed"));
        QCOMPARE(readFile(QStringLiteral("alpha.txt")), QByteArray("changed\n"));
        QCOMPARE(readFile(QStringLiteral("beta.txt")), QByteArray("new file\n"));
        QVERIFY(m_events.contains(QStringLiteral("edit.completed")));

        const QVariantMap undone = AIEditTransactions::undo(m_database, proposalId, sink());
        QCOMPARE(undone.value(QStringLiteral("proposal")).toMap().value(QStringLiteral("status")).toString(), QStringLiteral("undone"));
        QCOMPARE(readFile(QStringLiteral("alpha.txt")), QByteArray("original\n"));
        QCOMPARE(readFile(QStringLiteral("beta.txt")), QByteArray("<missing>"));
        QCOMPARE(
            errorFrom([&]() { AIEditTransactions::undo(m_database, proposalId, sink()); }),
            QStringLiteral("Only completed AI edit transactions can be undone.")
        );
    }

    void staleProposalsDoNotWrite()
    {
        writeFile(QStringLiteral("alpha.txt"), "original\n");
        LibraryFiles::scan(m_database, m_libraryId);
        const QString runId = newRun().value(QStringLiteral("id")).toString();
        const QVariantMap patched = tool(runId, QStringLiteral("patch_file"), {{QStringLiteral("path"), QStringLiteral("alpha.txt")}, {QStringLiteral("content"), QStringLiteral("changed\n")}});
        const QString proposalId = patched.value(QStringLiteral("output")).toMap().value(QStringLiteral("proposalId")).toString();
        AIRunRepository::completeRun(m_database, runId, QStringLiteral("Done"), {});

        writeFile(QStringLiteral("alpha.txt"), "edited by hand\n");
        QCOMPARE(
            errorFrom([&]() { AIEditTransactions::approve(m_database, proposalId, std::nullopt, sink()); }),
            QStringLiteral("The AI edit proposal is stale and was not executed.")
        );
        QCOMPARE(AIEditProposals::requireProposal(m_database, proposalId).value(QStringLiteral("status")).toString(), QStringLiteral("stale"));
        QCOMPARE(readFile(QStringLiteral("alpha.txt")), QByteArray("edited by hand\n"));
        QVERIFY(m_events.contains(QStringLiteral("edit.stale")));
    }

    void rejectsProposals()
    {
        const QString runId = newRun().value(QStringLiteral("id")).toString();
        const QVariantMap created = tool(runId, QStringLiteral("create_file"), {{QStringLiteral("path"), QStringLiteral("gamma.txt")}, {QStringLiteral("content"), QStringLiteral("x")}});
        const QString proposalId = created.value(QStringLiteral("output")).toMap().value(QStringLiteral("proposalId")).toString();
        const QVariantMap rejected = AIEditTransactions::reject(m_database, proposalId, sink());
        QCOMPARE(rejected.value(QStringLiteral("status")).toString(), QStringLiteral("rejected"));
        QCOMPARE(
            errorFrom([&]() { AIEditTransactions::reject(m_database, proposalId, sink()); }),
            QStringLiteral("This AI edit proposal has already been reviewed.")
        );
        QCOMPARE(readFile(QStringLiteral("gamma.txt")), QByteArray("<missing>"));
    }
};

QTEST_GUILESS_MAIN(CoreAITest)
#include "core_ai_test.moc"

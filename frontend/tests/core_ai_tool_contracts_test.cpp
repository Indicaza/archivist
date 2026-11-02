// Pins the AI tool contracts that providers and the runtime depend on: tool
// ids, names, descriptions, JSON schemas, discovery and ordering for every
// Availability combination, permission levels, execution modes, lifecycle
// event payloads and invalid-input error shapes.
//
// The snapshot in tests/fixtures/ai_tool_contracts.json was recorded before
// the AI tools were split into separate modules. A refactor must leave it
// unchanged. To record a deliberate contract change, run this test with
// ARCHIVIST_UPDATE_TOOL_CONTRACTS=1 and review the fixture diff.
#include "Core/Database/core_database.h"
#include "Core/Domains/AI/ai_run_repository.h"
#include "Core/Domains/AI/ai_tools.h"
#include "Core/Domains/Agents/agent_defaults.h"
#include "Core/Domains/Chats/chat_repository.h"
#include "Core/Domains/Libraries/library_files.h"
#include "Core/Domains/Libraries/library_repository.h"
#include "Core/Support/core_values.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

using namespace CoreValues;

class CoreAIToolContractsTest final : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    CoreDatabase m_database;
    QString m_libraryId;
    QString m_chatId;
    QString m_runId;
    QString m_root;

    static QVariantMap availabilityMap(const AITools::Availability &availability)
    {
        return {
            {QStringLiteral("discovery"), availability.discovery},
            {QStringLiteral("fullFileRead"), availability.fullFileRead},
            {QStringLiteral("verification"), availability.verification},
            {QStringLiteral("mutationProposals"), availability.mutationProposals},
            {QStringLiteral("workspace"), availability.workspace},
        };
    }

    static QVariantList names(const QVariantList &tools)
    {
        QVariantList result;
        for (const QVariant &tool : tools) {
            result.append(tool.toMap().value(QStringLiteral("name")));
        }
        return result;
    }

    // Lifecycle payloads without the values that differ on every run.
    static QVariantMap stable(QVariantMap payload)
    {
        payload.remove(QStringLiteral("executionId"));
        payload.remove(QStringLiteral("durationMs"));
        return payload;
    }

    // Ids, timestamps and durations differ on every run; everything else in
    // a tool's summaries, outputs and stored record is part of its contract.
    static QVariant withoutVolatile(const QVariant &value)
    {
        if (value.typeId() == QMetaType::QVariantMap) {
            QVariantMap result;
            const QVariantMap map = value.toMap();
            for (auto iterator = map.cbegin(); iterator != map.cend(); ++iterator) {
                const QString &key = iterator.key();
                if (key == QStringLiteral("id") || key.endsWith(QStringLiteral("Id")) || key.endsWith(QStringLiteral("At"))
                    || key == QStringLiteral("durationMs")) {
                    continue;
                }
                result.insert(key, withoutVolatile(iterator.value()));
            }
            return result;
        }
        if (value.typeId() == QMetaType::QVariantList) {
            QVariantList result;
            for (const QVariant &item : value.toList()) {
                result.append(withoutVolatile(item));
            }
            return result;
        }
        return value;
    }

    void writeFile(const QString &relativePath, const QByteArray &content)
    {
        const QString path = QDir(m_root).filePath(relativePath);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(content);
    }

    // A successful run: the lifecycle events (with the input and output
    // summaries), the output and the stored execution record. The workspace
    // answers with the request it received, so the request each workspace
    // tool sends is pinned as well.
    QVariantMap completedExecution(const QString &toolId, const QVariantMap &input)
    {
        QVariantList events;
        AITools::Context context;
        context.database = &m_database;
        context.runId = m_runId;
        context.chatId = m_chatId;
        context.libraryId = m_libraryId;
        context.emitEvent = [&events](const QString &type, const QVariantMap &payload) {
            events.append(QVariantMap{{QStringLiteral("type"), type}, {QStringLiteral("payload"), payload}});
        };
        context.workspace = [](const QVariantMap &request) {
            return QVariantMap{
                {QStringLiteral("ok"), true},
                {QStringLiteral("status"), QStringLiteral("applied")},
                {QStringLiteral("notes"), QStringList{QStringLiteral("note")}},
                {QStringLiteral("areas"), QVariantList{QVariantMap(), QVariantMap()}},
                {QStringLiteral("unresolved"), request.value(QStringLiteral("unresolved"))},
                {QStringLiteral("request"), request},
            };
        };

        QVariantMap execution;
        try {
            execution = AITools::execute(context, toolId, input);
        } catch (const AITools::ToolError &error) {
            return {{QStringLiteral("unexpectedError"), error.code() + QStringLiteral(": ") + error.message()}};
        }

        return withoutVolatile(QVariantMap{
            {QStringLiteral("events"), events},
            {QStringLiteral("execution"), execution},
        }).toMap();
    }

    QVariantMap lifecycles()
    {
        writeFile(QStringLiteral("alpha.txt"), "line one\nline two\nline three\n");
        writeFile(QStringLiteral("beta.txt"), "beta line\n");
        writeFile(QStringLiteral("gamma.txt"), "gamma line\n");
        writeFile(QStringLiteral("sub/other.txt"), "other line\n");
        LibraryFiles::scan(m_database, m_libraryId);

        const QVariantMap alphaRange{{QStringLiteral("fileId"), QStringLiteral("alpha.txt")}, {QStringLiteral("startLine"), 1}, {QStringLiteral("endLine"), 2}};
        const QVariantMap fileItem{{QStringLiteral("kind"), QStringLiteral("file")}, {QStringLiteral("reference"), QStringLiteral("alpha.txt")}, {QStringLiteral("library"), QString()}};
        const QList<QPair<QString, QVariantMap>> calls{
            {QStringLiteral("list_directory"), {{QStringLiteral("path"), QString()}, {QStringLiteral("limit"), 20}}},
            {QStringLiteral("search_filenames"), {{QStringLiteral("query"), QStringLiteral("a")}, {QStringLiteral("limit"), 5}}},
            {QStringLiteral("search_library"), {{QStringLiteral("query"), QStringLiteral("line")}, {QStringLiteral("limit"), 5}}},
            {QStringLiteral("read_file"), {{QStringLiteral("fileId"), QStringLiteral("alpha.txt")}}},
            {QStringLiteral("read_file_range"), alphaRange},
            {QStringLiteral("read_file_ranges"), {{QStringLiteral("ranges"), QVariantList{alphaRange}}}},
            {QStringLiteral("create_file"), {{QStringLiteral("path"), QStringLiteral("new.txt")}, {QStringLiteral("content"), QStringLiteral("x")}}},
            {QStringLiteral("patch_file"), {{QStringLiteral("path"), QStringLiteral("alpha.txt")}, {QStringLiteral("content"), QStringLiteral("changed\n")}}},
            {QStringLiteral("create_directory"), {{QStringLiteral("path"), QStringLiteral("docs")}}},
            {QStringLiteral("rename_file"), {{QStringLiteral("sourcePath"), QStringLiteral("beta.txt")}, {QStringLiteral("destinationPath"), QStringLiteral("beta2.txt")}}},
            {QStringLiteral("move_file"), {{QStringLiteral("sourcePath"), QStringLiteral("gamma.txt")}, {QStringLiteral("destinationPath"), QStringLiteral("sub/gamma.txt")}}},
            {QStringLiteral("workspace_view"), {}},
            {QStringLiteral("workspace_show"), {{QStringLiteral("items"), QVariantList{fileItem}}, {QStringLiteral("where"), QStringLiteral("right")}, {QStringLiteral("purpose"), QString()}, {QStringLiteral("focus"), false}}},
            {QStringLiteral("workspace_arrange"), {{QStringLiteral("steps"), QVariantList{
                QVariantMap{{QStringLiteral("action"), QStringLiteral("focus")}, {QStringLiteral("area"), QStringLiteral("the code")}, {QStringLiteral("change"), QString()}, {QStringLiteral("to"), QString()}, {QStringLiteral("name"), QString()}},
                QVariantMap{{QStringLiteral("action"), QStringLiteral("resize")}, {QStringLiteral("area"), QStringLiteral("the chat")}, {QStringLiteral("change"), QStringLiteral("larger")}, {QStringLiteral("to"), QString()}, {QStringLiteral("name"), QString()}},
            }}, {QStringLiteral("save_as"), QString()}}},
            {QStringLiteral("workspace_compose"), {
                {QStringLiteral("mode"), QStringLiteral("patch")}, {QStringLiteral("template"), QStringLiteral("none")}, {QStringLiteral("arrangement"), QStringLiteral("auto")},
                {QStringLiteral("rows"), 0}, {QStringLiteral("columns"), 0}, {QStringLiteral("focus"), QString()}, {QStringLiteral("save_as"), QString()},
                {QStringLiteral("slots"), QVariantList{QVariantMap{
                    {QStringLiteral("name"), QStringLiteral("Code")}, {QStringLiteral("holds"), QVariantList()}, {QStringLiteral("weight"), 0}, {QStringLiteral("position"), QString()},
                    {QStringLiteral("items"), QVariantList{fileItem, QVariantMap{{QStringLiteral("kind"), QStringLiteral("file")}, {QStringLiteral("reference"), QString()}, {QStringLiteral("library"), QString()}}}},
                }}},
            }},
            {QStringLiteral("workspace_restore"), {{QStringLiteral("scope"), QStringLiteral("last_change")}, {QStringLiteral("layout"), QString()}}},
            {QStringLiteral("workspace_layout"), {{QStringLiteral("action"), QStringLiteral("list")}, {QStringLiteral("name"), QString()}, {QStringLiteral("new_name"), QString()}, {QStringLiteral("replace"), false}}},
            {QStringLiteral("workspace_preference"), {{QStringLiteral("action"), QStringLiteral("set")}, {QStringLiteral("preference"), QStringLiteral("chat_location")}, {QStringLiteral("value"), QStringLiteral("bottom")}, {QStringLiteral("area"), QString()}, {QStringLiteral("duration"), QStringLiteral("always")}}},
        };

        QVariantMap result;
        for (const auto &call : calls) {
            result.insert(call.first, completedExecution(call.first, call.second));
        }
        result.insert(QStringLiteral("storedExecutionCount"), AITools::executionsByRun(m_database, m_runId).size());
        return result;
    }

    QVariantMap failedExecution(const QString &toolId, const QVariant &input)
    {
        QVariantList events;
        AITools::Context context;
        context.database = &m_database;
        context.runId = m_runId;
        context.chatId = m_chatId;
        context.libraryId = m_libraryId;
        context.emitEvent = [&events](const QString &type, const QVariantMap &payload) {
            events.append(QVariantMap{{QStringLiteral("type"), type}, {QStringLiteral("payload"), stable(payload)}});
        };
        // Never reached: every input here fails validation first.
        context.workspace = [](const QVariantMap &) { return QVariantMap{{QStringLiteral("ok"), false}}; };

        QVariantMap error{{QStringLiteral("thrown"), false}};
        try {
            AITools::execute(context, toolId, input);
        } catch (const AITools::ToolError &thrown) {
            error = {
                {QStringLiteral("thrown"), true},
                {QStringLiteral("code"), thrown.code()},
                {QStringLiteral("message"), thrown.message()},
                {QStringLiteral("details"), thrown.details()},
            };
        }

        return {{QStringLiteral("error"), error}, {QStringLiteral("events"), events}};
    }

    QVariantMap contracts()
    {
        QVariantList discovery;
        for (int mask = 0; mask < 32; ++mask) {
            AITools::Availability availability;
            availability.discovery = mask & 1;
            availability.fullFileRead = mask & 2;
            availability.verification = mask & 4;
            availability.mutationProposals = mask & 8;
            availability.workspace = mask & 16;
            discovery.append(QVariantMap{
                {QStringLiteral("availability"), availabilityMap(availability)},
                {QStringLiteral("tools"), names(AITools::providerTools(availability))},
            });
        }

        AITools::Availability everything;
        everything.mutationProposals = true;
        everything.workspace = true;

        QVariantMap kinds;
        QVariantMap failures;
        QStringList ids = AITools::toolIds();
        for (const QString &id : std::as_const(ids)) {
            kinds.insert(id, QVariantMap{
                {QStringLiteral("mutationProposal"), AITools::isMutationProposalTool(id)},
                {QStringLiteral("workspace"), AITools::isWorkspaceTool(id)},
            });
            failures.insert(id, QVariantMap{
                {QStringLiteral("notAnObject"), failedExecution(id, 42)},
                {QStringLiteral("unknownKey"), failedExecution(id, QVariantMap{{QStringLiteral("__unexpected"), 1}})},
            });
        }

        return {
            {QStringLiteral("toolIds"), toVariantList(ids)},
            {QStringLiteral("providerTools"), AITools::providerTools(everything)},
            {QStringLiteral("discovery"), discovery},
            {QStringLiteral("kinds"), kinds},
            {QStringLiteral("unknownTool"), QVariantMap{
                {QStringLiteral("mutationProposal"), AITools::isMutationProposalTool(QStringLiteral("delete_everything"))},
                {QStringLiteral("workspace"), AITools::isWorkspaceTool(QStringLiteral("delete_everything"))},
                {QStringLiteral("execution"), failedExecution(QStringLiteral("delete_everything"), QVariantMap())},
            }},
            {QStringLiteral("invalidInput"), failures},
            {QStringLiteral("lifecycles"), lifecycles()},
        };
    }

private slots:
    void init()
    {
        QVERIFY(m_directory.isValid());
        m_root = m_directory.filePath(newId());
        const QString root = m_root;
        QDir().mkpath(root);
        m_database.open(m_directory.filePath(newId() + QStringLiteral(".db")));
        m_database.runMigrations();
        m_libraryId = LibraryRepository::createLibrary(m_database, {{QStringLiteral("rootPath"), root}})
                          .value(QStringLiteral("library")).toMap().value(QStringLiteral("id")).toString();
        m_chatId = ChatRepository::createChat(m_database, {{QStringLiteral("libraryId"), m_libraryId}})
                       .value(QStringLiteral("id")).toString();
        const auto message = [this](const QString &role, const QString &status) {
            return ChatRepository::createMessage(
                       m_database,
                       m_chatId,
                       {{QStringLiteral("role"), role}, {QStringLiteral("content"), QStringLiteral("…")}, {QStringLiteral("status"), status}}
            )
                .value(QStringLiteral("id"))
                .toString();
        };
        const QString userId = message(QStringLiteral("user"), QStringLiteral("complete"));
        const QString assistantId = message(QStringLiteral("assistant"), QStringLiteral("streaming"));
        m_runId = AIRunRepository::createRun(
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
        )
                      .value(QStringLiteral("id"))
                      .toString();
    }

    void cleanup()
    {
        m_database.close();
    }

    void toolContractsMatchSnapshot()
    {
        const QByteArray actual = QJsonDocument::fromVariant(contracts()).toJson(QJsonDocument::Indented);
        const QString fixture = QStringLiteral(ARCHIVIST_TEST_FIXTURES "/ai_tool_contracts.json");

        if (qEnvironmentVariableIsSet("ARCHIVIST_UPDATE_TOOL_CONTRACTS")) {
            QFile file(fixture);
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            file.write(actual);
            QSKIP("Recorded tool contracts; review the fixture diff.");
        }

        QFile file(fixture);
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(fixture));
        const QByteArray expected = file.readAll();

        if (actual != expected) {
            const QString written = m_directory.filePath(QStringLiteral("ai_tool_contracts.actual.json"));
            QFile out(written);
            if (out.open(QIODevice::WriteOnly)) {
                out.write(actual);
            }
            // Keep a copy outside the temporary directory for diffing.
            QFile::remove(QDir::temp().filePath(QStringLiteral("ai_tool_contracts.actual.json")));
            QFile::copy(written, QDir::temp().filePath(QStringLiteral("ai_tool_contracts.actual.json")));
            QFAIL(qPrintable(QStringLiteral("AI tool contracts changed. Compare %1 with %2.")
                                 .arg(fixture, QDir::temp().filePath(QStringLiteral("ai_tool_contracts.actual.json")))));
        }
    }

    void everyListedToolIsDiscoverableAndOrdered()
    {
        AITools::Availability everything;
        everything.mutationProposals = true;
        everything.workspace = true;
        const QVariantList offered = names(AITools::providerTools(everything));
        QCOMPARE(offered, toVariantList(AITools::toolIds()));

        QStringList sorted = AITools::toolIds();
        std::sort(sorted.begin(), sorted.end());
        QCOMPARE(AITools::toolIds(), sorted);
        QCOMPARE(AITools::toolIds().size(), 18);
    }
};

QTEST_GUILESS_MAIN(CoreAIToolContractsTest)
#include "core_ai_tool_contracts_test.moc"

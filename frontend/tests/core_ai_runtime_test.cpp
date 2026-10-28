#include "Core/Database/core_database.h"
#include "Core/Domains/AI/ai_runtime.h"
#include "Core/Domains/AI/ai_settings.h"
#include "Core/Domains/Agents/agent_repository.h"
#include "Core/Domains/Chats/chat_repository.h"
#include "Core/Domains/Libraries/library_files.h"
#include "Core/Domains/Libraries/library_repository.h"
#include "Core/Domains/Models/model_catalog_cache.h"
#include "Core/Domains/Workspace/workspace_gateway.h"
#include "Core/Support/core_values.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

#include <mutex>

using namespace CoreValues;

// A tiny stand-in for the OpenAI Responses API. Each POST /v1/responses gets
// the next queued SSE body; GET /v1/models lists gpt-5-mini.
class FakeOpenAI final : public QObject
{
public:
    QList<QByteArray> responses;
    QList<QJsonObject> requests;
    QStringList paths;
    QList<QByteArray> authorizations;
    QByteArray modelsReply = R"({"object":"list","data":[{"id":"gpt-5-mini","created":1,"owned_by":"openai"}]})";
    QTcpServer server;

    FakeOpenAI()
    {
        connect(&server, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket *socket = server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { handle(socket); });
            }
        });
        server.listen(QHostAddress::LocalHost);
    }

    QString baseUrl() const { return QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort()); }

    static QByteArray sse(const QList<QJsonObject> &events)
    {
        QByteArray body;

        for (const QJsonObject &event : events) {
            body += "event: " + event.value(QStringLiteral("type")).toString().toUtf8() + "\ndata: "
                + QJsonDocument(event).toJson(QJsonDocument::Compact) + "\n\n";
        }

        return body;
    }

private:
    QHash<QTcpSocket *, QByteArray> m_pending;

    void handle(QTcpSocket *socket)
    {
        QByteArray &data = m_pending[socket];
        data += socket->readAll();
        const qsizetype headerEnd = data.indexOf("\r\n\r\n");

        if (headerEnd < 0) {
            return;
        }

        const QByteArray head = data.left(headerEnd);
        qsizetype length = 0;

        QByteArray authorization;

        for (const QByteArray &line : head.split('\n')) {
            if (line.toLower().startsWith("content-length:")) {
                length = line.mid(15).trimmed().toLongLong();
            }

            if (line.toLower().startsWith("authorization:")) {
                authorization = line.mid(14).trimmed();
            }
        }

        if (data.size() < headerEnd + 4 + length) {
            return;
        }

        const QByteArray body = data.mid(headerEnd + 4, length);
        data.clear();
        QByteArray reply;
        QByteArray contentType = "text/event-stream";

        paths.append(QString::fromUtf8(head.left(head.indexOf('\r')).split(' ').value(1)));
        authorizations.append(authorization);

        if (head.startsWith("GET")) {
            contentType = "application/json";
            reply = modelsReply;
        } else {
            requests.append(QJsonDocument::fromJson(body).object());
            reply = responses.isEmpty() ? QByteArray() : responses.takeFirst();
        }

        socket->write("HTTP/1.1 200 OK\r\nContent-Type: " + contentType + "\r\nContent-Length: "
                      + QByteArray::number(reply.size()) + "\r\nConnection: close\r\n\r\n" + reply);
        socket->disconnectFromHost();
    }
};

class CoreAIRuntimeTest final : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    QString m_databasePath;
    QString m_chatId;

    static QJsonObject event(const QString &type, QJsonObject fields = {})
    {
        fields.insert(QStringLiteral("type"), type);
        return fields;
    }

    static QJsonObject created(const QString &responseId)
    {
        return event(QStringLiteral("response.created"), {{QStringLiteral("response"), QJsonObject{{QStringLiteral("id"), responseId}}}});
    }

    static QJsonObject completed(const QString &responseId)
    {
        return event(QStringLiteral("response.completed"), {{QStringLiteral("response"), QJsonObject{{QStringLiteral("id"), responseId}}}});
    }

    static QJsonObject delta(const QString &text)
    {
        return event(QStringLiteral("response.output_text.delta"), {{QStringLiteral("delta"), text}});
    }

    CoreResult call(const std::function<void(AIRuntime::Callback)> &start)
    {
        CoreResult result;
        bool done = false;
        start([&](const CoreResult &value) {
            result = value;
            done = true;
        });
        [&]() { QTRY_VERIFY_WITH_TIMEOUT(done, 10000); }();
        return result;
    }


    // A stand-in workspace on the other side of the gateway: two built-in
    // layouts, and every request recorded.
    struct FakeWorkspace
    {
        std::mutex mutex;
        QList<QVariantMap> requests;
        QString current = QStringLiteral("Development");
        QVariantMap failure; // when set, every change fails with it

        void install()
        {
            Workspace::Gateway::install([this](const QVariantMap &request) -> QVariantMap {
                std::lock_guard<std::mutex> lock(mutex);
                requests.append(request);
                const QString action = request.value(QStringLiteral("action")).toString();
                if (action == QStringLiteral("describe")) {
                    return {
                        {QStringLiteral("ok"), true},
                        {QStringLiteral("areas"), QVariantList{QVariantMap{
                            {QStringLiteral("area"), QStringLiteral("Editor")},
                            {QStringLiteral("position"), QStringLiteral("left")},
                            {QStringLiteral("sizePercent"), 100},
                            {QStringLiteral("shows"), QVariantList{QVariantMap{{QStringLiteral("title"), QStringLiteral("notes.txt")}, {QStringLiteral("path"), QStringLiteral("notes.txt")}}}},
                        }}},
                        {QStringLiteral("layouts"), QVariantList{
                            QVariantMap{{QStringLiteral("name"), QStringLiteral("Development")}, {QStringLiteral("kind"), QStringLiteral("built in")}},
                            QVariantMap{{QStringLiteral("name"), QStringLiteral("Research")}, {QStringLiteral("kind"), QStringLiteral("built in")}},
                        }},
                        {QStringLiteral("currentLayout"), current},
                    };
                }
                if (!failure.isEmpty()) {
                    return failure;
                }
                if (action == QStringLiteral("layout") && request.value(QStringLiteral("op")).toString() == QStringLiteral("apply")) {
                    current = request.value(QStringLiteral("name")).toString();
                    return {{QStringLiteral("ok"), true}, {QStringLiteral("status"), QStringLiteral("applied")}, {QStringLiteral("currentLayout"), current}};
                }
                return {{QStringLiteral("ok"), true}, {QStringLiteral("status"), QStringLiteral("applied")}};
            });
        }

        ~FakeWorkspace() { Workspace::Gateway::install({}); }

        QVariantMap last()
        {
            std::lock_guard<std::mutex> lock(mutex);
            return requests.isEmpty() ? QVariantMap() : requests.last();
        }

        QStringList actions()
        {
            std::lock_guard<std::mutex> lock(mutex);
            QStringList out;
            for (const QVariantMap &request : std::as_const(requests)) {
                out.append(request.value(QStringLiteral("action")).toString());
            }
            return out;
        }
    };

    static QByteArray toolTurn(const QString &responseId, const QString &tool, const QString &arguments)
    {
        return FakeOpenAI::sse({
            created(responseId),
            event(QStringLiteral("response.output_item.done"), {{QStringLiteral("item"), QJsonObject{
                {QStringLiteral("type"), QStringLiteral("function_call")},
                {QStringLiteral("call_id"), QStringLiteral("call_") + responseId},
                {QStringLiteral("name"), tool},
                {QStringLiteral("arguments"), arguments},
            }}}),
            completed(responseId),
        });
    }

    static QByteArray textTurn(const QString &responseId, const QString &text)
    {
        return FakeOpenAI::sse({created(responseId), delta(text), completed(responseId)});
    }

    struct Turn
    {
        QStringList events;
        QHash<QString, QVariantMap> payloads; // last payload of each event type
        QString streamed;
        qint64 elapsedMs = 0;
    };

    Turn runTurn(AIRuntime &runtime, const QString &message)
    {
        Turn turn;
        QElapsedTimer timer;
        timer.start();
        const QMetaObject::Connection connection = connect(&runtime, &AIRuntime::runEvent, this, [&](const QVariantMap &value) {
            const QString type = value.value(QStringLiteral("eventType")).toString();
            turn.events.append(type);
            turn.payloads.insert(type, value.value(QStringLiteral("payload")).toMap());
            if (type == QStringLiteral("model.delta")) {
                turn.streamed += value.value(QStringLiteral("payload")).toMap().value(QStringLiteral("delta")).toString();
            }
        });
        const CoreResult started = call([&](AIRuntime::Callback done) { runtime.startChatRun(m_chatId, message, this, done); });
        if (started.ok) {
            [&]() { QTRY_VERIFY_WITH_TIMEOUT(turn.events.contains(QStringLiteral("run.completed")) || turn.events.contains(QStringLiteral("run.failed")), 15000); }();
        }
        disconnect(connection);
        turn.elapsedMs = timer.elapsed();
        return turn;
    }

    static QStringList toolNames(const QJsonObject &request)
    {
        QStringList names;
        for (const QJsonValue &tool : request.value(QStringLiteral("tools")).toArray()) {
            names.append(tool.toObject().value(QStringLiteral("name")).toString());
        }
        return names;
    }

private slots:
    void initTestCase()
    {
        // Keeps QSettings writes (local server address, non-macOS key store)
        // out of the real user settings.
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("ArchivistTest"));
        QCoreApplication::setApplicationName(QStringLiteral("core-ai-runtime-test"));
    }

    void init()
    {
        QVERIFY(m_directory.isValid());
        const QString root = m_directory.filePath(newId());
        QDir().mkpath(root);
        QFile file(QDir(root).filePath(QStringLiteral("notes.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("The harbour lights are green.\n");
        file.close();

        m_databasePath = m_directory.filePath(newId() + QStringLiteral(".db"));
        QFile::remove(ModelCatalogCache::cachePathForDatabase(m_databasePath));
        CoreDatabase database;
        database.open(m_databasePath);
        database.runMigrations();
        const QString libraryId = LibraryRepository::createLibrary(database, {{QStringLiteral("rootPath"), root}})
                                      .value(QStringLiteral("library")).toMap().value(QStringLiteral("id")).toString();
        LibraryFiles::scan(database, libraryId);
        m_chatId = ChatRepository::createChat(database, {{QStringLiteral("libraryId"), libraryId}}).value(QStringLiteral("id")).toString();
    }

    void streamsAToolLoopIntoTheAssistantMessage()
    {
        FakeOpenAI openai;
        QVERIFY(openai.server.isListening());
        qputenv("OPENAI_API_KEY", "test-key");
        qputenv("OPENAI_BASE_URL", openai.baseUrl().toUtf8());

        openai.responses.append(FakeOpenAI::sse({
            created(QStringLiteral("resp_1")),
            event(QStringLiteral("response.output_item.done"), {{QStringLiteral("item"), QJsonObject{
                {QStringLiteral("type"), QStringLiteral("function_call")},
                {QStringLiteral("call_id"), QStringLiteral("call_1")},
                {QStringLiteral("name"), QStringLiteral("list_directory")},
                {QStringLiteral("arguments"), QStringLiteral(R"({"path":"","limit":10})")},
            }}}),
            completed(QStringLiteral("resp_1")),
        }));
        openai.responses.append(FakeOpenAI::sse({
            created(QStringLiteral("resp_2")),
            delta(QStringLiteral("The lights ")),
            delta(QStringLiteral("are green.")),
            completed(QStringLiteral("resp_2")),
        }));

        AIRuntime runtime;
        QVERIFY(runtime.start(m_databasePath));
        QStringList events;
        QString streamed;
        connect(&runtime, &AIRuntime::runEvent, this, [&](const QVariantMap &value) {
            const QString type = value.value(QStringLiteral("eventType")).toString();
            events.append(type);

            if (type == QStringLiteral("run.failed")) {
                qWarning().noquote() << QJsonDocument::fromVariant(value.value(QStringLiteral("payload"))).toJson(QJsonDocument::Compact);
            }

            if (type == QStringLiteral("model.delta")) {
                streamed += value.value(QStringLiteral("payload")).toMap().value(QStringLiteral("delta")).toString();
            }
        });

        const CoreResult started = call([&](AIRuntime::Callback done) {
            runtime.startChatRun(m_chatId, QStringLiteral("Test the library tools: what colour are the harbour lights?"), this, done);
        });
        QVERIFY2(started.ok, qPrintable(started.errorMessage));
        const QString runId = started.value.toMap().value(QStringLiteral("run")).toMap().value(QStringLiteral("id")).toString();

        QTRY_VERIFY_WITH_TIMEOUT(events.contains(QStringLiteral("run.completed")) || events.contains(QStringLiteral("run.failed")), 15000);
        QVERIFY2(events.contains(QStringLiteral("run.completed")), qPrintable(events.join(QLatin1Char(','))));
        QVERIFY(events.contains(QStringLiteral("tool.completed")));
        QCOMPARE(streamed, QStringLiteral("The lights are green."));

        QCOMPARE(openai.requests.size(), 2);
        QCOMPARE(openai.requests.at(1).value(QStringLiteral("previous_response_id")).toString(), QStringLiteral("resp_1"));
        const QJsonObject output = openai.requests.at(1).value(QStringLiteral("input")).toArray().first().toObject();
        QCOMPARE(output.value(QStringLiteral("type")).toString(), QStringLiteral("function_call_output"));
        QVERIFY(output.value(QStringLiteral("output")).toString().contains(QStringLiteral("notes.txt")));

        const CoreResult snapshot = call([&](AIRuntime::Callback done) { runtime.runWithEvents(runId, 0, this, done); });
        QVERIFY(snapshot.ok);
        const QVariantMap run = snapshot.value.toMap().value(QStringLiteral("run")).toMap();
        QCOMPARE(run.value(QStringLiteral("status")).toString(), QStringLiteral("completed"));
        QCOMPARE(snapshot.value.toMap().value(QStringLiteral("events")).toList().size(), events.size());

        CoreDatabase database;
        database.open(m_databasePath);
        const QVariantList messages = ChatRepository::messages(database, m_chatId);
        QCOMPARE(messages.last().toMap().value(QStringLiteral("content")).toString(), QStringLiteral("The lights are green."));
        QCOMPARE(messages.last().toMap().value(QStringLiteral("status")).toString(), QStringLiteral("complete"));
    }

    // "Switch to Research", "try another", "put it back": each is one tool
    // call, with no Library search, applied right away in the conversation
    // in front.
    void layoutCommandsTakeOneToolCall()
    {
        FakeOpenAI openai;
        qputenv("OPENAI_API_KEY", "test-key");
        qputenv("OPENAI_BASE_URL", openai.baseUrl().toUtf8());
        FakeWorkspace workspace;
        workspace.install();
        AIRuntime runtime;
        QVERIFY(runtime.start(m_databasePath));

        struct Step
        {
            QString message;
            QString tool;
            QString arguments;
            QString reply;
            bool followUp;
        };
        const QList<Step> steps{
            {QStringLiteral("Switch to Research"), QStringLiteral("workspace_layout"), QStringLiteral(R"({"action":"apply","name":"Research","new_name":"","replace":false})"), QStringLiteral("Switched to Research."), false},
            {QStringLiteral("try another"), QStringLiteral("workspace_layout"), QStringLiteral(R"({"action":"apply","name":"Development","new_name":"","replace":false})"), QStringLiteral("Switched to Development."), true},
            {QStringLiteral("put it back"), QStringLiteral("workspace_restore"), QStringLiteral(R"({"scope":"before_task","layout":""})"), QStringLiteral("Put everything back."), false},
        };

        int round = 0;
        for (const Step &step : steps) {
            const qsizetype requestsBefore = openai.requests.size();
            openai.responses.append(toolTurn(QStringLiteral("r%1").arg(++round), step.tool, step.arguments));
            openai.responses.append(textTurn(QStringLiteral("r%1").arg(++round), step.reply));

            const Turn turn = runTurn(runtime, step.message);
            QVERIFY2(turn.events.contains(QStringLiteral("run.completed")), qPrintable(step.message + QStringLiteral(": ") + turn.events.join(QLatin1Char(','))));
            qInfo().noquote() << QStringLiteral("[timing] \"%1\": %2 ms end to end, %3 model calls, %4 tool call, prepare %5 ms")
                                     .arg(step.message)
                                     .arg(turn.elapsedMs)
                                     .arg(openai.requests.size() - requestsBefore)
                                     .arg(turn.payloads.value(QStringLiteral("model.completed")).value(QStringLiteral("toolCallCount")).toInt())
                                     .arg(turn.payloads.value(QStringLiteral("model.started")).value(QStringLiteral("prepareMs")).toDouble());

            // Routed before retrieval: no Library search for a layout command.
            QVERIFY(turn.payloads.value(QStringLiteral("retrieval.completed")).value(QStringLiteral("skipped")).toBool());
            const QVariantMap started = turn.payloads.value(QStringLiteral("model.started"));
            QCOMPARE(started.value(QStringLiteral("workspaceIntent")).toString(), QStringLiteral("layout"));
            QCOMPARE(started.value(QStringLiteral("workspaceFollowUp")).toBool(), step.followUp);
            // One tool call, two model calls (the call and the reply).
            QCOMPARE(turn.payloads.value(QStringLiteral("model.completed")).value(QStringLiteral("toolCallCount")).toInt(), 1);
            QCOMPARE(openai.requests.size() - requestsBefore, 2);
            // The model already knows the workspace; it doesn't look first.
            const QJsonObject first = openai.requests.at(requestsBefore);
            QVERIFY(first.value(QStringLiteral("instructions")).toString().contains(QStringLiteral("Layouts: Development, Research.")));
            QVERIFY(toolNames(first).contains(QStringLiteral("workspace_compose")));
            QVERIFY(!toolNames(first).contains(QStringLiteral("search_library")));
            // The person asked, so the workspace may apply it now.
            const QVariantMap request = workspace.last();
            QVERIFY(request.value(QStringLiteral("explicit")).toBool());
            QVERIFY(request.value(QStringLiteral("asked")).toBool());
            // A true report is left alone.
            QCOMPARE(turn.streamed, step.reply);
        }
        QCOMPARE(workspace.actions().count(QStringLiteral("describe")), 3); // one per turn
        QCOMPARE(workspace.actions().count(QStringLiteral("layout")), 2);
        QCOMPARE(workspace.actions().count(QStringLiteral("restore")), 1);
    }

    // The reply never claims a change the workspace didn't make.
    void claimsMatchWhatHappened()
    {
        FakeOpenAI openai;
        qputenv("OPENAI_API_KEY", "test-key");
        qputenv("OPENAI_BASE_URL", openai.baseUrl().toUtf8());
        FakeWorkspace workspace;
        workspace.install();
        AIRuntime runtime;
        QVERIFY(runtime.start(m_databasePath));

        // The change failed, but the model says it worked.
        workspace.failure = {{QStringLiteral("ok"), false}, {QStringLiteral("code"), QStringLiteral("LAYOUT_LOCKED")}, {QStringLiteral("error"), QStringLiteral("“Editor” is locked in place.")}};
        openai.responses.append(toolTurn(QStringLiteral("c1"), QStringLiteral("workspace_compose"), QStringLiteral(
            R"({"template":"none","strategy":"columns","slots":[{"name":"Left","holds":["code"],"weight":0,"items":[]},{"name":"Right","holds":["code"],"weight":0,"items":[]}],"focus":"","save_as":"Pair Programming"})")));
        openai.responses.append(textTurn(QStringLiteral("c2"), QStringLiteral("Done — I created and saved a Pair Programming layout.")));
        Turn turn = runTurn(runtime, QStringLiteral("Make me a pair programming layout"));
        QVERIFY(turn.events.contains(QStringLiteral("run.completed")));
        QVERIFY2(turn.streamed.contains(QStringLiteral("The workspace didn't change: “Editor” is locked in place.")), qPrintable(turn.streamed));
        QCOMPARE(turn.payloads.value(QStringLiteral("model.completed")).value(QStringLiteral("workspaceFailed")).toInt(), 1);

        // No tool call at all, yet it claims a switch.
        workspace.failure.clear();
        openai.responses.append(textTurn(QStringLiteral("c3"), QStringLiteral("Switched to Research.")));
        turn = runTurn(runtime, QStringLiteral("Switch to Research"));
        QVERIFY(turn.streamed.contains(QStringLiteral("The workspace didn't change in this reply.")));

        CoreDatabase database;
        database.open(m_databasePath);
        QVERIFY(ChatRepository::messages(database, m_chatId).last().toMap().value(QStringLiteral("content")).toString().contains(QStringLiteral("didn't change")));
    }

    // Finding files still searches the Library; other chat keeps the tools
    // within reach without treating the message as a layout request.
    void onlyPureLayoutRequestsSkipSearch()
    {
        FakeOpenAI openai;
        qputenv("OPENAI_API_KEY", "test-key");
        qputenv("OPENAI_BASE_URL", openai.baseUrl().toUtf8());
        FakeWorkspace workspace;
        workspace.install();
        AIRuntime runtime;
        QVERIFY(runtime.start(m_databasePath));

        openai.responses.append(textTurn(QStringLiteral("m1"), QStringLiteral("Here they are.")));
        Turn turn = runTurn(runtime, QStringLiteral("Open the files that explain the harbour lights beside the chat"));
        QVERIFY(turn.events.contains(QStringLiteral("run.completed")));
        QVERIFY(!turn.payloads.value(QStringLiteral("retrieval.completed")).value(QStringLiteral("skipped")).toBool());
        QCOMPARE(turn.payloads.value(QStringLiteral("model.started")).value(QStringLiteral("workspaceIntent")).toString(), QStringLiteral("mixed"));

        openai.responses.append(textTurn(QStringLiteral("m2"), QStringLiteral("They are green.")));
        turn = runTurn(runtime, QStringLiteral("What colour are the harbour lights?"));
        QVERIFY(turn.events.contains(QStringLiteral("run.completed")));
        QCOMPARE(turn.payloads.value(QStringLiteral("model.started")).value(QStringLiteral("workspaceIntent")).toString(), QStringLiteral("none"));
        QVERIFY(toolNames(openai.requests.last()).contains(QStringLiteral("workspace_compose")));
        // No workspace summary for a turn that isn't about it.
        QVERIFY(!openai.requests.last().value(QStringLiteral("instructions")).toString().contains(QStringLiteral("<workspace>")));
        QCOMPARE(turn.streamed, QStringLiteral("They are green."));

        // "try another" with nothing about the workspace before it is just chat.
        openai.responses.append(textTurn(QStringLiteral("m3"), QStringLiteral("Another colour: red.")));
        turn = runTurn(runtime, QStringLiteral("try another"));
        QCOMPARE(turn.payloads.value(QStringLiteral("model.started")).value(QStringLiteral("workspaceIntent")).toString(), QStringLiteral("none"));
    }

    void streamsALocalModelThroughChatCompletions()
    {
        FakeOpenAI server;
        QVERIFY(server.server.isListening());
        qunsetenv("OPENAI_API_KEY");
        qunsetenv("OPENAI_BASE_URL");
        qputenv("ARCHIVIST_LOCAL_AI_URL", server.baseUrl().toUtf8());
        server.modelsReply = R"({"object":"list","data":[{"id":"llama3.2","created":1,"owned_by":"library"}]})";

        const auto chunk = [](const QJsonObject &delta, const QJsonValue &finish = QJsonValue::Null) {
            const QJsonObject body{
                {QStringLiteral("id"), QStringLiteral("chatcmpl-1")},
                {QStringLiteral("object"), QStringLiteral("chat.completion.chunk")},
                {QStringLiteral("choices"), QJsonArray{QJsonObject{
                    {QStringLiteral("index"), 0},
                    {QStringLiteral("delta"), delta},
                    {QStringLiteral("finish_reason"), finish},
                }}},
            };
            return "data: " + QJsonDocument(body).toJson(QJsonDocument::Compact) + "\n\n";
        };
        const auto toolCall = [](const QJsonObject &call) {
            return QJsonObject{{QStringLiteral("tool_calls"), QJsonArray{call}}};
        };

        // Round 1 streams one tool call split over two chunks, as Ollama and
        // llama.cpp do. Round 2 streams the answer.
        server.responses.append(
            chunk(toolCall({
                {QStringLiteral("index"), 0},
                {QStringLiteral("id"), QStringLiteral("call_1")},
                {QStringLiteral("type"), QStringLiteral("function")},
                {QStringLiteral("function"), QJsonObject{{QStringLiteral("name"), QStringLiteral("list_directory")}, {QStringLiteral("arguments"), QStringLiteral(R"({"path":"",)")}}},
            }))
            + chunk(toolCall({
                {QStringLiteral("index"), 0},
                {QStringLiteral("function"), QJsonObject{{QStringLiteral("arguments"), QStringLiteral(R"("limit":10})")}}},
            }))
            + chunk({}, QStringLiteral("tool_calls")) + "data: [DONE]\n\n"
        );
        server.responses.append(
            chunk({{QStringLiteral("role"), QStringLiteral("assistant")}, {QStringLiteral("content"), QStringLiteral("Green ")}})
            + chunk({{QStringLiteral("content"), QStringLiteral("lights.")}})
            + chunk({}, QStringLiteral("stop")) + "data: [DONE]\n\n"
        );

        {
            CoreDatabase database;
            database.open(m_databasePath);
            const QString cachePath = ModelCatalogCache::cachePathForDatabase(m_databasePath);
            ModelCatalogCache::applyDiscovery(cachePath, QStringLiteral("local"), {QVariantMap{{QStringLiteral("modelId"), QStringLiteral("llama3.2")}}});
            QCOMPARE(ModelCatalogCache::models(cachePath, QStringLiteral("local")).size(), 1);
            QCOMPARE(ModelCatalogCache::health(cachePath, QStringLiteral("local")).value(QStringLiteral("status")).toString(), QStringLiteral("connected"));
            const QString agentId = ChatRepository::chatById(database, m_chatId)->value(QStringLiteral("agentId")).toString();
            AgentRepository::updateAgent(database, agentId, {{QStringLiteral("generation"), QVariantMap{
                {QStringLiteral("provider"), QStringLiteral("local")},
                {QStringLiteral("model"), QStringLiteral("llama3.2")},
            }}});
        }

        AIRuntime runtime;
        QVERIFY(runtime.start(m_databasePath));
        QStringList events;
        QString streamed;
        connect(&runtime, &AIRuntime::runEvent, this, [&](const QVariantMap &value) {
            const QString type = value.value(QStringLiteral("eventType")).toString();
            events.append(type);

            if (type == QStringLiteral("run.failed")) {
                qWarning().noquote() << QJsonDocument::fromVariant(value.value(QStringLiteral("payload"))).toJson(QJsonDocument::Compact);
            }

            if (type == QStringLiteral("model.delta")) {
                streamed += value.value(QStringLiteral("payload")).toMap().value(QStringLiteral("delta")).toString();
            }
        });

        const CoreResult started = call([&](AIRuntime::Callback done) {
            runtime.startChatRun(m_chatId, QStringLiteral("Test the library tools: what colour are the harbour lights?"), this, done);
        });
        QVERIFY2(started.ok, qPrintable(started.errorMessage));
        QTRY_VERIFY_WITH_TIMEOUT(events.contains(QStringLiteral("run.completed")) || events.contains(QStringLiteral("run.failed")), 15000);
        QVERIFY2(events.contains(QStringLiteral("run.completed")), qPrintable(events.join(QLatin1Char(','))));
        QVERIFY(events.contains(QStringLiteral("tool.completed")));
        QCOMPARE(streamed, QStringLiteral("Green lights."));

        QCOMPARE(server.requests.size(), 2);
        QVERIFY(server.paths.contains(QStringLiteral("/v1/chat/completions")));
        QVERIFY(!server.paths.contains(QStringLiteral("/v1/responses")));
        QVERIFY(server.authorizations.last().isEmpty());

        const QJsonObject first = server.requests.at(0);
        QCOMPARE(first.value(QStringLiteral("model")).toString(), QStringLiteral("llama3.2"));
        QCOMPARE(first.value(QStringLiteral("messages")).toArray().first().toObject().value(QStringLiteral("role")).toString(), QStringLiteral("system"));
        const QJsonObject tool = first.value(QStringLiteral("tools")).toArray().first().toObject();
        QCOMPARE(tool.value(QStringLiteral("type")).toString(), QStringLiteral("function"));
        QVERIFY(!tool.value(QStringLiteral("function")).toObject().value(QStringLiteral("name")).toString().isEmpty());

        const QJsonArray history = server.requests.at(1).value(QStringLiteral("messages")).toArray();
        const QJsonObject assistant = history.at(history.size() - 2).toObject();
        const QJsonObject result = history.last().toObject();
        QCOMPARE(assistant.value(QStringLiteral("role")).toString(), QStringLiteral("assistant"));
        const QJsonObject function = assistant.value(QStringLiteral("tool_calls")).toArray().first().toObject().value(QStringLiteral("function")).toObject();
        QCOMPARE(function.value(QStringLiteral("arguments")).toString(), QStringLiteral(R"({"path":"","limit":10})"));
        QCOMPARE(result.value(QStringLiteral("role")).toString(), QStringLiteral("tool"));
        QCOMPARE(result.value(QStringLiteral("tool_call_id")).toString(), QStringLiteral("call_1"));
        QVERIFY(result.value(QStringLiteral("content")).toString().contains(QStringLiteral("notes.txt")));

        CoreDatabase database;
        database.open(m_databasePath);
        const QVariantList messages = ChatRepository::messages(database, m_chatId);
        QCOMPARE(messages.last().toMap().value(QStringLiteral("content")).toString(), QStringLiteral("Green lights."));
        qunsetenv("ARCHIVIST_LOCAL_AI_URL");
    }

    void settingsDiscoverLocalModelsAndReportAMissingServer()
    {
        FakeOpenAI server;
        server.modelsReply = R"({"object":"list","data":[{"id":"qwen2.5-coder","created":1,"owned_by":"library"},{"id":"llama3.2","created":2,"owned_by":"library"}]})";
        qputenv("ARCHIVIST_LOCAL_AI_URL", server.baseUrl().toUtf8());

        AISettings settings;
        settings.setDatabasePath(m_databasePath);
        QCOMPARE(settings.models(QStringLiteral("local")).size(), 0);
        settings.refreshModels(QStringLiteral("local"));
        QVERIFY(settings.busy());
        QTRY_VERIFY_WITH_TIMEOUT(!settings.busy(), 10000);
        QVERIFY2(settings.errorMessage().isEmpty(), qPrintable(settings.errorMessage()));
        QCOMPARE(settings.models(QStringLiteral("local")).size(), 2);
        QCOMPARE(settings.health(QStringLiteral("local")).value(QStringLiteral("status")).toString(), QStringLiteral("connected"));
        QVERIFY(server.authorizations.last().isEmpty());

        // Nothing listening: the models stay, the health says why.
        QTcpServer closed;
        QVERIFY(closed.listen(QHostAddress::LocalHost));
        const quint16 deadPort = closed.serverPort();
        closed.close();
        qputenv("ARCHIVIST_LOCAL_AI_URL", QStringLiteral("http://127.0.0.1:%1/v1").arg(deadPort).toUtf8());
        settings.refreshModels(QStringLiteral("local"));
        QTRY_VERIFY_WITH_TIMEOUT(!settings.busy(), 10000);
        QVERIFY(settings.errorMessage().contains(QStringLiteral("No model server answered")));
        QCOMPARE(settings.models(QStringLiteral("local")).size(), 2);
        QCOMPARE(settings.health(QStringLiteral("local")).value(QStringLiteral("status")).toString(), QStringLiteral("unavailable"));
        qunsetenv("ARCHIVIST_LOCAL_AI_URL");
    }

#ifndef Q_OS_MACOS
    // On macOS this would touch the real login Keychain, so it runs elsewhere.
    void settingsSaveAndRemoveTheOpenAIKey()
    {
        FakeOpenAI openai;
        qunsetenv("OPENAI_API_KEY");
        qputenv("OPENAI_BASE_URL", openai.baseUrl().toUtf8());

        AISettings settings;
        settings.setDatabasePath(m_databasePath);
        QVERIFY(!settings.openAIKeyConfigured());
        QVERIFY(!settings.saveOpenAIKey(QStringLiteral("  ")));
        QVERIFY(settings.saveOpenAIKey(QStringLiteral("sk-test")));
        QCOMPARE(settings.openAIKeySource(), QStringLiteral("app"));
        QTRY_VERIFY_WITH_TIMEOUT(!settings.busy(), 10000);
        QCOMPARE(openai.authorizations.last(), QByteArray("Bearer sk-test"));
        QVERIFY(!settings.models(QStringLiteral("openai")).isEmpty());

        settings.removeOpenAIKey();
        QVERIFY(!settings.openAIKeyConfigured());
        qunsetenv("OPENAI_BASE_URL");
    }
#endif

    void failsClearlyWithoutAnApiKey()
    {
        qunsetenv("OPENAI_API_KEY");
        qunsetenv("OPENAI_BASE_URL");
        AIRuntime runtime;
        QVERIFY(runtime.start(m_databasePath));
        QVariantMap failure;
        connect(&runtime, &AIRuntime::runEvent, this, [&](const QVariantMap &value) {
            if (value.value(QStringLiteral("eventType")).toString() == QStringLiteral("run.failed")) {
                failure = value.value(QStringLiteral("payload")).toMap();
            }
        });

        const CoreResult started = call([&](AIRuntime::Callback done) { runtime.startChatRun(m_chatId, QStringLiteral("Hi"), this, done); });

        if (started.ok) {
            QTRY_VERIFY_WITH_TIMEOUT(!failure.isEmpty(), 10000);
            QVERIFY(JsonValues_contains(failure, QStringLiteral("OPENAI_API_KEY")));
        } else {
            QVERIFY2(started.errorMessage.contains(QStringLiteral("OPENAI_API_KEY")), qPrintable(started.errorMessage));
        }
    }

private:
    static bool JsonValues_contains(const QVariantMap &map, const QString &needle)
    {
        return QString::fromUtf8(QJsonDocument::fromVariant(map).toJson()).contains(needle);
    }
};

QTEST_GUILESS_MAIN(CoreAIRuntimeTest)
#include "core_ai_runtime_test.moc"

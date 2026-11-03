#include "Core/Database/core_database.h"
#include "Core/Domains/AI/ai_runtime.h"
#include "Core/Domains/AI/ai_settings.h"
#include "Core/Domains/Agents/agent_defaults.h"
#include "Core/Domains/Agents/agent_repository.h"
#include "Core/Domains/Chats/chat_repository.h"
#include "Core/Domains/Libraries/library_files.h"
#include "Core/Domains/Libraries/library_repository.h"
#include "Core/Domains/Models/model_catalog_cache.h"
#include "Core/Domains/Observatory/live_events.h"
#include "Core/Domains/Observatory/live_session.h"
#include "Core/Domains/Retrieval/embedding_index.h"
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
    // Per POST, in order: an HTTP status other than 200 answers with that
    // status, a retry-after-ms of 5 and no stream (and keeps the queued body).
    QList<int> statuses;
    // When set, each SSE event of a 200 reply is written this long apart.
    int gapMs = 0;
    QList<QJsonObject> requests;
    QStringList paths;
    QList<QByteArray> authorizations;
    QByteArray modelsReply = R"({"object":"list","data":[{"id":"gpt-5-mini","created":1,"owned_by":"openai"}]})";
    // POST /v1/embeddings: each request body, and the status to answer with
    // (200 answers with one unit vector per input along the first axis).
    QList<QJsonObject> embeddingRequests;
    int embeddingStatus = 200;
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
        } else if (paths.last().endsWith(QStringLiteral("/embeddings"))) {
            const QJsonObject request = QJsonDocument::fromJson(body).object();
            embeddingRequests.append(request);
            QJsonArray data;
            const int dimensions = request.value(QStringLiteral("dimensions")).toInt();
            for (int index = 0; index < request.value(QStringLiteral("input")).toArray().size(); ++index) {
                QJsonArray vector;
                for (int position = 0; position < dimensions; ++position) {
                    vector.append(position == 0 ? 1.0 : 0.0);
                }
                data.append(QJsonObject{{QStringLiteral("index"), index}, {QStringLiteral("embedding"), vector}});
            }
            const QByteArray json = embeddingStatus == 200
                ? QJsonDocument(QJsonObject{{QStringLiteral("data"), data}, {QStringLiteral("usage"), QJsonObject{{QStringLiteral("prompt_tokens"), 5}}}}).toJson(QJsonDocument::Compact)
                : QByteArray(R"({"error":{"message":"embeddings unavailable"}})");
            socket->write("HTTP/1.1 " + QByteArray::number(embeddingStatus) + " X\r\nContent-Type: application/json\r\nContent-Length: "
                          + QByteArray::number(json.size()) + "\r\nConnection: close\r\n\r\n" + json);
            socket->disconnectFromHost();
            return;
        } else {
            requests.append(QJsonDocument::fromJson(body).object());
            const int status = statuses.isEmpty() ? 200 : statuses.takeFirst();
            if (status != 200) {
                const QByteArray error = R"({"error":{"message":"busy"}})";
                socket->write("HTTP/1.1 " + QByteArray::number(status) + " Busy\r\nContent-Type: application/json\r\nretry-after-ms: 5\r\nContent-Length: "
                              + QByteArray::number(error.size()) + "\r\nConnection: close\r\n\r\n" + error);
                socket->disconnectFromHost();
                return;
            }
            reply = responses.isEmpty() ? QByteArray() : responses.takeFirst();
            if (gapMs > 0) {
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nContent-Length: " + QByteArray::number(reply.size())
                              + "\r\nConnection: close\r\n\r\n");
                const QList<QByteArray> blocks = reply.split('\n');
                QByteArray pending;
                int delay = 0;
                for (const QByteArray &line : blocks) {
                    pending += line + '\n';
                    if (line.isEmpty() && !pending.trimmed().isEmpty()) {
                        delay += gapMs;
                        const QByteArray piece = pending;
                        QTimer::singleShot(delay, socket, [socket, piece]() { socket->write(piece); });
                        pending.clear();
                    }
                }
                // The split leaves one trailing newline too many; keep the
                // byte count exact.
                QTimer::singleShot(delay + 1, socket, [socket, pending]() {
                    if (pending.size() > 1) {
                        socket->write(pending.left(pending.size() - 1));
                    }
                    socket->disconnectFromHost();
                });
                return;
            }
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
                    return {{QStringLiteral("ok"), true}, {QStringLiteral("status"), QStringLiteral("applied")}, {QStringLiteral("currentLayout"), current},
                            {QStringLiteral("notes"), QStringList{QStringLiteral("Switched to the “%1” layout").arg(current)}}};
                }
                if (action == QStringLiteral("compose")) {
                    QVariantList panels;
                    for (const QVariant &slot : request.value(QStringLiteral("slots")).toList()) {
                        panels.append(QVariantMap{{QStringLiteral("name"), slot.toMap().value(QStringLiteral("name"))}, {QStringLiteral("widthPercent"), slot.toMap().value(QStringLiteral("name")) == QStringLiteral("Chat") ? 64 : 36}});
                    }
                    return {{QStringLiteral("ok"), true}, {QStringLiteral("status"), QStringLiteral("applied")}, {QStringLiteral("panels"), panels},
                            {QStringLiteral("resources"), QVariantList{QVariantMap{{QStringLiteral("reference"), QStringLiteral("ghost.png")}, {QStringLiteral("status"), QStringLiteral("not_found")}, {QStringLiteral("detail"), QStringLiteral("“ghost.png” isn't open or in the Library.")}}}}};
                }
                if (action == QStringLiteral("restore")) {
                    const bool redo = request.value(QStringLiteral("scope")).toString() == QStringLiteral("redo");
                    return {{QStringLiteral("ok"), true}, {QStringLiteral("status"), QStringLiteral("applied")},
                            {QStringLiteral("notes"), QStringList{redo ? QStringLiteral("Redid: Put the workspace back the way it was") : QStringLiteral("Put the workspace back the way it was")}}};
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

    // Every event of a recording, by name (several of one name in order).
    static QMultiMap<QString, QJsonObject> traceByName(const Live::Session &session, QJsonArray *all = nullptr)
    {
        QMultiMap<QString, QJsonObject> byName;
        const QJsonArray events = Live::eventsJson(session.snapshot()).value(QStringLiteral("events")).toArray();
        for (const QJsonValue &value : events) {
            const QJsonObject event = value.toObject();
            byName.insert(event.value(QStringLiteral("category")).toString() + QLatin1Char('.') + event.value(QStringLiteral("name")).toString(), event);
        }
        if (all) {
            *all = events;
        }
        return byName;
    }

    static quint64 idOf(const QJsonObject &event) { return static_cast<quint64>(event.value(QStringLiteral("id")).toDouble()); }
    static quint64 parentOf(const QJsonObject &event) { return static_cast<quint64>(event.value(QStringLiteral("parent")).toDouble()); }
    static QJsonObject fieldsOf(const QJsonObject &event) { return event.value(QStringLiteral("fields")).toObject(); }

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

    // A complete mocked turn, recorded: the trace nests by cause from the
    // call that started it, through preparation, provider rounds with their
    // HTTP attempts (one refused and retried), the tool round and the
    // persistence, and carries numbers only, never the conversation.
    void aTracedTurnNestsByCauseAndCarriesNoContent()
    {
        FakeOpenAI openai;
        qputenv("OPENAI_API_KEY", "test-key-secret");
        qputenv("OPENAI_BASE_URL", openai.baseUrl().toUtf8());
        openai.statuses = {503};
        openai.responses.append(toolTurn(QStringLiteral("resp_1"), QStringLiteral("list_directory"), QStringLiteral(R"({"path":"","limit":10})")));
        openai.responses.append(FakeOpenAI::sse({
            created(QStringLiteral("resp_2")),
            delta(QStringLiteral("The lights ")),
            delta(QStringLiteral("are green.")),
            event(QStringLiteral("response.completed"), {{QStringLiteral("response"), QJsonObject{
                {QStringLiteral("id"), QStringLiteral("resp_2")},
                {QStringLiteral("usage"), QJsonObject{{QStringLiteral("input_tokens"), 321}, {QStringLiteral("output_tokens"), 7}}},
            }}}),
        }));
        openai.gapMs = 30;

        Live::Session session;
        session.reset(LiveEvents::nowNs());
        LiveEvents::setSink(&session);
        AIRuntime runtime;
        QVERIFY(runtime.start(m_databasePath));
        const Turn turn = runTurn(runtime, QStringLiteral("Test the library tools: what colour are the harbour lights?"));
        QVERIFY2(turn.events.contains(QStringLiteral("run.completed")), qPrintable(turn.events.join(QLatin1Char(','))));
        runtime.stop();
        LiveEvents::setSink(nullptr);

        QJsonArray all;
        const QMultiMap<QString, QJsonObject> trace = traceByName(session, &all);
        const QJsonObject call = trace.value(QStringLiteral("ai.start-chat-run"));
        const QJsonObject run = trace.value(QStringLiteral("ai.run"));
        QVERIFY(!call.isEmpty() && !run.isEmpty());
        QCOMPARE(parentOf(trace.value(QStringLiteral("ai.queue-wait"))), idOf(call));
        QCOMPARE(parentOf(trace.value(QStringLiteral("ai.callback-delay"))), idOf(call));
        QCOMPARE(parentOf(run), idOf(call));
        QCOMPARE(run.value(QStringLiteral("thread")).toString(), QStringLiteral("ArchivistAI"));
        const QJsonObject runFields = fieldsOf(run);
        QCOMPARE(runFields.value(QStringLiteral("outcome")).toString(), QStringLiteral("completed"));
        QCOMPARE(runFields.value(QStringLiteral("toolCalls")).toInt(), 1);
        QCOMPARE(runFields.value(QStringLiteral("rounds")).toInt(), 2);
        QCOMPARE(runFields.value(QStringLiteral("inputTokens")).toInt(), 321);
        QCOMPARE(runFields.value(QStringLiteral("outputTokens")).toInt(), 7);
        QVERIFY(runFields.value(QStringLiteral("eventWrites")).toInt() > 3);
        QCOMPARE(run.value(QStringLiteral("ok")).toBool(), true);

        const QJsonObject prepare = trace.value(QStringLiteral("ai.prepare"));
        QCOMPARE(parentOf(prepare), idOf(run));
        for (const char *stage : {"ai.conversation-load", "ai.attachments", "ai.retrieval", "ai.context-compile"}) {
            QVERIFY2(trace.contains(QLatin1String(stage)), stage);
            QCOMPARE(parentOf(trace.value(QLatin1String(stage))), idOf(prepare));
        }
        QVERIFY(fieldsOf(trace.value(QStringLiteral("ai.context-compile"))).contains(QStringLiteral("estimatedTokens")));
        // Retrieval and compiler stages nest under their step.
        const QJsonObject search = trace.value(QStringLiteral("retrieval.library-search"));
        QCOMPARE(parentOf(search), idOf(trace.value(QStringLiteral("ai.retrieval"))));
        QCOMPARE(parentOf(trace.value(QStringLiteral("retrieval.fts-query"))), idOf(search));
        QCOMPARE(parentOf(trace.value(QStringLiteral("retrieval.rank"))), idOf(search));
        QVERIFY(fieldsOf(trace.value(QStringLiteral("retrieval.fts-query"))).value(QStringLiteral("rows")).toInt() >= 1);
        QCOMPARE(parentOf(trace.value(QStringLiteral("retrieval.select"))), idOf(trace.value(QStringLiteral("ai.retrieval"))));
        const QJsonObject strategy = trace.value(QStringLiteral("context.strategy"));
        QCOMPARE(parentOf(strategy), idOf(trace.value(QStringLiteral("ai.context-compile"))));
        QCOMPARE(parentOf(trace.value(QStringLiteral("context.assemble"))), idOf(strategy));

        const QList<QJsonObject> rounds = trace.values(QStringLiteral("ai.provider-round"));
        QCOMPARE(rounds.size(), 2);
        QList<QJsonObject> attempts = trace.values(QStringLiteral("ai.http-attempt"));
        QCOMPARE(attempts.size(), 3); // 503, then the tool round; then the answer
        int refused = 0;
        for (const QJsonObject &attempt : attempts) {
            const quint64 parent = parentOf(attempt);
            QVERIFY(parent == idOf(rounds.at(0)) || parent == idOf(rounds.at(1)));
            if (fieldsOf(attempt).value(QStringLiteral("status")).toInt() == 503) {
                ++refused;
                QCOMPARE(fieldsOf(attempt).value(QStringLiteral("retry")).toBool(), true);
                QCOMPARE(attempt.value(QStringLiteral("ok")).toBool(), false);
            }
        }
        QCOMPARE(refused, 1);
        const QJsonObject retry = trace.value(QStringLiteral("ai.retry-wait"));
        QCOMPARE(fieldsOf(retry).value(QStringLiteral("waitMs")).toInt(), 5);
        QVERIFY(trace.values(QStringLiteral("ai.request-serialize")).size() == 3);
        QVERIFY(trace.values(QStringLiteral("ai.first-byte")).size() == 3);
        QCOMPARE(trace.values(QStringLiteral("ai.first-delta")).size(), 1);

        // The answer round: two deltas, 30 ms apart on the wire.
        QJsonObject answer;
        for (const QJsonObject &round : rounds) {
            if (fieldsOf(round).value(QStringLiteral("deltas")).toInt() == 2) {
                answer = round;
            }
        }
        QVERIFY(!answer.isEmpty());
        QCOMPARE(fieldsOf(answer).value(QStringLiteral("events")).toInt(), 4);
        QVERIFY(fieldsOf(answer).value(QStringLiteral("maxGapMs")).toDouble() >= 20);
        QCOMPARE(fieldsOf(answer).value(QStringLiteral("inputTokens")).toInt(), 321);
        QVERIFY(fieldsOf(answer).value(QStringLiteral("bytesReceived")).toInt() > 100);

        const QJsonObject toolRound = trace.value(QStringLiteral("ai.tool-round"));
        QCOMPARE(parentOf(toolRound), idOf(run));
        const QJsonObject tool = trace.value(QStringLiteral("ai.tool"));
        QCOMPARE(parentOf(tool), idOf(toolRound));
        QCOMPARE(fieldsOf(tool).value(QStringLiteral("tool")).toString(), QStringLiteral("list_directory"));
        QCOMPARE(fieldsOf(tool).value(QStringLiteral("callId")).toString(), QStringLiteral("call_resp_1"));
        QVERIFY(fieldsOf(tool).value(QStringLiteral("resultBytes")).toInt() > 0);
        QCOMPARE(parentOf(trace.value(QStringLiteral("ai.persist"))), idOf(run));

        // Never content: not the message, the reply, file names, the key.
        const QByteArray text = QJsonDocument(all).toJson(QJsonDocument::Compact);
        // The Node trace analysis tests read a trace made exactly this way.
        if (const QString out = qEnvironmentVariable("ARCHIVIST_TRACE_EXPORT"); !out.isEmpty()) {
            QFile file(out);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(QJsonDocument(QJsonObject{{QStringLiteral("events"), all}}).toJson(QJsonDocument::Indented));
        }
        for (const char *secret : {"harbour", "green", "notes.txt", "test-key-secret", "Bearer", "path"}) {
            QVERIFY2(!text.contains(secret), secret);
        }
        qunsetenv("OPENAI_BASE_URL");
        qunsetenv("OPENAI_API_KEY");
    }

    // Not recording: no spans, no ids, and the turn works the same.
    void anUntracedTurnRecordsNothing()
    {
        FakeOpenAI openai;
        qputenv("OPENAI_API_KEY", "test-key");
        qputenv("OPENAI_BASE_URL", openai.baseUrl().toUtf8());
        openai.responses.append(textTurn(QStringLiteral("resp_1"), QStringLiteral("Fine.")));
        QVERIFY(!LiveEvents::recording());
        AIRuntime runtime;
        QVERIFY(runtime.start(m_databasePath));
        const Turn turn = runTurn(runtime, QStringLiteral("Hello"));
        QVERIFY(turn.events.contains(QStringLiteral("run.completed")));
        QCOMPARE(LiveEvents::currentParent(), quint64(0));
        qunsetenv("OPENAI_BASE_URL");
        qunsetenv("OPENAI_API_KEY");
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
            QString tool;      // empty: a built-in command, done without the model
            QString arguments;
            QString reply;     // built from the workspace's result
            bool followUp;
        };
        const QList<Step> steps{
            {QStringLiteral("Switch to Research"), {}, {}, QStringLiteral("Switched to the “Research” layout."), false},
            {QStringLiteral("try another"), QStringLiteral("workspace_layout"), QStringLiteral(R"({"action":"apply","name":"Development","new_name":"","replace":false})"), QStringLiteral("Switched to the “Development” layout."), true},
            {QStringLiteral("Put everything back."), {}, {}, QStringLiteral("Put the workspace back the way it was."), false},
            {QStringLiteral("redo"), {}, {}, QStringLiteral("Redid: Put the workspace back the way it was."), true},
        };

        int round = 0;
        for (const Step &step : steps) {
            const qsizetype requestsBefore = openai.requests.size();
            if (!step.tool.isEmpty()) {
                openai.responses.append(toolTurn(QStringLiteral("r%1").arg(++round), step.tool, step.arguments));
            }

            const Turn turn = runTurn(runtime, step.message);
            QVERIFY2(turn.events.contains(QStringLiteral("run.completed")), qPrintable(step.message + QStringLiteral(": ") + turn.events.join(QLatin1Char(','))));
            const QVariantMap completed = turn.payloads.value(QStringLiteral("model.completed"));
            qInfo().noquote() << QStringLiteral("[timing] \"%1\": %2 ms end to end, %3 model calls, %4 tool call, prepare %5 ms")
                                     .arg(step.message)
                                     .arg(turn.elapsedMs)
                                     .arg(openai.requests.size() - requestsBefore)
                                     .arg(completed.value(QStringLiteral("toolCallCount")).toInt())
                                     .arg(turn.payloads.value(QStringLiteral("model.started")).value(QStringLiteral("prepareMs")).toDouble());

            // Routed before retrieval: no Library search for a layout command.
            QVERIFY(turn.payloads.value(QStringLiteral("retrieval.completed")).value(QStringLiteral("skipped")).toBool());
            const QVariantMap started = turn.payloads.value(QStringLiteral("model.started"));
            QCOMPARE(started.value(QStringLiteral("workspaceIntent")).toString(), QStringLiteral("layout"));
            QCOMPARE(started.value(QStringLiteral("workspaceFollowUp")).toBool(), step.followUp);
            // One workspace call; built-in commands need no model call, the
            // rest one (no second round just to confirm).
            QCOMPARE(completed.value(QStringLiteral("toolCallCount")).toInt(), 1);
            QCOMPARE(completed.value(QStringLiteral("workspaceApplied")).toInt(), 1);
            QCOMPARE(int(openai.requests.size() - requestsBefore), step.tool.isEmpty() ? 0 : 1);
            QCOMPARE(completed.value(QStringLiteral("providerRoundCount")).toInt(), step.tool.isEmpty() ? 0 : 1);
            QCOMPARE(completed.value(QStringLiteral("workspaceNative")).toBool(), step.tool.isEmpty());
            if (!step.tool.isEmpty()) {
                // The model already knows the workspace; it doesn't look first.
                const QJsonObject first = openai.requests.at(requestsBefore);
                QVERIFY(first.value(QStringLiteral("instructions")).toString().contains(QStringLiteral("Layouts: Development, Research.")));
                QVERIFY(toolNames(first).contains(QStringLiteral("workspace_compose")));
                QVERIFY(!toolNames(first).contains(QStringLiteral("search_library")));
                // Picking a recipe needs little thought: no default reasoning.
                QCOMPARE(first.value(QStringLiteral("reasoning")).toObject().value(QStringLiteral("effort")).toString(), QStringLiteral("minimal"));
            }
            // The person asked, so the workspace may apply it now.
            const QVariantMap request = workspace.last();
            QVERIFY(request.value(QStringLiteral("explicit")).toBool());
            QVERIFY(request.value(QStringLiteral("asked")).toBool());
            // The reply says what the workspace reported, briefly.
            QCOMPARE(turn.streamed, step.reply);
        }
        QVERIFY(openai.responses.isEmpty());
        QCOMPARE(workspace.actions().count(QStringLiteral("describe")), 4); // one per turn
        QCOMPARE(workspace.actions().count(QStringLiteral("layout")), 2);
        QCOMPARE(workspace.actions().count(QStringLiteral("restore")), 2);
    }

    // "Make chat dominant and show three images stacked on the right": one
    // model call that composes, and a short reply grounded in the result.
    void composeRepliesFromTheResult()
    {
        FakeOpenAI openai;
        qputenv("OPENAI_API_KEY", "test-key");
        qputenv("OPENAI_BASE_URL", openai.baseUrl().toUtf8());
        FakeWorkspace workspace;
        workspace.install();
        AIRuntime runtime;
        QVERIFY(runtime.start(m_databasePath));

        openai.responses.append(toolTurn(QStringLiteral("d1"), QStringLiteral("workspace_compose"), QStringLiteral(
            R"({"template":"none","arrangement":"split","rows":0,"columns":0,"slots":[{"name":"Chat","holds":["chat"],"weight":10,"items":[{"kind":"chat","reference":"","library":""}]},)"
            R"({"name":"Image A","holds":["preview"],"weight":0,"items":[]},{"name":"Image B","holds":["preview"],"weight":0,"items":[]},{"name":"Image C","holds":["preview"],"weight":0,"items":[]}],"focus":"","save_as":""})")));
        const Turn turn = runTurn(runtime, QStringLiteral("Make chat dominant and show three images stacked on the right"));
        QVERIFY2(turn.events.contains(QStringLiteral("run.completed")), qPrintable(turn.events.join(QLatin1Char(','))));
        const QVariantMap completed = turn.payloads.value(QStringLiteral("model.completed"));
        qInfo().noquote() << QStringLiteral("[timing] compose: %1 ms end to end, %2 model call(s)").arg(turn.elapsedMs).arg(openai.requests.size());
        QCOMPARE(openai.requests.size(), 1);
        QCOMPARE(completed.value(QStringLiteral("providerRoundCount")).toInt(), 1);
        QVERIFY(completed.value(QStringLiteral("workspaceAcknowledged")).toBool());
        QCOMPARE(workspace.actions().count(QStringLiteral("compose")), 1);
        QCOMPARE(turn.streamed, QStringLiteral("Done: Chat (64% wide), Image A (36% wide), Image B (36% wide), Image C (36% wide). “ghost.png” isn't open or in the Library."));
    }

    // "4 panes, chat on bottom left" is a grid the app builds itself: no
    // model call, one compose, and the same answer every time.
    void panesAreANativeGrid()
    {
        FakeOpenAI openai;
        qputenv("OPENAI_API_KEY", "test-key");
        qputenv("OPENAI_BASE_URL", openai.baseUrl().toUtf8());
        FakeWorkspace workspace;
        workspace.install();
        AIRuntime runtime;
        QVERIFY(runtime.start(m_databasePath));

        struct Case
        {
            QString message;
            int rows;
            int columns;
            QString chatAt;
        };
        const QList<Case> cases{
            {QStringLiteral("4 panes, chat on bottom left"), 2, 2, QStringLiteral("bottom-left")},
            {QStringLiteral("Four panes with chat bottom-left please"), 2, 2, QStringLiteral("bottom-left")},
            {QStringLiteral("2x2 grid, chat in the top right"), 2, 2, QStringLiteral("top-right")},
            {QStringLiteral("give me three columns"), 1, 3, QString()},
            {QStringLiteral("6 panels"), 2, 3, QString()},
        };
        for (const Case &test : cases) {
            const qsizetype requestsBefore = openai.requests.size();
            const Turn turn = runTurn(runtime, test.message);
            QVERIFY2(turn.events.contains(QStringLiteral("run.completed")), qPrintable(test.message + QStringLiteral(": ") + turn.events.join(QLatin1Char(','))));
            const QVariantMap completed = turn.payloads.value(QStringLiteral("model.completed"));
            qInfo().noquote() << QStringLiteral("[timing] \"%1\": %2 ms end to end, %3 model calls, prepare %4 ms")
                                     .arg(test.message).arg(turn.elapsedMs).arg(openai.requests.size() - requestsBefore)
                                     .arg(turn.payloads.value(QStringLiteral("model.started")).value(QStringLiteral("prepareMs")).toDouble());
            QCOMPARE(int(openai.requests.size() - requestsBefore), 0);
            QVERIFY(completed.value(QStringLiteral("workspaceNative")).toBool());
            QVERIFY(turn.payloads.value(QStringLiteral("retrieval.completed")).value(QStringLiteral("skipped")).toBool());
            const QVariantMap request = workspace.last();
            QCOMPARE(request.value(QStringLiteral("action")).toString(), QStringLiteral("compose"));
            QCOMPARE(request.value(QStringLiteral("strategy")).toString(), QStringLiteral("grid"));
            QCOMPARE(request.value(QStringLiteral("rows")).toInt(), test.rows);
            QCOMPARE(request.value(QStringLiteral("columns")).toInt(), test.columns);
            const QVariantList panels = request.value(QStringLiteral("slots")).toList();
            QCOMPARE(panels.size(), test.chatAt.isEmpty() ? 0 : 1);
            if (!test.chatAt.isEmpty()) {
                QCOMPARE(panels.first().toMap().value(QStringLiteral("position")).toString(), test.chatAt);
                QCOMPARE(panels.first().toMap().value(QStringLiteral("items")).toList().first().toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("chat"));
            }
            QVERIFY(!turn.streamed.isEmpty());
        }
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
            R"({"template":"none","arrangement":"columns","rows":0,"columns":0,"slots":[{"name":"Left","holds":["code"],"weight":0,"items":[]},{"name":"Right","holds":["code"],"weight":0,"items":[]}],"focus":"","save_as":"Pair Programming"})")));
        openai.responses.append(textTurn(QStringLiteral("c2"), QStringLiteral("Done — I created and saved a Pair Programming layout.")));
        Turn turn = runTurn(runtime, QStringLiteral("Make me a pair programming layout"));
        QVERIFY(turn.events.contains(QStringLiteral("run.completed")));
        QVERIFY2(turn.streamed.contains(QStringLiteral("The workspace didn't change: “Editor” is locked in place.")), qPrintable(turn.streamed));
        QCOMPARE(turn.payloads.value(QStringLiteral("model.completed")).value(QStringLiteral("workspaceFailed")).toInt(), 1);

        // No tool call at all, yet it claims a switch.
        workspace.failure.clear();
        openai.responses.append(textTurn(QStringLiteral("c3"), QStringLiteral("Switched to Research.")));
        turn = runTurn(runtime, QStringLiteral("I want the Research layout with the chat wider"));
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
    // Context Assembly v3 is chosen per Agent, never by migration.
    void useContextAssembly(const QVariantMap &extra = {})
    {
        CoreDatabase database;
        database.open(m_databasePath);
        QVariantMap config{{QStringLiteral("totalTokens"), 32000}, {QStringLiteral("responseTokenReserve"), 4000}};
        config.insert(extra);
        AgentRepository::updateAgent(database, AgentDefaults::defaultAgentId, {
            {QStringLiteral("context"), QVariantMap{
                {QStringLiteral("compiler"), QVariantMap{{QStringLiteral("id"), QStringLiteral("context-assembly")}, {QStringLiteral("version"), 3}}},
                {QStringLiteral("config"), config},
            }},
        });
    }

    QVariantMap contextRunFor(const QString &userContent)
    {
        CoreDatabase database;
        database.open(m_databasePath);
        const QVariantList messages = ChatRepository::messages(database, m_chatId);
        for (qsizetype index = 0; index + 1 < messages.size(); ++index) {
            if (messages.at(index).toMap().value(QStringLiteral("content")).toString() == userContent) {
                return ChatRepository::contextRun(database, m_chatId, messages.at(index + 1).toMap().value(QStringLiteral("id")).toString());
            }
        }
        return {};
    }

    static QStringList inputRoles(const QJsonObject &request)
    {
        QStringList roles;
        for (const QJsonValue &value : request.value(QStringLiteral("input")).toArray()) {
            roles.append(value.toObject().value(QStringLiteral("role")).toString());
        }
        return roles;
    }

private slots:
    // Saved Agents keep the compiler they have, and its message shape.
    void savedCompilersKeepTheirShape()
    {
        FakeOpenAI openai;
        qputenv("OPENAI_API_KEY", "test-key");
        qputenv("OPENAI_BASE_URL", openai.baseUrl().toUtf8());
        openai.responses.append(textTurn(QStringLiteral("resp_legacy"), QStringLiteral("Green.")));
        AIRuntime runtime;
        QVERIFY(runtime.start(m_databasePath));

        const QString question = QStringLiteral("What colour are the harbour lights?");
        const Turn turn = runTurn(runtime, question);
        QVERIFY2(turn.events.contains(QStringLiteral("run.completed")), qPrintable(turn.events.join(QLatin1Char(','))));
        QVERIFY(!turn.payloads.value(QStringLiteral("context.completed")).contains(QStringLiteral("assemblyVersion")));
        const QVariantMap contextRun = contextRunFor(question);
        const QVariantMap manifest = contextRun.value(QStringLiteral("manifest")).toMap();
        QCOMPARE(manifest.value(QStringLiteral("compiler")).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("recent-history"));
        QVERIFY(!manifest.contains(QStringLiteral("assembly")));
        // The older pipeline still sends Library evidence as a system message.
        QVERIFY(inputRoles(openai.requests.first()).contains(QStringLiteral("system")));
        QCOMPARE(inputRoles(openai.requests.first()).last(), QStringLiteral("user"));
        qunsetenv("OPENAI_BASE_URL");
        qunsetenv("OPENAI_API_KEY");
    }

    // v3: evidence reaches the model as quoted user-role data before the
    // question, the ledger covers the whole request, and the Context
    // Inspector gets its usual fields plus the assembly record.
    void contextAssemblySendsEvidenceAsQuotedData()
    {
        useContextAssembly();
        FakeOpenAI openai;
        qputenv("OPENAI_API_KEY", "test-key");
        qputenv("OPENAI_BASE_URL", openai.baseUrl().toUtf8());
        openai.responses.append(FakeOpenAI::sse({created(QStringLiteral("resp_v3")), delta(QStringLiteral("They are ")), delta(QStringLiteral("green.")), completed(QStringLiteral("resp_v3"))}));
        AIRuntime runtime;
        QVERIFY(runtime.start(m_databasePath));

        const QString question = QStringLiteral("What colour are the harbour lights?");
        const Turn turn = runTurn(runtime, question);
        QVERIFY2(turn.events.contains(QStringLiteral("run.completed")), qPrintable(turn.events.join(QLatin1Char(','))));
        QCOMPARE(turn.streamed, QStringLiteral("They are green."));
        QCOMPARE(turn.payloads.value(QStringLiteral("context.completed")).value(QStringLiteral("assemblyVersion")).toInt(), 3);

        QCOMPARE(openai.requests.size(), 1);
        const QJsonObject request = openai.requests.first();
        const QJsonArray input = request.value(QStringLiteral("input")).toArray();
        QVERIFY(!inputRoles(request).contains(QStringLiteral("system")));
        QVERIFY(!inputRoles(request).contains(QStringLiteral("developer")));
        QCOMPARE(input.last().toObject().value(QStringLiteral("content")).toString(), question);
        const QString reference = input.at(input.size() - 2).toObject().value(QStringLiteral("content")).toString();
        QCOMPARE(input.at(input.size() - 2).toObject().value(QStringLiteral("role")).toString(), QStringLiteral("user"));
        QVERIFY2(reference.startsWith(QStringLiteral("<archivist-reference-material>")), qPrintable(reference.left(200)));
        QVERIFY(reference.contains(QStringLiteral("The harbour lights are green.")));
        QVERIFY(reference.contains(QStringLiteral("path=\"notes.txt\"")));
        QVERIFY(request.value(QStringLiteral("instructions")).toString().contains(QStringLiteral("<archivist-reference-material>")));

        const QVariantMap contextRun = contextRunFor(question);
        const QVariantMap manifest = contextRun.value(QStringLiteral("manifest")).toMap();
        QCOMPARE(manifest.value(QStringLiteral("compiler")).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("context-assembly"));
        QCOMPARE(manifest.value(QStringLiteral("includedSources")).toList().size(), 1);
        const QVariantMap ledger = manifest.value(QStringLiteral("assembly")).toMap().value(QStringLiteral("ledger")).toMap();
        QCOMPARE(ledger.value(QStringLiteral("used")).toInt(), manifest.value(QStringLiteral("estimatedInputTokens")).toInt());
        QVERIFY(!ledger.value(QStringLiteral("overBudget")).toBool());
        QStringList kinds;
        for (const QVariant &line : ledger.value(QStringLiteral("lines")).toList()) {
            if (line.toMap().value(QStringLiteral("tokens")).toInt() > 0) {
                kinds.append(line.toMap().value(QStringLiteral("kind")).toString());
            }
        }
        QVERIFY2(kinds.contains(QStringLiteral("instructions")) && kinds.contains(QStringLiteral("library-evidence"))
                     && kinds.contains(QStringLiteral("current-request")),
                 qPrintable(kinds.join(QLatin1Char(','))));
        // Tools offered this turn are charged too.
        QCOMPARE(kinds.contains(QStringLiteral("tool-definitions")), !request.value(QStringLiteral("tools")).toArray().isEmpty());
        const QVariantList sources = contextRun.value(QStringLiteral("sources")).toList();
        QCOMPARE(sources.size(), 1);
        QCOMPARE(sources.first().toMap().value(QStringLiteral("status")).toString(), QStringLiteral("included"));
        qunsetenv("OPENAI_BASE_URL");
        qunsetenv("OPENAI_API_KEY");
    }

    // Hybrid retrieval: the Chat's Library opted in and its chunk already has
    // a vector along the axis the fake embeddings endpoint answers with.
    void indexLibrarySemantically()
    {
        CoreDatabase database;
        database.open(m_databasePath);
        const QString libraryId = ChatRepository::chatById(database, m_chatId)->value(QStringLiteral("libraryId")).toString();
        const Retrieval::EmbeddingConfig config = Retrieval::currentEmbeddingConfig();
        EmbeddingIndex::setLibraryEnabled(database, libraryId, true);
        for (const QVariantMap &chunk : database.prepare(QStringLiteral(
                 "SELECT id, content_hash AS hash, library_file_id AS fileId FROM library_chunks WHERE library_id = ?"
             )).bind(1, libraryId).all()) {
            QVector<float> vector(config.dimensions, 0.0f);
            vector[0] = 1.0f;
            EmbeddingIndex::storeVector(database, chunk.value(QStringLiteral("hash")).toString(), config, vector, 0);
            EmbeddingIndex::SourceText source;
            source.sourceType = Retrieval::SourceType::libraryChunk;
            source.sourceId = chunk.value(QStringLiteral("id")).toString();
            source.scopeType = Retrieval::ScopeType::library;
            source.scopeId = libraryId;
            source.contentHash = chunk.value(QStringLiteral("hash")).toString();
            source.parentId = chunk.value(QStringLiteral("fileId")).toString();
            EmbeddingIndex::upsertSource(database, source);
        }
    }

private slots:
    // v3 + an opted-in Library: the question is embedded and a chunk that
    // shares no keyword with it reaches the model, cited as usual.
    void contextAssemblyUsesHybridRetrievalWhenTheLibraryOptedIn()
    {
        useContextAssembly();
        indexLibrarySemantically();
        FakeOpenAI openai;
        qputenv("OPENAI_API_KEY", "test-key");
        qputenv("OPENAI_BASE_URL", openai.baseUrl().toUtf8());
        openai.responses.append(textTurn(QStringLiteral("resp_hybrid"), QStringLiteral("Green.")));
        AIRuntime runtime;
        QVERIFY(runtime.start(m_databasePath));

        // No keyword in common with "The harbour lights are green."
        const QString question = QStringLiteral("Describe the maritime beacons");
        const Turn turn = runTurn(runtime, question);
        QVERIFY2(turn.events.contains(QStringLiteral("run.completed")), qPrintable(turn.events.join(QLatin1Char(','))));
        QCOMPARE(openai.embeddingRequests.size(), 1);
        QCOMPARE(openai.embeddingRequests.first().value(QStringLiteral("model")).toString(), QStringLiteral("text-embedding-3-small"));
        QCOMPARE(openai.embeddingRequests.first().value(QStringLiteral("input")).toArray().first().toString(), question);

        const QJsonArray input = openai.requests.first().value(QStringLiteral("input")).toArray();
        const QString reference = input.at(input.size() - 2).toObject().value(QStringLiteral("content")).toString();
        QVERIFY2(reference.contains(QStringLiteral("The harbour lights are green.")), qPrintable(reference.left(300)));
        QVERIFY(reference.contains(QStringLiteral("path=\"notes.txt\"")));

        const QVariantMap contextRun = contextRunFor(question);
        const QVariantMap manifest = contextRun.value(QStringLiteral("manifest")).toMap();
        const QVariantMap retrieval = manifest.value(QStringLiteral("retrieval")).toMap();
        QCOMPARE(retrieval.value(QStringLiteral("mode")).toString(), QStringLiteral("hybrid"));
        QCOMPARE(retrieval.value(QStringLiteral("semanticCandidates")).toInt(), 1);
        QCOMPARE(manifest.value(QStringLiteral("includedSources")).toList().size(), 1);
        bool semanticItem = false;
        for (const QVariant &item : manifest.value(QStringLiteral("assembly")).toMap().value(QStringLiteral("items")).toList()) {
            const QVariantMap ref = item.toMap().value(QStringLiteral("ref")).toMap();
            semanticItem = semanticItem || ref.value(QStringLiteral("producer")).toString() == QStringLiteral("library-semantic");
        }
        QVERIFY(semanticItem);
        qunsetenv("OPENAI_BASE_URL");
        qunsetenv("OPENAI_API_KEY");
    }

    // The embeddings endpoint failing never fails the turn: keyword search
    // is used and the Context Inspector says why.
    void hybridRetrievalFallsBackToKeywordSearch()
    {
        useContextAssembly();
        indexLibrarySemantically();
        FakeOpenAI openai;
        openai.embeddingStatus = 500;
        qputenv("OPENAI_API_KEY", "test-key");
        qputenv("OPENAI_BASE_URL", openai.baseUrl().toUtf8());
        openai.responses.append(textTurn(QStringLiteral("resp_fallback"), QStringLiteral("Green.")));
        AIRuntime runtime;
        QVERIFY(runtime.start(m_databasePath));

        const QString question = QStringLiteral("What colour are the harbour lights?");
        const Turn turn = runTurn(runtime, question);
        QVERIFY2(turn.events.contains(QStringLiteral("run.completed")), qPrintable(turn.events.join(QLatin1Char(','))));
        QCOMPARE(openai.embeddingRequests.size(), 1);
        const QVariantMap contextRun = contextRunFor(question);
        const QVariantMap manifest = contextRun.value(QStringLiteral("manifest")).toMap();
        QVERIFY(!manifest.contains(QStringLiteral("retrieval"))); // plain FTS
        QCOMPARE(manifest.value(QStringLiteral("includedSources")).toList().size(), 1);
        bool warned = false;
        for (const QVariant &warning : contextRun.value(QStringLiteral("warnings")).toList()) {
            warned = warned || warning.toString().startsWith(QStringLiteral("Semantic Library search was unavailable"));
        }
        QVERIFY(warned);
        qunsetenv("OPENAI_BASE_URL");
        qunsetenv("OPENAI_API_KEY");
    }

    // Saved compilers, and v3 with semantic retrieval off, never embed.
    void legacyCompilersAndSemanticOffNeverEmbed()
    {
        indexLibrarySemantically();
        FakeOpenAI openai;
        qputenv("OPENAI_API_KEY", "test-key");
        qputenv("OPENAI_BASE_URL", openai.baseUrl().toUtf8());
        openai.responses.append(textTurn(QStringLiteral("resp_legacy_semantic"), QStringLiteral("Green.")));
        openai.responses.append(textTurn(QStringLiteral("resp_off"), QStringLiteral("Green.")));
        AIRuntime runtime;
        QVERIFY(runtime.start(m_databasePath));

        const Turn legacy = runTurn(runtime, QStringLiteral("What colour are the harbour lights?"));
        QVERIFY(legacy.events.contains(QStringLiteral("run.completed")));
        QCOMPARE(openai.embeddingRequests.size(), 0);
        QVERIFY(inputRoles(openai.requests.first()).contains(QStringLiteral("system")));

        useContextAssembly({{QStringLiteral("semanticRetrieval"), QStringLiteral("off")}});
        const Turn off = runTurn(runtime, QStringLiteral("And the harbour lights again?"));
        QVERIFY(off.events.contains(QStringLiteral("run.completed")));
        QCOMPARE(openai.embeddingRequests.size(), 0);
        qunsetenv("OPENAI_BASE_URL");
        qunsetenv("OPENAI_API_KEY");
    }

    // v3: a tool result larger than the tool-result reserve is cut to a
    // valid JSON notice instead of being sent over budget.
    void contextAssemblyKeepsToolResultsInBudget()
    {
        useContextAssembly({{QStringLiteral("toolResultTokens"), 500}});
        {
            CoreDatabase database;
            database.open(m_databasePath);
            const QVariantMap chat = ChatRepository::chatById(database, m_chatId).value();
            const QString libraryId = chat.value(QStringLiteral("libraryId")).toString();
            const QString root = LibraryRepository::libraryById(database, libraryId)->value(QStringLiteral("rootPath")).toString();
            for (int index = 0; index < 120; ++index) {
                QFile file(QDir(root).filePath(QStringLiteral("a-rather-long-file-name-for-budget-testing-%1.txt").arg(index, 3, 10, QLatin1Char('0'))));
                QVERIFY(file.open(QIODevice::WriteOnly));
                file.write("x\n");
            }
            LibraryFiles::scan(database, libraryId);
        }

        FakeOpenAI openai;
        qputenv("OPENAI_API_KEY", "test-key");
        qputenv("OPENAI_BASE_URL", openai.baseUrl().toUtf8());
        openai.responses.append(toolTurn(QStringLiteral("resp_t1"), QStringLiteral("list_directory"), QStringLiteral(R"({"path":"","limit":200})")));
        openai.responses.append(textTurn(QStringLiteral("resp_t2"), QStringLiteral("Lots of files.")));
        AIRuntime runtime;
        QVERIFY(runtime.start(m_databasePath));

        const QString question = QStringLiteral("Test the library tools: list the directory.");
        const Turn turn = runTurn(runtime, question);
        QVERIFY2(turn.events.contains(QStringLiteral("run.completed")), qPrintable(turn.events.join(QLatin1Char(','))));
        QCOMPARE(openai.requests.size(), 2);
        const QJsonObject output = openai.requests.at(1).value(QStringLiteral("input")).toArray().first().toObject();
        QCOMPARE(output.value(QStringLiteral("type")).toString(), QStringLiteral("function_call_output"));
        const QString sent = output.value(QStringLiteral("output")).toString();
        const QJsonObject wrapper = QJsonDocument::fromJson(sent.toUtf8()).object();
        QVERIFY2(wrapper.value(QStringLiteral("truncated")).toBool(), qPrintable(sent.left(200)));
        QVERIFY(sent.size() <= 500 * 4);

        const QVariantMap assembly = contextRunFor(question).value(QStringLiteral("manifest")).toMap().value(QStringLiteral("assembly")).toMap();
        QCOMPARE(assembly.value(QStringLiteral("truncatedToolResults")).toInt(), 1);
        const QVariantMap reserve = assembly.value(QStringLiteral("ledger")).toMap().value(QStringLiteral("reserves")).toMap().value(QStringLiteral("tool-results")).toMap();
        QCOMPARE(reserve.value(QStringLiteral("tokens")).toInt(), 500);
        QCOMPARE(reserve.value(QStringLiteral("drawn")).toInt(), 500);
        QVERIFY(contextRunFor(question).value(QStringLiteral("warnings")).toList().size() >= 1);
        qunsetenv("OPENAI_BASE_URL");
        qunsetenv("OPENAI_API_KEY");
    }

    // v3 keeps streaming and cancellation: a run cancelled mid-stream keeps
    // what streamed so far and settles as cancelled.
    void contextAssemblyStreamsAndCancels()
    {
        useContextAssembly();
        FakeOpenAI openai;
        openai.gapMs = 150;
        qputenv("OPENAI_API_KEY", "test-key");
        qputenv("OPENAI_BASE_URL", openai.baseUrl().toUtf8());
        QList<QJsonObject> events{created(QStringLiteral("resp_slow"))};
        for (int index = 0; index < 20; ++index) {
            events.append(delta(QStringLiteral("word%1 ").arg(index)));
        }
        events.append(completed(QStringLiteral("resp_slow")));
        openai.responses.append(FakeOpenAI::sse(events));
        AIRuntime runtime;
        QVERIFY(runtime.start(m_databasePath));

        QStringList seen;
        connect(&runtime, &AIRuntime::runEvent, this, [&](const QVariantMap &value) {
            seen.append(value.value(QStringLiteral("eventType")).toString());
        });
        const CoreResult started = call([&](AIRuntime::Callback done) {
            runtime.startChatRun(m_chatId, QStringLiteral("Tell me a long story about the harbour."), this, done);
        });
        QVERIFY2(started.ok, qPrintable(started.errorMessage));
        const QString runId = started.value.toMap().value(QStringLiteral("run")).toMap().value(QStringLiteral("id")).toString();
        QTRY_VERIFY_WITH_TIMEOUT(seen.contains(QStringLiteral("model.delta")), 10000);
        QVERIFY(seen.contains(QStringLiteral("context.completed")));

        const CoreResult cancelled = call([&](AIRuntime::Callback done) { runtime.cancelRun(runId, this, done); });
        QVERIFY2(cancelled.ok, qPrintable(cancelled.errorMessage));
        QCOMPARE(cancelled.value.toMap().value(QStringLiteral("status")).toString(), QStringLiteral("cancelled"));
        QTRY_VERIFY_WITH_TIMEOUT(seen.contains(QStringLiteral("run.cancelled")), 10000);
        QVERIFY(!seen.contains(QStringLiteral("run.completed")));

        CoreDatabase database;
        database.open(m_databasePath);
        const QVariantMap assistant = ChatRepository::messages(database, m_chatId).last().toMap();
        QCOMPARE(assistant.value(QStringLiteral("status")).toString(), QStringLiteral("cancelled"));
        QVERIFY(assistant.value(QStringLiteral("content")).toString().startsWith(QStringLiteral("word0")));
        qunsetenv("OPENAI_BASE_URL");
        qunsetenv("OPENAI_API_KEY");
    }

private:
    static bool JsonValues_contains(const QVariantMap &map, const QString &needle)
    {
        return QString::fromUtf8(QJsonDocument::fromVariant(map).toJson()).contains(needle);
    }
};

QTEST_GUILESS_MAIN(CoreAIRuntimeTest)
#include "core_ai_runtime_test.moc"

#include "Core/Database/core_database.h"
#include "Core/Domains/Chats/chat_repository.h"
#include "Core/Domains/Ide/ide_server.h"
#include "Core/Support/login_environment.h"
#include "Core/Domains/Libraries/library_files.h"
#include "Core/Domains/Libraries/library_repository.h"
#include "Core/Support/core_values.h"
#include "Core/Support/json_values.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QUrl>
#include <QtEndian>
#include <QtTest>
#include <atomic>
#include <vector>
#include <thread>

using namespace CoreValues;

namespace
{
struct HttpReply
{
    int status = 0;
    QByteArray headers;
    QByteArray body;

    QVariantMap json() const { return JsonValues::parse(QString::fromUtf8(body)).toMap(); }
};

// A minimal blocking HTTP/1.1 client for loopback tests.
HttpReply request(quint16 port, const QByteArray &method, const QByteArray &target, const QByteArray &body = {}, QList<QByteArray> headers = {})
{
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, port);

    if (!socket.waitForConnected(3000)) {
        return {};
    }

    bool hasHost = false;

    for (const QByteArray &header : headers) {
        hasHost = hasHost || header.toLower().startsWith("host:");
    }

    if (!hasHost) {
        headers.prepend("Host: 127.0.0.1:" + QByteArray::number(port));
    }

    QByteArray raw = method + " " + target + " HTTP/1.1\r\n";

    for (const QByteArray &header : headers) {
        raw += header + "\r\n";
    }

    if (!body.isEmpty()) {
        raw += "Content-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n";
    }

    socket.write(raw + "\r\n" + body);
    QByteArray response;

    while (socket.state() == QAbstractSocket::ConnectedState && socket.waitForReadyRead(5000)) {
        response += socket.readAll();
    }

    response += socket.readAll();
    const qsizetype split = response.indexOf("\r\n\r\n");
    HttpReply reply;
    reply.headers = response.left(split);
    reply.body = response.mid(split + 4);
    reply.status = response.mid(9, 3).toInt();
    return reply;
}

// A minimal client WebSocket: masked text frames out, unmasked frames in.
class TestWebSocket
{
public:
    bool open(quint16 port, const QByteArray &target, const QByteArray &origin, QByteArray *rejection = nullptr)
    {
        m_socket.connectToHost(QHostAddress::LocalHost, port);

        if (!m_socket.waitForConnected(3000)) {
            return false;
        }

        m_socket.write(
            "GET " + target + " HTTP/1.1\r\n"
            "Host: 127.0.0.1:" + QByteArray::number(port) + "\r\n"
            "Upgrade: websocket\r\nConnection: Upgrade\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n"
            + (origin.isEmpty() ? QByteArray() : "Origin: " + origin + "\r\n") + "\r\n"
        );

        while (!m_buffer.contains("\r\n\r\n") && m_socket.waitForReadyRead(5000)) {
            m_buffer += m_socket.readAll();
        }

        const qsizetype split = m_buffer.indexOf("\r\n\r\n");
        const QByteArray head = m_buffer.left(split);
        m_buffer.remove(0, split + 4);

        if (rejection) {
            *rejection = head.left(head.indexOf("\r\n"));
        }

        return head.startsWith("HTTP/1.1 101") && head.contains("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
    }

    void send(const QByteArray &text)
    {
        QByteArray frame;
        frame.append(char(0x81));

        if (text.size() < 126) {
            frame.append(char(0x80 | text.size()));
        } else {
            frame.append(char(0x80 | 126));
            char length[2];
            qToBigEndian(quint16(text.size()), length);
            frame.append(length, 2);
        }

        const quint32 mask = QRandomGenerator::global()->generate();
        char maskBytes[4];
        memcpy(maskBytes, &mask, 4);
        frame.append(maskBytes, 4);

        for (qsizetype index = 0; index < text.size(); ++index) {
            frame.append(char(text[index] ^ maskBytes[index % 4]));
        }

        m_socket.write(frame);
        m_socket.flush();
    }

    // Next text message, or a "close:<code>" marker.
    QByteArray next(int timeout = 5000)
    {
        QElapsedTimer timer;
        timer.start();

        while (true) {
            if (m_buffer.size() >= 2) {
                const auto *bytes = reinterpret_cast<const uchar *>(m_buffer.constData());
                const quint8 opcode = bytes[0] & 0x0F;
                qsizetype length = bytes[1] & 0x7F;
                qsizetype offset = 2;

                if (length == 126 && m_buffer.size() >= 4) {
                    length = qFromBigEndian<quint16>(bytes + 2);
                    offset = 4;
                } else if (length == 127 && m_buffer.size() >= 10) {
                    length = qsizetype(qFromBigEndian<quint64>(bytes + 2));
                    offset = 10;
                }

                if (length < 126 || offset > 2) {
                    if (m_buffer.size() >= offset + length) {
                        const QByteArray payload = m_buffer.mid(offset, length);
                        m_buffer.remove(0, offset + length);

                        if (opcode == 0x8) {
                            return "close:" + QByteArray::number(payload.size() >= 2 ? qFromBigEndian<quint16>(reinterpret_cast<const uchar *>(payload.constData())) : 1005);
                        }

                        return payload;
                    }
                }
            }

            const int remaining = timeout - int(timer.elapsed());

            if (remaining <= 0 || (!m_socket.waitForReadyRead(remaining) && m_socket.bytesAvailable() == 0)) {
                return {};
            }

            m_buffer += m_socket.readAll();
        }
    }

    // Collects messages until `predicate` holds for their concatenation.
    QByteArray until(const std::function<bool(const QByteArray &)> &predicate, int timeout = 8000)
    {
        QByteArray all;
        QElapsedTimer timer;
        timer.start();

        while (!predicate(all) && timer.elapsed() < timeout) {
            const QByteArray message = next(timeout - int(timer.elapsed()));

            if (message.isEmpty()) {
                break;
            }

            all += message + "\n";
        }

        return all;
    }

private:
    QTcpSocket m_socket;
    QByteArray m_buffer;
};
}

class CoreIdeTest final : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    QString m_databasePath;
    QString m_distribution;
    QString m_root;
    QString m_libraryId;
    QString m_collectionId = QStringLiteral("11111111-2222-4333-8444-555555555555");

    QByteArray origin(const IdeServer &server) const
    {
        return "http://127.0.0.1:" + QByteArray::number(server.port());
    }

private slots:
    void loginPathMergesWithoutDuplicates()
    {
        QVERIFY(LoginEnvironment::hasBarePath(QStringLiteral("/usr/bin:/bin:/usr/sbin:/sbin")));
        QVERIFY(!LoginEnvironment::hasBarePath(QStringLiteral("/opt/homebrew/bin:/usr/bin")));
        QCOMPARE(
            LoginEnvironment::mergedPath(QStringLiteral("/opt/homebrew/bin:/usr/bin"), QStringLiteral("/usr/bin:/bin")),
            QStringLiteral("/opt/homebrew/bin:/usr/bin:/bin")
        );
    }

    void initTestCase()
    {
        QVERIFY(m_directory.isValid());
        m_distribution = m_directory.filePath(QStringLiteral("dist"));
        QDir().mkpath(m_distribution + QStringLiteral("/assets"));
        QFile index(m_distribution + QStringLiteral("/index.html"));
        QVERIFY(index.open(QIODevice::WriteOnly));
        index.write("<!doctype html><title>IDE</title>");
        index.close();
        QFile script(m_distribution + QStringLiteral("/assets/app.js"));
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write("console.log('ide');");
        script.close();
        QFile secret(m_directory.filePath(QStringLiteral("secret.txt")));
        QVERIFY(secret.open(QIODevice::WriteOnly));
        secret.write("secret");
        secret.close();

        m_root = m_directory.filePath(QStringLiteral("library"));
        QDir().mkpath(m_root);
        QFile notes(m_root + QStringLiteral("/notes.md"));
        QVERIFY(notes.open(QIODevice::WriteOnly));
        notes.write("# Notes\n");
        notes.close();

        // A stand-in language server: echoes the framed stdio messages back.
        const QString bin = m_directory.filePath(QStringLiteral("bin"));
        QDir().mkpath(bin);
        QFile server(bin + QStringLiteral("/marksman"));
        QVERIFY(server.open(QIODevice::WriteOnly));
        server.write("#!/bin/sh\nexec cat\n");
        server.close();
        server.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        qputenv("PATH", (bin + QStringLiteral(":") + qEnvironmentVariable("PATH")).toUtf8());
        qputenv("SHELL", "/bin/bash");

        m_databasePath = m_directory.filePath(QStringLiteral("archivist.db"));
        CoreDatabase database;
        database.open(m_databasePath);
        database.runMigrations();
        m_libraryId = LibraryRepository::createLibrary(database, {{QStringLiteral("rootPath"), m_root}})
                          .value(QStringLiteral("library")).toMap().value(QStringLiteral("id")).toString();
        LibraryFiles::scan(database, m_libraryId);
    }

    void servesTheWebBundleLocallyOnly()
    {
        IdeServer server;
        QVERIFY(server.start(m_databasePath, m_distribution));
        QVERIFY(server.url().toString().endsWith(QStringLiteral("/ide/")));
        const quint16 port = server.port();
        HttpReply reply;

        auto fetch = [&](const QByteArray &method, const QByteArray &target, const QByteArray &body = {}, const QList<QByteArray> &headers = {}) {
            std::atomic_bool done = false;
            std::thread worker([&]() {
                reply = request(port, method, target, body, headers);
                done = true;
            });
            QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
            worker.join();
        };

        fetch("GET", "/ide?surface=terminal");
        QCOMPARE(reply.status, 301);
        QVERIFY(reply.headers.contains("Location: /ide/?surface=terminal"));

        fetch("GET", "/ide/?surface=editor");
        QCOMPARE(reply.status, 200);
        QCOMPARE(reply.body, QByteArray("<!doctype html><title>IDE</title>"));

        fetch("GET", "/ide/assets/app.js");
        QCOMPARE(reply.status, 200);
        QVERIFY(reply.headers.contains("Content-Type: text/javascript"));

        fetch("GET", "/ide/%2e%2e/secret.txt");
        QVERIFY(reply.status == 403 || reply.status == 404);
        QVERIFY(!reply.body.contains("secret"));

        fetch("GET", "/ide/", {}, {"Host: evil.example:80"});
        QCOMPARE(reply.status, 403);

        fetch("GET", "/ide/", {}, {"Origin: https://evil.example"});
        QCOMPARE(reply.status, 403);

        fetch("GET", "/api/libraries/" + m_libraryId.toUtf8() + "/files");
        QCOMPARE(reply.status, 200);
        const QVariantList files = reply.json().value(QStringLiteral("files")).toList();
        QCOMPARE(files.size(), 1);
        const QString fileId = files.first().toMap().value(QStringLiteral("id")).toString();
        QCOMPARE(files.first().toMap().value(QStringLiteral("relativePath")).toString(), QStringLiteral("notes.md"));

        fetch("GET", "/api/libraries/" + m_libraryId.toUtf8() + "/files/" + fileId.toUtf8() + "/content");
        QCOMPARE(reply.status, 200);
        QCOMPARE(reply.json().value(QStringLiteral("preview")).toMap().value(QStringLiteral("content")).toString(), QStringLiteral("# Notes\n"));

        fetch("GET", "/api/libraries/" + newId().toUtf8() + "/files");
        QCOMPARE(reply.status, 404);
        QCOMPARE(reply.json().value(QStringLiteral("error")).toMap().value(QStringLiteral("message")).toString(), QStringLiteral("Library not found."));

        fetch("POST", "/api/language-support/events", R"({"events":[{"kind":"initialized","provider":"lsp","serverId":"markdown"}]})");
        QCOMPARE(reply.status, 202);
        fetch("POST", "/api/language-support/events", R"({"events":[{"kind":"nope"}]})");
        QCOMPARE(reply.status, 400);
        fetch("GET", "/api/language-support/events?limit=5");
        QCOMPARE(reply.json().value(QStringLiteral("events")).toList().size(), 1);
    }

    // The live recorder's routes: the panel (its origin and key) and, only
    // while the person allows it, local agents (the key in agent-access.json).
    void observatoryLiveRequestsNeedTheRightKey()
    {
        QTemporaryDir records;
        qputenv("ARCHIVIST_OBSERVATORY_DIR", records.path().toUtf8());
        IdeServer server;
        QVERIFY(server.start(m_databasePath, m_distribution));
        server.setLiveHandler([](const QString &operation, const QByteArray &body, const QString &caller) {
            const QByteArray json = QJsonDocument(QJsonObject{
                {QStringLiteral("schema"), QStringLiteral("archivist.observatory.response")},
                {QStringLiteral("ok"), true},
                {QStringLiteral("result"), QJsonObject{
                    {QStringLiteral("operation"), operation},
                    {QStringLiteral("caller"), caller},
                    {QStringLiteral("body"), QString::fromUtf8(body)},
                }},
            }).toJson(QJsonDocument::Compact);
            return Observatory::Response{200, "application/json", json, {}};
        });
        const quint16 port = server.port();
        const QByteArray panelOrigin = "Origin: " + origin(server);
        const QByteArray panelKey = "X-Observatory-Token: " + server.observatoryControlToken().toUtf8();
        HttpReply reply;
        auto post = [&](const QByteArray &operation, const QList<QByteArray> &headers, const QByteArray &body = "{}", const QByteArray &method = "POST") {
            std::atomic_bool done = false;
            std::thread worker([&]() {
                reply = request(port, method, "/observatory/api/live/" + operation, body, headers);
                done = true;
            });
            QElapsedTimer waited;
            waited.start();
            while (!done && waited.elapsed() < 10000) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            }
            worker.join();
            return reply.json();
        };
        const auto code = [](const QVariantMap &json) {
            return json.value(QStringLiteral("error")).toMap().value(QStringLiteral("code")).toString();
        };

        QVariantMap json = post("status", {panelOrigin, panelKey});
        QCOMPARE(reply.status, 200);
        QCOMPARE(json.value(QStringLiteral("result")).toMap().value(QStringLiteral("caller")).toString(), QStringLiteral("panel"));
        QCOMPARE(json.value(QStringLiteral("result")).toMap().value(QStringLiteral("agentAccess")).toBool(), false);

        post("status", {panelOrigin, "X-Observatory-Token: wrong"});
        QCOMPARE(reply.status, 401);
        post("status", {"Origin: https://evil.example", panelKey});
        QCOMPARE(reply.status, 403);
        post("status", {panelOrigin, panelKey, "Sec-Fetch-Site: cross-site"});
        QCOMPARE(reply.status, 403);
        post("status", {panelOrigin, panelKey}, {}, "GET");
        QCOMPARE(reply.status, 405);
        post("..%2Fcontrol", {panelOrigin, panelKey});
        QCOMPARE(reply.status, 400);

        // Agents are refused until the person turns access on.
        json = post("start", {});
        QCOMPARE(reply.status, 403);
        QCOMPARE(code(json), QStringLiteral("agent-access-off"));
        QVERIFY(!QFile::exists(IdeServer::agentAccessFile()));

        post("agent-access", {panelOrigin, panelKey}, R"({"enabled":"yes"})");
        QCOMPARE(reply.status, 400);
        json = post("agent-access", {panelOrigin, panelKey}, R"({"enabled":true})");
        QCOMPARE(reply.status, 200);
        QCOMPARE(json.value(QStringLiteral("result")).toMap().value(QStringLiteral("agentAccess")).toBool(), true);
        QVERIFY(server.observatoryAgentAccess());
        QFile accessFile(IdeServer::agentAccessFile());
        QVERIFY(accessFile.exists());
        QCOMPARE(accessFile.permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther | QFileDevice::WriteGroup | QFileDevice::WriteOther),
                 QFileDevice::Permissions());
        QVERIFY(accessFile.open(QIODevice::ReadOnly));
        const QJsonObject access = QJsonDocument::fromJson(accessFile.readAll()).object();
        accessFile.close();
        QCOMPARE(access.value(QStringLiteral("endpoint")).toString(), QStringLiteral("http://127.0.0.1:%1/observatory/api/live/").arg(port));
        const QByteArray agentKey = "X-Observatory-Agent-Token: " + access.value(QStringLiteral("token")).toString().toUtf8();
        QVERIFY(access.value(QStringLiteral("token")).toString() != server.observatoryControlToken());

        json = post("start", {agentKey}, R"({"durationSeconds":30})");
        QCOMPARE(reply.status, 200);
        QCOMPARE(json.value(QStringLiteral("result")).toMap().value(QStringLiteral("caller")).toString(), QStringLiteral("agent"));
        QCOMPARE(json.value(QStringLiteral("result")).toMap().value(QStringLiteral("body")).toString(), QStringLiteral(R"({"durationSeconds":30})"));
        post("start", {"X-Observatory-Agent-Token: wrong"});
        QCOMPARE(reply.status, 401);
        post("start", {panelKey}); // the panel's key is not an agent key
        QCOMPARE(reply.status, 401);
        post("start", {agentKey, "Sec-Fetch-Site: cross-site"}); // a web page without Origin
        QCOMPARE(reply.status, 403);
        post("agent-access", {agentKey}, R"({"enabled":true})"); // agents can't grant themselves access
        QCOMPARE(reply.status, 403);
        post("agent-access", {panelOrigin, panelKey}, R"({"enabled":true})"); // a fresh key replaces the old one
        post("start", {agentKey});
        QCOMPARE(reply.status, 401);

        post("agent-access", {panelOrigin, panelKey}, R"({"enabled":false})");
        QCOMPARE(reply.status, 200);
        QVERIFY(!QFile::exists(IdeServer::agentAccessFile()));
        QCOMPARE(code(post("start", {agentKey})), QStringLiteral("agent-access-off"));

        // Stopping the server takes the file away with its address.
        QVERIFY(server.setObservatoryAgentAccess(true));
        QVERIFY(QFile::exists(IdeServer::agentAccessFile()));
        server.stop();
        QVERIFY(!QFile::exists(IdeServer::agentAccessFile()));
        QVERIFY(!server.observatoryAgentAccess());
        qunsetenv("ARCHIVIST_OBSERVATORY_DIR");
    }

    // Observatory work has its own threads and a cap: with both of them busy
    // (a slow export, say) the editor's Library requests still answer, extra
    // Observatory requests are refused with 503 instead of piling up, and the
    // live operations that would call back into the app are refused at once.
    void observatoryWorkDoesNotStarveLibraryRequests()
    {
        QTemporaryDir records;
        qputenv("ARCHIVIST_OBSERVATORY_DIR", records.path().toUtf8());
        IdeServer server;
        QVERIFY(server.start(m_databasePath, m_distribution));
        const quint16 port = server.port();
        const QByteArray panelOrigin = "Origin: " + origin(server);
        const QByteArray panelKey = "X-Observatory-Token: " + server.observatoryControlToken().toUtf8();

        QSemaphore release;
        std::atomic_int blocked = 0;
        for (int index = 0; index < 2; ++index) {
            server.observatoryPoolForTesting().start([&]() {
                ++blocked;
                release.acquire();
            });
        }
        QTRY_COMPARE_WITH_TIMEOUT(blocked.load(), 2, 5000);

        const auto fetch = [&](const QByteArray &method, const QByteArray &target, const QList<QByteArray> &headers = {}, const QByteArray &body = {}) {
            HttpReply reply;
            std::atomic_bool done = false;
            std::thread worker([&]() {
                reply = request(port, method, target, body, headers);
                done = true;
            });
            QElapsedTimer waited;
            waited.start();
            while (!done && waited.elapsed() < 10000) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            }
            worker.join();
            return reply;
        };

        QElapsedTimer library;
        library.start();
        QCOMPARE(fetch("GET", "/api/libraries/" + m_libraryId.toUtf8() + "/files").status, 200);
        QVERIFY2(library.elapsed() < 3000, "a Library request waited behind Observatory work");

        // Fill the queue up to the cap; these wait for the blocked threads.
        std::vector<std::thread> queued;
        std::vector<int> statuses(IdeServer::kMaxObservatoryRequests, 0);
        std::atomic_int answered = 0;
        for (int index = 0; index < IdeServer::kMaxObservatoryRequests; ++index) {
            queued.emplace_back([&, index]() {
                statuses[index] = request(port, "GET", "/observatory/api/runs", {}, {}).status;
                ++answered;
            });
        }
        QTRY_COMPARE_WITH_TIMEOUT(server.observatoryRequestsInFlight(), IdeServer::kMaxObservatoryRequests, 5000);

        HttpReply refused = fetch("GET", "/observatory/api/runs");
        QCOMPARE(refused.status, 503);
        refused = fetch("POST", "/observatory/api/control/observatory.tests.status", {panelOrigin, panelKey, "Content-Type: application/json"}, "{}");
        QCOMPARE(refused.status, 503);
        QCOMPARE(refused.json().value(QStringLiteral("error")).toMap().value(QStringLiteral("code")).toString(), QStringLiteral("unavailable"));

        // Refused before any thread or script: they belong to api/live.
        for (const QByteArray &operation : {QByteArray("wait"), QByteArray("start"), QByteArray("status")}) {
            const HttpReply live = fetch("POST", "/observatory/api/control/observatory.live." + operation, {panelOrigin, panelKey, "Content-Type: application/json"}, "{}");
            QCOMPARE(live.status, 400);
            QVERIFY(live.body.contains("/observatory/api/live/"));
        }

        release.release(2);
        QTRY_COMPARE_WITH_TIMEOUT(answered.load(), IdeServer::kMaxObservatoryRequests, 10000);
        for (std::thread &thread : queued) {
            thread.join();
        }
        for (const int status : statuses) {
            QVERIFY2(status != 503 && status != 0, "a queued Observatory request was dropped");
        }
        QTRY_COMPARE_WITH_TIMEOUT(server.observatoryRequestsInFlight(), 0, 5000);
        server.stop();
        qunsetenv("ARCHIVIST_OBSERVATORY_DIR");
    }

    void runsATerminalOverAWebSocket()
    {
        IdeServer server;
        QVERIFY(server.start(m_databasePath, m_distribution));
        const quint16 port = server.port();
        const QByteArray target = "/api/terminals/socket?sessionId=primary&collectionId=" + m_collectionId.toUtf8()
            + "&libraryId=" + m_libraryId.toUtf8() + "&cols=100&rows=30";
        QByteArray transcript;
        QByteArray refused;
        QByteArray badRequest;
        std::atomic_bool done = false;

        std::thread client([&]() {
            TestWebSocket noOrigin;
            noOrigin.open(port, target, {}, &refused);

            TestWebSocket invalid;
            invalid.open(port, "/api/terminals/socket?sessionId=bad%20id&collectionId=x&libraryId=y", "http://127.0.0.1:" + QByteArray::number(port), &badRequest);

            TestWebSocket socket;

            if (socket.open(port, target, "http://127.0.0.1:" + QByteArray::number(port))) {
                transcript += socket.until([](const QByteArray &all) { return all.contains("\"ready\""); });
                socket.send(R"({"type":"ping","sentAt":42})");
                transcript += socket.until([](const QByteArray &all) { return all.contains("pong"); });
                socket.send(R"({"type":"input","data":"echo archivist-$((40+2))\n"})");
                transcript += socket.until([](const QByteArray &all) { return all.contains("archivist-42"); });
                socket.send(R"({"type":"kill"})");
                transcript += socket.until([](const QByteArray &all) { return all.contains("\"exit\""); });
            }

            done = true;
        });

        QTRY_VERIFY_WITH_TIMEOUT(done, 30000);
        client.join();

        QVERIFY2(refused.startsWith("HTTP/1.1 403"), refused.constData());
        QVERIFY2(badRequest.startsWith("HTTP/1.1 400"), badRequest.constData());
        QVERIFY2(transcript.contains("\"ready\""), transcript.constData());
        QVERIFY2(transcript.contains("\"sentAt\":42"), transcript.constData());
        QVERIFY2(transcript.contains("archivist-42"), transcript.constData());
        QVERIFY2(transcript.contains("\"exit\""), transcript.constData());
    }

    void bridgesALanguageServer()
    {
        IdeServer server;
        QVERIFY(server.start(m_databasePath, m_distribution));
        const quint16 port = server.port();
        HttpReply created;
        QByteArray echoed;
        QByteArray reused;
        std::atomic_bool done = false;

        std::thread client([&]() {
            created = request(
                port,
                "POST",
                "/api/language-support/sessions",
                QJsonDocument(QJsonObject{{QStringLiteral("libraryId"), m_libraryId}, {QStringLiteral("serverId"), QStringLiteral("markdown")}}).toJson(QJsonDocument::Compact),
                {"Origin: http://127.0.0.1:" + QByteArray::number(port)}
            );
            const QUrl socketUrl(created.json().value(QStringLiteral("session")).toMap().value(QStringLiteral("socketUrl")).toString());
            const QByteArray target = socketUrl.path(QUrl::FullyEncoded).toUtf8() + "?" + socketUrl.query(QUrl::FullyEncoded).toUtf8();
            TestWebSocket socket;

            if (socket.open(port, target, "http://127.0.0.1:" + QByteArray::number(port))) {
                socket.send(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})");
                echoed = socket.next(8000);
            }

            TestWebSocket again;
            again.open(port, target, "http://127.0.0.1:" + QByteArray::number(port), &reused);
            done = true;
        });

        QTRY_VERIFY_WITH_TIMEOUT(done, 30000);
        client.join();

        QCOMPARE(created.status, 201);
        QCOMPARE(created.json().value(QStringLiteral("session")).toMap().value(QStringLiteral("serverId")).toString(), QStringLiteral("markdown"));
        QCOMPARE(echoed, QByteArray(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})"));
        QVERIFY2(reused.startsWith("HTTP/1.1 403"), reused.constData());
    }
};

QTEST_GUILESS_MAIN(CoreIdeTest)
#include "core_ide_test.moc"

#include "ide_server.h"

#include "Core/Domains/Ide/ide_http_error.h"
#include "Core/Domains/Ide/language_support.h"
#include "Core/Domains/Ide/terminal_sessions.h"
#include "Core/Domains/Ide/web_socket_connection.h"
#include "Core/Domains/Libraries/library_files.h"
#include "Core/Domains/Libraries/library_repository.h"
#include "Core/Domains/Observatory/observatory_records.h"
#include "Core/Support/json_values.h"
#include "Core/core_error.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QPointer>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrlQuery>
#include <cmath>
#include <functional>
#include <memory>

namespace
{
constexpr qsizetype maximumHeaderBytes = 64 * 1024;
constexpr qsizetype maximumBodyBytes = 1024 * 1024;
constexpr qint64 terminalMaximumPayload = 128 * 1024;
constexpr qint64 languageSupportMaximumPayload = 4 * 1024 * 1024;

QByteArray reasonPhrase(int status)
{
    switch (status) {
    case 200: return "OK";
    case 201: return "Created";
    case 202: return "Accepted";
    case 301: return "Moved Permanently";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 409: return "Conflict";
    case 413: return "Payload Too Large";
    case 415: return "Unsupported Media Type";
    case 429: return "Too Many Requests";
    case 503: return "Service Unavailable";
    default: return status >= 500 ? "Internal Server Error" : "Error";
    }
}

QByteArray contentType(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    static const QHash<QString, QByteArray> types{
        {QStringLiteral("html"), "text/html; charset=utf-8"},
        {QStringLiteral("js"), "text/javascript; charset=utf-8"},
        {QStringLiteral("mjs"), "text/javascript; charset=utf-8"},
        {QStringLiteral("css"), "text/css; charset=utf-8"},
        {QStringLiteral("json"), "application/json; charset=utf-8"},
        {QStringLiteral("map"), "application/json; charset=utf-8"},
        {QStringLiteral("wasm"), "application/wasm"},
        {QStringLiteral("svg"), "image/svg+xml"},
        {QStringLiteral("png"), "image/png"},
        {QStringLiteral("ico"), "image/x-icon"},
        {QStringLiteral("ttf"), "font/ttf"},
        {QStringLiteral("woff"), "font/woff"},
        {QStringLiteral("woff2"), "font/woff2"},
        {QStringLiteral("txt"), "text/plain; charset=utf-8"},
    };
    return types.value(suffix, "application/octet-stream");
}

bool isLoopback(const QHostAddress &address)
{
    return address.isLoopback()
        || address == QHostAddress(QStringLiteral("::ffff:127.0.0.1"));
}

struct Request
{
    QByteArray method;
    QString path;
    QUrlQuery query;
    QString rawTarget;
    QHash<QByteArray, QByteArray> headers;
    QByteArray body;
};
}

// One HTTP request on one loopback connection. Responses close the
// connection; WebSocket upgrades hand the socket to a WebSocketConnection.
class IdeHttpConnection final : public QObject
{
public:
    IdeHttpConnection(IdeServer *server, QTcpSocket *socket)
        : QObject(server)
        , m_server(server)
        , m_socket(socket)
    {
        socket->setParent(this);
        connect(socket, &QTcpSocket::readyRead, this, &IdeHttpConnection::readAvailable);
        connect(socket, &QTcpSocket::disconnected, this, [this]() {
            if (!m_upgraded) {
                deleteLater();
            }
        });
    }

private:
    void readAvailable()
    {
        if (m_handled) {
            m_socket->readAll();
            return;
        }

        m_buffer += m_socket->readAll();
        const qsizetype headerEnd = m_buffer.indexOf("\r\n\r\n");

        if (headerEnd < 0) {
            if (m_buffer.size() > maximumHeaderBytes) {
                m_handled = true;
                respondText(413, "Payload Too Large");
            }

            return;
        }

        if (m_request.method.isEmpty() && !parseHead(m_buffer.left(headerEnd))) {
            m_handled = true;
            respondText(400, "Bad Request");
            return;
        }

        const qsizetype length = m_request.headers.value("content-length", "0").toLongLong();

        if (length < 0 || length > maximumBodyBytes) {
            m_handled = true;
            respondText(413, "Payload Too Large");
            return;
        }

        if (m_buffer.size() < headerEnd + 4 + length) {
            return;
        }

        m_request.body = m_buffer.mid(headerEnd + 4, length);
        m_extra = m_buffer.mid(headerEnd + 4 + length);
        m_handled = true;
        handle();
    }

    bool parseHead(const QByteArray &head)
    {
        const QList<QByteArray> lines = head.split('\n');
        const QList<QByteArray> requestLine = lines.value(0).trimmed().split(' ');

        if (requestLine.size() != 3 || !requestLine.at(2).startsWith("HTTP/1.")) {
            return false;
        }

        m_request.method = requestLine.at(0);
        m_request.rawTarget = QString::fromLatin1(requestLine.at(1));
        const QUrl target(QStringLiteral("http://localhost") + m_request.rawTarget);

        if (!target.isValid() || !m_request.rawTarget.startsWith(QLatin1Char('/'))) {
            return false;
        }

        m_request.path = target.path(QUrl::FullyDecoded);
        m_request.query = QUrlQuery(target);

        for (qsizetype index = 1; index < lines.size(); ++index) {
            const QByteArray line = lines.at(index).trimmed();
            const qsizetype colon = line.indexOf(':');

            if (colon > 0) {
                m_request.headers.insert(line.left(colon).trimmed().toLower(), line.mid(colon + 1).trimmed());
            }
        }

        return true;
    }

    [[nodiscard]] QStringList allowedOrigins() const
    {
        const QString port = QString::number(m_server->port());
        return {QStringLiteral("http://127.0.0.1:") + port, QStringLiteral("http://localhost:") + port};
    }

    void handle()
    {
        const QString port = QString::number(m_server->port());
        const QString host = QString::fromLatin1(m_request.headers.value("host")).toLower();
        const bool hasOrigin = m_request.headers.contains("origin");
        const QString origin = QString::fromLatin1(m_request.headers.value("origin"));

        if (m_request.headers.value("upgrade").toLower() == "websocket") {
            handleUpgrade(hasOrigin, origin);
            return;
        }

        if (host != QStringLiteral("127.0.0.1:") + port && host != QStringLiteral("localhost:") + port && host != QStringLiteral("[::1]:") + port) {
            respondJson(403, errorBody(QStringLiteral("Archivist only accepts requests from this computer.")));
            return;
        }

        if (hasOrigin && !allowedOrigins().contains(origin)) {
            respondJson(403, errorBody(QStringLiteral("Archivist does not accept requests from web pages.")));
            return;
        }

        try {
            route();
        } catch (const IdeHttpError &error) {
            respondJson(error.status(), errorBody(error.message(), error.details()));
        } catch (const CoreError &error) {
            respondJson(400, errorBody(error.message(), error.details().isEmpty() ? QVariant() : QVariant(error.details())));
        } catch (const std::exception &) {
            respondJson(500, errorBody(QStringLiteral("An unexpected server error occurred.")));
        }
    }

    static QVariantMap errorBody(const QString &message, const QVariant &details = {})
    {
        QVariantMap error{{QStringLiteral("message"), message}};

        if (details.isValid()) {
            error.insert(QStringLiteral("details"), details);
        }

        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
    }

    QVariant jsonBody() const
    {
        if (m_request.body.trimmed().isEmpty()) {
            return QVariantMap();
        }

        bool ok = false;
        const QVariant value = JsonValues::parse(QString::fromUtf8(m_request.body), &ok);

        if (!ok) {
            throw IdeHttpError(400, QStringLiteral("The request body is not valid JSON."));
        }

        return value;
    }

    void route()
    {
        const QString &path = m_request.path;
        const bool get = m_request.method == "GET" || m_request.method == "HEAD";
        const bool post = m_request.method == "POST";

        if (path == QStringLiteral("/ide")) {
            const qsizetype queryStart = m_request.rawTarget.indexOf(QLatin1Char('?'));
            respondRedirect(QStringLiteral("/ide/") + (queryStart >= 0 ? m_request.rawTarget.mid(queryStart) : QString()));
            return;
        }

        if (path.startsWith(QStringLiteral("/ide/")) && get) {
            serveStatic(path.mid(5));
            return;
        }

        // The Observatory dashboard: a read-only page over local build and
        // UI timing records, shown in its own panel.
        if (path == QStringLiteral("/observatory") && get) {
            respondRedirect(QStringLiteral("/observatory/"));
            return;
        }

        if (path.startsWith(QStringLiteral("/observatory/"))) {
            if (!get) {
                respondJson(405, errorBody(QStringLiteral("The Observatory dashboard is read-only.")));
                return;
            }

            serveObservatory(path.mid(13));
            return;
        }

        if (path == QStringLiteral("/api/health") && get) {
            respondJson(200, {{QStringLiteral("ok"), true}, {QStringLiteral("app"), QStringLiteral("Archivist IDE host")}});
            return;
        }

        static const QRegularExpression filesPattern(QStringLiteral("^/api/libraries/([^/]+)/files$"));
        static const QRegularExpression contentPattern(QStringLiteral("^/api/libraries/([^/]+)/files/([^/]+)/content$"));

        if (const auto match = filesPattern.match(path); match.hasMatch() && get) {
            const QString libraryId = match.captured(1);
            runInPool([libraryId](CoreDatabase &database) {
                if (!LibraryRepository::libraryById(database, libraryId)) {
                    throw IdeHttpError(404, QStringLiteral("Library not found."));
                }

                QVariantMap body = LibraryFiles::catalogWithDirectories(database, libraryId);
                body.insert(QStringLiteral("ok"), true);
                return body;
            });
            return;
        }

        if (const auto match = contentPattern.match(path); match.hasMatch() && get) {
            const QString libraryId = match.captured(1);
            const QString fileId = match.captured(2);
            runInPool([libraryId, fileId](CoreDatabase &database) {
                return QVariantMap{
                    {QStringLiteral("ok"), true},
                    {QStringLiteral("preview"), LibraryFiles::readText(database, libraryId, fileId)},
                };
            });
            return;
        }

        LanguageSupportService &languageSupport = *m_server->m_languageSupport;

        if (path == QStringLiteral("/api/language-support/config") && get) {
            QVariantMap query;

            for (const auto &[key, value] : m_request.query.queryItems(QUrl::FullyDecoded)) {
                query.insert(key, value);
            }

            respondJson(200, languageSupport.config(m_server->m_database, query));
            return;
        }

        if (path == QStringLiteral("/api/language-support/events") && get) {
            bool ok = false;
            const double requested = m_request.query.queryItemValue(QStringLiteral("limit")).toDouble(&ok);
            const int limit = !m_request.query.hasQueryItem(QStringLiteral("limit")) ? 250 : ok ? static_cast<int>(std::floor(requested)) : 250;
            respondJson(200, {{QStringLiteral("ok"), true}, {QStringLiteral("events"), languageSupport.events(limit)}});
            return;
        }

        if (path == QStringLiteral("/api/language-support/events") && post) {
            languageSupport.appendEvents(jsonBody());
            respondJson(202, {{QStringLiteral("ok"), true}});
            return;
        }

        if (path == QStringLiteral("/api/language-support/sessions") && get) {
            respondJson(200, {{QStringLiteral("ok"), true}, {QStringLiteral("sessions"), languageSupport.listSessions()}});
            return;
        }

        if (path == QStringLiteral("/api/language-support/sessions") && post) {
            const QVariant body = jsonBody();
            const QVariantMap session = languageSupport.createSession(m_server->m_database, body.toMap());
            respondJson(201, {{QStringLiteral("ok"), true}, {QStringLiteral("session"), session}});
            return;
        }

        respondJson(404, errorBody(QStringLiteral("Not Found")));
    }

    void runInPool(std::function<QVariantMap(CoreDatabase &)> work)
    {
        QPointer<IdeHttpConnection> self(this);
        const QString databasePath = m_server->m_databasePath;

        m_server->m_pool.start([self, databasePath, work = std::move(work)]() {
            thread_local std::unique_ptr<CoreDatabase> connection;
            int status = 200;
            QVariantMap body;

            try {
                if (!connection || !connection->isOpen() || connection->path() != databasePath) {
                    connection = std::make_unique<CoreDatabase>();
                    connection->open(databasePath);
                }

                body = work(*connection);
            } catch (const IdeHttpError &error) {
                status = error.status();
                body = errorBody(error.message(), error.details());
            } catch (const CoreError &error) {
                status = 400;
                body = errorBody(error.message(), error.details().isEmpty() ? QVariant() : QVariant(error.details()));
            } catch (const std::exception &) {
                status = 500;
                body = errorBody(QStringLiteral("An unexpected server error occurred."));
            }

            QMetaObject::invokeMethod(
                qApp,
                [self, status, body]() {
                    if (self) {
                        self->respondJson(status, body);
                    }
                },
                Qt::QueuedConnection
            );
        });
    }

    // Reads records off the main thread: a long history is many small files.
    void serveObservatory(const QString &relativePath)
    {
        QPointer<IdeHttpConnection> self(this);
        const bool head = m_request.method == "HEAD";

        m_server->m_pool.start([self, relativePath, head]() {
            Observatory::Response response;

            try {
                response = Observatory::handle(Observatory::defaultRoot(), Observatory::defaultPagePath(), relativePath);
            } catch (const std::exception &) {
                response = {500, "application/json; charset=utf-8", "{\"error\":\"An unexpected server error occurred.\"}", Observatory::securityHeaders()};
            }

            QMetaObject::invokeMethod(
                qApp,
                [self, response, head]() {
                    if (self) {
                        self->respond(response.status, response.contentType, head ? QByteArray() : response.body, response.body.size(), response.headers);
                    }
                },
                Qt::QueuedConnection
            );
        });
    }

    void serveStatic(const QString &relativePath)
    {
        const QString root = QFileInfo(m_server->m_distributionPath).canonicalFilePath();

        if (root.isEmpty()) {
            respondText(404, "The IDE bundle has not been built. Run npm run build:ide.");
            return;
        }

        QString target = relativePath.isEmpty() || relativePath.endsWith(QLatin1Char('/'))
            ? relativePath + QStringLiteral("index.html")
            : relativePath;

        if (target.contains(QChar(0)) || target.split(QLatin1Char('/')).contains(QStringLiteral(".."))) {
            respondText(403, "Forbidden");
            return;
        }

        QString resolved = QFileInfo(QDir(root).filePath(target)).canonicalFilePath();

        if (!resolved.isEmpty() && QFileInfo(resolved).isDir()) {
            resolved = QFileInfo(QDir(resolved).filePath(QStringLiteral("index.html"))).canonicalFilePath();
        }

        if (resolved.isEmpty() || !(resolved == root || resolved.startsWith(root + QLatin1Char('/'))) || !QFileInfo(resolved).isFile()) {
            respondText(404, "Not Found");
            return;
        }

        QFile file(resolved);

        if (!file.open(QIODevice::ReadOnly)) {
            respondText(404, "Not Found");
            return;
        }

        respond(200, contentType(resolved), m_request.method == "HEAD" ? QByteArray() : file.readAll(), file.size(), {"Cache-Control: no-cache"});
    }

    void handleUpgrade(bool hasOrigin, const QString &origin)
    {
        const QString &path = m_request.path;
        const bool terminal = path == QStringLiteral("/api/terminals/socket");
        const bool languageSupport = path == QStringLiteral("/language-support");

        if (!terminal && !languageSupport) {
            rejectUpgrade(404, "Not Found");
            return;
        }

        if (!isLoopback(m_socket->peerAddress())) {
            rejectUpgrade(403, "Forbidden");
            return;
        }

        if (!hasOrigin || !allowedOrigins().contains(origin)) {
            rejectUpgrade(403, "Forbidden");
            return;
        }

        const QByteArray key = m_request.headers.value("sec-websocket-key");

        if (key.isEmpty() || m_request.headers.value("sec-websocket-version") != "13") {
            rejectUpgrade(400, "Bad Request");
            return;
        }

        if (terminal) {
            upgradeTerminal(key);
        } else {
            upgradeLanguageSupport(key);
        }
    }

    WebSocketConnection *acceptUpgrade(const QByteArray &key, qint64 maximumPayload)
    {
        m_socket->write(
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: " + WebSocketConnection::acceptKey(key) + "\r\n\r\n"
        );
        m_upgraded = true;
        QTcpSocket *socket = m_socket;
        socket->disconnect(this);
        auto *connection = new WebSocketConnection(socket, maximumPayload, m_server);
        m_socket = nullptr;

        if (!m_extra.isEmpty()) {
            connection->receive(m_extra);
        }

        deleteLater();
        return connection;
    }

    void upgradeTerminal(const QByteArray &key)
    {
        IdeTerminals::Context context;
        TerminalSession *session = nullptr;

        try {
            context = IdeTerminals::parseContext(m_request.query);
        } catch (const IdeHttpError &error) {
            rejectUpgrade(error.status(), reasonPhrase(error.status()));
            return;
        }

        try {
            session = m_server->m_terminals->getOrCreate(m_server->m_database, context);
        } catch (const IdeHttpError &error) {
            rejectUpgrade(error.status(), error.message().toUtf8());
            return;
        } catch (const std::exception &) {
            rejectUpgrade(500, "Terminal unavailable");
            return;
        }

        WebSocketConnection *socket = acceptUpgrade(key, terminalMaximumPayload);
        QPointer<TerminalSession> guarded(session);
        session->attach(socket);

        connect(socket, &WebSocketConnection::binaryMessageReceived, socket, [socket]() {
            socket->close(1003, QStringLiteral("Binary terminal messages are not supported."));
        });
        connect(socket, &WebSocketConnection::textMessageReceived, socket, [socket, guarded](const QByteArray &data) {
            bool ok = false;
            const QVariant parsed = JsonValues::parse(QString::fromUtf8(data), &ok);

            if (!ok) {
                socket->close(1007, QStringLiteral("Invalid terminal message."));
                return;
            }

            const QVariantMap message = parsed.toMap();
            const QString type = message.value(QStringLiteral("type")).toString();
            const auto invalid = [socket]() { socket->close(1008, QStringLiteral("Invalid terminal message.")); };

            if (parsed.typeId() != QMetaType::QVariantMap) {
                invalid();
                return;
            }

            if (type == QStringLiteral("input")) {
                const QVariant value = message.value(QStringLiteral("data"));

                if (value.typeId() != QMetaType::QString || value.toString().size() > 65536) {
                    invalid();
                    return;
                }

                if (guarded) {
                    guarded->write(value.toString());
                }
            } else if (type == QStringLiteral("resize")) {
                bool colsOk = false;
                bool rowsOk = false;
                const double cols = message.value(QStringLiteral("cols")).toDouble(&colsOk);
                const double rows = message.value(QStringLiteral("rows")).toDouble(&rowsOk);

                if (!colsOk || !rowsOk || cols != std::floor(cols) || rows != std::floor(rows)
                    || cols < 2 || cols > 500 || rows < 2 || rows > 500) {
                    invalid();
                    return;
                }

                if (guarded) {
                    guarded->resize(static_cast<int>(cols), static_cast<int>(rows));
                }
            } else if (type == QStringLiteral("kill")) {
                if (guarded) {
                    guarded->kill();
                }
            } else if (type == QStringLiteral("ping")) {
                const QVariant sentAt = message.value(QStringLiteral("sentAt"));
                const int sentAtType = sentAt.typeId();

                if ((sentAtType != QMetaType::Double && sentAtType != QMetaType::Int && sentAtType != QMetaType::LongLong)
                    || !std::isfinite(sentAt.toDouble())) {
                    invalid();
                    return;
                }

                socket->sendText(JsonValues::toJson(QVariantMap{{QStringLiteral("type"), QStringLiteral("pong")}, {QStringLiteral("sentAt"), sentAt}}));
            } else {
                invalid();
            }
        });
        connect(socket, &WebSocketConnection::closed, socket, [socket, guarded]() {
            if (guarded) {
                guarded->detach(socket);
            }

            socket->deleteLater();
        });
    }

    void upgradeLanguageSupport(const QByteArray &key)
    {
        QString pending;

        try {
            pending = m_server->m_languageSupport->consumePendingSession(
                m_request.query.queryItemValue(QStringLiteral("sessionId"), QUrl::FullyDecoded),
                m_request.query.queryItemValue(QStringLiteral("token"), QUrl::FullyDecoded)
            );
        } catch (const IdeHttpError &error) {
            rejectUpgrade(error.status(), error.message().toUtf8());
            return;
        }

        m_server->m_languageSupport->launch(pending, acceptUpgrade(key, languageSupportMaximumPayload));
    }

    void rejectUpgrade(int status, const QByteArray &message)
    {
        const QByteArray body = message + "\n";
        m_socket->write(
            "HTTP/1.1 " + QByteArray::number(status) + " " + message + "\r\n"
            "Connection: close\r\n"
            "Content-Type: text/plain; charset=utf-8\r\n"
            "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body
        );
        m_socket->disconnectFromHost();
    }

    void respondRedirect(const QString &location)
    {
        respond(301, "text/plain; charset=utf-8", "Redirecting\n", -1, {"Location: " + location.toUtf8()});
    }

    void respondText(int status, const QByteArray &message)
    {
        respond(status, "text/plain; charset=utf-8", message + "\n");
    }

    void respondJson(int status, const QVariantMap &body)
    {
        respond(status, "application/json; charset=utf-8", JsonValues::toJson(body));
    }

    void respond(int status, const QByteArray &type, const QByteArray &body, qint64 length = -1, const QList<QByteArray> &headers = {})
    {
        if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) {
            deleteLater();
            return;
        }

        QByteArray head = "HTTP/1.1 " + QByteArray::number(status) + " " + reasonPhrase(status) + "\r\n"
            "Content-Type: " + type + "\r\n"
            "Content-Length: " + QByteArray::number(length >= 0 ? length : body.size()) + "\r\n"
            "Connection: close\r\n";

        for (const QByteArray &header : headers) {
            head += header + "\r\n";
        }

        m_socket->write(head + "\r\n" + body);
        m_socket->disconnectFromHost();
    }

    IdeServer *m_server;
    QTcpSocket *m_socket;
    QByteArray m_buffer;
    QByteArray m_extra;
    Request m_request;
    bool m_handled = false;
    bool m_upgraded = false;
};

IdeServer::IdeServer(QObject *parent)
    : QObject(parent)
{
    m_pool.setMaxThreadCount(2);
    m_pool.setExpiryTimeout(30000);
}

IdeServer::~IdeServer()
{
    stop();
}

QString IdeServer::defaultDistributionPath()
{
    const QString configured = qEnvironmentVariable("ARCHIVIST_IDE_DIST").trimmed();

    if (!configured.isEmpty()) {
        return configured;
    }

    const QString bundled = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../Resources/ide"));

    if (QFileInfo::exists(QDir(bundled).filePath(QStringLiteral("index.html")))) {
        return QDir::cleanPath(bundled);
    }

#ifdef ARCHIVIST_REPOSITORY_ROOT
    return QDir(QStringLiteral(ARCHIVIST_REPOSITORY_ROOT)).filePath(QStringLiteral("frontend/dist/ide"));
#else
    return bundled;
#endif
}

quint16 IdeServer::defaultPort()
{
    bool ok = false;
    const uint configured = qEnvironmentVariable("ARCHIVIST_IDE_PORT").toUInt(&ok);
    return ok && configured > 0 && configured < 65536 ? static_cast<quint16>(configured) : quint16(3334);
}

bool IdeServer::start(const QString &databasePath, const QString &distributionPath, quint16 port)
{
    stop();

    try {
        m_database.open(databasePath);
    } catch (const CoreError &error) {
        qWarning().noquote() << "[IDE] Could not open the database:" << error.message();
        return false;
    }

    m_databasePath = m_database.path();
    m_distributionPath = distributionPath.isEmpty() ? defaultDistributionPath() : distributionPath;
    // Node kept terminal state in backend/data/terminals, beside the database.
    m_terminals = new TerminalSessionManager(QDir(QFileInfo(m_databasePath).absolutePath()).filePath(QStringLiteral("terminals")), this);
    m_languageSupport = new LanguageSupportService(this);
    m_server = new QTcpServer(this);

    const quint16 preferred = port != 0 ? port : defaultPort();
    bool listening = m_server->listen(QHostAddress::LocalHost, preferred);

    if (!listening && port == 0) {
        qWarning().noquote() << "[IDE] Port" << preferred << "is taken; using a free port. Editor and terminal positions will not be restored.";
        listening = m_server->listen(QHostAddress::LocalHost, 0);
    }

    if (!listening) {
        qWarning().noquote() << "[IDE] Could not listen on the loopback interface:" << m_server->errorString();
        stop();
        return false;
    }

    m_languageSupport->setPort(m_server->serverPort());
    connect(m_server, &QTcpServer::newConnection, this, [this]() {
        while (QTcpSocket *socket = m_server->nextPendingConnection()) {
            new IdeHttpConnection(this, socket);
        }
    });

    qInfo().noquote() << "[IDE] Host listening at" << url().toString();
    emit stateChanged();
    return true;
}

void IdeServer::stop()
{
    const bool wasReady = ready();

    if (m_terminals) {
        m_terminals->shutdown();
    }

    if (m_languageSupport) {
        m_languageSupport->close();
    }

    if (m_server) {
        m_server->close();
    }

    m_pool.waitForDone();

    const QObjectList owned = children();

    for (QObject *child : owned) {
        if (child != m_server && child != m_terminals && child != m_languageSupport) {
            delete child;
        }
    }

    delete m_server;
    delete m_terminals;
    delete m_languageSupport;
    m_server = nullptr;
    m_terminals = nullptr;
    m_languageSupport = nullptr;
    m_database.close();

    if (wasReady) {
        emit stateChanged();
    }
}

QUrl IdeServer::url() const
{
    return ready() ? QUrl(QStringLiteral("http://127.0.0.1:%1/ide/").arg(m_server->serverPort())) : QUrl();
}

QUrl IdeServer::observatoryUrl() const
{
    return ready() ? QUrl(QStringLiteral("http://127.0.0.1:%1/observatory/").arg(m_server->serverPort())) : QUrl();
}

bool IdeServer::ready() const
{
    return m_server && m_server->isListening();
}

quint16 IdeServer::port() const
{
    return ready() ? m_server->serverPort() : 0;
}

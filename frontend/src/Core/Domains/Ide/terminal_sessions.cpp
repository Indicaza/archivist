#include "terminal_sessions.h"

#include "Core/Domains/Ide/ide_http_error.h"
#include "Core/Domains/Ide/web_socket_connection.h"
#include "Core/Domains/Libraries/library_repository.h"
#include "Core/Support/js_compat.h"
#include "Core/Support/json_values.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>

#include <cerrno>
#include <cmath>
#include <csignal>
#include <fcntl.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#if defined(Q_OS_MACOS)
#include <util.h>
#else
#include <pty.h>
#endif



namespace
{
constexpr int maximumTerminalSessions = 16;
constexpr qsizetype maximumScrollbackCharacters = 1'500'000;

QString identity(const IdeTerminals::Context &context)
{
    return QStringList{context.collectionId, context.libraryId, context.sessionId}.join(QLatin1Char(':'));
}

QString hashFor(const IdeTerminals::Context &context)
{
    return JsonValues::sha256Hex(identity(context).toUtf8());
}

QString quoteForShell(const QString &value)
{
    QString escaped = value;
    escaped.replace(QStringLiteral("'"), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}

void writePrivateFile(const QString &path, const QString &content)
{
    QFile file(path);

    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(content.toUtf8());
        file.close();
        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
}

QString sourceHomeZshFile(const QString &fileName)
{
    return QStringList{
        QStringLiteral("typeset __archivist_zdotdir=\"$ZDOTDIR\""),
        QStringLiteral("export ZDOTDIR=\"$HOME\""),
        QStringLiteral("[[ -r \"$HOME/%1\" ]] && source \"$HOME/%1\"").arg(fileName),
        QStringLiteral("export ZDOTDIR=\"$__archivist_zdotdir\""),
        QStringLiteral("unset __archivist_zdotdir"),
        QString(),
    }.join(QLatin1Char('\n'));
}

QMap<QString, QString> baseEnvironment()
{
    QMap<QString, QString> environment;
    const QProcessEnvironment system = QProcessEnvironment::systemEnvironment();

    for (const QString &key : system.keys()) {
        environment.insert(key, system.value(key));
    }

    environment.insert(QStringLiteral("TERM"), QStringLiteral("xterm-256color"));
    environment.insert(QStringLiteral("COLORTERM"), QStringLiteral("truecolor"));
    environment.insert(QStringLiteral("ARCHIVIST_TERMINAL"), QStringLiteral("1"));
    return environment;
}

QStringList flatten(const QMap<QString, QString> &environment)
{
    QStringList result;

    for (auto iterator = environment.cbegin(); iterator != environment.cend(); ++iterator) {
        result.append(iterator.key() + QLatin1Char('=') + iterator.value());
    }

    return result;
}

bool matchesUuid(const QString &value)
{
    static const QRegularExpression pattern(QStringLiteral(
        "^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"
    ));
    return pattern.match(value).hasMatch();
}

// z.coerce.number().int().min(2).max(500)
std::optional<int> terminalDimension(const QVariant &value)
{
    bool ok = false;
    double number = 0;

    if (value.typeId() == QMetaType::Double || value.typeId() == QMetaType::Int || value.typeId() == QMetaType::LongLong) {
        number = value.toDouble();
        ok = true;
    } else if (value.typeId() == QMetaType::QString) {
        const QString text = JsCompat::trimmed(value.toString());
        number = text.isEmpty() ? 0 : text.toDouble(&ok);
    }

    if (!ok || number != std::floor(number) || number < 2 || number > 500) {
        return std::nullopt;
    }

    return static_cast<int>(number);
}

QByteArray message(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}
}

namespace IdeTerminals
{
Context parseContext(const QUrlQuery &query)
{
    const auto invalid = []() { return IdeHttpError(400, QStringLiteral("Invalid terminal connection.")); };
    static const QRegularExpression sessionPattern(QStringLiteral("^[A-Za-z0-9_-]+$"));
    Context context;

    if (!query.hasQueryItem(QStringLiteral("sessionId"))) {
        throw invalid();
    }

    context.sessionId = JsCompat::trimmed(query.queryItemValue(QStringLiteral("sessionId"), QUrl::FullyDecoded));

    if (context.sessionId.isEmpty() || context.sessionId.size() > 80 || !sessionPattern.match(context.sessionId).hasMatch()) {
        throw invalid();
    }

    context.collectionId = query.queryItemValue(QStringLiteral("collectionId"), QUrl::FullyDecoded);
    context.libraryId = query.queryItemValue(QStringLiteral("libraryId"), QUrl::FullyDecoded);

    if (!matchesUuid(context.collectionId) || !matchesUuid(context.libraryId)) {
        throw invalid();
    }

    for (const auto &[key, target] : {std::pair{QStringLiteral("cols"), &context.cols}, std::pair{QStringLiteral("rows"), &context.rows}}) {
        if (!query.hasQueryItem(key)) {
            continue;
        }

        const auto dimension = terminalDimension(query.queryItemValue(key, QUrl::FullyDecoded));

        if (!dimension) {
            throw invalid();
        }

        *target = *dimension;
    }

    return context;
}

PreparedShell prepareShell(const QString &stateRoot, const Context &context, const QString &shell)
{
    const QString stateDirectory = QDir(stateRoot).filePath(QStringLiteral("shells/") + hashFor(context));
    const QString historyPath = QDir(stateDirectory).filePath(QStringLiteral("history"));
    QDir().mkpath(stateDirectory);

    {
        QFile history(historyPath);

        if (history.open(QIODevice::Append)) {
            history.close();
        }

        history.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }

    QMap<QString, QString> environment = baseEnvironment();
    const QString shellName = QFileInfo(shell).fileName().toLower();

    if (shellName.contains(QStringLiteral("zsh"))) {
        const QString zdotDirectory = QDir(stateDirectory).filePath(QStringLiteral("zdotdir"));
        QDir().mkpath(zdotDirectory);

        for (const QString &fileName : {QStringLiteral(".zshenv"), QStringLiteral(".zprofile"), QStringLiteral(".zlogin"), QStringLiteral(".zlogout")}) {
            writePrivateFile(QDir(zdotDirectory).filePath(fileName), sourceHomeZshFile(fileName));
        }

        const QString historyLiteral = quoteForShell(historyPath);
        QString rc = sourceHomeZshFile(QStringLiteral(".zshrc"));

        while (rc.endsWith(QLatin1Char('\n'))) {
            rc.chop(1);
        }

        writePrivateFile(
            QDir(zdotDirectory).filePath(QStringLiteral(".zshrc")),
            QStringList{
                QStringLiteral("export HISTFILE=") + historyLiteral,
                rc,
                QStringLiteral("export HISTFILE=") + historyLiteral,
                QStringLiteral("unsetopt SHARE_HISTORY"),
                QStringLiteral("unsetopt PROMPT_SP"),
                QStringLiteral("PROMPT_EOL_MARK=''"),
                QStringLiteral("setopt APPEND_HISTORY"),
                QStringLiteral("setopt INC_APPEND_HISTORY"),
                QStringLiteral("fc -p \"$HISTFILE\""),
                QString(),
            }.join(QLatin1Char('\n'))
        );

        environment.insert(QStringLiteral("ZDOTDIR"), zdotDirectory);
        environment.insert(QStringLiteral("HISTFILE"), historyPath);
        environment.insert(QStringLiteral("ARCHIVIST_TERMINAL_HISTORY"), historyPath);
        return {{QStringLiteral("-l")}, flatten(environment)};
    }

    if (shellName.contains(QStringLiteral("bash"))) {
        const QString rcPath = QDir(stateDirectory).filePath(QStringLiteral("bashrc"));
        writePrivateFile(
            rcPath,
            QStringList{
                QStringLiteral("[[ -r \"$HOME/.bashrc\" ]] && source \"$HOME/.bashrc\""),
                QStringLiteral("export HISTFILE=") + quoteForShell(historyPath),
                QStringLiteral("history -c"),
                QStringLiteral("history -r \"$HISTFILE\" 2>/dev/null || true"),
                QStringLiteral("shopt -s histappend"),
                QStringLiteral("PROMPT_COMMAND=\"history -a${PROMPT_COMMAND:+;$PROMPT_COMMAND}\""),
                QString(),
            }.join(QLatin1Char('\n'))
        );

        environment.insert(QStringLiteral("HISTFILE"), historyPath);
        environment.insert(QStringLiteral("ARCHIVIST_TERMINAL_HISTORY"), historyPath);
        return {{QStringLiteral("--noprofile"), QStringLiteral("--rcfile"), rcPath, QStringLiteral("-i")}, flatten(environment)};
    }

    environment.insert(QStringLiteral("HISTFILE"), historyPath);
    environment.insert(QStringLiteral("ARCHIVIST_TERMINAL_HISTORY"), historyPath);
    return {{QStringLiteral("-l")}, flatten(environment)};
}
}

TerminalSession::TerminalSession(
    const IdeTerminals::Context &context,
    const QString &cwd,
    const QString &shell,
    const QString &stateRoot,
    QObject *parent
)
    : QObject(parent)
    , m_context(context)
    , m_cwd(cwd)
    , m_shell(shell)
    , m_stateRoot(stateRoot)
{
    QFile snapshotFile(snapshotPath());

    if (snapshotFile.open(QIODevice::ReadOnly)) {
        const QJsonObject snapshot = QJsonDocument::fromJson(snapshotFile.readAll()).object();

        if (snapshot.value(QStringLiteral("version")).toInt() == 2
            && snapshot.value(QStringLiteral("sessionId")).toString() == context.sessionId
            && snapshot.value(QStringLiteral("collectionId")).toString() == context.collectionId
            && snapshot.value(QStringLiteral("libraryId")).toString() == context.libraryId) {
            m_scrollback = snapshot.value(QStringLiteral("scrollback")).toString();
        }
    }

    const IdeTerminals::PreparedShell prepared = IdeTerminals::prepareShell(stateRoot, context, shell);

    // Everything the child needs is built before fork(): only async-signal-safe
    // calls are allowed after it in a threaded process.
    std::vector<std::string> argumentStorage{shell.toStdString()};
    std::vector<std::string> environmentStorage;

    for (const QString &argument : prepared.args) {
        argumentStorage.push_back(argument.toStdString());
    }

    for (const QString &entry : prepared.environment) {
        environmentStorage.push_back(entry.toStdString());
    }

    std::vector<char *> arguments;
    std::vector<char *> environment;

    for (std::string &argument : argumentStorage) {
        arguments.push_back(argument.data());
    }

    for (std::string &entry : environmentStorage) {
        environment.push_back(entry.data());
    }

    arguments.push_back(nullptr);
    environment.push_back(nullptr);
    const std::string workingDirectory = cwd.toStdString();
    const long descriptorLimit = std::min<long>(sysconf(_SC_OPEN_MAX) > 0 ? sysconf(_SC_OPEN_MAX) : 1024, 10240);

    struct winsize size {};
    size.ws_col = static_cast<unsigned short>(context.cols);
    size.ws_row = static_cast<unsigned short>(context.rows);
    int master = -1;
    const pid_t pid = forkpty(&master, nullptr, nullptr, &size);

    if (pid < 0) {
        throw IdeHttpError(500, QStringLiteral("Terminal unavailable"));
    }

    if (pid == 0) {
        sigset_t signalMask;
        sigemptyset(&signalMask);
        sigprocmask(SIG_SETMASK, &signalMask, nullptr);

        for (int signal = 1; signal < NSIG; ++signal) {
            ::signal(signal, SIG_DFL);
        }

        for (long descriptor = 3; descriptor < descriptorLimit; ++descriptor) {
            ::close(static_cast<int>(descriptor));
        }

        if (::chdir(workingDirectory.c_str()) != 0) {
            _exit(1);
        }

        ::execve(arguments.front(), arguments.data(), environment.data());
        _exit(1);
    }

    m_pid = pid;
    m_master = master;
    ::fcntl(m_master, F_SETFL, ::fcntl(m_master, F_GETFL) | O_NONBLOCK);
    ::fcntl(m_master, F_SETFD, FD_CLOEXEC);

    m_readNotifier = new QSocketNotifier(m_master, QSocketNotifier::Read, this);
    connect(m_readNotifier, &QSocketNotifier::activated, this, &TerminalSession::readOutput);
    m_writeNotifier = new QSocketNotifier(m_master, QSocketNotifier::Write, this);
    m_writeNotifier->setEnabled(false);
    connect(m_writeNotifier, &QSocketNotifier::activated, this, &TerminalSession::flushInput);

    m_exitTimer.setInterval(250);
    connect(&m_exitTimer, &QTimer::timeout, this, &TerminalSession::checkExit);
    m_exitTimer.start();

    m_snapshotTimer.setSingleShot(true);
    m_snapshotTimer.setInterval(750);
    connect(&m_snapshotTimer, &QTimer::timeout, this, &TerminalSession::persistSnapshot);
}

TerminalSession::~TerminalSession()
{
    if (!m_exited && m_pid > 0) {
        ::kill(static_cast<pid_t>(m_pid), SIGHUP);
    }

    if (m_master >= 0) {
        ::close(m_master);
    }
}

bool TerminalSession::hasExited() const
{
    return m_exited;
}

qint64 TerminalSession::processId() const
{
    return m_pid;
}

QString TerminalSession::cwd() const
{
    return m_cwd;
}

QString TerminalSession::shell() const
{
    return m_shell;
}

void TerminalSession::attach(WebSocketConnection *socket)
{
    if (m_exited) {
        socket->sendText(message({
            {QStringLiteral("type"), QStringLiteral("error")},
            {QStringLiteral("sessionId"), m_context.sessionId},
            {QStringLiteral("message"), QStringLiteral("This terminal process has exited.")},
        }));
        socket->close(1011, QStringLiteral("Terminal process exited."));
        return;
    }

    m_sockets.insert(socket);
    socket->sendText(message({
        {QStringLiteral("type"), QStringLiteral("ready")},
        {QStringLiteral("sessionId"), m_context.sessionId},
        {QStringLiteral("cwd"), m_cwd},
        {QStringLiteral("shell"), m_shell},
    }));

    if (!m_scrollback.isEmpty()) {
        socket->sendText(message({
            {QStringLiteral("type"), QStringLiteral("output")},
            {QStringLiteral("sessionId"), m_context.sessionId},
            {QStringLiteral("data"), m_scrollback},
        }));
    }
}

void TerminalSession::detach(WebSocketConnection *socket)
{
    m_sockets.remove(socket);
}

void TerminalSession::write(const QString &data)
{
    if (m_exited) {
        return;
    }

    m_pendingInput += data.toUtf8();
    flushInput();
}

void TerminalSession::flushInput()
{
    while (!m_pendingInput.isEmpty() && m_master >= 0) {
        const ssize_t written = ::write(m_master, m_pendingInput.constData(), static_cast<size_t>(m_pendingInput.size()));

        if (written > 0) {
            m_pendingInput.remove(0, written);
            continue;
        }

        if (written < 0 && errno == EINTR) {
            continue;
        }

        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            m_writeNotifier->setEnabled(true);
            return;
        }

        m_pendingInput.clear();
        break;
    }

    if (m_writeNotifier) {
        m_writeNotifier->setEnabled(false);
    }
}

void TerminalSession::resize(int cols, int rows)
{
    if (m_exited || m_master < 0) {
        return;
    }

    struct winsize size {};
    size.ws_col = static_cast<unsigned short>(cols);
    size.ws_row = static_cast<unsigned short>(rows);
    ::ioctl(m_master, TIOCSWINSZ, &size);
}

void TerminalSession::kill()
{
    if (m_exited) {
        return;
    }

    m_removeSnapshotOnExit = true;
    m_snapshotTimer.stop();
    deleteSnapshot();
    QDir(QDir(m_stateRoot).filePath(QStringLiteral("shells/") + identityHash())).removeRecursively();
    ::kill(static_cast<pid_t>(m_pid), SIGHUP);
}

void TerminalSession::dispose()
{
    const QSet<WebSocketConnection *> sockets = std::exchange(m_sockets, {});

    for (WebSocketConnection *socket : sockets) {
        socket->close(1001, QStringLiteral("Archivist backend is stopping."));
    }

    m_snapshotTimer.stop();
    persistSnapshot();

    if (!m_exited) {
        ::kill(static_cast<pid_t>(m_pid), SIGHUP);
    }
}

void TerminalSession::readOutput()
{
    char buffer[65536];
    QString output;
    bool ended = false;

    while (true) {
        const ssize_t count = ::read(m_master, buffer, sizeof buffer);

        if (count > 0) {
            output += m_decoder.decode(QByteArrayView(buffer, count));
            continue;
        }

        if (count < 0 && errno == EINTR) {
            continue;
        }

        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        }

        // EOF or EIO: the shell side of the pty is gone.
        ended = true;
        break;
    }

    if (!output.isEmpty()) {
        appendScrollback(output);
        m_snapshotTimer.start();
        broadcast(message({
            {QStringLiteral("type"), QStringLiteral("output")},
            {QStringLiteral("sessionId"), m_context.sessionId},
            {QStringLiteral("data"), output},
        }));
    }

    if (ended && m_readNotifier) {
        m_readNotifier->setEnabled(false);
        checkExit();
    }
}

void TerminalSession::checkExit()
{
    if (m_exited || m_pid <= 0) {
        return;
    }

    int status = 0;
    const pid_t result = ::waitpid(static_cast<pid_t>(m_pid), &status, WNOHANG);

    if (result == 0) {
        return;
    }

    if (result < 0 && errno == EINTR) {
        return;
    }

    if (m_readNotifier && m_readNotifier->isEnabled()) {
        readOutput();
    }

    const int exitCode = result > 0 && WIFEXITED(status) ? WEXITSTATUS(status) : 0;
    const int signal = result > 0 && WIFSIGNALED(status) ? WTERMSIG(status) : 0;
    finishExit(exitCode, signal);
}

void TerminalSession::finishExit(int exitCode, int signal)
{
    m_exited = true;
    m_exitTimer.stop();
    m_snapshotTimer.stop();

    if (m_readNotifier) {
        m_readNotifier->setEnabled(false);
    }

    if (m_writeNotifier) {
        m_writeNotifier->setEnabled(false);
    }

    if (m_removeSnapshotOnExit) {
        deleteSnapshot();
    } else {
        persistSnapshot();
    }

    broadcast(message({
        {QStringLiteral("type"), QStringLiteral("exit")},
        {QStringLiteral("sessionId"), m_context.sessionId},
        {QStringLiteral("exitCode"), exitCode},
        {QStringLiteral("signal"), signal},
    }));

    const QSet<WebSocketConnection *> sockets = std::exchange(m_sockets, {});

    for (WebSocketConnection *socket : sockets) {
        socket->close(1000, QStringLiteral("Terminal process exited."));
    }

    emit exited(this);
}

void TerminalSession::appendScrollback(const QString &data)
{
    m_scrollback += data;

    if (m_scrollback.size() > maximumScrollbackCharacters) {
        m_scrollback = m_scrollback.right(maximumScrollbackCharacters);
    }
}

void TerminalSession::broadcast(const QByteArray &payload)
{
    for (WebSocketConnection *socket : std::as_const(m_sockets)) {
        socket->sendText(payload);
    }
}

QString TerminalSession::identityHash() const
{
    return hashFor(m_context);
}

QString TerminalSession::snapshotPath() const
{
    return QDir(m_stateRoot).filePath(identityHash() + QStringLiteral(".json"));
}

void TerminalSession::persistSnapshot()
{
    QDir().mkpath(m_stateRoot);
    QSaveFile file(snapshotPath());

    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }

    file.write(QJsonDocument(QJsonObject{
        {QStringLiteral("version"), 2},
        {QStringLiteral("sessionId"), m_context.sessionId},
        {QStringLiteral("collectionId"), m_context.collectionId},
        {QStringLiteral("libraryId"), m_context.libraryId},
        {QStringLiteral("cwd"), m_cwd},
        {QStringLiteral("shell"), m_shell},
        {QStringLiteral("scrollback"), m_scrollback},
        {QStringLiteral("savedAt"), JsCompat::isoTimestamp(QDateTime::currentDateTimeUtc())},
    }).toJson(QJsonDocument::Compact) + '\n');
    file.commit();
}

void TerminalSession::deleteSnapshot()
{
    QFile::remove(snapshotPath());
}

TerminalSessionManager::TerminalSessionManager(QString stateRoot, QObject *parent)
    : QObject(parent)
    , m_stateRoot(std::move(stateRoot))
{
}

TerminalSessionManager::~TerminalSessionManager()
{
    shutdown();
}

QString TerminalSessionManager::resolveShell()
{
    const QString configured = qEnvironmentVariable("SHELL");

    if (!configured.isEmpty() && QFileInfo(configured).isAbsolute() && QFileInfo::exists(configured)) {
        return configured;
    }

    return QFileInfo::exists(QStringLiteral("/bin/zsh")) ? QStringLiteral("/bin/zsh") : QStringLiteral("/bin/bash");
}

TerminalSession *TerminalSessionManager::getOrCreate(CoreDatabase &database, const IdeTerminals::Context &context)
{
    const QString key = identity(context);
    const QPointer<TerminalSession> existing = m_sessions.value(key);

    if (existing && !existing->hasExited()) {
        existing->resize(context.cols, context.rows);
        return existing;
    }

    m_sessions.remove(key);

    if (m_sessions.size() >= maximumTerminalSessions) {
        throw IdeHttpError(429, QStringLiteral("Too many terminal sessions are open."));
    }

    const auto library = LibraryRepository::libraryById(database, context.libraryId);

    if (!library) {
        throw IdeHttpError(404, QStringLiteral("Library not found."));
    }

    if (!library->value(QStringLiteral("archivedAt")).isNull()) {
        throw IdeHttpError(409, QStringLiteral("Archived Libraries cannot open terminals."));
    }

    const QString rootPath = QFileInfo(library->value(QStringLiteral("rootPath")).toString()).canonicalFilePath();

    if (rootPath.isEmpty()) {
        throw IdeHttpError(404, QStringLiteral("The Library folder could not be resolved."));
    }

    if (!QFileInfo(rootPath).isDir()) {
        throw IdeHttpError(409, QStringLiteral("The Library root is no longer a folder."));
    }

    if (::access(QFile::encodeName(rootPath).constData(), R_OK | X_OK) != 0) {
        throw IdeHttpError(403, QStringLiteral("Archivist cannot enter the Library folder."));
    }

    auto *session = new TerminalSession(context, rootPath, resolveShell(), m_stateRoot, this);
    connect(session, &TerminalSession::exited, this, [this, key](TerminalSession *exited) {
        if (m_sessions.value(key) == exited) {
            m_sessions.remove(key);
        }

        exited->deleteLater();
    });
    m_sessions.insert(key, session);
    return session;
}

void TerminalSessionManager::shutdown()
{
    const auto sessions = std::exchange(m_sessions, {});

    for (const QPointer<TerminalSession> &session : sessions) {
        if (session) {
            session->dispose();
        }
    }
}

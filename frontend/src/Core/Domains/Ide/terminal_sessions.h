#pragma once

#include "Core/Database/core_database.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QSocketNotifier>
#include <QString>
#include <QStringDecoder>
#include <QTimer>
#include <QUrlQuery>

class WebSocketConnection;

// Native port of backend/src/api/terminals: one login shell per
// Collection/Library/session id on a pseudo-terminal, shared by every IDE
// surface attached to it, with scrollback and history kept between launches.
namespace IdeTerminals
{
struct Context
{
    QString sessionId;
    QString collectionId;
    QString libraryId;
    int cols = 120;
    int rows = 30;
};

// terminalSocketQuerySchema. Throws IdeHttpError(400).
[[nodiscard]] Context parseContext(const QUrlQuery &query);

struct PreparedShell
{
    QStringList args;
    QStringList environment; // KEY=value
};

// prepareTerminalShell: per-terminal history and rc files under `stateRoot`.
[[nodiscard]] PreparedShell prepareShell(const QString &stateRoot, const Context &context, const QString &shell);
}

class TerminalSession final : public QObject
{
    Q_OBJECT

public:
    TerminalSession(
        const IdeTerminals::Context &context,
        const QString &cwd,
        const QString &shell,
        const QString &stateRoot,
        QObject *parent = nullptr
    );
    ~TerminalSession() override;

    [[nodiscard]] bool hasExited() const;
    [[nodiscard]] qint64 processId() const;
    [[nodiscard]] QString cwd() const;
    [[nodiscard]] QString shell() const;

    void attach(WebSocketConnection *socket);
    void detach(WebSocketConnection *socket);
    void write(const QString &data);
    void resize(int cols, int rows);
    // Ends the shell and forgets its scrollback and history.
    void kill();
    // App shutdown: keep the scrollback, stop the shell.
    void dispose();

signals:
    void exited(TerminalSession *session);

private:
    void readOutput();
    void flushInput();
    void checkExit();
    void finishExit(int exitCode, int signal);
    void appendScrollback(const QString &data);
    void broadcast(const QByteArray &message);
    void persistSnapshot();
    void deleteSnapshot();
    [[nodiscard]] QString snapshotPath() const;
    [[nodiscard]] QString identityHash() const;

    IdeTerminals::Context m_context;
    QString m_cwd;
    QString m_shell;
    QString m_stateRoot;
    int m_master = -1;
    qint64 m_pid = -1;
    QSocketNotifier *m_readNotifier = nullptr;
    QSocketNotifier *m_writeNotifier = nullptr;
    QTimer m_exitTimer;
    QTimer m_snapshotTimer;
    QStringDecoder m_decoder{QStringDecoder::Utf8};
    QByteArray m_pendingInput;
    QString m_scrollback;
    QSet<WebSocketConnection *> m_sockets;
    bool m_removeSnapshotOnExit = false;
    bool m_exited = false;
};

class TerminalSessionManager final : public QObject
{
    Q_OBJECT

public:
    // Snapshots and shell state live in `<stateRoot>` (backend/data/terminals).
    explicit TerminalSessionManager(QString stateRoot, QObject *parent = nullptr);
    ~TerminalSessionManager() override;

    // Throws IdeHttpError like the Node manager.
    TerminalSession *getOrCreate(CoreDatabase &database, const IdeTerminals::Context &context);
    void shutdown();

    [[nodiscard]] static QString resolveShell();

private:
    QString m_stateRoot;
    QHash<QString, QPointer<TerminalSession>> m_sessions;
};

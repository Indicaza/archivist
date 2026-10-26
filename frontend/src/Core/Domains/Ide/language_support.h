#pragma once

#include "Core/Database/core_database.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <optional>

class WebSocketConnection;

// Native port of backend/src/api/languageSupport: the language-server
// registry, workspace and executable resolution, short-lived session tokens,
// and the bridge between one IDE WebSocket and one language server's stdio.
namespace LanguageServers
{
struct Definition
{
    QString id;
    QString displayName;
    QStringList languageIds;
    QStringList executableCandidates;
    QStringList args;
    QStringList rootMarkers;
    bool enabledByDefault = true;
};

struct Workspace
{
    QString libraryId;
    QString libraryRoot;
    QString workspaceRoot;
    QString filePath; // empty when there is none
};

struct Launch
{
    QStringList args;
    QString buildDirectory;
    QStringList importDirectories;
};

[[nodiscard]] const QList<Definition> &definitions();
[[nodiscard]] const Definition *definition(const QString &serverId);

// Throws IdeHttpError with the Node messages.
[[nodiscard]] Workspace resolveWorkspace(
    CoreDatabase &database,
    const Definition &definition,
    const QString &libraryId,
    const QString &workspaceRoot,
    const QString &filePath
);
[[nodiscard]] QString resolveExecutable(const Definition &definition, const QString &workspaceRoot);
[[nodiscard]] Launch resolveLaunch(const Definition &definition, const Workspace &workspace);
}

class LanguageSupportService final : public QObject
{
    Q_OBJECT

public:
    explicit LanguageSupportService(QObject *parent = nullptr);
    ~LanguageSupportService() override;

    // ws://127.0.0.1:<port>/language-support carries the sessions.
    void setPort(quint16 port);

    // GET /api/language-support/config
    [[nodiscard]] QVariantMap config(CoreDatabase &database, const QVariantMap &query) const;
    // POST /api/language-support/sessions -> descriptor. Throws IdeHttpError.
    QVariantMap createSession(CoreDatabase &database, const QVariantMap &input);
    // GET /api/language-support/sessions
    [[nodiscard]] QVariantList listSessions();
    // POST / GET /api/language-support/events
    void appendEvents(const QVariant &body);
    [[nodiscard]] QVariantList events(int limit) const;

    // Validates sessionId/token and hands the socket to a new server process.
    // Throws IdeHttpError before the upgrade is accepted.
    QString consumePendingSession(const QString &sessionId, const QString &token);
    void launch(const QString &pendingKey, WebSocketConnection *socket);

    void close();

private:
    struct Pending
    {
        QVariantMap descriptor;
        QString token;
        LanguageServers::Definition definition;
        LanguageServers::Workspace workspace;
        QString executablePath;
        LanguageServers::Launch launch;
        qint64 expiresAtMilliseconds = 0;
    };

    struct Active
    {
        QVariantMap summary;
        QString key;
        QPointer<QProcess> process;
        QPointer<WebSocketConnection> socket;
        qint64 startedAtMilliseconds = 0;
        QByteArray stdoutBuffer;
        QString stderrBuffer;
        QStringList stderrTail;
    };

    void removeExpiredPendingSessions();
    void finishActive(const QString &sessionId, const QString &reason);
    void readServerOutput(const QString &sessionId);
    void recordServerStderr(const QString &sessionId, const QByteArray &data, bool flush);
    [[nodiscard]] QVariantMap describe(CoreDatabase &database, const LanguageServers::Definition &definition, const QVariantMap &input) const;

    quint16 m_port = 0;
    QHash<QString, Pending> m_pending;
    QHash<QString, Active> m_active;
    QHash<QString, QString> m_activeIdByKey;
    QHash<QString, Pending> m_consumed;
    QVariantList m_events;
    qint64 m_nextEventSequence = 1;
    QTimer m_cleanupTimer;
};

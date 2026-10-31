#pragma once

#include "Core/Database/core_database.h"
#include "Core/Domains/Observatory/observatory_records.h"

#include <QObject>
#include <QString>
#include <QThreadPool>
#include <QUrl>

#include <atomic>
#include <functional>

class LanguageSupportService;
class QTcpServer;
class QTcpSocket;
class TerminalSessionManager;

// The in-process replacement for the Node server's IDE half: serves the
// Monaco/xterm web bundle, the few Library and language-support endpoints the
// web IDE calls, and its terminal and language-server WebSockets.
//
// It binds loopback port 3334 (ARCHIVIST_IDE_PORT overrides), next to the
// Node backend's 3333. The web IDE keeps editor and terminal positions in
// localStorage, which is per origin, so the port should stay the same between
// launches; a free port is used only when 3334 is taken. Requests from other
// origins or hosts are refused, like the Node local-only guard.
class IdeServer final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QUrl url READ url NOTIFY stateChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged)
    // The Observatory dashboard page (see Core/Domains/Observatory).
    Q_PROPERTY(QUrl observatoryUrl READ observatoryUrl NOTIFY stateChanged)
    // This session's key for the Observatory test controls. Only the
    // Observatory panel passes it to its page; see Observatory/observatory_control.h.
    Q_PROPERTY(QString observatoryControlToken READ observatoryControlToken CONSTANT)
    // Whether coding agents on this computer may start and stop live
    // recordings (off until the person turns it on in Observatory > Live).
    Q_PROPERTY(bool observatoryAgentAccess READ observatoryAgentAccess NOTIFY observatoryAgentAccessChanged)

public:
    explicit IdeServer(QObject *parent = nullptr);
    ~IdeServer() override;

    // `distributionPath` holds the built web IDE (frontend/dist/ide).
    // Port 0 means the default port, or any free port when that is taken.
    bool start(const QString &databasePath, const QString &distributionPath = {}, quint16 port = 0);
    void stop();

    [[nodiscard]] QUrl url() const;
    [[nodiscard]] QUrl observatoryUrl() const;
    [[nodiscard]] QString observatoryControlToken() const;
    [[nodiscard]] bool ready() const;
    [[nodiscard]] quint16 port() const;

    // Answers /observatory/api/live/<operation> (the live recorder lives in
    // the app layer, so it registers itself here). `caller` is "panel" or "agent".
    using LiveHandler = std::function<Observatory::Response(const QString &operation, const QByteArray &body, const QString &caller)>;
    void setLiveHandler(LiveHandler handler);

    [[nodiscard]] bool observatoryAgentAccess() const { return !m_agentToken.isEmpty(); }
    // On: writes <Observatory root>/live/agent-access.json (readable only by
    // this user) with this server's address and a fresh key; agents use it to
    // reach the live recorder. Off (and at quit): deletes the file and the key.
    Q_INVOKABLE bool setObservatoryAgentAccess(bool enabled);
    [[nodiscard]] static QString agentAccessFile();

    // Observatory page reads and control operations run on their own two
    // threads, so a slow export or a queue of polls never delays the editor's
    // Library requests (which use the other pool). At most
    // kMaxObservatoryRequests wait or run at once; more are refused with 503.
    static constexpr int kMaxObservatoryRequests = 8;
    // For tests: the pool Observatory work runs on.
    [[nodiscard]] QThreadPool &observatoryPoolForTesting() { return m_observatoryPool; }
    [[nodiscard]] int observatoryRequestsInFlight() const { return m_observatoryRequests.load(); }

    [[nodiscard]] static QString defaultDistributionPath();
    [[nodiscard]] static quint16 defaultPort();

signals:
    void stateChanged();
    void observatoryAgentAccessChanged();

private:
    friend class IdeHttpConnection;

    QTcpServer *m_server = nullptr;
    CoreDatabase m_database;
    QString m_databasePath;
    QString m_distributionPath;
    QByteArray m_observatoryControlToken;
    QByteArray m_agentToken;
    LiveHandler m_liveHandler;
    TerminalSessionManager *m_terminals = nullptr;
    LanguageSupportService *m_languageSupport = nullptr;
    // Declared before the pools: their destructors wait for tasks that use it.
    std::atomic_int m_observatoryRequests{0};
    QThreadPool m_pool;
    QThreadPool m_observatoryPool;
};

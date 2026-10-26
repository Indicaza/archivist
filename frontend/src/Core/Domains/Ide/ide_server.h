#pragma once

#include "Core/Database/core_database.h"

#include <QObject>
#include <QString>
#include <QThreadPool>
#include <QUrl>

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

public:
    explicit IdeServer(QObject *parent = nullptr);
    ~IdeServer() override;

    // `distributionPath` holds the built web IDE (frontend/dist/ide).
    // Port 0 means the default port, or any free port when that is taken.
    bool start(const QString &databasePath, const QString &distributionPath = {}, quint16 port = 0);
    void stop();

    [[nodiscard]] QUrl url() const;
    [[nodiscard]] bool ready() const;
    [[nodiscard]] quint16 port() const;

    [[nodiscard]] static QString defaultDistributionPath();
    [[nodiscard]] static quint16 defaultPort();

signals:
    void stateChanged();

private:
    friend class IdeHttpConnection;

    QTcpServer *m_server = nullptr;
    CoreDatabase m_database;
    QString m_databasePath;
    QString m_distributionPath;
    TerminalSessionManager *m_terminals = nullptr;
    LanguageSupportService *m_languageSupport = nullptr;
    QThreadPool m_pool;
};

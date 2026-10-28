#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>

#include <functional>

class WorkspaceStore;

// Carries agent workspace requests from the AI thread to the UI thread.
//
// Installs itself as the Workspace::Gateway handler. A request from another
// thread is queued onto this object's thread and the caller waits (bounded)
// for the answer; a request that times out is dropped before it runs, so a
// late UI thread never applies a change the agent was told failed.
//
// `foreground` decides whether the agent's conversation is the one in
// front; WorkspaceStore decides what that allows.
class WorkspaceAgentBridge final : public QObject
{
    Q_OBJECT

public:
    using ForegroundCheck = std::function<bool(const QString &chatId)>;

    explicit WorkspaceAgentBridge(WorkspaceStore *store, QObject *parent = nullptr);
    ~WorkspaceAgentBridge() override;

    void setForegroundCheck(ForegroundCheck check) { m_foreground = std::move(check); }
    void setTimeoutMs(int timeoutMs) { m_timeoutMs = timeoutMs; }

    // Thread safe.
    QVariantMap call(const QVariantMap &request);

private:
    QVariantMap handle(const QVariantMap &request);

    QPointer<WorkspaceStore> m_store;
    ForegroundCheck m_foreground;
    int m_timeoutMs = 8000;
};

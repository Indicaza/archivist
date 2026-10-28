#include "App/Domains/Workspace/workspace_agent_bridge.h"

#include "App/Domains/Workspace/workspace_store.h"
#include "Core/Domains/Workspace/workspace_gateway.h"

#include <QMetaObject>
#include <QThread>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>

WorkspaceAgentBridge::WorkspaceAgentBridge(WorkspaceStore *store, QObject *parent)
    : QObject(parent)
    , m_store(store)
{
    Workspace::Gateway::install([this](const QVariantMap &request) { return call(request); });
}

WorkspaceAgentBridge::~WorkspaceAgentBridge()
{
    Workspace::Gateway::install({});
}

QVariantMap WorkspaceAgentBridge::handle(const QVariantMap &request)
{
    if (!m_store) {
        return {
            {QStringLiteral("ok"), false},
            {QStringLiteral("code"), QStringLiteral("UNAVAILABLE")},
            {QStringLiteral("error"), QStringLiteral("The workspace isn't available right now.")},
        };
    }
    const QString chatId = request.value(QStringLiteral("chatId")).toString();
    const bool foreground = m_foreground ? m_foreground(chatId) : true;
    return m_store->runAgentRequest(request, foreground);
}

QVariantMap WorkspaceAgentBridge::call(const QVariantMap &request)
{
    if (QThread::currentThread() == thread()) {
        return handle(request);
    }

    // waiting -> running (the UI thread took it) or abandoned (timed out).
    enum State : int { Waiting, Running, Abandoned };
    struct Pending
    {
        std::promise<QVariantMap> promise;
        std::atomic_int state{Waiting};
    };
    auto pending = std::make_shared<Pending>();
    std::future<QVariantMap> answer = pending->promise.get_future();

    QMetaObject::invokeMethod(
        this,
        [this, request, pending]() {
            int expected = Waiting;
            if (!pending->state.compare_exchange_strong(expected, Running)) {
                return;
            }
            pending->promise.set_value(handle(request));
        },
        Qt::QueuedConnection
    );

    const QVariantMap timedOut{
        {QStringLiteral("ok"), false},
        {QStringLiteral("code"), QStringLiteral("TIMED_OUT")},
        {QStringLiteral("error"), QStringLiteral("The workspace didn't answer in time, so nothing changed.")},
    };

    if (answer.wait_for(std::chrono::milliseconds(m_timeoutMs)) != std::future_status::ready) {
        int expected = Waiting;
        if (pending->state.compare_exchange_strong(expected, Abandoned)) {
            return timedOut;
        }
        // Already running on the UI thread: its answer is moments away.
    }

    try {
        return answer.get();
    } catch (const std::future_error &) {
        // The bridge went away before the request ran (shutdown).
        return {
            {QStringLiteral("ok"), false},
            {QStringLiteral("code"), QStringLiteral("UNAVAILABLE")},
            {QStringLiteral("error"), QStringLiteral("The workspace closed before the change ran.")},
        };
    }
}

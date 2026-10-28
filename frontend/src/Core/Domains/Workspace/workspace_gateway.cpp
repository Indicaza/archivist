#include "Core/Domains/Workspace/workspace_gateway.h"

#include <QMutex>
#include <QMutexLocker>

#include <memory>

namespace Workspace::Gateway
{
namespace
{
QMutex &mutex()
{
    static QMutex instance;
    return instance;
}

// Shared so a call in flight keeps its handler alive while another thread
// uninstalls it.
std::shared_ptr<Handler> &current()
{
    static std::shared_ptr<Handler> handler;
    return handler;
}
} // namespace

void install(Handler handler)
{
    QMutexLocker lock(&mutex());
    current() = handler ? std::make_shared<Handler>(std::move(handler)) : nullptr;
}

bool installed()
{
    QMutexLocker lock(&mutex());
    return current() != nullptr;
}

QVariantMap call(const QVariantMap &request)
{
    std::shared_ptr<Handler> handler;
    {
        QMutexLocker lock(&mutex());
        handler = current();
    }

    if (!handler) {
        return {
            {QStringLiteral("ok"), false},
            {QStringLiteral("code"), QStringLiteral("UNAVAILABLE")},
            {QStringLiteral("error"), QStringLiteral("The workspace isn't available right now.")},
        };
    }

    return (*handler)(request);
}
} // namespace Workspace::Gateway

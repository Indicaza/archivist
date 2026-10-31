#include "live_events.h"

#include <QElapsedTimer>
#include <QRegularExpression>

#include <atomic>

namespace LiveEvents
{
namespace
{
std::atomic<Sink *> g_sink{nullptr};

const QElapsedTimer &clock()
{
    static const QElapsedTimer timer = [] {
        QElapsedTimer started;
        started.start();
        return started;
    }();
    return timer;
}
} // namespace

qint64 nowNs()
{
    return clock().nsecsElapsed();
}

bool recording()
{
    return g_sink.load(std::memory_order_acquire) != nullptr;
}

void setSink(Sink *sink)
{
    g_sink.store(sink, std::memory_order_release);
}

QVariantMap cleanFields(const QVariantMap &fields)
{
    static const QRegularExpression key(QStringLiteral("^[A-Za-z][A-Za-z0-9]{0,31}$"));
    QVariantMap clean;
    for (auto it = fields.constBegin(); it != fields.constEnd() && clean.size() < 8; ++it) {
        if (!key.match(it.key()).hasMatch()) {
            continue;
        }
        const QVariant &value = it.value();
        switch (value.typeId()) {
        case QMetaType::Bool:
        case QMetaType::Int:
        case QMetaType::UInt:
        case QMetaType::LongLong:
        case QMetaType::ULongLong:
        case QMetaType::Double:
        case QMetaType::Float:
            clean.insert(it.key(), value);
            break;
        case QMetaType::QString:
            clean.insert(it.key(), value.toString().left(64));
            break;
        default:
            break;
        }
    }
    return clean;
}

void marker(const QString &category, const QString &name, const QVariantMap &fields, int ok)
{
    Sink *sink = g_sink.load(std::memory_order_acquire);
    if (!sink) {
        return;
    }
    sink->addEvent({QStringLiteral("marker"), category, name, nowNs(), -1, ok, cleanFields(fields)});
}

void span(const QString &category, const QString &name, qint64 startNs, qint64 endNs, int ok, const QVariantMap &fields)
{
    Sink *sink = g_sink.load(std::memory_order_acquire);
    if (!sink) {
        return;
    }
    sink->addEvent({QStringLiteral("span"), category, name, startNs, endNs, ok, cleanFields(fields)});
}

quint64 begin(const QString &category, const QString &name, const QVariantMap &fields)
{
    Sink *sink = g_sink.load(std::memory_order_acquire);
    if (!sink) {
        return 0;
    }
    return sink->openSpan({QStringLiteral("span"), category, name, nowNs(), -1, -1, cleanFields(fields)});
}

void end(quint64 token, int ok, const QVariantMap &fields)
{
    if (token == 0) {
        return;
    }
    Sink *sink = g_sink.load(std::memory_order_acquire);
    if (!sink) {
        return;
    }
    sink->closeSpan(token, nowNs(), ok, cleanFields(fields));
}

Scope::Scope(QString category, QString name, QVariantMap fields)
    : m_category(std::move(category))
    , m_name(std::move(name))
    , m_fields(std::move(fields))
    , m_startNs(recording() ? nowNs() : -1)
{
}

Scope::~Scope()
{
    if (m_startNs >= 0) {
        span(m_category, m_name, m_startNs, nowNs(), m_ok, m_fields);
    }
}
} // namespace LiveEvents

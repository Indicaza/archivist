#include "live_events.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QThread>

#include <atomic>
#include <vector>

namespace LiveEvents
{
namespace
{
std::atomic<Sink *> g_sink{nullptr};
std::atomic<quint64> g_nextId{1};

// This thread's open Scopes and ParentScopes, innermost last. Bounded, so a
// leak can never grow it without limit.
thread_local std::vector<quint64> t_parents;
constexpr size_t kMaxDepth = 64;

quint64 resolveParent(quint64 parent)
{
    if (parent != kCurrentParent) {
        return parent;
    }
    return t_parents.empty() ? 0 : t_parents.back();
}

quint64 nextId()
{
    return g_nextId.fetch_add(1, std::memory_order_relaxed);
}

// A short, stable label for the calling thread: "main" for the GUI thread,
// the thread's objectName when it has one (ArchivistAI, ArchivistCoreWriter),
// otherwise "pool". Read once per thread.
const QString &threadLabel()
{
    thread_local QString label;
    thread_local bool known = false;
    if (!known) {
        QThread *current = QThread::currentThread();
        QCoreApplication *app = QCoreApplication::instance();
        if (app && app->thread() == current) {
            label = QStringLiteral("main");
        } else if (current && !current->objectName().isEmpty()) {
            label = current->objectName().left(32);
        } else {
            label = QStringLiteral("pool");
        }
        // Before QCoreApplication exists every thread looks unnamed; ask again.
        known = app != nullptr;
    }
    return label;
}

Event makeEvent(QString kind, const QString &category, const QString &name, qint64 startNs, qint64 endNs, int ok,
                const QVariantMap &fields, quint64 id, quint64 parent)
{
    Event event{std::move(kind), category, name, startNs, endNs, ok, cleanFields(fields)};
    event.id = id;
    event.parent = resolveParent(parent);
    event.thread = threadLabel();
    return event;
}

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

quint64 currentParent()
{
    return t_parents.empty() ? 0 : t_parents.back();
}

bool recording()
{
    return g_sink.load(std::memory_order_acquire) != nullptr;
}

void setSink(Sink *sink)
{
    g_sink.store(sink, std::memory_order_release);
}

void clearSink(Sink *owner)
{
    Sink *expected = owner;
    g_sink.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
}

QVariantMap cleanFields(const QVariantMap &fields)
{
    static const QRegularExpression key(QStringLiteral("^[A-Za-z][A-Za-z0-9]{0,31}$"));
    QVariantMap clean;
    for (auto it = fields.constBegin(); it != fields.constEnd() && clean.size() < kMaxFields; ++it) {
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

void marker(const QString &category, const QString &name, const QVariantMap &fields, int ok, quint64 parent)
{
    Sink *sink = g_sink.load(std::memory_order_acquire);
    if (!sink) {
        return;
    }
    sink->addEvent(makeEvent(QStringLiteral("marker"), category, name, nowNs(), -1, ok, fields, 0, parent));
}

quint64 span(const QString &category, const QString &name, qint64 startNs, qint64 endNs, int ok, const QVariantMap &fields,
             quint64 parent)
{
    Sink *sink = g_sink.load(std::memory_order_acquire);
    if (!sink) {
        return 0;
    }
    const quint64 id = nextId();
    sink->addEvent(makeEvent(QStringLiteral("span"), category, name, startNs, endNs, ok, fields, id, parent));
    return id;
}

quint64 begin(const QString &category, const QString &name, const QVariantMap &fields, quint64 parent)
{
    Sink *sink = g_sink.load(std::memory_order_acquire);
    if (!sink) {
        return 0;
    }
    const quint64 id = nextId();
    return sink->openSpan(makeEvent(QStringLiteral("span"), category, name, nowNs(), -1, -1, fields, id, parent)) ? id : 0;
}

void end(quint64 id, int ok, const QVariantMap &fields)
{
    if (id == 0) {
        return;
    }
    Sink *sink = g_sink.load(std::memory_order_acquire);
    if (!sink) {
        return;
    }
    sink->closeSpan(id, nowNs(), ok, cleanFields(fields));
}

quint64 beginAt(qint64 startNs, const QString &category, const QString &name, const QVariantMap &fields, quint64 parent)
{
    Sink *sink = g_sink.load(std::memory_order_acquire);
    if (!sink || startNs < 0) {
        return 0;
    }
    const quint64 id = nextId();
    return sink->openSpan(makeEvent(QStringLiteral("span"), category, name, startNs, -1, -1, fields, id, parent)) ? id : 0;
}

namespace
{
void popParent(quint64 id)
{
    // Normally the innermost; tolerate out-of-order ends.
    for (auto it = t_parents.rbegin(); it != t_parents.rend(); ++it) {
        if (*it == id) {
            t_parents.erase(std::next(it).base());
            return;
        }
    }
}
} // namespace

Step::Step(const QString &category, const QString &name, const QVariantMap &fields, quint64 parent)
    : m_span(begin(category, name, fields, parent))
    , m_id(m_span)
{
    if (m_id != 0 && t_parents.size() < kMaxDepth) {
        t_parents.push_back(m_id);
        m_pushed = true;
    }
}

Step::~Step()
{
    if (m_id != 0) {
        done(0, {{QStringLiteral("abandoned"), true}});
    }
}

void Step::done(int ok, const QVariantMap &fields)
{
    if (m_id == 0) {
        return;
    }
    if (m_pushed) {
        popParent(m_id);
        m_pushed = false;
    }
    end(m_id, ok, fields);
    m_id = 0;
}

ParentScope::ParentScope(quint64 parent)
{
    if (parent != 0 && t_parents.size() < kMaxDepth) {
        t_parents.push_back(parent);
        m_pushed = true;
    }
}

ParentScope::~ParentScope()
{
    if (m_pushed && !t_parents.empty()) {
        t_parents.pop_back();
    }
}

Scope::Scope(QString category, QString name, QVariantMap fields, quint64 parent)
    : m_category(std::move(category))
    , m_name(std::move(name))
{
    if (!recording()) {
        return;
    }
    m_fields = std::move(fields);
    m_startNs = nowNs();
    m_id = nextId();
    m_parent = resolveParent(parent);
    if (t_parents.size() < kMaxDepth) {
        t_parents.push_back(m_id);
    } else {
        m_id = 0;
    }
}

Scope::~Scope()
{
    if (m_startNs < 0) {
        return;
    }
    if (m_id != 0 && !t_parents.empty() && t_parents.back() == m_id) {
        t_parents.pop_back();
    }
    Sink *sink = g_sink.load(std::memory_order_acquire);
    if (!sink) {
        return;
    }
    sink->addEvent(makeEvent(QStringLiteral("span"), m_category, m_name, m_startNs, nowNs(), m_ok, m_fields, m_id ? m_id : nextId(), m_parent));
}
} // namespace LiveEvents

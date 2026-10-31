#pragma once

#include <QString>
#include <QVariantMap>
#include <QtGlobal>

// The small API the rest of the app uses to tell the Observatory live
// recorder what it is doing ("sidebar resize started", "workspace publish
// took 3 ms"). While no recording runs, every call returns after one atomic
// load, so it is safe to leave in place.
//
// Events carry operation names and a few small facts (a view type, an
// outcome), never content: no titles, text, paths or keystrokes.
//
// Spans have ids, so work can nest by cause, not only by time: a Scope is
// the parent of every span, marker and begin() made on its thread while it
// is open, and work handed to another thread names its parent explicitly
// (pass the id, or hold a ParentScope there). Each event records the thread
// it was made on. Ids are only assigned while recording.
namespace LiveEvents
{
// One monotonic clock for every live measurement, in nanoseconds since the
// process first asked for it.
qint64 nowNs();

// True only while a recording is running.
bool recording();

struct Event
{
    // span: started and ended, duration measured.
    // marker: something happened at one moment; no duration is claimed.
    // annotation: a note the person added with "Add marker".
    QString kind;
    QString category;
    QString name;
    qint64 startNs = 0;
    qint64 endNs = -1;
    int ok = -1; // -1 unknown, 0 failed, 1 succeeded
    QVariantMap fields;
    quint64 id = 0;     // 0 for markers
    quint64 parent = 0; // 0: no parent
    QString thread;     // "main", the thread's name, or "pool"
};

// Means "the innermost Scope or ParentScope open on this thread".
constexpr quint64 kCurrentParent = ~quint64(0);

// The innermost open Scope / ParentScope on this thread, or 0.
quint64 currentParent();

// Where events go while recording (the recorder's session). Set by the
// recorder; implementations must be thread-safe.
class Sink
{
public:
    virtual ~Sink() = default;
    virtual void addEvent(Event event) = 0;
    // event.id is already assigned. Returns false when the sink is full
    // (the span is then counted as dropped and end() does nothing).
    virtual bool openSpan(Event event) = 0;
    virtual void closeSpan(quint64 id, qint64 endNs, int ok, const QVariantMap &fields) = 0;
};

void setSink(Sink *sink);
// Removes `owner` if it is the current sink, and leaves any other sink in
// place (so the recorder stopping never cuts off the UI Lab's collection).
void clearSink(Sink *owner);

// Keeps only short scalar facts: at most kMaxFields keys of letters and
// digits, and strings of at most 64 characters. Anything else is dropped.
constexpr int kMaxFields = 12;
QVariantMap cleanFields(const QVariantMap &fields);

void marker(const QString &category, const QString &name, const QVariantMap &fields = {}, int ok = -1,
            quint64 parent = kCurrentParent);
// A span measured elsewhere. Returns its id (0 when not recording).
quint64 span(const QString &category, const QString &name, qint64 startNs, qint64 endNs, int ok = -1,
             const QVariantMap &fields = {}, quint64 parent = kCurrentParent);

// For operations whose end comes later (a drag, a resize, a network
// request). Returns the span's id, 0 when not recording or the sink is
// full; end(0, ...) does nothing. The id can be another span's parent.
quint64 begin(const QString &category, const QString &name, const QVariantMap &fields = {},
              quint64 parent = kCurrentParent);
void end(quint64 id, int ok = -1, const QVariantMap &fields = {});

// begin() with a start time measured earlier (when a job was queued).
quint64 beginAt(qint64 startNs, const QString &category, const QString &name, const QVariantMap &fields = {},
                quint64 parent = kCurrentParent);

// A span ended explicitly with done(), and the current parent on its thread
// while open, for sequential steps inside one long function. Destroyed
// without done() (an exception, an early return), it ends as failed.
class Step
{
public:
    Step(const QString &category, const QString &name, const QVariantMap &fields = {}, quint64 parent = kCurrentParent);
    ~Step();
    void done(int ok = 1, const QVariantMap &fields = {});
    // The span's id, also after done(); 0 when not recording.
    [[nodiscard]] quint64 id() const { return m_span; }
    Step(const Step &) = delete;
    Step &operator=(const Step &) = delete;

private:
    quint64 m_span = 0;
    quint64 m_id = 0; // while open
    bool m_pushed = false;
};

// Makes `parent` the current parent on this thread until destroyed, without
// being a span itself: for work that runs on another thread (a job queued
// to a worker) on behalf of a span opened elsewhere.
class ParentScope
{
public:
    explicit ParentScope(quint64 parent);
    ~ParentScope();
    ParentScope(const ParentScope &) = delete;
    ParentScope &operator=(const ParentScope &) = delete;

private:
    bool m_pushed = false;
};

// Times a scope as a span, if a recording was running when it began. While
// open it is the current parent on its thread.
class Scope
{
public:
    Scope(QString category, QString name, QVariantMap fields = {}, quint64 parent = kCurrentParent);
    ~Scope();
    void setOk(bool ok) { m_ok = ok ? 1 : 0; }
    void setField(const QString &key, const QVariant &value)
    {
        if (m_startNs >= 0) {
            m_fields.insert(key, value);
        }
    }
    // 0 when not recording.
    [[nodiscard]] quint64 id() const { return m_id; }

    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;

private:
    QString m_category;
    QString m_name;
    QVariantMap m_fields;
    qint64 m_startNs = -1;
    int m_ok = -1;
    quint64 m_id = 0;
    quint64 m_parent = 0;
};
} // namespace LiveEvents

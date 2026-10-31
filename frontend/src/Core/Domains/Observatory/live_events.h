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
};

// Where events go while recording (the recorder's session). Set by the
// recorder; implementations must be thread-safe.
class Sink
{
public:
    virtual ~Sink() = default;
    virtual void addEvent(Event event) = 0;
    virtual quint64 openSpan(Event event) = 0;
    virtual void closeSpan(quint64 token, qint64 endNs, int ok, const QVariantMap &fields) = 0;
};

void setSink(Sink *sink);
// Removes `owner` if it is the current sink, and leaves any other sink in
// place (so the recorder stopping never cuts off the UI Lab's collection).
void clearSink(Sink *owner);

// Keeps only short scalar facts: at most 8 keys of letters and digits, and
// strings of at most 64 characters. Anything else is dropped.
QVariantMap cleanFields(const QVariantMap &fields);

void marker(const QString &category, const QString &name, const QVariantMap &fields = {}, int ok = -1);
void span(const QString &category, const QString &name, qint64 startNs, qint64 endNs, int ok = -1,
          const QVariantMap &fields = {});

// For operations whose end comes later (a drag, a resize). Returns 0 when
// not recording; end(0, ...) does nothing.
quint64 begin(const QString &category, const QString &name, const QVariantMap &fields = {});
void end(quint64 token, int ok = -1, const QVariantMap &fields = {});

// Times a scope as a span, if a recording was running when it began.
class Scope
{
public:
    Scope(QString category, QString name, QVariantMap fields = {});
    ~Scope();
    void setOk(bool ok) { m_ok = ok ? 1 : 0; }
    void setField(const QString &key, const QVariant &value) { m_fields.insert(key, value); }

    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;

private:
    QString m_category;
    QString m_name;
    QVariantMap m_fields;
    qint64 m_startNs = -1;
    int m_ok = -1;
};
} // namespace LiveEvents

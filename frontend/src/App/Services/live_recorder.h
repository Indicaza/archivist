#pragma once

#include "Core/Domains/Observatory/live_session.h"
#include "Core/Domains/Observatory/observatory_records.h"
#include "Core/Domains/Observatory/process_metrics.h"

#include <QHash>
#include <QJsonObject>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QThreadPool>
#include <QTimer>
#include <QVariantMap>

class QQuickWindow;

// The Observatory live recorder ("flight recorder"): while a recording
// runs, it notes every window's frame swaps, how late a UI-thread timer
// fires, the process's CPU, memory and threads, and the operations the app
// reports through LiveEvents. The results become an Observatory run
// (Core/Domains/Observatory/live_store.h).
//
// It belongs to the application, not to the Observatory panel, so closing
// or moving the panel doesn't stop or orphan a recording. A recording always
// ends: when asked, when its chosen duration (at most 120 s) runs out, or
// when the app quits.
//
// Work on the render threads is one lock and one append per frame; files
// are written on a separate thread.
class LiveRecorder final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool recording READ recording NOTIFY stateChanged)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString runId READ runId NOTIFY stateChanged)
    Q_PROPERTY(int elapsedSeconds READ elapsedSeconds NOTIFY elapsedChanged)
    Q_PROPERTY(int durationSeconds READ durationSeconds NOTIFY stateChanged)

public:
    // The durations the recorder accepts, in seconds.
    static const QList<int> &allowedDurations();
    static constexpr int kDefaultDuration = 30;

    explicit LiveRecorder(QString root, QObject *parent = nullptr);
    ~LiveRecorder() override;

    [[nodiscard]] bool recording() const { return m_state == State::Recording; }
    [[nodiscard]] QString state() const;
    [[nodiscard]] QString runId() const { return m_runId; }
    [[nodiscard]] int elapsedSeconds() const;
    [[nodiscard]] int durationSeconds() const { return m_durationS; }

    // Results are the "result" (or "error": {code, message}) of the control
    // envelope; see handle().
    Q_INVOKABLE QVariantMap start(int durationSeconds = kDefaultDuration, const QString &label = {},
                                  const QString &startedBy = QStringLiteral("panel"));
    Q_INVOKABLE QVariantMap stop(const QString &reason = QStringLiteral("user"));
    Q_INVOKABLE QVariantMap mark(const QString &label = {});
    Q_INVOKABLE QVariantMap status() const;

    // For QML: time an operation (begin returns 0 when not recording).
    Q_INVOKABLE double begin(const QString &category, const QString &name, const QVariantMap &fields = {});
    Q_INVOKABLE void end(double token, bool ok = true, const QVariantMap &fields = {});
    Q_INVOKABLE void marker(const QString &category, const QString &name, const QVariantMap &fields = {});

    // The /observatory/api/live/<operation> requests (status, start, stop,
    // mark), answered in the Observatory control envelope. `caller` is
    // "panel" or "agent".
    Observatory::Response handle(const QString &operation, const QByteArray &body, const QString &caller);

    // Blocks until files being written are on disk (used at quit and in tests).
    void waitForWrites();

    // Shorter timers for tests.
    void setIntervalsForTesting(int probeMs, int sampleMs, int flushMs);

signals:
    void stateChanged();
    void elapsedChanged();
    // A recording finished and its files are written.
    void finished(const QString &runId, const QString &status);

private:
    enum class State { Idle, Recording, Finalizing };

    void attachWindows();
    void attachWindow(QQuickWindow *window);
    void detachAll();
    void probe();
    void sample();
    void flush();
    QVariantMap failure(const QString &code, const QString &message) const;
    QJsonObject runningSummary(const QString &label, const QString &startedBy) const;

    QString m_root;
    State m_state = State::Idle;
    Live::Session m_session;
    QString m_runId;
    QString m_directory;
    QString m_label;
    QString m_startedBy;
    int m_durationS = 0;
    qint64 m_startNs = 0;
    QString m_startedAt;

    // The summary as written at the start; the writer thread adds the
    // source (git) facts. Guarded by m_summaryMutex.
    mutable QMutex m_summaryMutex;
    QJsonObject m_running;

    struct Attached
    {
        QPointer<QQuickWindow> window;
        int id = 0;
        QMetaObject::Connection frames;
        QMetaObject::Connection destroyed;
    };
    QList<Attached> m_windows;
    int m_nextWindowId = 1;

    QTimer m_probeTimer;
    QTimer m_sampleTimer;
    QTimer m_flushTimer;
    QTimer m_limitTimer;
    qint64 m_lastProbeNs = 0;
    int m_probeMs = 50;
    ProcessMetrics::Reading m_lastReading;
    qint64 m_lastReadingNs = 0;
    QMetaObject::Connection m_appState;

    QVariantMap m_lastRun;
    QString m_writeError;

    // One thread, so the stream's chunks and the final files are written in order.
    QThreadPool m_writer;
};

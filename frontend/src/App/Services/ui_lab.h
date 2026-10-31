#pragma once

#include "Core/Domains/Observatory/live_events.h"

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantMap>

#include <atomic>

class QQuickWindow;

// The Observatory UI Lab's hook into the running app. It exists only when
// ARCHIVIST_UI_LAB_SCENARIO is set, and only in a disposable profile
// (ARCHIVIST_PROFILE_DIR, see AppProfile); otherwise the app refuses to
// start. The scenario itself runs in QML (UiLabDriver.qml); this gives it a
// monotonic clock, the window's real frame swaps, synthetic mouse input
// delivered through the window, and a place to write results.
//
// It also collects what the app reports through LiveEvents (the same
// spans and markers the live recorder sees) and how late the event loop
// runs, all on one clock (LiveEvents::nowNs), and tags each event with the
// action that was running when it began. So a slow action can be opened up
// into the operations measured inside it. Collection is bounded.
class UiLab final : public QObject, public LiveEvents::Sink
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap scenario READ scenario CONSTANT)

public:
    explicit UiLab(QObject *parent = nullptr);
    ~UiLab() override;

    // At most this many operations and stalls are kept.
    static constexpr int kMaxEvents = 50000;
    static constexpr int kMaxStalls = 5000;
    // The event-loop probe: a timer every kProbeMs; firing kStallMs or more
    // late is a stall.
    static constexpr int kProbeMs = 50;
    static constexpr int kStallMs = 50;

    // ARCHIVIST_UI_LAB_SCENARIO is set.
    [[nodiscard]] static bool requested();

    // Reads the scenario and checks, before anything opens a database, that
    // the database and settings are inside the test profile.
    bool load(const QString &databasePath, QString *error);

    [[nodiscard]] QVariantMap scenario() const { return m_scenario; }

    // Milliseconds on the shared monotonic clock (LiveEvents::nowNs), so
    // actions, operations, frames and stalls line up.
    Q_INVOKABLE double nowMs() const;

    // The action now running (its index in the plan), or -1 between
    // actions. Operations that begin while it is set are tagged with it.
    Q_INVOKABLE void setAction(int index);
    // Everything collected so far: { clock, operations: [{kind, category,
    // name, startMs, endMs, ok, action, fields}], stalls: [{atMs, lateMs,
    // action}], dropped, open }.
    Q_INVOKABLE QVariantMap operations() const;

    // Deterministic chat fixtures for chat scenarios, written to this test
    // profile's database (never anywhere else): spec {seed, chats: [{key,
    // title, messages}]}. Runs on the database writer thread; seedStatus()
    // is {state: idle|running|done|failed, chats: {key: chatId}, error,
    // messages}. Message text is generated from the seed (long paragraphs,
    // Markdown lists, code blocks), so every run gets the same content.
    Q_INVOKABLE bool seedChats(const QVariantMap &spec);
    Q_INVOKABLE QVariantMap seedStatus() const { return m_seed; }
    // The text of message `index` for a seed (exposed for tests).
    static QString fixtureMessage(quint32 seed, int index);

    // LiveEvents::Sink (any thread).
    void addEvent(LiveEvents::Event event) override;
    quint64 openSpan(LiveEvents::Event event) override;
    void closeSpan(quint64 token, qint64 endNs, int ok, const QVariantMap &fields) override;

    // Records every frame the window swaps (QQuickWindow::frameSwapped, on
    // the render thread, timed there).
    Q_INVOKABLE void attachWindow(QObject *window);
    // Frames swapped in (fromMs, toMs]: count, first (ms after fromMs),
    // last (ms after fromMs), maxIntervalMs between consecutive swaps.
    Q_INVOKABLE QVariantMap frames(double fromMs, double toMs) const;
    Q_INVOKABLE double lastFrameMs() const;

    // Where this instance keeps its data, and whether each is in the profile.
    Q_INVOKABLE QVariantMap isolation() const;

    // The first item under the window with this objectName, or null.
    Q_INVOKABLE QObject *findItem(const QString &objectName) const;
    // Sends a left-button mouse event ("press", "move", "release") to the
    // window at a scene position, as the windowing system would.
    Q_INVOKABLE bool mouse(const QString &type, double sceneX, double sceneY);

    // Writes the results (ARCHIVIST_UI_LAB_OUTPUT), replacing the file
    // atomically, so a crash leaves the last complete copy.
    Q_INVOKABLE bool writeResult(const QVariantMap &result);
    Q_INVOKABLE void log(const QString &message) const;
    Q_INVOKABLE void finish(int exitCode);

private:
    void probe();

    struct Recorded
    {
        LiveEvents::Event event;
        int action = -1;
    };

    QElapsedTimer m_clock;
    std::atomic<int> m_action{-1};
    mutable QMutex m_eventsMutex;
    QList<Recorded> m_events;
    QHash<quint64, Recorded> m_open;
    quint64 m_nextToken = 1;
    int m_dropped = 0;
    QList<QVariantMap> m_stalls;
    QTimer m_probe;
    qint64 m_lastProbeNs = 0;
    bool m_sinkInstalled = false;
    QVariantMap m_seed{{QStringLiteral("state"), QStringLiteral("idle")}};
    QPointer<QQuickWindow> m_window;
    mutable QMutex m_framesMutex;
    QList<double> m_frames;
    QVariantMap m_scenario;
    QString m_databasePath;
    QString m_outputPath;
    bool m_buttonDown = false;
};

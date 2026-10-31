#pragma once

#include "live_events.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMutex>
#include <QString>

#include <limits>

// What one live recording collects, and how it is summarised. The recorder
// (App/Services/live_recorder) feeds a Session from the windows' render
// threads and the UI thread; everything here is plain data and arithmetic,
// so it can be tested without a window.
//
// Memory is bounded: past each limit new items are counted as dropped
// instead of stored, and the summary says so.
namespace Live
{
// Bump when what a measurement means changes; recordings with different
// versions are not compared.
//  1: first recorder.
//  2: CPU % is left unknown for a sample taken less than half a sample
//     interval after the previous reading (the first one at start), which
//     inflated the maximum and the mean of short recordings.
inline constexpr int kMeasurementVersion = 2;

struct WindowInfo
{
    int id = 0;
    QString role;      // "main", "floating", "detached", or the QML type
    QString className; // the window's C++/QML class, never its title
    int width = 0;
    int height = 0;
    double devicePixelRatio = 0;
    qint64 attachedNs = 0;
    qint64 detachedNs = -1;
};

struct Frame
{
    qint64 ns = 0;
    int window = 0;
};

struct Probe
{
    qint64 ns = 0;     // when the probe timer actually fired
    double delayMs = 0; // how much later than scheduled
};

struct Sample
{
    qint64 ns = 0;
    double cpuPercent = std::numeric_limits<double>::quiet_NaN(); // of one core
    qint64 rssBytes = -1;
    qint64 footprintBytes = -1; // macOS phys_footprint (what Activity Monitor shows)
    int threads = -1;
};

struct Limits
{
    int maxFrames = 200000;
    int maxEvents = 20000;
    int maxProbes = 6000;
    int maxSamples = 1000;
};

struct Thresholds
{
    double longGapMs = 50;   // a frame gap or stall at least this long is notable
    double idleGapMs = 250;  // longer frame gaps usually mean nothing was drawing
    double probeIntervalMs = 50;
};

struct Snapshot
{
    qint64 startNs = 0;
    QList<WindowInfo> windows;
    QList<Frame> frames;
    QList<Probe> probes;
    QList<Sample> samples;
    QList<LiveEvents::Event> events;
    qint64 droppedFrames = 0;
    qint64 droppedEvents = 0;
    qint64 droppedProbes = 0;
    qint64 droppedSamples = 0;
};

class Session final : public LiveEvents::Sink
{
public:
    void reset(qint64 startNs, Limits limits = {});

    // Windows are numbered from 1 in the order they are first seen.
    void addWindow(const WindowInfo &window);
    void updateWindowSize(int id, int width, int height, double devicePixelRatio);
    void detachWindow(int id, qint64 ns);

    // Called on a render thread for every frame swap: one lock, one append.
    void addFrame(int window, qint64 ns);
    void addProbe(qint64 ns, double delayMs);
    void addSample(const Sample &sample);

    void addEvent(LiveEvents::Event event) override;
    bool openSpan(LiveEvents::Event event) override;
    void closeSpan(quint64 id, qint64 endNs, int ok, const QVariantMap &fields) override;

    // Everything recorded since the previous call (windows: all of them),
    // for the crash-safe stream on disk.
    Snapshot takeChunk();
    // Everything so far. Spans still open are included as unfinished.
    Snapshot snapshot() const;

    qint64 frameCount() const;
    qint64 eventCount() const;
    int windowCount() const;

private:
    mutable QMutex m_frameMutex;
    QList<Frame> m_frames;
    qint64 m_droppedFrames = 0;
    int m_maxFrames = Limits{}.maxFrames;

    mutable QMutex m_mutex;
    qint64 m_startNs = 0;
    Limits m_limits;
    QList<WindowInfo> m_windows;
    QList<Probe> m_probes;
    QList<Sample> m_samples;
    QList<LiveEvents::Event> m_events;
    QHash<quint64, LiveEvents::Event> m_open;
    qint64 m_droppedEvents = 0;
    qint64 m_droppedProbes = 0;
    qint64 m_droppedSamples = 0;

    // How far takeChunk has got.
    qsizetype m_chunkFrames = 0;
    qsizetype m_chunkProbes = 0;
    qsizetype m_chunkSamples = 0;
    qsizetype m_chunkEvents = 0;
};

// Milliseconds since the recording started, to 0.01 ms.
double relativeMs(qint64 ns, qint64 startNs);

// One line of the crash-safe stream (live-stream.ndjson).
QJsonObject chunkJson(const Snapshot &chunk);
// Rebuilds a recording from its stream, as far as it got. Times stay
// relative: the result's startNs is 0. Lines that can't be read are counted.
Snapshot fromStream(const QByteArray &ndjson, int *unreadableLines = nullptr);

// metrics.json: every frame swap, probe and resource sample.
QJsonObject metricsJson(const Snapshot &snapshot);
// events.json: operations, markers and notes, in time order.
QJsonObject eventsJson(const Snapshot &snapshot);
// The figures that go in summary.json under live.stats.
QJsonObject statsJson(const Snapshot &snapshot, qint64 endNs, const Thresholds &thresholds = {});
// The notable moments, worst first (see live_session.cpp for the rules).
QJsonArray spikesJson(const Snapshot &snapshot, const Thresholds &thresholds = {}, int limit = 20);

// Nearest-rank percentile of sorted values; NaN when empty.
double percentile(const QList<double> &sorted, double p);
} // namespace Live

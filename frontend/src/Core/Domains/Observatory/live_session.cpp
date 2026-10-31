#include "live_session.h"

#include <QJsonDocument>
#include <QMutexLocker>

#include <algorithm>
#include <cmath>

namespace Live
{
namespace
{
QJsonValue number(double value)
{
    return std::isfinite(value) ? QJsonValue(std::round(value * 100.0) / 100.0) : QJsonValue(QJsonValue::Null);
}

QJsonValue count(qint64 value)
{
    return value < 0 ? QJsonValue(QJsonValue::Null) : QJsonValue(static_cast<double>(value));
}

QJsonObject windowJson(const WindowInfo &window, qint64 startNs)
{
    return {
        {QStringLiteral("id"), window.id},
        {QStringLiteral("role"), window.role},
        {QStringLiteral("className"), window.className},
        {QStringLiteral("width"), window.width},
        {QStringLiteral("height"), window.height},
        {QStringLiteral("devicePixelRatio"), number(window.devicePixelRatio)},
        {QStringLiteral("attachedMs"), relativeMs(window.attachedNs, startNs)},
        {QStringLiteral("detachedMs"), window.detachedNs < 0 ? QJsonValue(QJsonValue::Null) : QJsonValue(relativeMs(window.detachedNs, startNs))},
    };
}

WindowInfo windowFromJson(const QJsonObject &object)
{
    WindowInfo window;
    window.id = object.value(QStringLiteral("id")).toInt();
    window.role = object.value(QStringLiteral("role")).toString();
    window.className = object.value(QStringLiteral("className")).toString();
    window.width = object.value(QStringLiteral("width")).toInt();
    window.height = object.value(QStringLiteral("height")).toInt();
    window.devicePixelRatio = object.value(QStringLiteral("devicePixelRatio")).toDouble();
    window.attachedNs = static_cast<qint64>(object.value(QStringLiteral("attachedMs")).toDouble() * 1e6);
    const QJsonValue detached = object.value(QStringLiteral("detachedMs"));
    window.detachedNs = detached.isDouble() ? static_cast<qint64>(detached.toDouble() * 1e6) : -1;
    return window;
}

QJsonObject sampleJson(const Sample &sample, qint64 startNs)
{
    return {
        {QStringLiteral("atMs"), relativeMs(sample.ns, startNs)},
        {QStringLiteral("cpuPercent"), number(sample.cpuPercent)},
        {QStringLiteral("rssBytes"), count(sample.rssBytes)},
        {QStringLiteral("footprintBytes"), count(sample.footprintBytes)},
        {QStringLiteral("threads"), sample.threads < 0 ? QJsonValue(QJsonValue::Null) : QJsonValue(sample.threads)},
    };
}

Sample sampleFromJson(const QJsonObject &object)
{
    Sample sample;
    sample.ns = static_cast<qint64>(object.value(QStringLiteral("atMs")).toDouble() * 1e6);
    const QJsonValue cpu = object.value(QStringLiteral("cpuPercent"));
    sample.cpuPercent = cpu.isDouble() ? cpu.toDouble() : std::numeric_limits<double>::quiet_NaN();
    const QJsonValue rss = object.value(QStringLiteral("rssBytes"));
    sample.rssBytes = rss.isDouble() ? static_cast<qint64>(rss.toDouble()) : -1;
    const QJsonValue footprint = object.value(QStringLiteral("footprintBytes"));
    sample.footprintBytes = footprint.isDouble() ? static_cast<qint64>(footprint.toDouble()) : -1;
    const QJsonValue threads = object.value(QStringLiteral("threads"));
    sample.threads = threads.isDouble() ? threads.toInt() : -1;
    return sample;
}

QJsonObject eventJson(const LiveEvents::Event &event, qint64 startNs)
{
    const bool ended = event.endNs >= 0;
    QJsonObject object{
        {QStringLiteral("kind"), event.kind},
        {QStringLiteral("category"), event.category},
        {QStringLiteral("name"), event.name},
        {QStringLiteral("atMs"), relativeMs(event.startNs, startNs)},
        {QStringLiteral("endMs"), ended ? QJsonValue(relativeMs(event.endNs, startNs)) : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("durationMs"), ended && event.kind == QLatin1String("span") ? number((event.endNs - event.startNs) / 1e6) : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("ok"), event.ok < 0 ? QJsonValue(QJsonValue::Null) : QJsonValue(event.ok == 1)},
    };
    if (!event.fields.isEmpty()) {
        object.insert(QStringLiteral("fields"), QJsonObject::fromVariantMap(event.fields));
    }
    return object;
}

LiveEvents::Event eventFromJson(const QJsonObject &object)
{
    LiveEvents::Event event;
    event.kind = object.value(QStringLiteral("kind")).toString();
    event.category = object.value(QStringLiteral("category")).toString();
    event.name = object.value(QStringLiteral("name")).toString();
    event.startNs = static_cast<qint64>(object.value(QStringLiteral("atMs")).toDouble() * 1e6);
    const QJsonValue end = object.value(QStringLiteral("endMs"));
    event.endNs = end.isDouble() ? static_cast<qint64>(end.toDouble() * 1e6) : -1;
    const QJsonValue ok = object.value(QStringLiteral("ok"));
    event.ok = ok.isBool() ? (ok.toBool() ? 1 : 0) : -1;
    event.fields = LiveEvents::cleanFields(object.value(QStringLiteral("fields")).toObject().toVariantMap());
    return event;
}

template <typename T>
QList<T> tail(const QList<T> &list, qsizetype from)
{
    return from >= list.size() ? QList<T>() : list.mid(from);
}

// Per window, the intervals between consecutive frame swaps, in time order.
struct Interval
{
    int window = 0;
    qint64 startNs = 0; // the earlier swap
    double ms = 0;
    double previousMs = -1; // -1: none
    double nextMs = -1;
};

QHash<int, QList<Interval>> intervalsByWindow(const QList<Frame> &frames)
{
    QHash<int, QList<qint64>> swaps;
    for (const Frame &frame : frames) {
        swaps[frame.window].append(frame.ns);
    }
    QHash<int, QList<Interval>> result;
    for (auto it = swaps.begin(); it != swaps.end(); ++it) {
        QList<qint64> &times = it.value();
        std::sort(times.begin(), times.end());
        QList<Interval> &list = result[it.key()];
        for (qsizetype i = 1; i < times.size(); ++i) {
            list.append({it.key(), times[i - 1], (times[i] - times[i - 1]) / 1e6});
        }
        for (qsizetype i = 0; i < list.size(); ++i) {
            list[i].previousMs = i > 0 ? list[i - 1].ms : -1;
            list[i].nextMs = i + 1 < list.size() ? list[i + 1].ms : -1;
        }
    }
    return result;
}

// The probe's stall periods: [firedNs - delay, firedNs].
bool overlapsStall(const QList<Probe> &probes, qint64 fromNs, qint64 toNs, double longGapMs)
{
    for (const Probe &probe : probes) {
        if (probe.delayMs < longGapMs) {
            continue;
        }
        const qint64 stallStart = probe.ns - static_cast<qint64>(probe.delayMs * 1e6);
        if (stallStart < toNs && probe.ns > fromNs) {
            return true;
        }
    }
    return false;
}
} // namespace

double relativeMs(qint64 ns, qint64 startNs)
{
    return std::round((ns - startNs) / 1e4) / 100.0;
}

double percentile(const QList<double> &sorted, double p)
{
    if (sorted.isEmpty()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const auto rank = static_cast<qsizetype>(std::ceil(p / 100.0 * sorted.size()));
    return sorted.at(std::clamp<qsizetype>(rank - 1, 0, sorted.size() - 1));
}

// ---- Session ------------------------------------------------------------------

void Session::reset(qint64 startNs, Limits limits)
{
    {
        QMutexLocker lock(&m_frameMutex);
        m_frames.clear();
        m_frames.reserve(std::min(limits.maxFrames, 20000));
        m_droppedFrames = 0;
        m_maxFrames = limits.maxFrames;
    }
    QMutexLocker lock(&m_mutex);
    m_startNs = startNs;
    m_limits = limits;
    m_windows.clear();
    m_probes.clear();
    m_samples.clear();
    m_events.clear();
    m_open.clear();
    m_droppedEvents = m_droppedProbes = m_droppedSamples = 0;
    m_chunkFrames = m_chunkProbes = m_chunkSamples = m_chunkEvents = 0;
}

void Session::addWindow(const WindowInfo &window)
{
    QMutexLocker lock(&m_mutex);
    m_windows.append(window);
}

void Session::updateWindowSize(int id, int width, int height, double devicePixelRatio)
{
    QMutexLocker lock(&m_mutex);
    for (WindowInfo &window : m_windows) {
        if (window.id == id) {
            window.width = width;
            window.height = height;
            window.devicePixelRatio = devicePixelRatio;
        }
    }
}

void Session::detachWindow(int id, qint64 ns)
{
    QMutexLocker lock(&m_mutex);
    for (WindowInfo &window : m_windows) {
        if (window.id == id && window.detachedNs < 0) {
            window.detachedNs = ns;
        }
    }
}

void Session::addFrame(int window, qint64 ns)
{
    QMutexLocker lock(&m_frameMutex);
    if (m_frames.size() >= m_maxFrames) {
        ++m_droppedFrames;
        return;
    }
    m_frames.append({ns, window});
}

void Session::addProbe(qint64 ns, double delayMs)
{
    QMutexLocker lock(&m_mutex);
    if (m_probes.size() >= m_limits.maxProbes) {
        ++m_droppedProbes;
        return;
    }
    m_probes.append({ns, delayMs});
}

void Session::addSample(const Sample &sample)
{
    QMutexLocker lock(&m_mutex);
    if (m_samples.size() >= m_limits.maxSamples) {
        ++m_droppedSamples;
        return;
    }
    m_samples.append(sample);
}

void Session::addEvent(LiveEvents::Event event)
{
    QMutexLocker lock(&m_mutex);
    if (m_events.size() + m_open.size() >= m_limits.maxEvents) {
        ++m_droppedEvents;
        return;
    }
    m_events.append(std::move(event));
}

quint64 Session::openSpan(LiveEvents::Event event)
{
    QMutexLocker lock(&m_mutex);
    if (m_events.size() + m_open.size() >= m_limits.maxEvents || m_open.size() >= 256) {
        ++m_droppedEvents;
        return 0;
    }
    const quint64 token = m_nextToken++;
    m_open.insert(token, std::move(event));
    return token;
}

void Session::closeSpan(quint64 token, qint64 endNs, int ok, const QVariantMap &fields)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_open.find(token);
    if (it == m_open.end()) {
        return;
    }
    LiveEvents::Event event = std::move(it.value());
    m_open.erase(it);
    event.endNs = endNs;
    event.ok = ok;
    for (auto field = fields.constBegin(); field != fields.constEnd(); ++field) {
        event.fields.insert(field.key(), field.value());
    }
    event.fields = LiveEvents::cleanFields(event.fields);
    m_events.append(std::move(event));
}

Snapshot Session::takeChunk()
{
    Snapshot chunk;
    {
        QMutexLocker lock(&m_frameMutex);
        chunk.frames = tail(m_frames, m_chunkFrames);
        m_chunkFrames = m_frames.size();
        chunk.droppedFrames = m_droppedFrames;
    }
    QMutexLocker lock(&m_mutex);
    chunk.startNs = m_startNs;
    chunk.windows = m_windows;
    chunk.probes = tail(m_probes, m_chunkProbes);
    chunk.samples = tail(m_samples, m_chunkSamples);
    chunk.events = tail(m_events, m_chunkEvents);
    m_chunkProbes = m_probes.size();
    m_chunkSamples = m_samples.size();
    m_chunkEvents = m_events.size();
    chunk.droppedEvents = m_droppedEvents;
    chunk.droppedProbes = m_droppedProbes;
    chunk.droppedSamples = m_droppedSamples;
    return chunk;
}

Snapshot Session::snapshot() const
{
    Snapshot all;
    {
        QMutexLocker lock(&m_frameMutex);
        all.frames = m_frames;
        all.droppedFrames = m_droppedFrames;
    }
    QMutexLocker lock(&m_mutex);
    all.startNs = m_startNs;
    all.windows = m_windows;
    all.probes = m_probes;
    all.samples = m_samples;
    all.events = m_events;
    for (const LiveEvents::Event &open : m_open) {
        LiveEvents::Event unfinished = open;
        unfinished.fields.insert(QStringLiteral("unfinished"), true);
        all.events.append(unfinished);
    }
    all.droppedEvents = m_droppedEvents;
    all.droppedProbes = m_droppedProbes;
    all.droppedSamples = m_droppedSamples;
    return all;
}

qint64 Session::frameCount() const
{
    QMutexLocker lock(&m_frameMutex);
    return m_frames.size();
}

qint64 Session::eventCount() const
{
    QMutexLocker lock(&m_mutex);
    return m_events.size();
}

int Session::windowCount() const
{
    QMutexLocker lock(&m_mutex);
    return static_cast<int>(m_windows.size());
}

// ---- the stream on disk ---------------------------------------------------------

QJsonObject chunkJson(const Snapshot &chunk)
{
    QJsonArray windows;
    for (const WindowInfo &window : chunk.windows) {
        windows.append(windowJson(window, chunk.startNs));
    }
    QJsonArray frames;
    for (const Frame &frame : chunk.frames) {
        frames.append(QJsonArray{frame.window, relativeMs(frame.ns, chunk.startNs)});
    }
    QJsonArray probes;
    for (const Probe &probe : chunk.probes) {
        probes.append(QJsonArray{relativeMs(probe.ns, chunk.startNs), number(probe.delayMs)});
    }
    QJsonArray samples;
    for (const Sample &sample : chunk.samples) {
        samples.append(sampleJson(sample, chunk.startNs));
    }
    QJsonArray events;
    for (const LiveEvents::Event &event : chunk.events) {
        events.append(eventJson(event, chunk.startNs));
    }
    return {
        {QStringLiteral("type"), QStringLiteral("chunk")},
        {QStringLiteral("windows"), windows},
        {QStringLiteral("frames"), frames},
        {QStringLiteral("probes"), probes},
        {QStringLiteral("samples"), samples},
        {QStringLiteral("events"), events},
        {QStringLiteral("dropped"), QJsonObject{
            {QStringLiteral("frames"), static_cast<double>(chunk.droppedFrames)},
            {QStringLiteral("events"), static_cast<double>(chunk.droppedEvents)},
            {QStringLiteral("probes"), static_cast<double>(chunk.droppedProbes)},
            {QStringLiteral("samples"), static_cast<double>(chunk.droppedSamples)},
        }},
    };
}

Snapshot fromStream(const QByteArray &ndjson, int *unreadableLines)
{
    Snapshot result;
    int unreadable = 0;
    const QList<QByteArray> lines = ndjson.split('\n');
    for (const QByteArray &line : lines) {
        if (line.trimmed().isEmpty()) {
            continue;
        }
        const QJsonDocument document = QJsonDocument::fromJson(line);
        const QJsonObject chunk = document.object();
        if (!document.isObject() || chunk.value(QStringLiteral("type")).toString() != QLatin1String("chunk")) {
            // The last line may be cut off by a crash mid-write.
            ++unreadable;
            continue;
        }
        result.windows.clear();
        for (const QJsonValue &window : chunk.value(QStringLiteral("windows")).toArray()) {
            result.windows.append(windowFromJson(window.toObject()));
        }
        for (const QJsonValue &value : chunk.value(QStringLiteral("frames")).toArray()) {
            const QJsonArray pair = value.toArray();
            result.frames.append({static_cast<qint64>(pair.at(1).toDouble() * 1e6), pair.at(0).toInt()});
        }
        for (const QJsonValue &value : chunk.value(QStringLiteral("probes")).toArray()) {
            const QJsonArray pair = value.toArray();
            result.probes.append({static_cast<qint64>(pair.at(0).toDouble() * 1e6), pair.at(1).toDouble()});
        }
        for (const QJsonValue &value : chunk.value(QStringLiteral("samples")).toArray()) {
            result.samples.append(sampleFromJson(value.toObject()));
        }
        for (const QJsonValue &value : chunk.value(QStringLiteral("events")).toArray()) {
            result.events.append(eventFromJson(value.toObject()));
        }
        const QJsonObject dropped = chunk.value(QStringLiteral("dropped")).toObject();
        result.droppedFrames = static_cast<qint64>(dropped.value(QStringLiteral("frames")).toDouble());
        result.droppedEvents = static_cast<qint64>(dropped.value(QStringLiteral("events")).toDouble());
        result.droppedProbes = static_cast<qint64>(dropped.value(QStringLiteral("probes")).toDouble());
        result.droppedSamples = static_cast<qint64>(dropped.value(QStringLiteral("samples")).toDouble());
    }
    if (unreadableLines) {
        *unreadableLines = unreadable;
    }
    return result;
}

// ---- artifacts ------------------------------------------------------------------

QJsonObject metricsJson(const Snapshot &snapshot)
{
    QJsonArray windows;
    for (const WindowInfo &window : snapshot.windows) {
        windows.append(windowJson(window, snapshot.startNs));
    }
    QHash<int, QJsonArray> swaps;
    QList<Frame> frames = snapshot.frames;
    std::stable_sort(frames.begin(), frames.end(), [](const Frame &a, const Frame &b) { return a.ns < b.ns; });
    for (const Frame &frame : frames) {
        swaps[frame.window].append(relativeMs(frame.ns, snapshot.startNs));
    }
    QJsonObject frameSwaps;
    for (auto it = swaps.constBegin(); it != swaps.constEnd(); ++it) {
        frameSwaps.insert(QString::number(it.key()), it.value());
    }
    QJsonArray probes;
    for (const Probe &probe : snapshot.probes) {
        probes.append(QJsonArray{relativeMs(probe.ns, snapshot.startNs), number(probe.delayMs)});
    }
    QJsonArray samples;
    for (const Sample &sample : snapshot.samples) {
        samples.append(sampleJson(sample, snapshot.startNs));
    }
    return {
        {QStringLiteral("schema"), QStringLiteral("archivist.observatory.live-metrics")},
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("measurementVersion"), kMeasurementVersion},
        {QStringLiteral("timeUnit"), QStringLiteral("ms since the recording started (monotonic clock)")},
        {QStringLiteral("windows"), windows},
        {QStringLiteral("frameSwaps"), frameSwaps},
        {QStringLiteral("eventLoopProbes"), probes},
        {QStringLiteral("samples"), samples},
        {QStringLiteral("definitions"), QJsonObject{
            {QStringLiteral("frameSwaps"), QStringLiteral("When each window's QQuickWindow::frameSwapped fired, timed on its render thread. "
                                                          "An interval between swaps is not GPU time, compositor latency or when the frame reached the screen. "
                                                          "Qt draws only when something changes, so long intervals while idle are normal.")},
            {QStringLiteral("eventLoopProbes"), QStringLiteral("[atMs, delayMs]: a UI-thread timer set to fire every 50 ms; delayMs is how much later it fired. "
                                                               "A large delay means the UI thread was busy (or the app was throttled in the background), not that a frame took that long to draw.")},
            {QStringLiteral("samples"), QStringLiteral("Every 500 ms: process CPU time as % of one core since the previous sample, resident memory, "
                                                       "macOS memory footprint, and thread count. null means the platform didn't provide it; "
                                                       "cpuPercent is also null for the first sample (no full interval before it).")},
        }},
    };
}

QJsonObject eventsJson(const Snapshot &snapshot)
{
    QList<LiveEvents::Event> events = snapshot.events;
    std::stable_sort(events.begin(), events.end(), [](const LiveEvents::Event &a, const LiveEvents::Event &b) {
        return a.startNs < b.startNs;
    });
    QJsonArray list;
    for (const LiveEvents::Event &event : events) {
        list.append(eventJson(event, snapshot.startNs));
    }
    return {
        {QStringLiteral("schema"), QStringLiteral("archivist.observatory.live-events")},
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("measurementVersion"), kMeasurementVersion},
        {QStringLiteral("events"), list},
        {QStringLiteral("definitions"), QJsonObject{
            {QStringLiteral("span"), QStringLiteral("An operation with a measured start and end (durationMs).")},
            {QStringLiteral("marker"), QStringLiteral("Something happened at atMs; no duration is claimed.")},
            {QStringLiteral("annotation"), QStringLiteral("A note added during the recording with Add marker.")},
        }},
    };
}

QJsonObject statsJson(const Snapshot &snapshot, qint64 endNs, const Thresholds &thresholds)
{
    const QHash<int, QList<Interval>> intervals = intervalsByWindow(snapshot.frames);
    QHash<int, qint64> frameCounts;
    for (const Frame &frame : snapshot.frames) {
        ++frameCounts[frame.window];
    }
    static const QList<double> bucketLimits{8.4, 16.8, 25.1, 33.4, 50, 100, 250};
    QJsonArray windows;
    QList<int> ids = frameCounts.keys();
    for (const WindowInfo &window : snapshot.windows) {
        if (!ids.contains(window.id)) {
            ids.append(window.id);
        }
    }
    std::sort(ids.begin(), ids.end());
    for (int id : ids) {
        const QList<Interval> list = intervals.value(id);
        QList<double> drawing;
        QList<qint64> buckets(bucketLimits.size() + 1, 0);
        int longGaps = 0;
        double maxAll = 0;
        for (const Interval &interval : list) {
            maxAll = std::max(maxAll, interval.ms);
            if (interval.ms >= thresholds.longGapMs) {
                ++longGaps;
            }
            if (interval.ms < thresholds.idleGapMs) {
                drawing.append(interval.ms);
            }
            const auto bucket = std::lower_bound(bucketLimits.begin(), bucketLimits.end(), interval.ms) - bucketLimits.begin();
            ++buckets[bucket];
        }
        std::sort(drawing.begin(), drawing.end());
        double sum = 0;
        for (double value : drawing) {
            sum += value;
        }
        QJsonArray histogram;
        for (qsizetype i = 0; i < buckets.size(); ++i) {
            histogram.append(QJsonObject{
                {QStringLiteral("upToMs"), i < bucketLimits.size() ? QJsonValue(bucketLimits[i]) : QJsonValue(QJsonValue::Null)},
                {QStringLiteral("count"), static_cast<double>(buckets[i])},
            });
        }
        windows.append(QJsonObject{
            {QStringLiteral("window"), id},
            {QStringLiteral("frames"), static_cast<double>(frameCounts.value(id))},
            {QStringLiteral("intervals"), static_cast<double>(list.size())},
            {QStringLiteral("whileDrawing"), QJsonObject{
                {QStringLiteral("definition"), QStringLiteral("intervals shorter than %1 ms").arg(thresholds.idleGapMs)},
                {QStringLiteral("count"), static_cast<double>(drawing.size())},
                {QStringLiteral("meanMs"), drawing.isEmpty() ? QJsonValue(QJsonValue::Null) : number(sum / drawing.size())},
                {QStringLiteral("p50Ms"), number(percentile(drawing, 50))},
                {QStringLiteral("p90Ms"), number(percentile(drawing, 90))},
                {QStringLiteral("p95Ms"), drawing.size() >= 20 ? number(percentile(drawing, 95)) : QJsonValue(QJsonValue::Null)},
                {QStringLiteral("p99Ms"), drawing.size() >= 100 ? number(percentile(drawing, 99)) : QJsonValue(QJsonValue::Null)},
                {QStringLiteral("maxMs"), number(drawing.isEmpty() ? std::numeric_limits<double>::quiet_NaN() : drawing.last())},
            }},
            {QStringLiteral("maxIntervalMs"), list.isEmpty() ? QJsonValue(QJsonValue::Null) : number(maxAll)},
            {QStringLiteral("gapsAtLeastMs"), thresholds.longGapMs},
            {QStringLiteral("gaps"), longGaps},
            {QStringLiteral("histogram"), histogram},
        });
    }

    QList<double> delays;
    int stalls = 0;
    for (const Probe &probe : snapshot.probes) {
        delays.append(probe.delayMs);
        if (probe.delayMs >= thresholds.longGapMs) {
            ++stalls;
        }
    }
    std::sort(delays.begin(), delays.end());

    double cpuSum = 0;
    double cpuMax = std::numeric_limits<double>::quiet_NaN();
    int cpuSamples = 0;
    qint64 rssFirst = -1, rssLast = -1, rssMax = -1;
    qint64 footFirst = -1, footLast = -1, footMax = -1;
    int threadsMin = -1, threadsMax = -1;
    for (const Sample &sample : snapshot.samples) {
        if (std::isfinite(sample.cpuPercent)) {
            cpuSum += sample.cpuPercent;
            cpuMax = cpuSamples ? std::max(cpuMax, sample.cpuPercent) : sample.cpuPercent;
            ++cpuSamples;
        }
        if (sample.rssBytes >= 0) {
            rssFirst = rssFirst < 0 ? sample.rssBytes : rssFirst;
            rssLast = sample.rssBytes;
            rssMax = std::max(rssMax, sample.rssBytes);
        }
        if (sample.footprintBytes >= 0) {
            footFirst = footFirst < 0 ? sample.footprintBytes : footFirst;
            footLast = sample.footprintBytes;
            footMax = std::max(footMax, sample.footprintBytes);
        }
        if (sample.threads >= 0) {
            threadsMin = threadsMin < 0 ? sample.threads : std::min(threadsMin, sample.threads);
            threadsMax = std::max(threadsMax, sample.threads);
        }
    }

    QJsonObject categories;
    int spans = 0, markers = 0, annotations = 0;
    for (const LiveEvents::Event &event : snapshot.events) {
        categories.insert(event.category, categories.value(event.category).toInt() + 1);
        if (event.kind == QLatin1String("span")) {
            ++spans;
        } else if (event.kind == QLatin1String("annotation")) {
            ++annotations;
        } else {
            ++markers;
        }
    }

    return {
        {QStringLiteral("durationMs"), relativeMs(endNs, snapshot.startNs)},
        {QStringLiteral("frames"), QJsonObject{
            {QStringLiteral("total"), static_cast<double>(snapshot.frames.size())},
            {QStringLiteral("windows"), windows},
        }},
        {QStringLiteral("eventLoop"), QJsonObject{
            {QStringLiteral("probes"), static_cast<double>(snapshot.probes.size())},
            {QStringLiteral("intervalMs"), thresholds.probeIntervalMs},
            {QStringLiteral("p50DelayMs"), number(percentile(delays, 50))},
            {QStringLiteral("p95DelayMs"), delays.size() >= 20 ? number(percentile(delays, 95)) : QJsonValue(QJsonValue::Null)},
            {QStringLiteral("maxDelayMs"), number(delays.isEmpty() ? std::numeric_limits<double>::quiet_NaN() : delays.last())},
            {QStringLiteral("stallsAtLeastMs"), thresholds.longGapMs},
            {QStringLiteral("stalls"), stalls},
        }},
        {QStringLiteral("cpu"), QJsonObject{
            {QStringLiteral("available"), cpuSamples > 0},
            {QStringLiteral("unit"), QStringLiteral("% of one core")},
            {QStringLiteral("samples"), cpuSamples},
            {QStringLiteral("meanPercent"), cpuSamples ? number(cpuSum / cpuSamples) : QJsonValue(QJsonValue::Null)},
            {QStringLiteral("maxPercent"), number(cpuMax)},
        }},
        {QStringLiteral("memory"), QJsonObject{
            {QStringLiteral("available"), rssFirst >= 0},
            {QStringLiteral("startRssBytes"), count(rssFirst)},
            {QStringLiteral("endRssBytes"), count(rssLast)},
            {QStringLiteral("maxRssBytes"), count(rssMax)},
            {QStringLiteral("growthRssBytes"), rssFirst >= 0 ? QJsonValue(static_cast<double>(rssLast - rssFirst)) : QJsonValue(QJsonValue::Null)},
            {QStringLiteral("footprintAvailable"), footFirst >= 0},
            {QStringLiteral("startFootprintBytes"), count(footFirst)},
            {QStringLiteral("endFootprintBytes"), count(footLast)},
            {QStringLiteral("maxFootprintBytes"), count(footMax)},
            {QStringLiteral("growthFootprintBytes"), footFirst >= 0 ? QJsonValue(static_cast<double>(footLast - footFirst)) : QJsonValue(QJsonValue::Null)},
        }},
        {QStringLiteral("threads"), QJsonObject{
            {QStringLiteral("available"), threadsMax >= 0},
            {QStringLiteral("min"), threadsMin < 0 ? QJsonValue(QJsonValue::Null) : QJsonValue(threadsMin)},
            {QStringLiteral("max"), threadsMax < 0 ? QJsonValue(QJsonValue::Null) : QJsonValue(threadsMax)},
        }},
        {QStringLiteral("events"), QJsonObject{
            {QStringLiteral("total"), static_cast<double>(snapshot.events.size())},
            {QStringLiteral("spans"), spans},
            {QStringLiteral("markers"), markers},
            {QStringLiteral("annotations"), annotations},
            {QStringLiteral("byCategory"), categories},
        }},
        {QStringLiteral("dropped"), QJsonObject{
            {QStringLiteral("frames"), static_cast<double>(snapshot.droppedFrames)},
            {QStringLiteral("events"), static_cast<double>(snapshot.droppedEvents)},
            {QStringLiteral("probes"), static_cast<double>(snapshot.droppedProbes)},
            {QStringLiteral("samples"), static_cast<double>(snapshot.droppedSamples)},
        }},
    };
}

// Notable moments, worst first:
//  - frame-gap: a frame interval of at least longGapMs, shorter than
//    idleGapMs, with intervals shorter than longGapMs on both sides (the
//    window was drawing steadily, then paused);
//  - frame-gap (longer): an interval of idleGapMs or more is only listed when
//    an event-loop stall overlaps it, since long pauses are normal when idle;
//  - event-loop-stall: the UI-thread probe fired at least longGapMs late.
QJsonArray spikesJson(const Snapshot &snapshot, const Thresholds &thresholds, int limit)
{
    struct Spike
    {
        QString type;
        qint64 startNs;
        double ms;
        int window;
        QString rule;
    };
    QList<Spike> spikes;
    const QHash<int, QList<Interval>> intervals = intervalsByWindow(snapshot.frames);
    for (auto it = intervals.constBegin(); it != intervals.constEnd(); ++it) {
        for (const Interval &interval : it.value()) {
            if (interval.ms < thresholds.longGapMs) {
                continue;
            }
            const qint64 endNs = interval.startNs + static_cast<qint64>(interval.ms * 1e6);
            if (interval.ms < thresholds.idleGapMs) {
                const bool steadyBefore = interval.previousMs >= 0 && interval.previousMs < thresholds.longGapMs;
                const bool steadyAfter = interval.nextMs >= 0 && interval.nextMs < thresholds.longGapMs;
                if (steadyBefore && steadyAfter) {
                    spikes.append({QStringLiteral("frame-gap"), interval.startNs, interval.ms, interval.window, QStringLiteral("between steady frames")});
                }
            } else if (overlapsStall(snapshot.probes, interval.startNs, endNs, thresholds.longGapMs)) {
                spikes.append({QStringLiteral("frame-gap"), interval.startNs, interval.ms, interval.window, QStringLiteral("overlaps an event-loop stall")});
            }
        }
    }
    for (const Probe &probe : snapshot.probes) {
        if (probe.delayMs >= thresholds.longGapMs) {
            spikes.append({QStringLiteral("event-loop-stall"), probe.ns - static_cast<qint64>(probe.delayMs * 1e6), probe.delayMs, 0, QStringLiteral("UI-thread timer fired late")});
        }
    }
    std::stable_sort(spikes.begin(), spikes.end(), [](const Spike &a, const Spike &b) { return a.ms > b.ms; });
    QJsonArray result;
    for (const Spike &spike : spikes) {
        if (result.size() >= limit) {
            break;
        }
        QJsonObject object{
            {QStringLiteral("type"), spike.type},
            {QStringLiteral("atMs"), relativeMs(spike.startNs, snapshot.startNs)},
            {QStringLiteral("durationMs"), number(spike.ms)},
            {QStringLiteral("rule"), spike.rule},
        };
        object.insert(QStringLiteral("window"), spike.window > 0 ? QJsonValue(spike.window) : QJsonValue(QJsonValue::Null));
        result.append(object);
    }
    return result;
}
} // namespace Live

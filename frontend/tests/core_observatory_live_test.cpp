#include "Core/Domains/Observatory/live_events.h"
#include "Core/Domains/Observatory/live_session.h"
#include "Core/Domains/Observatory/live_store.h"
#include "Core/Domains/Observatory/observatory_records.h"
#include "Core/Domains/Observatory/process_metrics.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include <cmath>
#include <limits>

// The parts of the live flight recorder that don't need a window: the event
// API, the bounded session, the statistics and the files on disk.
class ObservatoryLiveTest : public QObject
{
    Q_OBJECT

private:
    static constexpr qint64 Ms = 1000000;

    static QJsonObject readJson(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return {};
        }
        return QJsonDocument::fromJson(file.readAll()).object();
    }

    // Steady 16 ms frames on window 1 from `fromMs`, `count` of them.
    static void steadyFrames(Live::Session &session, double fromMs, int count, int window = 1)
    {
        for (int i = 0; i < count; ++i) {
            session.addFrame(window, static_cast<qint64>((fromMs + i * 16.0) * Ms));
        }
    }

    static QJsonObject runningSummary(const QString &runId, qint64 pid)
    {
        return {
            {QStringLiteral("schemaVersion"), 1},
            {QStringLiteral("kind"), QStringLiteral("live-session")},
            {QStringLiteral("runId"), runId},
            {QStringLiteral("status"), QStringLiteral("running")},
            {QStringLiteral("app"), QJsonObject{{QStringLiteral("pid"), static_cast<double>(pid)}}},
            {QStringLiteral("live"), QJsonObject{{QStringLiteral("requestedDurationS"), 30}}},
            {QStringLiteral("artifacts"), QJsonObject{{QStringLiteral("stream"), QStringLiteral("live-stream.ndjson")}}},
        };
    }

private slots:
    void cleanup()
    {
        LiveEvents::setSink(nullptr);
    }

    void eventApiDoesNothingWhileNotRecording()
    {
        QVERIFY(!LiveEvents::recording());
        QCOMPARE(LiveEvents::begin(QStringLiteral("sidebar"), QStringLiteral("resize")), quint64(0));
        LiveEvents::end(0);
        LiveEvents::marker(QStringLiteral("sidebar"), QStringLiteral("open"));
        { LiveEvents::Scope scope(QStringLiteral("workspace"), QStringLiteral("publish")); }

        Live::Session session;
        session.reset(LiveEvents::nowNs());
        QCOMPARE(session.snapshot().events.size(), 0);
    }

    void timestampsAreMonotonic()
    {
        qint64 previous = LiveEvents::nowNs();
        for (int i = 0; i < 10000; ++i) {
            const qint64 now = LiveEvents::nowNs();
            QVERIFY(now >= previous);
            previous = now;
        }
    }

    void spansMarkersAndUnfinishedOperationsAreRecorded()
    {
        Live::Session session;
        session.reset(LiveEvents::nowNs());
        LiveEvents::setSink(&session);
        QVERIFY(LiveEvents::recording());

        const quint64 drag = LiveEvents::begin(QStringLiteral("workspace"), QStringLiteral("drag"), {{QStringLiteral("kind"), QStringLiteral("view")}});
        QVERIFY(drag > 0);
        LiveEvents::marker(QStringLiteral("sidebar"), QStringLiteral("open"));
        {
            LiveEvents::Scope scope(QStringLiteral("workspace"), QStringLiteral("publish"));
            scope.setOk(true);
        }
        LiveEvents::end(drag, 1, {{QStringLiteral("outcome"), QStringLiteral("committed")}});
        const quint64 open = LiveEvents::begin(QStringLiteral("sidebar"), QStringLiteral("resize"));
        QVERIFY(open > 0);

        const Live::Snapshot snapshot = session.snapshot();
        QCOMPARE(snapshot.events.size(), 4);
        const QJsonArray events = Live::eventsJson(snapshot).value(QStringLiteral("events")).toArray();
        QCOMPARE(events.size(), 4);
        bool sawDrag = false, sawMarker = false, sawUnfinished = false;
        double lastAt = -1;
        for (const QJsonValue &value : events) {
            const QJsonObject event = value.toObject();
            QVERIFY(event.value(QStringLiteral("atMs")).toDouble() >= lastAt); // time order
            lastAt = event.value(QStringLiteral("atMs")).toDouble();
            if (event.value(QStringLiteral("name")).toString() == QLatin1String("drag")) {
                sawDrag = true;
                QCOMPARE(event.value(QStringLiteral("kind")).toString(), QStringLiteral("span"));
                QVERIFY(event.value(QStringLiteral("durationMs")).isDouble());
                QCOMPARE(event.value(QStringLiteral("ok")).toBool(), true);
                QCOMPARE(event.value(QStringLiteral("fields")).toObject().value(QStringLiteral("outcome")).toString(), QStringLiteral("committed"));
                QCOMPARE(event.value(QStringLiteral("fields")).toObject().value(QStringLiteral("kind")).toString(), QStringLiteral("view"));
            }
            if (event.value(QStringLiteral("name")).toString() == QLatin1String("open")) {
                sawMarker = true;
                QVERIFY(event.value(QStringLiteral("durationMs")).isNull()); // a marker claims no duration
            }
            if (event.value(QStringLiteral("name")).toString() == QLatin1String("resize")) {
                sawUnfinished = true;
                QVERIFY(event.value(QStringLiteral("endMs")).isNull());
                QCOMPARE(event.value(QStringLiteral("fields")).toObject().value(QStringLiteral("unfinished")).toBool(), true);
            }
        }
        QVERIFY(sawDrag && sawMarker && sawUnfinished);

        LiveEvents::setSink(nullptr);
        LiveEvents::end(open); // after the recording: ignored, no crash
    }

    void fieldsKeepOnlyShortScalars()
    {
        const QVariantMap clean = LiveEvents::cleanFields({
            {QStringLiteral("viewType"), QStringLiteral("chat")},
            {QStringLiteral("count"), 3},
            {QStringLiteral("long"), QString(200, QLatin1Char('x'))},
            {QStringLiteral("bad key"), 1},
            {QStringLiteral("list"), QVariantList{1, 2}},
            {QStringLiteral("map"), QVariantMap{{QStringLiteral("a"), 1}}},
        });
        QCOMPARE(clean.value(QStringLiteral("viewType")).toString(), QStringLiteral("chat"));
        QCOMPARE(clean.value(QStringLiteral("count")).toInt(), 3);
        QCOMPARE(clean.value(QStringLiteral("long")).toString().size(), 64);
        QVERIFY(!clean.contains(QStringLiteral("bad key")));
        QVERIFY(!clean.contains(QStringLiteral("list")));
        QVERIFY(!clean.contains(QStringLiteral("map")));
    }

    void buffersAreBounded()
    {
        Live::Session session;
        Live::Limits limits;
        limits.maxFrames = 100;
        limits.maxEvents = 5;
        limits.maxProbes = 3;
        limits.maxSamples = 2;
        session.reset(0, limits);
        steadyFrames(session, 0, 150);
        for (int i = 0; i < 9; ++i) {
            session.addEvent({QStringLiteral("marker"), QStringLiteral("test"), QStringLiteral("m"), i * Ms, -1, -1, {}});
            session.addProbe(i * Ms, 0);
            session.addSample({});
        }
        const Live::Snapshot snapshot = session.snapshot();
        QCOMPARE(snapshot.frames.size(), 100);
        QCOMPARE(snapshot.droppedFrames, qint64(50));
        QCOMPARE(snapshot.events.size(), 5);
        QCOMPARE(snapshot.droppedEvents, qint64(4));
        QCOMPARE(snapshot.probes.size(), 3);
        QCOMPARE(snapshot.samples.size(), 2);
        const QJsonObject dropped = Live::statsJson(snapshot, 3000 * Ms).value(QStringLiteral("dropped")).toObject();
        QCOMPARE(dropped.value(QStringLiteral("frames")).toInt(), 50);
        QCOMPARE(dropped.value(QStringLiteral("events")).toInt(), 4);
    }

    void frameIntervalsAreAggregatedPerWindow()
    {
        Live::Session session;
        session.reset(0);
        steadyFrames(session, 0, 31);           // 0 .. 480 ms
        steadyFrames(session, 560, 31);         // an 80 ms gap while drawing
        steadyFrames(session, 2560, 11);        // 1.5 s idle gap, no stall: not a spike
        steadyFrames(session, 0, 5, 2);         // a second window
        const Live::Snapshot snapshot = session.snapshot();
        const QJsonObject stats = Live::statsJson(snapshot, 3000 * Ms);
        QCOMPARE(stats.value(QStringLiteral("durationMs")).toDouble(), 3000.0);
        const QJsonObject frames = stats.value(QStringLiteral("frames")).toObject();
        QCOMPARE(frames.value(QStringLiteral("total")).toInt(), 31 + 31 + 11 + 5);
        const QJsonArray windows = frames.value(QStringLiteral("windows")).toArray();
        QCOMPARE(windows.size(), 2);
        const QJsonObject first = windows.at(0).toObject();
        QCOMPARE(first.value(QStringLiteral("window")).toInt(), 1);
        QCOMPARE(first.value(QStringLiteral("frames")).toInt(), 73);
        QCOMPARE(first.value(QStringLiteral("gaps")).toInt(), 2);
        QCOMPARE(first.value(QStringLiteral("maxIntervalMs")).toDouble(), 1520.0);
        const QJsonObject drawing = first.value(QStringLiteral("whileDrawing")).toObject();
        QCOMPARE(drawing.value(QStringLiteral("p50Ms")).toDouble(), 16.0);
        QCOMPARE(drawing.value(QStringLiteral("maxMs")).toDouble(), 80.0);
        QCOMPARE(drawing.value(QStringLiteral("count")).toInt(), 71);
        int histogramTotal = 0;
        for (const QJsonValue &bucket : first.value(QStringLiteral("histogram")).toArray()) {
            histogramTotal += bucket.toObject().value(QStringLiteral("count")).toInt();
        }
        QCOMPARE(histogramTotal, 72);

        QJsonArray spikes = Live::spikesJson(snapshot);
        QCOMPARE(spikes.size(), 1);
        QCOMPARE(spikes.at(0).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("frame-gap"));
        QCOMPARE(spikes.at(0).toObject().value(QStringLiteral("durationMs")).toDouble(), 80.0);
        QCOMPARE(spikes.at(0).toObject().value(QStringLiteral("atMs")).toDouble(), 480.0);
        QCOMPARE(spikes.at(0).toObject().value(QStringLiteral("window")).toInt(), 1);

        // A stall inside the long gap makes it notable too.
        session.addProbe(1700 * Ms, 300);
        spikes = Live::spikesJson(session.snapshot());
        QCOMPARE(spikes.size(), 3);
        QCOMPARE(spikes.at(0).toObject().value(QStringLiteral("durationMs")).toDouble(), 1520.0);
        QCOMPARE(spikes.at(0).toObject().value(QStringLiteral("rule")).toString(), QStringLiteral("overlaps an event-loop stall"));
        QCOMPARE(spikes.at(1).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("event-loop-stall"));
        QCOMPARE(spikes.at(1).toObject().value(QStringLiteral("atMs")).toDouble(), 1400.0);
        QVERIFY(spikes.at(1).toObject().value(QStringLiteral("window")).isNull());
    }

    void unavailableMetricsAreNullNotGuessed()
    {
        Live::Session session;
        session.reset(0);
        Live::Sample sample; // nothing available
        sample.ns = 500 * Ms;
        session.addSample(sample);
        const QJsonObject stats = Live::statsJson(session.snapshot(), 1000 * Ms);
        QCOMPARE(stats.value(QStringLiteral("cpu")).toObject().value(QStringLiteral("available")).toBool(), false);
        QVERIFY(stats.value(QStringLiteral("cpu")).toObject().value(QStringLiteral("meanPercent")).isNull());
        QCOMPARE(stats.value(QStringLiteral("memory")).toObject().value(QStringLiteral("available")).toBool(), false);
        QVERIFY(stats.value(QStringLiteral("memory")).toObject().value(QStringLiteral("growthRssBytes")).isNull());
        QCOMPARE(stats.value(QStringLiteral("threads")).toObject().value(QStringLiteral("available")).toBool(), false);
        QVERIFY(stats.value(QStringLiteral("eventLoop")).toObject().value(QStringLiteral("maxDelayMs")).isNull());
        const QJsonArray samples = Live::metricsJson(session.snapshot()).value(QStringLiteral("samples")).toArray();
        QCOMPARE(samples.size(), 1);
        QVERIFY(samples.at(0).toObject().value(QStringLiteral("cpuPercent")).isNull());
        QVERIFY(samples.at(0).toObject().value(QStringLiteral("rssBytes")).isNull());

        session.addSample({1000 * Ms, 50.0, 100, -1, 7});
        session.addSample({1500 * Ms, 150.0, 300, -1, 9});
        const QJsonObject more = Live::statsJson(session.snapshot(), 2000 * Ms);
        QCOMPARE(more.value(QStringLiteral("cpu")).toObject().value(QStringLiteral("meanPercent")).toDouble(), 100.0);
        QCOMPARE(more.value(QStringLiteral("memory")).toObject().value(QStringLiteral("growthRssBytes")).toInt(), 200);
        QCOMPARE(more.value(QStringLiteral("memory")).toObject().value(QStringLiteral("footprintAvailable")).toBool(), false);
        QCOMPARE(more.value(QStringLiteral("threads")).toObject().value(QStringLiteral("max")).toInt(), 9);
    }

    void processMetricsReadThisProcess()
    {
        const ProcessMetrics::Reading reading = ProcessMetrics::read();
#if defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
        QVERIFY(reading.cpuTimeNs >= 0);
        QVERIFY(reading.rssBytes > 1024 * 1024);
        QVERIFY(reading.threads >= 1);
#endif
#if defined(Q_OS_MACOS)
        QVERIFY(reading.footprintBytes > 0);
#else
        QCOMPARE(reading.footprintBytes, qint64(-1));
#endif
        QVERIFY(std::isnan(ProcessMetrics::cpuPercent({}, reading, 1000)));
        QCOMPARE(ProcessMetrics::cpuPercent({1000, -1, -1, -1}, {501000, -1, -1, -1}, 1000000), 50.0);
    }

    void streamChunksRebuildTheRecording()
    {
        Live::Session session;
        session.reset(0);
        session.addWindow({1, QStringLiteral("window"), QStringLiteral("QQuickWindow"), 800, 600, 2.0, 0, -1});
        steadyFrames(session, 0, 10);
        session.addProbe(50 * Ms, 1.5);
        session.addEvent({QStringLiteral("marker"), QStringLiteral("sidebar"), QStringLiteral("open"), 20 * Ms, -1, -1, {}});
        const Live::Snapshot first = session.takeChunk();
        QCOMPARE(first.frames.size(), 10);
        steadyFrames(session, 200, 5);
        session.addSample({300 * Ms, 12.5, 4096, -1, 3});
        const Live::Snapshot second = session.takeChunk();
        QCOMPARE(second.frames.size(), 5); // only what is new
        QCOMPARE(second.events.size(), 0);
        QCOMPARE(session.takeChunk().frames.size(), 0);

        QByteArray stream = QJsonDocument(Live::chunkJson(first)).toJson(QJsonDocument::Compact) + "\n"
            + QJsonDocument(Live::chunkJson(second)).toJson(QJsonDocument::Compact) + "\n"
            + R"({"type":"chunk","frames":[[1,99)"; // cut off by a crash
        int unreadable = 0;
        const Live::Snapshot rebuilt = Live::fromStream(stream, &unreadable);
        QCOMPARE(unreadable, 1);
        QCOMPARE(rebuilt.frames.size(), 15);
        QCOMPARE(rebuilt.probes.size(), 1);
        QCOMPARE(rebuilt.samples.size(), 1);
        QCOMPARE(rebuilt.samples.at(0).cpuPercent, 12.5);
        QCOMPARE(rebuilt.events.size(), 1);
        QCOMPARE(rebuilt.windows.size(), 1);
        QCOMPARE(rebuilt.windows.at(0).width, 800);
        QCOMPARE(rebuilt.frames.at(14).ns, 264 * Ms);
    }

    void runIdsFitObservatoryRecords()
    {
        const QString runId = Live::makeRunId(QDateTime(QDate(2026, 10, 30), QTime(11, 17, 52, 582), QTimeZone::utc()));
        QVERIFY(runId.startsWith(QStringLiteral("20261030T111752-582Z-live-")));
        QVERIFY(Observatory::isRunId(runId));
        QTemporaryDir root;
        QString created;
        QString error;
        const QString first = Live::createRunDirectory(root.path(), &created, &error);
        QVERIFY2(!first.isEmpty(), qPrintable(error));
        QVERIFY(Observatory::isRunId(created));
        QString second;
        QVERIFY(Live::createRunDirectory(root.path(), &second, &error) != first);
    }

    void finishedRecordingsListTheirArtifacts()
    {
        QTemporaryDir root;
        QString runId;
        QString error;
        const QString dir = Live::createRunDirectory(root.path(), &runId, &error);
        Live::Session session;
        session.reset(0);
        steadyFrames(session, 0, 20);
        session.addEvent({QStringLiteral("annotation"), QStringLiteral("note"), QStringLiteral("slow here"), 100 * Ms, -1, -1, {}});
        QVERIFY(Live::finish(dir, runningSummary(runId, 1), session.snapshot(), 1000 * Ms,
                             {QStringLiteral("completed"), QStringLiteral("user"), false, {}}, &error));
        const QJsonObject summary = readJson(QDir(dir).filePath(QStringLiteral("summary.json")));
        QCOMPARE(summary.value(QStringLiteral("status")).toString(), QStringLiteral("completed"));
        QVERIFY(!summary.value(QStringLiteral("finishedAt")).toString().isEmpty());
        const QJsonObject live = summary.value(QStringLiteral("live")).toObject();
        QCOMPARE(live.value(QStringLiteral("stopReason")).toString(), QStringLiteral("user"));
        QCOMPARE(live.value(QStringLiteral("partial")).toBool(), false);
        QCOMPARE(live.value(QStringLiteral("durationMs")).toDouble(), 1000.0);
        QCOMPARE(live.value(QStringLiteral("requestedDurationS")).toInt(), 30);
        QCOMPARE(live.value(QStringLiteral("stats")).toObject().value(QStringLiteral("frames")).toObject().value(QStringLiteral("total")).toInt(), 20);
        const QJsonObject artifacts = summary.value(QStringLiteral("artifacts")).toObject();
        QCOMPARE(artifacts.value(QStringLiteral("metrics")).toString(), QStringLiteral("metrics.json"));
        QCOMPARE(artifacts.value(QStringLiteral("events")).toString(), QStringLiteral("events.json"));

        // Served like any other run's artifacts.
        const Observatory::Response metrics = Observatory::artifact(root.path(), runId, QStringLiteral("metrics"));
        QCOMPARE(metrics.status, 200);
        const QJsonObject metricsJson = QJsonDocument::fromJson(metrics.body).object();
        QCOMPARE(metricsJson.value(QStringLiteral("schema")).toString(), QStringLiteral("archivist.observatory.live-metrics"));
        QCOMPARE(metricsJson.value(QStringLiteral("frameSwaps")).toObject().value(QStringLiteral("1")).toArray().size(), 20);
        const QJsonObject events = readJson(QDir(dir).filePath(QStringLiteral("events.json")));
        QCOMPARE(events.value(QStringLiteral("events")).toArray().at(0).toObject().value(QStringLiteral("kind")).toString(), QStringLiteral("annotation"));
    }

    void recordingsLeftRunningAreFinishedAsInterrupted()
    {
        QTemporaryDir root;
        QString error;
        QString crashed, alive, done;
        const QString crashedDir = Live::createRunDirectory(root.path(), &crashed, &error);
        const QString aliveDir = Live::createRunDirectory(root.path(), &alive, &error);
        const QString doneDir = Live::createRunDirectory(root.path(), &done, &error);
        QVERIFY(Live::writeJson(QDir(crashedDir).filePath(QStringLiteral("summary.json")), runningSummary(crashed, 111)));
        QVERIFY(Live::writeJson(QDir(aliveDir).filePath(QStringLiteral("summary.json")), runningSummary(alive, 222)));
        QJsonObject finished = runningSummary(done, 111);
        finished.insert(QStringLiteral("status"), QStringLiteral("completed"));
        QVERIFY(Live::writeJson(QDir(doneDir).filePath(QStringLiteral("summary.json")), finished));

        Live::Session session;
        session.reset(0);
        steadyFrames(session, 0, 40);
        session.addProbe(400 * Ms, 2);
        QVERIFY(Live::appendLine(QDir(crashedDir).filePath(QStringLiteral("live-stream.ndjson")),
                                 QJsonDocument(Live::chunkJson(session.takeChunk())).toJson(QJsonDocument::Compact)));

        const QStringList recovered = Live::recoverInterrupted(root.path(), [](qint64 pid) { return pid == 222; });
        QCOMPARE(recovered, QStringList{crashed});

        const QJsonObject summary = readJson(QDir(crashedDir).filePath(QStringLiteral("summary.json")));
        QCOMPARE(summary.value(QStringLiteral("status")).toString(), QStringLiteral("interrupted"));
        QCOMPARE(summary.value(QStringLiteral("live")).toObject().value(QStringLiteral("partial")).toBool(), true);
        QCOMPARE(summary.value(QStringLiteral("live")).toObject().value(QStringLiteral("stopReason")).toString(), QStringLiteral("app-ended"));
        QCOMPARE(summary.value(QStringLiteral("live")).toObject().value(QStringLiteral("durationMs")).toDouble(), 624.0);
        QVERIFY(summary.value(QStringLiteral("error")).toString().contains(QStringLiteral("stopped while recording")));
        QVERIFY(QFile::exists(QDir(crashedDir).filePath(QStringLiteral("metrics.json"))));

        QCOMPARE(readJson(QDir(aliveDir).filePath(QStringLiteral("summary.json"))).value(QStringLiteral("status")).toString(), QStringLiteral("running"));
        QCOMPARE(readJson(QDir(doneDir).filePath(QStringLiteral("summary.json"))).value(QStringLiteral("status")).toString(), QStringLiteral("completed"));
        // Once the other app is gone too, that one is finished as well.
        QVERIFY(Live::recoverInterrupted(root.path(), [](qint64) { return false; }) == QStringList{alive});
    }

    void processAliveSeesThisProcess()
    {
#if defined(Q_OS_UNIX)
        QVERIFY(Live::processAlive(QCoreApplication::applicationPid()));
#endif
        QVERIFY(!Live::processAlive(0));
        QVERIFY(!Live::processAlive(-5));
    }
};

QTEST_GUILESS_MAIN(ObservatoryLiveTest)

#include "core_observatory_live_test.moc"

#include "App/Services/live_recorder.h"

#include "Core/Domains/Observatory/live_events.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

#include <memory>

// The live recorder in a real (offscreen) Qt Quick app: real windows, real
// frame swaps, the real event loop and process metrics.
class LiveRecorderTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_root;

    // A window that redraws every 16 ms, like an animation.
    struct Animated
    {
        std::unique_ptr<QQuickWindow> window;
        std::unique_ptr<QTimer> timer;
    };

    static Animated animatedWindow()
    {
        Animated animated{std::make_unique<QQuickWindow>(), std::make_unique<QTimer>()};
        QQuickWindow *window = animated.window.get();
        window->resize(320, 240);
        window->setColor(Qt::darkGray);
        window->show();
        animated.timer->setInterval(16);
        QObject::connect(animated.timer.get(), &QTimer::timeout, window, [window]() { window->update(); });
        animated.timer->start();
        return animated;
    }

    QJsonObject readJson(const QString &runId, const QString &file) const
    {
        QFile handle(QDir(m_root.path()).filePath(QStringLiteral("runs/%1/%2").arg(runId, file)));
        if (!handle.open(QIODevice::ReadOnly)) {
            return {};
        }
        return QJsonDocument::fromJson(handle.readAll()).object();
    }

    static QString errorCode(const QVariantMap &result)
    {
        return result.value(QStringLiteral("error")).toMap().value(QStringLiteral("code")).toString();
    }

    static void finish(LiveRecorder &recorder)
    {
        QTRY_COMPARE_WITH_TIMEOUT(recorder.state(), QStringLiteral("idle"), 10000);
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_root.isValid());
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    }

    void recordsFramesStallsResourcesAndOperations()
    {
        Animated animated = animatedWindow();
        LiveRecorder recorder(m_root.path());
        recorder.waitForWrites();
        recorder.setIntervalsForTesting(20, 100, 200);
        QSignalSpy finished(&recorder, &LiveRecorder::finished);

        const QVariantMap started = recorder.start(15, QStringLiteral("resize test\u0007"), QStringLiteral("panel"));
        QVERIFY2(errorCode(started).isEmpty(), qPrintable(errorCode(started)));
        QVERIFY(recorder.recording());
        QVERIFY(LiveEvents::recording());
        const QString runId = started.value(QStringLiteral("runId")).toString();
        QVERIFY(!runId.isEmpty());
        QCOMPARE(started.value(QStringLiteral("label")).toString(), QStringLiteral("resize test"));
        QCOMPARE(readJson(runId, QStringLiteral("summary.json")).value(QStringLiteral("status")).toString(), QStringLiteral("running"));

        // Only one recording at a time.
        QCOMPARE(errorCode(recorder.start(15)), QStringLiteral("busy"));

        // An operation the app reports, and a deliberate 120 ms UI-thread stall.
        QTest::qWait(150);
        const quint64 resize = LiveEvents::begin(QStringLiteral("sidebar"), QStringLiteral("resize"));
        QTest::qWait(60);
        LiveEvents::end(resize, 1);
        QCOMPARE(errorCode(recorder.mark(QStringLiteral("felt slow"))), QString());
        QTimer::singleShot(0, this, []() { QThread::msleep(120); });
        QTest::qWait(600);
        QVERIFY(QFile::exists(QDir(m_root.path()).filePath(QStringLiteral("runs/%1/live-stream.ndjson").arg(runId))));

        const QVariantMap during = recorder.status();
        QVERIFY(during.value(QStringLiteral("frames")).toInt() > 10);
        QVERIFY(during.value(QStringLiteral("remainingMs")).toDouble() < 15000);

        const QVariantMap stopped = recorder.stop();
        QCOMPARE(stopped.value(QStringLiteral("state")).toString(), QStringLiteral("finalizing"));
        QVERIFY(!LiveEvents::recording());
        QCOMPARE(errorCode(recorder.stop()), QStringLiteral("not-recording"));
        finish(recorder);
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished.at(0).at(1).toString(), QStringLiteral("completed"));
        QCOMPARE(recorder.status().value(QStringLiteral("lastRun")).toMap().value(QStringLiteral("runId")).toString(), runId);

        const QJsonObject summary = readJson(runId, QStringLiteral("summary.json"));
        QCOMPARE(summary.value(QStringLiteral("kind")).toString(), QStringLiteral("live-session"));
        QCOMPARE(summary.value(QStringLiteral("status")).toString(), QStringLiteral("completed"));
        const QJsonObject live = summary.value(QStringLiteral("live")).toObject();
        QCOMPARE(live.value(QStringLiteral("stopReason")).toString(), QStringLiteral("user"));
        QCOMPARE(live.value(QStringLiteral("requestedDurationS")).toInt(), 15);
        QVERIFY(live.value(QStringLiteral("durationMs")).toDouble() > 700);
        QVERIFY(live.value(QStringLiteral("durationMs")).toDouble() < 5000);
        QCOMPARE(summary.value(QStringLiteral("app")).toObject().value(QStringLiteral("pid")).toInt(), int(QCoreApplication::applicationPid()));
        QVERIFY(summary.value(QStringLiteral("source")).toObject().contains(QStringLiteral("commit")));

        const QJsonObject stats = live.value(QStringLiteral("stats")).toObject();
        const int frames = stats.value(QStringLiteral("frames")).toObject().value(QStringLiteral("total")).toInt();
        QVERIFY2(frames > 20, qPrintable(QString::number(frames)));
        // The 120 ms stall shows up as an event-loop delay of about that size.
        const double maxDelay = stats.value(QStringLiteral("eventLoop")).toObject().value(QStringLiteral("maxDelayMs")).toDouble();
        QVERIFY2(maxDelay >= 80 && maxDelay < 1000, qPrintable(QString::number(maxDelay)));
        bool stallListed = false;
        for (const QJsonValue &spike : live.value(QStringLiteral("spikes")).toArray()) {
            stallListed = stallListed || spike.toObject().value(QStringLiteral("type")).toString() == QLatin1String("event-loop-stall");
        }
        QVERIFY(stallListed);
#if defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
        QVERIFY(stats.value(QStringLiteral("cpu")).toObject().value(QStringLiteral("available")).toBool());
        QVERIFY(stats.value(QStringLiteral("memory")).toObject().value(QStringLiteral("maxRssBytes")).toDouble() > 0);
        QVERIFY(stats.value(QStringLiteral("threads")).toObject().value(QStringLiteral("max")).toInt() >= 1);
#endif

        const QJsonArray events = readJson(runId, QStringLiteral("events.json")).value(QStringLiteral("events")).toArray();
        QStringList names;
        for (const QJsonValue &event : events) {
            names.append(event.toObject().value(QStringLiteral("category")).toString() + QLatin1Char('.') + event.toObject().value(QStringLiteral("name")).toString());
        }
        QVERIFY2(names.contains(QStringLiteral("recording.start")), qPrintable(names.join(QLatin1Char(','))));
        QVERIFY(names.contains(QStringLiteral("sidebar.resize")));
        QVERIFY(names.contains(QStringLiteral("note.felt slow")));
        QVERIFY(names.contains(QStringLiteral("recording.stop")));

        const QJsonObject metrics = readJson(runId, QStringLiteral("metrics.json"));
        const QJsonArray swaps = metrics.value(QStringLiteral("frameSwaps")).toObject().value(QStringLiteral("1")).toArray();
        QCOMPARE(swaps.size(), frames);
        for (qsizetype i = 1; i < swaps.size(); ++i) {
            QVERIFY(swaps.at(i).toDouble() >= swaps.at(i - 1).toDouble());
        }
        const QJsonArray samples = metrics.value(QStringLiteral("samples")).toArray();
        QVERIFY(samples.size() >= 5);
        // The first sample has no full interval behind it, so no CPU figure.
        QVERIFY(samples.at(0).toObject().value(QStringLiteral("cpuPercent")).isNull());
#if defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
        QVERIFY(samples.at(1).toObject().value(QStringLiteral("cpuPercent")).isDouble());
#endif
        QCOMPARE(live.value(QStringLiteral("measurementVersion")).toInt(), Live::kMeasurementVersion);
        QCOMPARE(metrics.value(QStringLiteral("windows")).toArray().at(0).toObject().value(QStringLiteral("width")).toInt(), 320);
    }

    void stopsByItselfAtTheTimeLimit()
    {
        Animated animated = animatedWindow();
        LiveRecorder recorder(m_root.path());
        recorder.waitForWrites();
        QVERIFY(errorCode(recorder.start(15)).isEmpty());
        QCOMPARE(recorder.durationSeconds(), 15);
        // The limit is a real 15 s timer.
        QTRY_COMPARE_WITH_TIMEOUT(recorder.state(), QStringLiteral("idle"), 20000);
        const QString runId = recorder.status().value(QStringLiteral("lastRun")).toMap().value(QStringLiteral("runId")).toString();
        const QJsonObject live = readJson(runId, QStringLiteral("summary.json")).value(QStringLiteral("live")).toObject();
        QCOMPARE(live.value(QStringLiteral("stopReason")).toString(), QStringLiteral("time-limit"));
        QVERIFY(qAbs(live.value(QStringLiteral("durationMs")).toDouble() - 15000) < 1000);
    }

    void windowsOpenedAndClosedDuringARecordingAreHandled()
    {
        Animated first = animatedWindow();
        LiveRecorder recorder(m_root.path());
        recorder.waitForWrites();
        recorder.setIntervalsForTesting(20, 100, 100);
        QVERIFY(errorCode(recorder.start(30)).isEmpty());
        const QString runId = recorder.runId();
        QTest::qWait(200);
        Animated second = animatedWindow();
        QTest::qWait(400); // the recorder notices new windows on its next flush
        second.timer.reset();
        second.window.reset(); // destroyed while recording
        QTest::qWait(200);
        recorder.stop();
        finish(recorder);

        const QJsonObject metrics = readJson(runId, QStringLiteral("metrics.json"));
        const QJsonArray windows = metrics.value(QStringLiteral("windows")).toArray();
        QCOMPARE(windows.size(), 2);
        QVERIFY(windows.at(1).toObject().value(QStringLiteral("detachedMs")).toDouble() > windows.at(1).toObject().value(QStringLiteral("attachedMs")).toDouble());
        QVERIFY(metrics.value(QStringLiteral("frameSwaps")).toObject().value(QStringLiteral("1")).toArray().size() > 10);
        QVERIFY(metrics.value(QStringLiteral("frameSwaps")).toObject().value(QStringLiteral("2")).toArray().size() > 3);
        const QJsonArray perWindow = readJson(runId, QStringLiteral("summary.json")).value(QStringLiteral("live")).toObject()
            .value(QStringLiteral("stats")).toObject().value(QStringLiteral("frames")).toObject().value(QStringLiteral("windows")).toArray();
        QCOMPARE(perWindow.size(), 2);
    }

    void controlRequestsAreValidated()
    {
        LiveRecorder recorder(m_root.path());
        recorder.waitForWrites();
        const auto call = [&recorder](const QString &operation, const QByteArray &body, const QString &caller = QStringLiteral("panel")) {
            const Observatory::Response response = recorder.handle(operation, body, caller);
            return std::make_pair(response.status, QJsonDocument::fromJson(response.body).object());
        };
        auto [status, body] = call(QStringLiteral("status"), {});
        QCOMPARE(status, 200);
        QCOMPARE(body.value(QStringLiteral("schema")).toString(), QStringLiteral("archivist.observatory.response"));
        QCOMPARE(body.value(QStringLiteral("operation")).toString(), QStringLiteral("observatory.live.status"));
        QCOMPARE(body.value(QStringLiteral("result")).toObject().value(QStringLiteral("state")).toString(), QStringLiteral("idle"));

        const QList<QByteArray> refused{
            R"({"durationSeconds":45})", R"({"durationSeconds":"30"})", R"({"durationSeconds":30.5})",
            R"({"label":7})", R"({"path":"/etc"})", R"([1,2])", "not json",
        };
        for (const QByteArray &input : refused) {
            std::tie(status, body) = call(QStringLiteral("start"), input);
            QCOMPARE(status, 400);
            QCOMPARE(body.value(QStringLiteral("ok")).toBool(), false);
            QCOMPARE(body.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("invalid-input"));
        }
        QVERIFY(!recorder.recording());

        std::tie(status, body) = call(QStringLiteral("shell"), {});
        QCOMPARE(status, 400);
        QCOMPARE(body.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("unknown-operation"));
        std::tie(status, body) = call(QStringLiteral("stop"), {});
        QCOMPARE(status, 409);

        std::tie(status, body) = call(QStringLiteral("start"), R"({"durationSeconds":60,"label":"from an agent"})", QStringLiteral("agent"));
        QCOMPARE(status, 200);
        QCOMPARE(body.value(QStringLiteral("result")).toObject().value(QStringLiteral("startedBy")).toString(), QStringLiteral("agent"));
        std::tie(status, body) = call(QStringLiteral("start"), {});
        QCOMPARE(status, 409);
        QCOMPARE(body.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString(), QStringLiteral("busy"));
        std::tie(status, body) = call(QStringLiteral("mark"), R"({"label":"here"})", QStringLiteral("agent"));
        QCOMPARE(status, 200);
        std::tie(status, body) = call(QStringLiteral("stop"), {}, QStringLiteral("agent"));
        QCOMPARE(status, 200);
        finish(recorder);
        const QString runId = recorder.status().value(QStringLiteral("lastRun")).toMap().value(QStringLiteral("runId")).toString();
        QCOMPARE(readJson(runId, QStringLiteral("summary.json")).value(QStringLiteral("live")).toObject().value(QStringLiteral("stopReason")).toString(), QStringLiteral("agent"));
    }

    void quittingWhileRecordingLeavesAnInterruptedRecording()
    {
        auto recorder = std::make_unique<LiveRecorder>(m_root.path());
        recorder->waitForWrites();
        QVERIFY(errorCode(recorder->start(120)).isEmpty());
        const QString runId = recorder->runId();
        QTest::qWait(100);
        recorder.reset(); // the app going away
        const QJsonObject summary = readJson(runId, QStringLiteral("summary.json"));
        QCOMPARE(summary.value(QStringLiteral("status")).toString(), QStringLiteral("interrupted"));
        QCOMPARE(summary.value(QStringLiteral("live")).toObject().value(QStringLiteral("stopReason")).toString(), QStringLiteral("app-quit"));
        QVERIFY(!LiveEvents::recording());
    }
};

QTEST_MAIN(LiveRecorderTest)

#include "live_recorder_test.moc"

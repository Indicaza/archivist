#include "App/Services/live_recorder.h"

#include "Core/Domains/Observatory/live_events.h"
#include "Core/Domains/Observatory/live_store.h"
#include "Core/Domains/Observatory/observatory_control.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutexLocker>
#include <QQuickWindow>
#include <QRegularExpression>

#include <cmath>
#include <limits>

namespace
{
QString cleanLabel(const QString &label)
{
    QString clean;
    for (const QChar character : label) {
        clean.append(character.isPrint() ? character : QLatin1Char(' '));
    }
    return clean.simplified().left(64);
}

// "FloatingSurfaceWindow_QMLTYPE_12" -> "FloatingSurfaceWindow"
QString windowClass(const QQuickWindow *window)
{
    static const QRegularExpression suffix(QStringLiteral("_QML(TYPE)?_[0-9]+$"));
    return QString::fromLatin1(window->metaObject()->className()).remove(suffix);
}

QString applicationState(Qt::ApplicationState state)
{
    switch (state) {
    case Qt::ApplicationActive:
        return QStringLiteral("active");
    case Qt::ApplicationInactive:
        return QStringLiteral("inactive");
    case Qt::ApplicationHidden:
        return QStringLiteral("hidden");
    case Qt::ApplicationSuspended:
        return QStringLiteral("suspended");
    }
    return QStringLiteral("unknown");
}

QByteArray envelope(const QString &operation, const QVariantMap &outcome)
{
    QJsonObject body{
        {QStringLiteral("schema"), QStringLiteral("archivist.observatory.response")},
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("operation"), operation},
    };
    if (outcome.contains(QStringLiteral("error"))) {
        body.insert(QStringLiteral("ok"), false);
        body.insert(QStringLiteral("error"), QJsonObject::fromVariantMap(outcome.value(QStringLiteral("error")).toMap()));
    } else {
        body.insert(QStringLiteral("ok"), true);
        body.insert(QStringLiteral("result"), QJsonObject::fromVariantMap(outcome));
    }
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

int httpStatus(const QVariantMap &outcome)
{
    const QString code = outcome.value(QStringLiteral("error")).toMap().value(QStringLiteral("code")).toString();
    if (code.isEmpty()) {
        return 200;
    }
    if (code == QLatin1String("busy") || code == QLatin1String("not-recording")) {
        return 409;
    }
    if (code == QLatin1String("invalid-input") || code == QLatin1String("unknown-operation")) {
        return 400;
    }
    return 500;
}
} // namespace

const QList<int> &LiveRecorder::allowedDurations()
{
    static const QList<int> durations{15, 30, 60, 120};
    return durations;
}

LiveRecorder::LiveRecorder(QString root, QObject *parent)
    : QObject(parent)
    , m_root(std::move(root))
{
    m_writer.setMaxThreadCount(1);
    m_writer.setExpiryTimeout(-1);

    m_probeTimer.setTimerType(Qt::PreciseTimer);
    m_probeTimer.setInterval(m_probeMs);
    connect(&m_probeTimer, &QTimer::timeout, this, &LiveRecorder::probe);
    m_sampleTimer.setInterval(500);
    connect(&m_sampleTimer, &QTimer::timeout, this, &LiveRecorder::sample);
    m_flushTimer.setInterval(1000);
    connect(&m_flushTimer, &QTimer::timeout, this, &LiveRecorder::flush);
    m_limitTimer.setSingleShot(true);
    m_limitTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_limitTimer, &QTimer::timeout, this, [this]() { stop(QStringLiteral("time-limit")); });

    // A quit while recording still leaves a finished (if shortened) recording.
    if (QCoreApplication *app = QCoreApplication::instance()) {
        connect(app, &QCoreApplication::aboutToQuit, this, [this]() {
            if (recording()) {
                stop(QStringLiteral("app-quit"));
            }
            waitForWrites();
        });
    }

    // Finish recordings an earlier run of the app left behind.
    m_writer.start([directory = m_root]() {
        const QStringList recovered = Live::recoverInterrupted(directory, Live::processAlive);
        for (const QString &runId : recovered) {
            qInfo().noquote() << "[Observatory] Finished an interrupted live recording:" << runId;
        }
    });
}

LiveRecorder::~LiveRecorder()
{
    if (recording()) {
        stop(QStringLiteral("app-quit"));
    }
    LiveEvents::clearSink(&m_session);
    detachAll();
    m_writer.waitForDone();
}

QString LiveRecorder::state() const
{
    switch (m_state) {
    case State::Idle:
        return QStringLiteral("idle");
    case State::Recording:
        return QStringLiteral("recording");
    case State::Finalizing:
        return QStringLiteral("finalizing");
    }
    return QStringLiteral("idle");
}

int LiveRecorder::elapsedSeconds() const
{
    return recording() ? static_cast<int>((LiveEvents::nowNs() - m_startNs) / 1000000000LL) : 0;
}

void LiveRecorder::setIntervalsForTesting(int probeMs, int sampleMs, int flushMs)
{
    m_probeMs = probeMs;
    m_probeTimer.setInterval(probeMs);
    m_sampleTimer.setInterval(sampleMs);
    m_flushTimer.setInterval(flushMs);
}

QVariantMap LiveRecorder::failure(const QString &code, const QString &message) const
{
    return {{QStringLiteral("error"), QVariantMap{{QStringLiteral("code"), code}, {QStringLiteral("message"), message}}}};
}

QJsonObject LiveRecorder::runningSummary(const QString &label, const QString &startedBy) const
{
    const QFileInfo binary(QCoreApplication::applicationFilePath());
    return {
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("kind"), QString::fromLatin1(Live::kKind)},
        {QStringLiteral("runId"), m_runId},
        {QStringLiteral("status"), QStringLiteral("running")},
        {QStringLiteral("label"), label.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(label)},
        {QStringLiteral("createdAt"), m_startedAt},
        {QStringLiteral("finishedAt"), QJsonValue::Null},
        {QStringLiteral("source"), QJsonObject{}},
        {QStringLiteral("environment"), Live::environmentFacts()},
        {QStringLiteral("toolchain"), QJsonObject{{QStringLiteral("qtVersion"), QStringLiteral(QT_VERSION_STR)}}},
        {QStringLiteral("app"), QJsonObject{
            {QStringLiteral("version"), QCoreApplication::applicationVersion()},
            {QStringLiteral("qtRuntimeVersion"), QString::fromLatin1(qVersion())},
            {QStringLiteral("qtBuildVersion"), QStringLiteral(QT_VERSION_STR)},
            {QStringLiteral("pid"), static_cast<double>(QCoreApplication::applicationPid())},
            {QStringLiteral("binary"), binary.absoluteFilePath()},
            {QStringLiteral("binaryBytes"), static_cast<double>(binary.size())},
            {QStringLiteral("binaryModifiedAt"), binary.lastModified().toUTC().toString(Qt::ISODateWithMs)},
#ifdef QT_DEBUG
            {QStringLiteral("buildType"), QStringLiteral("debug")},
#else
            {QStringLiteral("buildType"), QStringLiteral("release")},
#endif
        }},
        {QStringLiteral("live"), QJsonObject{
            {QStringLiteral("measurementVersion"), Live::kMeasurementVersion},
            {QStringLiteral("requestedDurationS"), m_durationS},
            {QStringLiteral("startedBy"), startedBy},
            {QStringLiteral("stopReason"), QJsonValue::Null},
            {QStringLiteral("partial"), false},
            {QStringLiteral("probeIntervalMs"), m_probeMs},
            {QStringLiteral("sampleIntervalMs"), m_sampleTimer.interval()},
        }},
        {QStringLiteral("artifacts"), QJsonObject{{QStringLiteral("stream"), QStringLiteral("live-stream.ndjson")}}},
    };
}

QVariantMap LiveRecorder::start(int durationSeconds, const QString &label, const QString &startedBy)
{
    if (m_state == State::Recording) {
        return failure(QStringLiteral("busy"), QStringLiteral("A live recording is already running (%1).").arg(m_runId));
    }
    if (m_state == State::Finalizing) {
        return failure(QStringLiteral("busy"), QStringLiteral("The last recording is still being saved. Try again in a moment."));
    }
    if (!allowedDurations().contains(durationSeconds)) {
        return failure(QStringLiteral("invalid-input"), QStringLiteral("durationSeconds must be 15, 30, 60 or 120."));
    }
    if (startedBy != QLatin1String("panel") && startedBy != QLatin1String("agent") && startedBy != QLatin1String("app")) {
        return failure(QStringLiteral("invalid-input"), QStringLiteral("startedBy must be panel, agent or app."));
    }
    if (LiveEvents::recording()) {
        // Another collector (the UI Lab, in a test instance) has the event stream.
        return failure(QStringLiteral("busy"), QStringLiteral("This instance is running a UI Lab scenario, which collects the app's events itself."));
    }

    QString error;
    QString runId;
    const QString directory = Live::createRunDirectory(m_root, &runId, &error);
    if (directory.isEmpty()) {
        return failure(QStringLiteral("io-error"), error);
    }

    m_runId = runId;
    m_directory = directory;
    m_label = cleanLabel(label);
    m_startedBy = startedBy;
    m_durationS = durationSeconds;
    m_startedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    m_writeError.clear();
    m_startNs = LiveEvents::nowNs();
    {
        QMutexLocker lock(&m_summaryMutex);
        m_running = runningSummary(m_label, startedBy);
    }

    // The summary first (status "running"), then git facts, off this thread.
    const QString summaryPath = QDir(directory).filePath(QStringLiteral("summary.json"));
    {
        QMutexLocker lock(&m_summaryMutex);
        if (!Live::writeJson(summaryPath, m_running, &error)) {
            return failure(QStringLiteral("io-error"), error);
        }
    }
    m_writer.start([this, summaryPath]() {
        const QJsonObject source = Live::sourceFacts();
        QMutexLocker lock(&m_summaryMutex);
        if (m_running.value(QStringLiteral("status")).toString() == QLatin1String("running")) {
            m_running.insert(QStringLiteral("source"), source);
            Live::writeJson(summaryPath, m_running);
        }
    });

    m_session.reset(m_startNs);
    m_nextWindowId = 1;
    m_lastProbeNs = m_startNs;
    m_lastReading = ProcessMetrics::read();
    m_lastReadingNs = LiveEvents::nowNs();
    m_state = State::Recording;
    LiveEvents::setSink(&m_session);
    attachWindows();
    LiveEvents::marker(QStringLiteral("recording"), QStringLiteral("start"), {
        {QStringLiteral("startedBy"), startedBy},
        {QStringLiteral("appState"), applicationState(QGuiApplication::applicationState())},
    });
    if (auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        m_appState = connect(app, &QGuiApplication::applicationStateChanged, this, [](Qt::ApplicationState state) {
            LiveEvents::marker(QStringLiteral("app"), QStringLiteral("state"), {{QStringLiteral("state"), applicationState(state)}});
        });
    }
    sample();
    m_probeTimer.start();
    m_sampleTimer.start();
    m_flushTimer.start();
    m_limitTimer.start(durationSeconds * 1000);

    emit stateChanged();
    emit elapsedChanged();
    return status();
}

QVariantMap LiveRecorder::stop(const QString &reason)
{
    if (m_state != State::Recording) {
        return failure(QStringLiteral("not-recording"), QStringLiteral("No live recording is running."));
    }
    static const QStringList reasons{
        QStringLiteral("user"), QStringLiteral("agent"), QStringLiteral("time-limit"), QStringLiteral("app-quit"), QStringLiteral("error"),
    };
    const QString stopReason = reasons.contains(reason) ? reason : QStringLiteral("user");

    LiveEvents::marker(QStringLiteral("recording"), QStringLiteral("stop"), {{QStringLiteral("reason"), stopReason}});
    const qint64 endNs = LiveEvents::nowNs();
    m_probeTimer.stop();
    m_sampleTimer.stop();
    m_flushTimer.stop();
    m_limitTimer.stop();
    disconnect(m_appState);
    LiveEvents::clearSink(&m_session);
    detachAll();

    const Live::Snapshot snapshot = m_session.snapshot();
    Live::Ending ending;
    if (stopReason == QLatin1String("app-quit")) {
        ending = {QStringLiteral("interrupted"), stopReason, false,
                  QStringLiteral("Archivist quit before the recording's time was up; it holds everything up to the quit.")};
    } else if (stopReason == QLatin1String("error") || !m_writeError.isEmpty()) {
        ending = {QStringLiteral("failed"), stopReason, false, m_writeError.isEmpty() ? QStringLiteral("The recorder hit an error.") : m_writeError};
    } else {
        ending = {QStringLiteral("completed"), stopReason, false, {}};
    }

    m_state = State::Finalizing;
    emit stateChanged();

    const QString runId = m_runId;
    const QString directory = m_directory;
    QPointer<LiveRecorder> self(this);
    m_writer.start([this, self, runId, directory, snapshot, endNs, ending]() {
        QJsonObject running;
        {
            QMutexLocker lock(&m_summaryMutex);
            running = m_running;
            // From here on the start-up writer must not rewrite the summary.
            m_running.insert(QStringLiteral("status"), ending.status);
        }
        QString error;
        const bool ok = Live::finish(directory, running, snapshot, endNs, ending, &error);
        const QString status = ok ? ending.status : QStringLiteral("failed");
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, runId, status, ending, error]() {
                if (!self) {
                    return;
                }
                self->m_lastRun = {
                    {QStringLiteral("runId"), runId},
                    {QStringLiteral("status"), status},
                    {QStringLiteral("stopReason"), ending.stopReason},
                    {QStringLiteral("finishedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                    {QStringLiteral("error"), error.isEmpty() ? QVariant() : QVariant(error)},
                };
                self->m_state = State::Idle;
                emit self->stateChanged();
                emit self->finished(runId, status);
            },
            Qt::QueuedConnection);
    });

    return status();
}

QVariantMap LiveRecorder::mark(const QString &label)
{
    if (m_state != State::Recording) {
        return failure(QStringLiteral("not-recording"), QStringLiteral("No live recording is running."));
    }
    const QString clean = cleanLabel(label);
    m_session.addEvent({QStringLiteral("annotation"), QStringLiteral("note"), clean.isEmpty() ? QStringLiteral("marker") : clean,
                        LiveEvents::nowNs(), -1, -1, {}});
    QVariantMap result = status();
    result.insert(QStringLiteral("markedAtMs"), Live::relativeMs(LiveEvents::nowNs(), m_startNs));
    return result;
}

QVariantMap LiveRecorder::status() const
{
    QVariantMap result{
        {QStringLiteral("state"), state()},
        {QStringLiteral("recording"), recording()},
        {QStringLiteral("allowedDurations"), QVariantList{15, 30, 60, 120}},
        {QStringLiteral("defaultDuration"), kDefaultDuration},
        {QStringLiteral("lastRun"), m_lastRun.isEmpty() ? QVariant() : QVariant(m_lastRun)},
    };
    if (m_state != State::Idle) {
        const double elapsed = Live::relativeMs(LiveEvents::nowNs(), m_startNs);
        result.insert(QStringLiteral("runId"), m_runId);
        result.insert(QStringLiteral("label"), m_label.isEmpty() ? QVariant() : QVariant(m_label));
        result.insert(QStringLiteral("startedBy"), m_startedBy);
        result.insert(QStringLiteral("startedAt"), m_startedAt);
        result.insert(QStringLiteral("durationSeconds"), m_durationS);
        if (recording()) {
            result.insert(QStringLiteral("elapsedMs"), elapsed);
            result.insert(QStringLiteral("remainingMs"), std::max(0.0, m_durationS * 1000.0 - elapsed));
            result.insert(QStringLiteral("frames"), static_cast<double>(m_session.frameCount()));
            result.insert(QStringLiteral("events"), static_cast<double>(m_session.eventCount()));
            result.insert(QStringLiteral("windows"), m_session.windowCount());
        }
    }
    return result;
}

double LiveRecorder::begin(const QString &category, const QString &name, const QVariantMap &fields)
{
    return static_cast<double>(LiveEvents::begin(category, name, fields));
}

void LiveRecorder::end(double token, bool ok, const QVariantMap &fields)
{
    if (token > 0 && std::isfinite(token)) {
        LiveEvents::end(static_cast<quint64>(token), ok ? 1 : 0, fields);
    }
}

void LiveRecorder::marker(const QString &category, const QString &name, const QVariantMap &fields)
{
    LiveEvents::marker(category, name, fields);
}

Observatory::Response LiveRecorder::handle(const QString &operation, const QByteArray &body, const QString &caller)
{
    const QString name = QStringLiteral("observatory.live.") + operation;
    QJsonParseError parseError;
    const QJsonDocument document = body.trimmed().isEmpty() ? QJsonDocument(QJsonObject()) : QJsonDocument::fromJson(body, &parseError);
    QVariantMap outcome;
    if (!document.isObject()) {
        outcome = failure(QStringLiteral("invalid-input"), QStringLiteral("The input must be a JSON object."));
    } else {
        const QJsonObject input = document.object();
        const auto unknownKeys = [&input](const QStringList &allowed) {
            for (const QString &key : input.keys()) {
                if (!allowed.contains(key)) {
                    return key;
                }
            }
            return QString();
        };
        QString unknown;
        if (operation == QLatin1String("status")) {
            unknown = unknownKeys({});
            outcome = unknown.isEmpty() ? status() : QVariantMap();
        } else if (operation == QLatin1String("start")) {
            unknown = unknownKeys({QStringLiteral("durationSeconds"), QStringLiteral("label")});
            const QJsonValue duration = input.value(QStringLiteral("durationSeconds"));
            const QJsonValue label = input.value(QStringLiteral("label"));
            if (unknown.isEmpty()) {
                if (!duration.isUndefined() && !duration.isDouble()) {
                    outcome = failure(QStringLiteral("invalid-input"), QStringLiteral("durationSeconds must be 15, 30, 60 or 120."));
                } else if (!label.isUndefined() && !label.isString()) {
                    outcome = failure(QStringLiteral("invalid-input"), QStringLiteral("label must be a string."));
                } else {
                    const double seconds = duration.isUndefined() ? kDefaultDuration : duration.toDouble();
                    outcome = seconds == std::floor(seconds)
                        ? start(static_cast<int>(seconds), label.toString(), caller)
                        : failure(QStringLiteral("invalid-input"), QStringLiteral("durationSeconds must be 15, 30, 60 or 120."));
                }
            }
        } else if (operation == QLatin1String("stop")) {
            unknown = unknownKeys({});
            outcome = unknown.isEmpty() ? stop(caller == QLatin1String("agent") ? QStringLiteral("agent") : QStringLiteral("user")) : QVariantMap();
        } else if (operation == QLatin1String("mark")) {
            unknown = unknownKeys({QStringLiteral("label")});
            const QJsonValue label = input.value(QStringLiteral("label"));
            if (unknown.isEmpty()) {
                outcome = !label.isUndefined() && !label.isString()
                    ? failure(QStringLiteral("invalid-input"), QStringLiteral("label must be a string."))
                    : mark(label.toString());
            }
        } else {
            outcome = failure(QStringLiteral("unknown-operation"), QStringLiteral("Live operations are status, start, stop and mark."));
        }
        if (!unknown.isEmpty()) {
            outcome = failure(QStringLiteral("invalid-input"), QStringLiteral("Unknown input \"%1\".").arg(unknown.left(40)));
        }
    }
    QList<QByteArray> headers = Observatory::securityHeaders();
    return {httpStatus(outcome), "application/json; charset=utf-8", envelope(name, outcome), headers};
}

void LiveRecorder::waitForWrites()
{
    m_writer.waitForDone();
    // The writer hands its result back to this thread.
    QCoreApplication::sendPostedEvents(QCoreApplication::instance(), QEvent::MetaCall);
}

// ---- collection ---------------------------------------------------------------

void LiveRecorder::attachWindows()
{
    const QList<QWindow *> windows = QGuiApplication::topLevelWindows();
    for (QWindow *window : windows) {
        if (auto *quick = qobject_cast<QQuickWindow *>(window)) {
            attachWindow(quick);
        }
    }
}

void LiveRecorder::attachWindow(QQuickWindow *window)
{
    for (const Attached &attached : std::as_const(m_windows)) {
        if (attached.window == window) {
            return;
        }
    }
    const int id = m_nextWindowId++;
    Live::WindowInfo info;
    info.id = id;
    info.className = windowClass(window);
    info.role = window->transientParent() ? QStringLiteral("child") : (window->flags() & Qt::Tool) ? QStringLiteral("tool") : QStringLiteral("window");
    if (id == 1 && window == QGuiApplication::focusWindow()) {
        info.role = QStringLiteral("focused");
    }
    info.width = window->width();
    info.height = window->height();
    info.devicePixelRatio = window->devicePixelRatio();
    info.attachedNs = LiveEvents::nowNs();
    m_session.addWindow(info);

    Live::Session *session = &m_session;
    Attached attached;
    attached.window = window;
    attached.id = id;
    // Runs on the window's render thread: take the time, append, done.
    attached.frames = connect(window, &QQuickWindow::frameSwapped, window, [session, id]() {
        session->addFrame(id, LiveEvents::nowNs());
    }, Qt::DirectConnection);
    attached.destroyed = connect(window, &QObject::destroyed, this, [this, id]() {
        m_session.detachWindow(id, LiveEvents::nowNs());
        for (qsizetype i = 0; i < m_windows.size(); ++i) {
            if (m_windows[i].id == id) {
                m_windows.removeAt(i);
                break;
            }
        }
    });
    m_windows.append(attached);
}

void LiveRecorder::detachAll()
{
    const qint64 now = LiveEvents::nowNs();
    for (const Attached &attached : std::as_const(m_windows)) {
        disconnect(attached.frames);
        disconnect(attached.destroyed);
        if (attached.window) {
            m_session.updateWindowSize(attached.id, attached.window->width(), attached.window->height(),
                                       attached.window->devicePixelRatio());
        }
        m_session.detachWindow(attached.id, now);
    }
    m_windows.clear();
}

void LiveRecorder::probe()
{
    const qint64 now = LiveEvents::nowNs();
    const double delay = (now - m_lastProbeNs) / 1e6 - m_probeMs;
    m_lastProbeNs = now;
    m_session.addProbe(now, std::max(0.0, delay));
}

void LiveRecorder::sample()
{
    const ProcessMetrics::Reading reading = ProcessMetrics::read();
    const qint64 now = LiveEvents::nowNs();
    Live::Sample sample;
    sample.ns = now;
    // CPU over less than half a sample interval is mostly noise: the first
    // sample, taken right as recording starts, came out at 100-200 %.
    // Measurement version 2 leaves it unknown instead.
    const qint64 wallNs = now - m_lastReadingNs;
    sample.cpuPercent = wallNs * 2 >= static_cast<qint64>(m_sampleTimer.interval()) * 1000000LL
        ? ProcessMetrics::cpuPercent(m_lastReading, reading, wallNs)
        : std::numeric_limits<double>::quiet_NaN();
    sample.rssBytes = reading.rssBytes;
    sample.footprintBytes = reading.footprintBytes;
    sample.threads = reading.threads;
    m_session.addSample(sample);
    m_lastReading = reading;
    m_lastReadingNs = now;
}

void LiveRecorder::flush()
{
    // New windows (a panel floated out) are picked up within a second.
    attachWindows();
    const Live::Snapshot chunk = m_session.takeChunk();
    const QString path = QDir(m_directory).filePath(QStringLiteral("live-stream.ndjson"));
    QPointer<LiveRecorder> self(this);
    m_writer.start([self, chunk, path]() {
        QString error;
        if (!Live::appendLine(path, QJsonDocument(Live::chunkJson(chunk)).toJson(QJsonDocument::Compact), &error)) {
            QMetaObject::invokeMethod(QCoreApplication::instance(), [self, error]() {
                if (self && self->recording()) {
                    self->m_writeError = QStringLiteral("Could not save the recording as it ran: %1").arg(error);
                    self->stop(QStringLiteral("error"));
                }
            }, Qt::QueuedConnection);
        }
    });
    emit elapsedChanged();
}

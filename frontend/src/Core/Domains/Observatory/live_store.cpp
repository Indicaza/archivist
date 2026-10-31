#include "live_store.h"

#include "observatory_records.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QSysInfo>
#include <QThread>

#include <algorithm>

#if defined(Q_OS_UNIX)
#include <cerrno>
#include <signal.h>
#endif

namespace Live
{
QString makeRunId(const QDateTime &now)
{
    const QString stamp = now.toUTC().toString(QStringLiteral("yyyyMMdd'T'HHmmss'-'zzz'Z'"));
    const quint32 random = QRandomGenerator::system()->bounded(0x1000000u);
    return QStringLiteral("%1-live-%2").arg(stamp).arg(random, 6, 16, QLatin1Char('0'));
}

QString createRunDirectory(const QString &root, QString *runId, QString *error)
{
    const QString runs = QDir(root).filePath(QStringLiteral("runs"));
    if (!QDir().mkpath(runs)) {
        if (error) {
            *error = QStringLiteral("Could not create %1").arg(runs);
        }
        return {};
    }
    for (int attempt = 0; attempt < 5; ++attempt) {
        const QString id = makeRunId();
        // mkdir (not mkpath) fails when the folder exists: a run is never reused.
        if (QDir(runs).mkdir(id)) {
            *runId = id;
            return QDir(runs).filePath(id);
        }
    }
    if (error) {
        *error = QStringLiteral("Could not create a unique run folder in %1").arg(runs);
    }
    return {};
}

bool writeJson(const QString &path, const QJsonObject &object, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }
    return true;
}

bool appendLine(const QString &path, const QByteArray &line, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }
    QByteArray data = line;
    data.append('\n');
    if (file.write(data) != data.size() || !file.flush()) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }
    return true;
}

bool finish(const QString &directory, QJsonObject running, const Snapshot &snapshot, qint64 endNs,
            const Ending &ending, QString *error, int unreadableStreamLines)
{
    const QDir dir(directory);
    QString writeError;
    const bool metricsOk = writeJson(dir.filePath(QStringLiteral("metrics.json")), metricsJson(snapshot), &writeError);
    const bool eventsOk = metricsOk && writeJson(dir.filePath(QStringLiteral("events.json")), eventsJson(snapshot), &writeError);

    QJsonObject live = running.value(QStringLiteral("live")).toObject();
    QJsonArray windows;
    for (const WindowInfo &window : snapshot.windows) {
        windows.append(QJsonObject{
            {QStringLiteral("id"), window.id},
            {QStringLiteral("role"), window.role},
            {QStringLiteral("className"), window.className},
            {QStringLiteral("width"), window.width},
            {QStringLiteral("height"), window.height},
        });
    }
    const QJsonObject stats = statsJson(snapshot, endNs);
    live.insert(QStringLiteral("stopReason"), ending.stopReason);
    live.insert(QStringLiteral("partial"), ending.partial);
    live.insert(QStringLiteral("durationMs"), stats.value(QStringLiteral("durationMs")));
    live.insert(QStringLiteral("windows"), windows);
    live.insert(QStringLiteral("stats"), stats);
    live.insert(QStringLiteral("spikes"), spikesJson(snapshot));
    if (unreadableStreamLines > 0) {
        live.insert(QStringLiteral("unreadableStreamLines"), unreadableStreamLines);
    }
    running.insert(QStringLiteral("live"), live);

    QString status = ending.status;
    QString message = ending.error;
    if (!eventsOk) {
        status = QStringLiteral("failed");
        message = QStringLiteral("Could not write the recording's data: %1").arg(writeError);
    }
    running.insert(QStringLiteral("status"), status);
    running.insert(QStringLiteral("finishedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    if (!message.isEmpty()) {
        running.insert(QStringLiteral("error"), message);
    }
    QJsonObject artifacts = running.value(QStringLiteral("artifacts")).toObject();
    if (eventsOk) {
        artifacts.insert(QStringLiteral("metrics"), QStringLiteral("metrics.json"));
        artifacts.insert(QStringLiteral("events"), QStringLiteral("events.json"));
    }
    running.insert(QStringLiteral("artifacts"), artifacts);

    if (!writeJson(dir.filePath(QStringLiteral("summary.json")), running, &writeError)) {
        if (error) {
            *error = writeError;
        }
        return false;
    }
    if (!eventsOk && error) {
        *error = message;
    }
    return eventsOk;
}

bool processAlive(qint64 pid)
{
#if defined(Q_OS_UNIX)
    if (pid <= 0) {
        return false;
    }
    return ::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
#else
    Q_UNUSED(pid);
    return false;
#endif
}

namespace
{
QString git(const QString &repository, const QStringList &arguments, bool *ok)
{
    QProcess process;
    process.setWorkingDirectory(repository);
    process.start(QStringLiteral("git"), arguments, QIODevice::ReadOnly);
    *ok = process.waitForFinished(3000) && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(1000);
    }
    return *ok ? QString::fromUtf8(process.readAllStandardOutput()).trimmed() : QString();
}
} // namespace

QJsonObject sourceFacts()
{
    QJsonObject source{
        {QStringLiteral("commit"), QJsonValue::Null},
        {QStringLiteral("branch"), QJsonValue::Null},
        {QStringLiteral("dirty"), QJsonValue::Null},
        {QStringLiteral("changedFiles"), QJsonValue::Null},
    };
#ifdef ARCHIVIST_REPOSITORY_ROOT
    const QString repository = QStringLiteral(ARCHIVIST_REPOSITORY_ROOT);
    bool ok = false;
    const QString commit = git(repository, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")}, &ok);
    if (ok && !commit.isEmpty()) {
        source.insert(QStringLiteral("commit"), commit);
    }
    const QString branch = git(repository, {QStringLiteral("branch"), QStringLiteral("--show-current")}, &ok);
    if (ok && !branch.isEmpty()) {
        source.insert(QStringLiteral("branch"), branch);
    }
    const QString status = git(repository, {QStringLiteral("status"), QStringLiteral("--porcelain=v1")}, &ok);
    if (ok) {
        const qsizetype changed = status.isEmpty() ? 0 : status.split(QLatin1Char('\n'), Qt::SkipEmptyParts).size();
        source.insert(QStringLiteral("dirty"), changed > 0);
        source.insert(QStringLiteral("changedFiles"), static_cast<double>(changed));
    }
#endif
    return source;
}

QJsonObject environmentFacts()
{
    QString platform = QSysInfo::productType();
    if (platform == QLatin1String("macos") || platform == QLatin1String("osx")) {
        platform = QStringLiteral("darwin");
    }
    return {
        {QStringLiteral("platform"), platform},
        {QStringLiteral("osVersion"), QSysInfo::productVersion()},
        {QStringLiteral("kernelVersion"), QSysInfo::kernelVersion()},
        {QStringLiteral("architecture"), QSysInfo::currentCpuArchitecture()},
        {QStringLiteral("logicalCpus"), QThread::idealThreadCount()},
        {QStringLiteral("cpuModel"), QJsonValue::Null},
        {QStringLiteral("totalMemoryBytes"), QJsonValue::Null},
    };
}

namespace
{
// A recording always stops by itself at its length, so one still marked
// "running" well past it was left by an app that is gone, even when its pid
// now belongs to another process (pids are reused, for example after a restart).
bool pastItsLength(const QJsonObject &summary)
{
    const QDateTime createdAt = QDateTime::fromString(summary.value(QStringLiteral("createdAt")).toString(), Qt::ISODateWithMs);
    const double durationS = summary.value(QStringLiteral("live")).toObject().value(QStringLiteral("requestedDurationS")).toDouble();
    if (!createdAt.isValid() || !(durationS > 0)) {
        return false;
    }
    return createdAt.addMSecs(static_cast<qint64>(durationS * 1000) + kRunningGraceMs) < QDateTime::currentDateTimeUtc();
}
} // namespace

QStringList recoverInterrupted(const QString &root, const std::function<bool(qint64)> &isAlive)
{
    QStringList recovered;
    const QDir runs(QDir(root).filePath(QStringLiteral("runs")));
    const QStringList names = runs.entryList({QStringLiteral("*-live-*")}, QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &runId : names) {
        if (!Observatory::isRunId(runId)) {
            continue;
        }
        const QDir dir(runs.filePath(runId));
        QFile summaryFile(dir.filePath(QStringLiteral("summary.json")));
        if (!summaryFile.open(QIODevice::ReadOnly) || summaryFile.size() > 4 * 1024 * 1024) {
            continue;
        }
        const QJsonObject summary = QJsonDocument::fromJson(summaryFile.readAll()).object();
        summaryFile.close();
        if (summary.value(QStringLiteral("kind")).toString() != QLatin1String(kKind)
            || summary.value(QStringLiteral("status")).toString() != QLatin1String("running")) {
            continue;
        }
        const qint64 pid = static_cast<qint64>(summary.value(QStringLiteral("app")).toObject().value(QStringLiteral("pid")).toDouble());
        if (isAlive(pid) && !pastItsLength(summary)) {
            continue;
        }
        QFile stream(dir.filePath(QStringLiteral("live-stream.ndjson")));
        QByteArray data;
        if (stream.open(QIODevice::ReadOnly)) {
            data = stream.read(256 * 1024 * 1024);
        }
        int unreadable = 0;
        const Snapshot snapshot = fromStream(data, &unreadable);
        // The recording ended somewhere after its last measurement.
        qint64 lastNs = 0;
        for (const Frame &frame : snapshot.frames) {
            lastNs = std::max(lastNs, frame.ns);
        }
        for (const Probe &probe : snapshot.probes) {
            lastNs = std::max(lastNs, probe.ns);
        }
        for (const Sample &sample : snapshot.samples) {
            lastNs = std::max(lastNs, sample.ns);
        }
        for (const LiveEvents::Event &event : snapshot.events) {
            lastNs = std::max(lastNs, std::max(event.startNs, event.endNs));
        }
        const Ending ending{
            QStringLiteral("interrupted"),
            QStringLiteral("app-ended"),
            true,
            QStringLiteral("Archivist stopped while recording (it crashed, was killed, or the computer restarted). "
                           "This holds what was saved up to the last second before that."),
        };
        if (finish(dir.path(), summary, snapshot, lastNs, ending, nullptr, unreadable)) {
            recovered.append(runId);
        }
    }
    return recovered;
}
} // namespace Live

#pragma once

#include "live_session.h"

#include <QDateTime>
#include <QJsonObject>
#include <QString>

#include <functional>

// Live recordings stored as ordinary Observatory runs (kind "live-session")
// under <root>/runs/<runId>/, next to builds and UI tests:
//
//   summary.json         identity, status and the headline figures
//                        (status "running" until the recording is finished)
//   live-stream.ndjson   appended about once a second while recording, so a
//                        crash loses at most the last second
//   metrics.json         every frame swap, event-loop probe and resource sample
//   events.json          operations, markers and notes
//
// A recording whose app died is finished on the next launch from its stream
// and marked "interrupted", never "completed".
namespace Live
{
inline constexpr auto kKind = "live-session";

// "20261030T111752-582Z-live-1a2b3c", like makeRunId in storage.mjs.
QString makeRunId(const QDateTime &now = QDateTime::currentDateTimeUtc());

// Creates a fresh run folder (never reuses one). Empty on failure.
QString createRunDirectory(const QString &root, QString *runId, QString *error);

bool writeJson(const QString &path, const QJsonObject &object, QString *error = nullptr);
bool appendLine(const QString &path, const QByteArray &line, QString *error = nullptr);

struct Ending
{
    QString status;     // completed, interrupted, failed
    QString stopReason; // user, agent, time-limit, app-quit, app-ended, error
    bool partial = false;
    QString error;
};

// Writes metrics.json, events.json, then summary.json (last, so a summary
// that says finished always has its data). `running` is the summary written
// when the recording started.
bool finish(const QString &directory, QJsonObject running, const Snapshot &snapshot, qint64 endNs,
            const Ending &ending, QString *error = nullptr, int unreadableStreamLines = 0);

// How long past its requested length a recording may still be "running"
// (saving it takes well under a second; this leaves room for a stalled app).
inline constexpr qint64 kRunningGraceMs = 5 * 60 * 1000;

// Finishes every live recording still marked "running" whose app process is
// gone (isAlive(pid) is false), or that is more than kRunningGraceMs past its
// requested length (its pid may have been reused). Returns the run ids it finished.
QStringList recoverInterrupted(const QString &root, const std::function<bool(qint64)> &isAlive);

bool processAlive(qint64 pid);

// The working tree the app was built from (dev builds only; git is run with
// a short timeout, so call this off the UI thread): commit, branch, dirty,
// changedFiles, each null when unknown.
QJsonObject sourceFacts();
// Platform, OS version, CPU architecture and core count, and the Qt version
// the app runs with. Nothing here is guessed; unknown values are null.
QJsonObject environmentFacts();
} // namespace Live

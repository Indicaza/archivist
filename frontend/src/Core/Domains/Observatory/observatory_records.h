#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

// Read-only access to Observatory records (scripts/observatory) for the
// dashboard page that the IDE host serves at /observatory/. It answers the
// same two requests as the Node dashboard server, so the page is the same
// in a browser and inside Archivist:
//
//   api/runs                          {"root", "runs": [{"runId", "summary", "error"}]}
//   api/runs/<runId>/artifacts/<name> the text of an artifact the run's
//                                     summary lists under "artifacts"
//   api/baselines                     {"baselines", "baselinesError", "budgets",
//                                      "budgetsSource", "budgetsError"}
//
// Nothing here writes, and nothing outside a run's own folder is read.
namespace Observatory
{
struct Response
{
    int status = 200;
    QByteArray contentType;
    QByteArray body;
    QList<QByteArray> headers;
};

// Where records live: $ARCHIVIST_OBSERVATORY_DIR, else the same default as
// scripts/observatory/storage.mjs.
QString defaultRoot();

// The dashboard page: $ARCHIVIST_OBSERVATORY_PAGE, a bundled copy next to the
// app, else the repository's scripts/observatory/dashboard/public in dev builds.
// Empty when none exists.
QString defaultPagePath();

// The files the page is made of. Nothing else is served from its folder.
const QStringList &pageFiles();

bool isRunId(const QString &runId);

// The largest artifact served; bigger ones are refused, not read.
inline constexpr qint64 kMaxArtifactBytes = 32 * 1024 * 1024;

// Headers every dashboard response carries (CSP, nosniff, no caching).
QList<QByteArray> securityHeaders();

// `relativePath` is the request path after "/observatory/", decoded.
Response handle(const QString &root, const QString &pagePath, const QString &relativePath);

Response runsListing(const QString &root);
// <root>/baselines.json and the budgets: <root>/budgets.json, else the
// repository's scripts/observatory/budgets.json (two folders above the page).
Response baselinesAndBudgets(const QString &root, const QString &pagePath);
Response artifact(const QString &root, const QString &runId, const QString &name);
Response pageFile(const QString &pagePath, const QString &file);
} // namespace Observatory

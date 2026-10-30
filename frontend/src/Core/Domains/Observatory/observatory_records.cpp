#include "observatory_records.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>
#include <functional>

namespace Observatory
{
namespace
{
QByteArray jsonType()
{
    return "application/json; charset=utf-8";
}

Response failure(int status, const QString &message)
{
    // The same { error: "..." } shape as the Node dashboard server.
    return {status, jsonType(), QJsonDocument(QJsonObject{{QStringLiteral("error"), message}}).toJson(QJsonDocument::Compact), securityHeaders()};
}

QString runsDirectory(const QString &root)
{
    return QDir(root).filePath(QStringLiteral("runs"));
}

// A path that is `directory` itself or inside it, after following links.
bool inside(const QString &canonicalPath, const QString &canonicalDirectory)
{
    return !canonicalPath.isEmpty() && !canonicalDirectory.isEmpty()
        && canonicalPath.startsWith(canonicalDirectory + QLatin1Char('/'));
}

struct Summary
{
    QJsonValue value;  // null when unreadable
    QString error;
};

Summary readSummary(const QString &runDirectory)
{
    QFile file(QDir(runDirectory).filePath(QStringLiteral("summary.json")));

    if (!file.exists()) {
        return {QJsonValue(), QStringLiteral("summary.json is missing")};
    }

    if (!file.open(QIODevice::ReadOnly)) {
        return {QJsonValue(), QStringLiteral("summary.json is unreadable: ") + file.errorString()};
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);

    if (error.error != QJsonParseError::NoError) {
        return {QJsonValue(), QStringLiteral("summary.json is unreadable: ") + error.errorString()};
    }

    return {document.isArray() ? QJsonValue(document.array()) : QJsonValue(document.object()), QString()};
}
struct JsonFile
{
    bool found = false;
    QJsonValue value;  // null when missing or unreadable
    QString error;
};

JsonFile readJsonFile(const QString &path)
{
    QFile file(path);

    if (!file.exists()) {
        return {};
    }

    if (!file.open(QIODevice::ReadOnly)) {
        return {true, QJsonValue(), QFileInfo(path).fileName() + QStringLiteral(" can't be read: ") + file.errorString()};
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);

    if (error.error != QJsonParseError::NoError) {
        return {true, QJsonValue(), QFileInfo(path).fileName() + QStringLiteral(" can't be read: ") + error.errorString()};
    }

    return {true, document.isArray() ? QJsonValue(document.array()) : QJsonValue(document.object()), QString()};
}

QJsonValue orNull(const QString &text)
{
    return text.isEmpty() ? QJsonValue() : QJsonValue(text);
}
} // namespace

QString defaultRoot()
{
    const QString configured = qEnvironmentVariable("ARCHIVIST_OBSERVATORY_DIR").trimmed();

    if (!configured.isEmpty()) {
        return QDir::cleanPath(QDir::current().absoluteFilePath(configured));
    }

    const QString home = QDir::homePath();
#ifdef Q_OS_MACOS
    return QDir(home).filePath(QStringLiteral("Library/Application Support/Archivist/Observatory"));
#else
    const QString xdg = qEnvironmentVariable("XDG_DATA_HOME");
    const QString dataHome = xdg.isEmpty() ? QDir(home).filePath(QStringLiteral(".local/share")) : xdg;
    return QDir(dataHome).filePath(QStringLiteral("archivist/observatory"));
#endif
}

QString defaultPagePath()
{
    const auto hasPage = [](const QString &path) {
        return QFileInfo::exists(QDir(path).filePath(QStringLiteral("index.html")));
    };
    const QString configured = qEnvironmentVariable("ARCHIVIST_OBSERVATORY_PAGE").trimmed();

    if (!configured.isEmpty()) {
        return hasPage(configured) ? QDir::cleanPath(configured) : QString();
    }

    const QString bundled = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../Resources/observatory"));

    if (hasPage(bundled)) {
        return QDir::cleanPath(bundled);
    }

#ifdef ARCHIVIST_REPOSITORY_ROOT
    const QString repository = QDir(QStringLiteral(ARCHIVIST_REPOSITORY_ROOT)).filePath(QStringLiteral("scripts/observatory/dashboard/public"));

    if (hasPage(repository)) {
        return repository;
    }
#endif
    return {};
}

const QStringList &pageFiles()
{
    // Keep in step with PAGE_FILES in scripts/observatory/dashboard/server.mjs.
    static const QStringList files{
        QStringLiteral("index.html"),
        QStringLiteral("app.js"),
        QStringLiteral("charts.js"),
        QStringLiteral("data.js"),
        QStringLiteral("styles.css"),
        QStringLiteral("compare.mjs"),
        QStringLiteral("filter.mjs"),
        QStringLiteral("normalize.mjs"),
        QStringLiteral("preview.mjs"),
        QStringLiteral("regression.mjs"),
        QStringLiteral("redact.mjs"),
        QStringLiteral("diagnostic.mjs"),
        QStringLiteral("dom.js"),
        QStringLiteral("lab.js"),
        QStringLiteral("live.mjs"),
        QStringLiteral("live-page.js"),
    };
    return files;
}

bool isRunId(const QString &runId)
{
    // Keep in step with RUN_ID in scripts/observatory/dashboard/records.mjs.
    static const QRegularExpression pattern(QStringLiteral("^[0-9]{8}T[0-9]{6}-[0-9]{3}Z-[a-z0-9-]+-[0-9a-f]{6}$"));
    return pattern.match(runId).hasMatch();
}

QList<QByteArray> securityHeaders()
{
    return {
        "X-Content-Type-Options: nosniff",
        "Referrer-Policy: no-referrer",
        "Content-Security-Policy: default-src 'self'; style-src 'self'; script-src 'self'; img-src 'self' data:; connect-src 'self'; frame-ancestors 'none'",
        "Cache-Control: no-store",
    };
}

Response runsListing(const QString &root)
{
    QStringList names = QDir(runsDirectory(root)).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    names.erase(std::remove_if(names.begin(), names.end(), [](const QString &name) { return !isRunId(name); }), names.end());
    std::sort(names.begin(), names.end(), std::greater<QString>());

    QJsonArray runs;

    for (const QString &runId : names) {
        const Summary summary = readSummary(QDir(runsDirectory(root)).filePath(runId));
        runs.append(QJsonObject{
            {QStringLiteral("runId"), runId},
            {QStringLiteral("summary"), summary.value},
            {QStringLiteral("error"), summary.error.isEmpty() ? QJsonValue() : QJsonValue(summary.error)},
        });
    }

    const QJsonObject body{{QStringLiteral("root"), root}, {QStringLiteral("runs"), runs}};
    return {200, jsonType(), QJsonDocument(body).toJson(QJsonDocument::Compact), securityHeaders()};
}

Response baselinesAndBudgets(const QString &root, const QString &pagePath)
{
    const JsonFile baselines = readJsonFile(QDir(root).filePath(QStringLiteral("baselines.json")));
    QString budgetsPath = QDir(root).filePath(QStringLiteral("budgets.json"));
    JsonFile budgets = readJsonFile(budgetsPath);

    if (!budgets.found && !pagePath.isEmpty()) {
        budgetsPath = QDir::cleanPath(QDir(pagePath).filePath(QStringLiteral("../../budgets.json")));
        budgets = readJsonFile(budgetsPath);
    }

    const QJsonObject body{
        {QStringLiteral("baselines"), baselines.value},
        {QStringLiteral("baselinesError"), orNull(baselines.error)},
        {QStringLiteral("budgets"), budgets.value},
        {QStringLiteral("budgetsSource"), budgets.found ? QJsonValue(budgetsPath) : QJsonValue()},
        {QStringLiteral("budgetsError"), budgets.found ? orNull(budgets.error) : QJsonValue(QStringLiteral("no budgets file"))},
    };
    return {200, jsonType(), QJsonDocument(body).toJson(QJsonDocument::Compact), securityHeaders()};
}

Response artifact(const QString &root, const QString &runId, const QString &name)
{
    if (!isRunId(runId)) {
        return failure(400, QStringLiteral("Not a run id."));
    }

    const QString runDirectory = QDir(runsDirectory(root)).filePath(runId);
    const Summary summary = readSummary(runDirectory);

    if (!summary.value.isObject()) {
        return failure(404, summary.error.isEmpty() ? QStringLiteral("summary.json is not an object") : summary.error);
    }

    // Only names the summary lists, as relative paths inside the run's folder.
    const QJsonValue listed = summary.value.toObject().value(QStringLiteral("artifacts")).toObject().value(name);
    const QString relative = listed.isString() ? listed.toString() : QString();

    if (relative.isEmpty() || QDir::isAbsolutePath(relative) || relative.contains(QChar(0))) {
        return failure(404, QStringLiteral("This run lists no such artifact."));
    }

    const QString runCanonical = QFileInfo(runDirectory).canonicalFilePath();
    const QString lexical = QDir::cleanPath(QDir(runDirectory).filePath(relative));

    if (!lexical.startsWith(QDir::cleanPath(runDirectory) + QLatin1Char('/'))) {
        return failure(404, QStringLiteral("This run lists no such artifact."));
    }

    const QFileInfo info(lexical);
    const QString canonical = info.canonicalFilePath();

    if (!inside(canonical, runCanonical) || !QFileInfo(canonical).isFile()) {
        return failure(404, QStringLiteral("The artifact file is missing."));
    }

    if (QFileInfo(canonical).size() > kMaxArtifactBytes) {
        return failure(413, QStringLiteral("The artifact is larger than %1 MB; open it from the run folder.").arg(kMaxArtifactBytes / (1024 * 1024)));
    }

    QFile file(canonical);

    if (!file.open(QIODevice::ReadOnly)) {
        return failure(404, QStringLiteral("The artifact file is missing."));
    }

    QList<QByteArray> headers = securityHeaders();
    headers.append("Content-Disposition: inline; filename=\"" + runId.toUtf8() + "-" + QFileInfo(canonical).fileName().toUtf8().replace('"', '_') + "\"");
    return {200, "text/plain; charset=utf-8", file.readAll(), headers};
}

Response pageFile(const QString &pagePath, const QString &file)
{
    const QString name = file.isEmpty() ? QStringLiteral("index.html") : file;

    if (pagePath.isEmpty()) {
        return failure(404, QStringLiteral("The Observatory dashboard isn't part of this build of Archivist."));
    }

    if (!pageFiles().contains(name)) {
        return failure(404, QStringLiteral("Not found."));
    }

    QFile source(QDir(pagePath).filePath(name));

    if (!source.open(QIODevice::ReadOnly)) {
        return failure(404, QStringLiteral("Not found."));
    }

    const QString suffix = QFileInfo(name).suffix();
    const QByteArray type = suffix == QStringLiteral("html") ? "text/html; charset=utf-8"
        : suffix == QStringLiteral("css") ? "text/css; charset=utf-8"
        : "text/javascript; charset=utf-8";
    return {200, type, source.readAll(), securityHeaders()};
}

Response handle(const QString &root, const QString &pagePath, const QString &relativePath)
{
    if (relativePath == QStringLiteral("api/runs")) {
        return runsListing(root);
    }

    if (relativePath == QStringLiteral("api/baselines")) {
        return baselinesAndBudgets(root, pagePath);
    }

    static const QRegularExpression artifactPattern(QStringLiteral("^api/runs/([^/]+)/artifacts/([^/]+)$"));

    if (const auto match = artifactPattern.match(relativePath); match.hasMatch()) {
        return artifact(root, match.captured(1), match.captured(2));
    }

    return pageFile(pagePath, relativePath);
}
} // namespace Observatory

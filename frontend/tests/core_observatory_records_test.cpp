#include "Core/Domains/Observatory/observatory_records.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>

// The IDE host's read-only view of Observatory records for the dashboard
// panel. These mirror the Node dashboard server's tests.
class ObservatoryRecordsTest : public QObject
{
    Q_OBJECT

private:
    static constexpr auto Build = "20261021T090000-000Z-build-00000b";
    static constexpr auto Broken = "20261028T100000-000Z-ui-sidebar-000014";

    static void write(const QString &path, const QByteArray &content)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(content);
    }

    QTemporaryDir m_root;

    QString run(const QString &runId) const
    {
        return QDir(m_root.path()).filePath(QStringLiteral("runs/") + runId);
    }

    static QJsonObject json(const Observatory::Response &response)
    {
        return QJsonDocument::fromJson(response.body).object();
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_root.isValid());
        write(run(Build) + QStringLiteral("/summary.json"),
            R"({"schemaVersion":1,"kind":"build","label":"fixture: build","totalDurationMs":12000,
                "artifacts":{"log":"build.log","escape":"../../outside.txt","absolute":"/etc/hosts","gone":"missing.log","link":"link.log"}})");
        write(run(Build) + QStringLiteral("/build.log"), "line 1\n<script>alert(1)</script>\n");
        write(run(Broken) + QStringLiteral("/summary.json"), "{ this is not json");
        write(QDir(m_root.path()).filePath(QStringLiteral("outside.txt")), "private");
        QVERIFY(QFile::link(QDir(m_root.path()).filePath(QStringLiteral("outside.txt")), run(Build) + QStringLiteral("/link.log")));
        QDir().mkpath(QDir(m_root.path()).filePath(QStringLiteral("runs/not-a-run")));
    }

    void cleanup()
    {
        qunsetenv("ARCHIVIST_OBSERVATORY_DIR");
    }

    void listsRawSummariesNewestFirst()
    {
        const Observatory::Response response = Observatory::handle(m_root.path(), QString(), QStringLiteral("api/runs"));
        QCOMPARE(response.status, 200);
        const QJsonObject body = json(response);
        QCOMPARE(body.value(QStringLiteral("root")).toString(), m_root.path());
        const QJsonArray runs = body.value(QStringLiteral("runs")).toArray();
        QCOMPARE(runs.size(), 2);  // not-a-run is ignored
        QCOMPARE(runs.at(0).toObject().value(QStringLiteral("runId")).toString(), QString::fromLatin1(Broken));
        QVERIFY(runs.at(0).toObject().value(QStringLiteral("summary")).isNull());
        QVERIFY(runs.at(0).toObject().value(QStringLiteral("error")).toString().startsWith(QStringLiteral("summary.json is unreadable")));
        const QJsonObject build = runs.at(1).toObject();
        QCOMPARE(build.value(QStringLiteral("summary")).toObject().value(QStringLiteral("totalDurationMs")).toInt(), 12000);
        QVERIFY(build.value(QStringLiteral("error")).isNull());
    }

    void aMissingFolderListsNothing()
    {
        const QJsonObject body = json(Observatory::runsListing(QDir(m_root.path()).filePath(QStringLiteral("nowhere"))));
        QCOMPARE(body.value(QStringLiteral("runs")).toArray().size(), 0);
    }

    void servesListedArtifactsAsText()
    {
        const Observatory::Response response = Observatory::handle(m_root.path(), QString(), QStringLiteral("api/runs/%1/artifacts/log").arg(QString::fromLatin1(Build)));
        QCOMPARE(response.status, 200);
        QVERIFY(response.contentType.startsWith("text/plain"));
        QCOMPARE(response.body, QByteArray("line 1\n<script>alert(1)</script>\n"));
        QVERIFY(response.headers.contains("X-Content-Type-Options: nosniff"));
    }

    void refusesEverythingElse()
    {
        const QString build = QString::fromLatin1(Build);
        const auto status = [this](const QString &path) {
            return Observatory::handle(m_root.path(), QString(), path).status;
        };
        QCOMPARE(status(QStringLiteral("api/runs/not-a-run/artifacts/log")), 400);
        QCOMPARE(status(QStringLiteral("api/runs/../../x/artifacts/log")), 404);
        QCOMPARE(status(QStringLiteral("api/runs/%1/artifacts/summary").arg(build)), 404);
        QCOMPARE(status(QStringLiteral("api/runs/%1/artifacts/escape").arg(build)), 404);
        QCOMPARE(status(QStringLiteral("api/runs/%1/artifacts/absolute").arg(build)), 404);
        QCOMPARE(status(QStringLiteral("api/runs/%1/artifacts/gone").arg(build)), 404);
        QCOMPARE(status(QStringLiteral("api/runs/%1/artifacts/link").arg(build)), 404);
        QCOMPARE(status(QStringLiteral("api/runs/%1/artifacts/log").arg(QString::fromLatin1(Broken))), 404);
        QCOMPARE(status(QStringLiteral("api/runs/%1").arg(build)), 404);
    }

    void servesOnlyThePageFiles()
    {
        QTemporaryDir page;
        write(QDir(page.path()).filePath(QStringLiteral("index.html")), "<!doctype html>");
        write(QDir(page.path()).filePath(QStringLiteral("app.js")), "start()");
        write(QDir(page.path()).filePath(QStringLiteral("secret.txt")), "no");

        const Observatory::Response index = Observatory::handle(m_root.path(), page.path(), QString());
        QCOMPARE(index.status, 200);
        QVERIFY(index.contentType.startsWith("text/html"));
        QVERIFY(std::any_of(index.headers.cbegin(), index.headers.cend(), [](const QByteArray &header) {
            return header.startsWith("Content-Security-Policy: default-src 'self'");
        }));
        QCOMPARE(Observatory::handle(m_root.path(), page.path(), QStringLiteral("app.js")).contentType, QByteArray("text/javascript; charset=utf-8"));
        QCOMPARE(Observatory::handle(m_root.path(), page.path(), QStringLiteral("secret.txt")).status, 404);
        QCOMPARE(Observatory::handle(m_root.path(), page.path(), QStringLiteral("../index.html")).status, 404);
        QCOMPARE(Observatory::handle(m_root.path(), page.path(), QStringLiteral("charts.js")).status, 404);  // listed but absent
        QCOMPARE(Observatory::handle(m_root.path(), QString(), QStringLiteral("index.html")).status, 404);
    }

    void runIds()
    {
        QVERIFY(Observatory::isRunId(QString::fromLatin1(Build)));
        QVERIFY(!Observatory::isRunId(QStringLiteral("../20261021T090000-000Z-build-00000b")));
        QVERIFY(!Observatory::isRunId(QString()));
    }

    void rootFollowsTheEnvironment()
    {
        qputenv("ARCHIVIST_OBSERVATORY_DIR", m_root.path().toUtf8());
        QCOMPARE(Observatory::defaultRoot(), QDir::cleanPath(m_root.path()));
        qunsetenv("ARCHIVIST_OBSERVATORY_DIR");
        QVERIFY(Observatory::defaultRoot().toLower().contains(QStringLiteral("observatory")));
    }
};

QTEST_GUILESS_MAIN(ObservatoryRecordsTest)

#include "core_observatory_records_test.moc"

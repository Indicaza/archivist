#include "Core/core_error.h"
#include "Core/core_runtime.h"
#include "Core/Domains/Observatory/live_events.h"
#include "Core/Domains/Observatory/live_session.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

class CoreDatabaseTest final : public QObject
{
    Q_OBJECT

private slots:
    // While recording, each job is a db.<kind> span with its queue wait,
    // execution and callback delay as children: a fast write queued behind
    // a slow one waits, but doesn't execute slowly. Reads run on the calling
    // thread. Names are operation names, never SQL or its parameters.
    void queueWaitAndExecutionAreMeasuredApart()
    {
        QTemporaryDir directory;
        CoreRuntime runtime;
        QVERIFY(runtime.start(directory.filePath(QStringLiteral("archivist.db"))));
        runtime.reader().exec(QStringLiteral("CREATE TABLE t (v TEXT)"));
        Live::Session session;
        session.reset(LiveEvents::nowNs());
        LiveEvents::setSink(&session);

        int delivered = 0;
        runtime.write([](CoreDatabase &database) {
            QThread::msleep(150);
            database.exec(QStringLiteral("INSERT INTO t (v) VALUES ('secret-value')"));
            return QVariant();
        }, this, [&](const CoreResult &) { ++delivered; }, "slow-write");
        runtime.write([](CoreDatabase &) { return QVariant(QVariantList{1, 2, 3}); }, this, [&](const CoreResult &) { ++delivered; }, "fast-write");
        runtime.read([](CoreDatabase &) -> QVariant { throw CoreError(QStringLiteral("no")); }, this, [&](const CoreResult &) { ++delivered; });
        QTRY_COMPARE(delivered, 3);
        LiveEvents::setSink(nullptr);

        const QJsonArray events = Live::eventsJson(session.snapshot()).value(QStringLiteral("events")).toArray();
        QHash<quint64, QJsonObject> jobs;
        QMultiHash<quint64, QJsonObject> children;
        for (const QJsonValue &value : events) {
            const QJsonObject event = value.toObject();
            if (event.value(QStringLiteral("category")).toString() != QLatin1String("db")) {
                continue;
            }
            const quint64 parent = static_cast<quint64>(event.value(QStringLiteral("parent")).toDouble());
            if (parent) {
                children.insert(parent, event);
            } else {
                jobs.insert(static_cast<quint64>(event.value(QStringLiteral("id")).toDouble()), event);
            }
        }
        QCOMPARE(jobs.size(), 3);
        const auto child = [&](quint64 job, const char *name) {
            for (const QJsonObject &event : children.values(job)) {
                if (event.value(QStringLiteral("name")).toString() == QLatin1String(name)) {
                    return event;
                }
            }
            return QJsonObject();
        };
        bool sawFast = false, sawRead = false;
        for (auto it = jobs.constBegin(); it != jobs.constEnd(); ++it) {
            const QJsonObject fields = it.value().value(QStringLiteral("fields")).toObject();
            const QString op = fields.value(QStringLiteral("op")).toString();
            if (op == QLatin1String("fast-write")) {
                sawFast = true;
                QVERIFY2(child(it.key(), "queue-wait").value(QStringLiteral("durationMs")).toDouble() >= 100, "waited behind the slow write");
                QVERIFY(child(it.key(), "execute").value(QStringLiteral("durationMs")).toDouble() < 50);
                QCOMPARE(child(it.key(), "execute").value(QStringLiteral("thread")).toString(), QStringLiteral("ArchivistCoreWriter"));
                QCOMPARE(fields.value(QStringLiteral("rows")).toInt(), 3);
                QVERIFY(!child(it.key(), "callback-delay").isEmpty());
            }
            if (it.value().value(QStringLiteral("name")).toString() == QLatin1String("read")) {
                sawRead = true;
                QCOMPARE(op, QStringLiteral("CoreDatabaseTest"));
                QCOMPARE(child(it.key(), "execute").value(QStringLiteral("thread")).toString(), QStringLiteral("main"));
                QCOMPARE(it.value().value(QStringLiteral("ok")).toBool(), false);
                QCOMPARE(fields.value(QStringLiteral("error")).toString(), QStringLiteral("failed"));
            }
        }
        QVERIFY(sawFast && sawRead);
        QVERIFY(!QJsonDocument(events).toJson().contains("secret-value"));
        QVERIFY(!QJsonDocument(events).toJson().contains("INSERT"));
    }

    void migratesAnEmptyDatabaseToTheLatestSchema()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        CoreDatabase database;
        database.open(directory.filePath(QStringLiteral("archivist.db")));

        const int latest = CoreDatabase::latestBundledVersion();
        QVERIFY(latest >= 20);
        QCOMPARE(database.runMigrations(), latest);
        QCOMPARE(database.userVersion(), latest);
        QCOMPARE(database.runMigrations(), 0);

        for (const char *table : {"collections", "chats", "messages", "libraries",
                                  "library_chunk_search", "ai_runs", "ai_edit_operations"}) {
            QVERIFY2(database.tableExists(QString::fromLatin1(table)), table);
        }

        QVERIFY(database.hasCompileOption(QStringLiteral("ENABLE_FTS5")));
        QCOMPARE(
            database.prepare(QStringLiteral("PRAGMA journal_mode")).scalar().toString(),
            QStringLiteral("wal")
        );
        QCOMPARE(database.prepare(QStringLiteral("PRAGMA foreign_keys")).scalar().toInt(), 1);
    }

    void bindsReadsAndRollsBack()
    {
        QTemporaryDir directory;
        CoreDatabase database;
        database.open(directory.filePath(QStringLiteral("scratch.db")));
        database.exec(QStringLiteral("CREATE TABLE notes (id INTEGER PRIMARY KEY, body TEXT, score REAL, raw BLOB)"));

        database.prepare(QStringLiteral("INSERT INTO notes (body, score, raw) VALUES (:body, :score, :raw)"))
            .bind(QStringLiteral("body"), QStringLiteral("héllo"))
            .bind(QStringLiteral(":score"), 2.5)
            .bind(QStringLiteral("raw"), QByteArray("\x00\x01", 2))
            .run();

        const auto row = database.prepare(QStringLiteral("SELECT * FROM notes")).one();
        QVERIFY(row.has_value());
        QCOMPARE(row->value(QStringLiteral("body")).toString(), QStringLiteral("héllo"));
        QCOMPARE(row->value(QStringLiteral("score")).toDouble(), 2.5);
        QCOMPARE(row->value(QStringLiteral("raw")).toByteArray(), QByteArray("\x00\x01", 2));

        bool threw = false;

        try {
            database.transaction([&]() {
                database.exec(QStringLiteral("INSERT INTO notes (body) VALUES ('kept?')"));
                throw CoreError(QStringLiteral("abort"));
            });
        } catch (const CoreError &) {
            threw = true;
        }

        QVERIFY(threw);
        QCOMPARE(database.prepare(QStringLiteral("SELECT count(*) FROM notes")).scalar().toInt(), 1);

        bool badSql = false;

        try {
            (void)database.prepare(QStringLiteral("SELECT * FROM missing_table"));
        } catch (const CoreError &error) {
            badSql = error.message().contains(QStringLiteral("missing_table"));
        }

        QVERIFY(badSql);
    }

    void writesOnTheWriterThreadAndReportsBack()
    {
        QTemporaryDir directory;
        CoreRuntime runtime;
        QVERIFY2(
            runtime.start(directory.filePath(QStringLiteral("archivist.db"))),
            qPrintable(runtime.errorMessage())
        );
        QVERIFY(runtime.ready());
        QCOMPARE(runtime.schemaVersion(), CoreDatabase::latestBundledVersion());

        runtime.reader().exec(QStringLiteral(
            "CREATE TABLE core_test_settings (key TEXT PRIMARY KEY, value TEXT)"
        ));

        QThread *uiThread = QThread::currentThread();
        QThread *jobThread = nullptr;
        CoreResult success;
        bool successDelivered = false;

        runtime.write(
            [&](CoreDatabase &database) {
                jobThread = QThread::currentThread();
                database.exec(QStringLiteral(
                    "INSERT INTO core_test_settings (key, value) VALUES ('core.test', 'yes')"
                ));
                return QVariant(42);
            },
            this,
            [&](const CoreResult &result) {
                QCOMPARE(QThread::currentThread(), uiThread);
                success = result;
                successDelivered = true;
            }
        );

        QTRY_VERIFY(successDelivered);
        QVERIFY2(success.ok, qPrintable(success.errorMessage));
        QCOMPARE(success.value.toInt(), 42);
        QVERIFY(jobThread != uiThread);
        QCOMPARE(
            runtime.reader()
                .prepare(QStringLiteral("SELECT value FROM core_test_settings WHERE key = 'core.test'"))
                .scalar()
                .toString(),
            QStringLiteral("yes")
        );

        CoreResult failure;
        bool failureDelivered = false;

        runtime.write(
            [](CoreDatabase &database) -> QVariant {
                database.exec(QStringLiteral(
                    "INSERT INTO core_test_settings (key, value) VALUES ('core.rollback', 'no')"
                ));
                throw CoreError(QStringLiteral("deliberate failure"));
            },
            this,
            [&](const CoreResult &result) {
                failure = result;
                failureDelivered = true;
            }
        );

        QTRY_VERIFY(failureDelivered);
        QVERIFY(!failure.ok);
        QCOMPARE(failure.errorMessage, QStringLiteral("deliberate failure"));
        QVERIFY(!runtime.reader()
                     .prepare(QStringLiteral("SELECT 1 FROM core_test_settings WHERE key = 'core.rollback'"))
                     .one()
                     .has_value());
    }
};

QTEST_GUILESS_MAIN(CoreDatabaseTest)
#include "core_database_test.moc"

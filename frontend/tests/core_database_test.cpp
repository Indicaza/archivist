#include "Core/core_error.h"
#include "Core/core_runtime.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

class CoreDatabaseTest final : public QObject
{
    Q_OBJECT

private slots:
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

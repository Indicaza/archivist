#include "Core/Support/app_profile.h"
#include "Core/core_runtime.h"

#include <QDir>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

// The disposable profile the Observatory UI Lab runs in.
class AppProfileTest : public QObject
{
    Q_OBJECT

private slots:
    void cleanup()
    {
        qunsetenv("ARCHIVIST_PROFILE_DIR");
        qunsetenv("ARCHIVIST_DB_PATH");
    }

    void inactiveWithoutTheVariable()
    {
        QVERIFY(!AppProfile::active());
        QVERIFY(AppProfile::directory().isEmpty());
        QVERIFY(!AppProfile::contains(QDir::tempPath()));
    }

    void keepsDataInsideTheProfile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        qputenv("ARCHIVIST_PROFILE_DIR", dir.path().toUtf8());

        QVERIFY(AppProfile::active());
        QCOMPARE(AppProfile::dataLocation(), QDir::cleanPath(dir.path()));
        // Never the repository's database, even when it exists.
        QCOMPARE(CoreRuntime::defaultDatabasePath(), QDir(dir.path()).filePath(QStringLiteral("archivist.db")));
        QVERIFY(AppProfile::contains(CoreRuntime::defaultDatabasePath()));
        QVERIFY(AppProfile::contains(QDir(dir.path()).filePath(QStringLiteral("not/yet/made.json"))));
        QVERIFY(!AppProfile::contains(QDir(dir.path()).filePath(QStringLiteral("../elsewhere.db"))));
        QVERIFY(!AppProfile::contains(dir.path() + QStringLiteral("-sibling/archivist.db")));

        QVERIFY(AppProfile::isolateSettings());
        QSettings settings;
        settings.setValue(QStringLiteral("lab/check"), 1);
        settings.sync();
        QVERIFY2(AppProfile::contains(settings.fileName()), qPrintable(settings.fileName()));
    }

    void anExplicitDatabaseStillWins()
    {
        QTemporaryDir dir;
        qputenv("ARCHIVIST_PROFILE_DIR", dir.path().toUtf8());
        qputenv("ARCHIVIST_DB_PATH", "/somewhere/else.db");
        // The UI Lab rejects this one: it is outside the profile.
        QCOMPARE(CoreRuntime::defaultDatabasePath(), QStringLiteral("/somewhere/else.db"));
        QVERIFY(!AppProfile::contains(CoreRuntime::defaultDatabasePath()));
    }
};

QTEST_GUILESS_MAIN(AppProfileTest)
#include "core_app_profile_test.moc"

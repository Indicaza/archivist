#include "App/Services/live_recorder.h"
#include "App/Services/ui_lab.h"
#include "Core/Domains/AI/library_retrieval.h"

#include "Core/Domains/Observatory/live_events.h"
#include "Core/Domains/Chats/chat_repository.h"
#include "Core/Support/app_profile.h"
#include "Core/core_runtime.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QtTest>

// The UI Lab's side of the shared timeline: it collects LiveEvents on the
// same clock the driver times actions with, tags them with the running
// action, notices event-loop stalls, and keeps the live recorder from taking
// the event stream away.
class UiLabTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_profile;
    QTemporaryDir m_root;
    std::unique_ptr<UiLab> m_lab;

    static QVariantMap find(const QVariantList &operations, const QString &name)
    {
        for (const QVariant &entry : operations) {
            if (entry.toMap().value(QStringLiteral("name")).toString() == name) {
                return entry.toMap();
            }
        }
        return {};
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_profile.isValid());
        qputenv("ARCHIVIST_PROFILE_DIR", m_profile.path().toUtf8());
        QVERIFY(AppProfile::isolateSettings());
        const QString scenario = QDir(m_profile.path()).filePath(QStringLiteral("scenario.json"));
        QFile file(scenario);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"({"id":"unit","version":1,"steps":[]})");
        file.close();
        qputenv("ARCHIVIST_UI_LAB_SCENARIO", scenario.toUtf8());
        qputenv("ARCHIVIST_UI_LAB_OUTPUT", QDir(m_profile.path()).filePath(QStringLiteral("events.json")).toUtf8());

        LiveEvents::span(QStringLiteral("startup"), QStringLiteral("before-lab"), 0, 1);
        m_lab = std::make_unique<UiLab>();
        QString error;
        QVERIFY2(m_lab->load(QDir(m_profile.path()).filePath(QStringLiteral("archivist.db")), &error), qPrintable(error));
    }

    void sharesTheLiveEventsClock()
    {
        const double before = LiveEvents::nowNs() / 1e6;
        const double lab = m_lab->nowMs();
        const double after = LiveEvents::nowNs() / 1e6;
        QVERIFY(lab >= before && lab <= after);
        QVERIFY(LiveEvents::recording());
    }

    void tagsOperationsWithTheRunningAction()
    {
        m_lab->setAction(3);
        const quint64 slow = LiveEvents::begin(QStringLiteral("chat"), QStringLiteral("open-conversation"));
        {
            LiveEvents::Scope inner(QStringLiteral("chat"), QStringLiteral("publish-messages"), {{QStringLiteral("messages"), 160}});
            QThread::msleep(5);
        }
        LiveEvents::marker(QStringLiteral("shell"), QStringLiteral("navigate"));
        m_lab->setAction(-1);
        // Ends after the action: still belongs to the action it began in.
        LiveEvents::end(slow, 1, {{QStringLiteral("messages"), 160}});
        LiveEvents::marker(QStringLiteral("shell"), QStringLiteral("between"));

        const QVariantMap result = m_lab->operations();
        const QVariantList operations = result.value(QStringLiteral("operations")).toList();
        QVERIFY(find(operations, QStringLiteral("before-lab")).isEmpty());
        const QVariantMap open = find(operations, QStringLiteral("open-conversation"));
        const QVariantMap publish = find(operations, QStringLiteral("publish-messages"));
        QCOMPARE(open.value(QStringLiteral("action")).toInt(), 3);
        QCOMPARE(publish.value(QStringLiteral("action")).toInt(), 3);
        QCOMPARE(find(operations, QStringLiteral("between")).value(QStringLiteral("action")).toInt(), -1);
        QVERIFY(publish.value(QStringLiteral("endMs")).toDouble() - publish.value(QStringLiteral("startMs")).toDouble() >= 4.0);
        // Nested by time: the inner span lies inside the outer one.
        QVERIFY(publish.value(QStringLiteral("startMs")).toDouble() >= open.value(QStringLiteral("startMs")).toDouble());
        QVERIFY(publish.value(QStringLiteral("endMs")).toDouble() <= open.value(QStringLiteral("endMs")).toDouble());
        QCOMPARE(open.value(QStringLiteral("ok")).toBool(), true);
        QCOMPARE(open.value(QStringLiteral("fields")).toMap().value(QStringLiteral("messages")).toInt(), 160);
        QCOMPARE(result.value(QStringLiteral("dropped")).toInt(), 0);
    }

    void noticesEventLoopStalls()
    {
        m_lab->setAction(7);
        QTimer::singleShot(10, this, []() { QThread::msleep(180); });
        QTest::qWait(400);
        m_lab->setAction(-1);
        const QVariantList stalls = m_lab->operations().value(QStringLiteral("stalls")).toList();
        bool found = false;
        for (const QVariant &stall : stalls) {
            const QVariantMap entry = stall.toMap();
            if (entry.value(QStringLiteral("action")).toInt() == 7 && entry.value(QStringLiteral("lateMs")).toDouble() >= 100) {
                found = true;
            }
        }
        QVERIFY2(found, "a 180 ms block during action 7 is reported as a stall");
    }

    void theRecorderCannotTakeTheStream()
    {
        LiveRecorder recorder(m_root.path());
        const QVariantMap result = recorder.start(15, {}, QStringLiteral("agent"));
        QCOMPARE(result.value(QStringLiteral("error")).toMap().value(QStringLiteral("code")).toString(), QStringLiteral("busy"));
        QVERIFY(!recorder.recording());
        // And its clean-up never removes the lab's collector.
        recorder.stop();
        QVERIFY(LiveEvents::recording());
        LiveEvents::clearSink(nullptr);
        QVERIFY(LiveEvents::recording());
    }

    void fixtureMessagesAreDeterministic()
    {
        QCOMPARE(UiLab::fixtureMessage(7, 5), UiLab::fixtureMessage(7, 5));
        QVERIFY(UiLab::fixtureMessage(7, 5) != UiLab::fixtureMessage(8, 5));
        QVERIFY(UiLab::fixtureMessage(7, 5).contains(QStringLiteral("```cpp")));
        QVERIFY(UiLab::fixtureMessage(7, 3).startsWith(QStringLiteral("## ")));
        QVERIFY(UiLab::fixtureMessage(7, 1).size() > 800);
    }

    void seedsChatsIntoTheProfileDatabase()
    {
        CoreRuntime core;
        QVERIFY(core.start(QDir(m_profile.path()).filePath(QStringLiteral("archivist.db"))));
        QVERIFY(m_lab->seedChats({
            {QStringLiteral("seed"), 7},
            {QStringLiteral("chats"), QVariantList{
                QVariantMap{{QStringLiteral("key"), QStringLiteral("big")}, {QStringLiteral("title"), QStringLiteral("Lab Big")}, {QStringLiteral("messages"), 300}},
                QVariantMap{{QStringLiteral("key"), QStringLiteral("small")}, {QStringLiteral("title"), QStringLiteral("Lab Small")}, {QStringLiteral("messages"), 10}},
            }},
        }));
        QTRY_VERIFY_WITH_TIMEOUT(m_lab->seedStatus().value(QStringLiteral("state")).toString() != QLatin1String("running"), 60000);
        const QVariantMap status = m_lab->seedStatus();
        QVERIFY2(status.value(QStringLiteral("state")).toString() == QLatin1String("done"), qPrintable(status.value(QStringLiteral("error")).toString()));
        QCOMPARE(status.value(QStringLiteral("messages")).toInt(), 310);
        const QString big = status.value(QStringLiteral("chats")).toMap().value(QStringLiteral("big")).toString();
        QVERIFY(!big.isEmpty());

        int count = -1;
        bool done = false;
        core.read([big](CoreDatabase &database) { return QVariant(ChatRepository::messages(database, big).size()); }, this,
                  [&](const CoreResult &result) {
                      count = result.value.toInt();
                      done = true;
                  });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        QCOMPARE(count, 300);
        // The seeding is visible on the shared timeline too.
        bool seen = false;
        for (const QVariant &op : m_lab->operations().value(QStringLiteral("operations")).toList()) {
            seen = seen || op.toMap().value(QStringLiteral("name")).toString() == QLatin1String("seed-chats");
        }
        QVERIFY(seen);
    }

    // The AI scenarios' synthetic Library: notes written and indexed, so
    // retrieval has something to find.
    void seedsASearchableLibrary()
    {
        // Its own database: the profile's library folder is already a
        // Library in the one the test above seeded.
        CoreRuntime core;
        QVERIFY(core.start(QDir(m_profile.path()).filePath(QStringLiteral("library-test.db"))));
        QVERIFY(m_lab->seedChats({
            {QStringLiteral("seed"), 7},
            {QStringLiteral("files"), 12},
            {QStringLiteral("chats"), QVariantList{QVariantMap{{QStringLiteral("key"), QStringLiteral("ask")}, {QStringLiteral("title"), QStringLiteral("Lab Ask")}, {QStringLiteral("messages"), 2}}}},
        }));
        QTRY_VERIFY_WITH_TIMEOUT(m_lab->seedStatus().value(QStringLiteral("state")).toString() != QLatin1String("running"), 60000);
        const QVariantMap status = m_lab->seedStatus();
        QVERIFY2(status.value(QStringLiteral("state")).toString() == QLatin1String("done"), qPrintable(status.value(QStringLiteral("error")).toString()));
        QCOMPARE(status.value(QStringLiteral("files")).toInt(), 12);
        const QString libraryId = status.value(QStringLiteral("libraryId")).toString();
        int hits = -1;
        bool done = false;
        core.read([libraryId](CoreDatabase &database) {
            return QVariant(LibraryRetrieval::search(database, QStringLiteral("archive layout render"), libraryId, 10).value(QStringLiteral("candidates")).toList().size());
        }, this, [&](const CoreResult &result) {
            hits = result.value.toInt();
            done = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        QVERIFY2(hits > 0, "the seeded notes are indexed for search");
    }

    void cleanupTestCase()
    {
        m_lab.reset();
        QVERIFY(!LiveEvents::recording());
        qunsetenv("ARCHIVIST_PROFILE_DIR");
        qunsetenv("ARCHIVIST_UI_LAB_SCENARIO");
        qunsetenv("ARCHIVIST_UI_LAB_OUTPUT");
    }
};

QTEST_MAIN(UiLabTest)
#include "ui_lab_test.moc"

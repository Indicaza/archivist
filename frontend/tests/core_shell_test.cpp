#include "App/Domains/Shell/shell_store.h"
#include "Core/Domains/Shell/recent_items.h"
#include "Core/Domains/Shell/shell_navigation.h"

#include <QSet>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace Shell;

namespace
{
RecentItem recent(const QString &kind, const QString &id, const QString &title = {}, const QString &collectionId = {})
{
    RecentItem item;
    item.kind = kind;
    item.id = id;
    item.title = title;
    item.collectionId = collectionId;
    return item;
}

NavigationState workspaceWith(const QString &panelId, bool open)
{
    NavigationState state;
    state.destination = Destinations::Workspace;
    state.panelId = panelId;
    state.panelOpen = open;
    return state;
}
} // namespace

class CoreShellTest final : public QObject
{
    Q_OBJECT

private slots:
    // ---- rail ----------------------------------------------------------

    void railStartsWithHomeAndKeepsLegacyIndexes()
    {
        const QList<RailItem> &items = railItems();
        QVERIFY(items.size() >= 6);
        QCOMPARE(items.first().id, RailIds::Home);
        QVERIFY(!items.first().isPanel());

        // Shell state saved before ids existed stored these indexes.
        QCOMPARE(panelIdForLegacyIndex(0), RailIds::Navigator);
        QCOMPARE(panelIdForLegacyIndex(1), RailIds::Archive);
        QCOMPARE(panelIdForLegacyIndex(2), RailIds::Search);
        QCOMPARE(panelIdForLegacyIndex(3), RailIds::Plugins);
        QCOMPARE(panelIdForLegacyIndex(4), RailIds::Tools);
        QCOMPARE(panelIdForLegacyIndex(99), RailIds::Navigator);
        QCOMPARE(legacyIndexForPanel(RailIds::Tools), 4);
        QCOMPARE(legacyIndexForPanel(QStringLiteral("nope")), 0);
    }

    void railIdsAreUniqueAndDescribed()
    {
        QSet<QString> ids;
        for (const RailItem &item : railItems()) {
            QVERIFY2(!ids.contains(item.id), qPrintable(item.id));
            ids.insert(item.id);
            QVERIFY(item.id.startsWith(QStringLiteral("navigation.")));
            QVERIFY(!item.label.isEmpty());
            QVERIFY(!item.icon.isEmpty());
            QVERIFY(!item.description.isEmpty());
        }
    }

    // ---- navigation ----------------------------------------------------

    void defaultIsHome()
    {
        const NavigationState state;
        QVERIFY(state.homeActive());
        QVERIFY(!state.panelShown());
    }

    void homeTogglesBackToTheSameWorkspace()
    {
        const NavigationState work = workspaceWith(RailIds::Search, true);
        const NavigationState home = activate(work, RailIds::Home);
        QVERIFY(home.homeActive());
        // Leaving keeps the panel exactly as it was.
        QCOMPARE(home.panelId, RailIds::Search);
        QVERIFY(home.panelOpen);
        QVERIFY(!home.panelShown());

        const NavigationState back = activate(home, RailIds::Home);
        QCOMPARE(back, work);
    }

    void panelFromHomeOpensItInTheWorkspace()
    {
        NavigationState home;
        home.panelId = RailIds::Navigator;
        home.panelOpen = false;
        const NavigationState next = activate(home, RailIds::Tools);
        QCOMPARE(next.destination, Destinations::Workspace);
        QCOMPARE(next.panelId, RailIds::Tools);
        QVERIFY(next.panelOpen);
    }

    void panelClicksMatchTheOldRail()
    {
        const NavigationState open = workspaceWith(RailIds::Navigator, true);
        // Clicking the open panel closes it.
        const NavigationState closed = activate(open, RailIds::Navigator);
        QVERIFY(!closed.panelOpen);
        QCOMPARE(closed.panelId, RailIds::Navigator);
        // Clicking it again opens it.
        QVERIFY(activate(closed, RailIds::Navigator).panelOpen);
        // Another panel switches and opens.
        const NavigationState other = activate(closed, RailIds::Archive);
        QCOMPARE(other.panelId, RailIds::Archive);
        QVERIFY(other.panelOpen);
    }

    void unknownIdsChangeNothing()
    {
        const NavigationState work = workspaceWith(RailIds::Search, false);
        QCOMPARE(activate(work, QStringLiteral("navigation.unknown")), work);
        QCOMPARE(showPanel(work, QStringLiteral("navigation.home")), work);
    }

    void showPanelLeavesHome()
    {
        const NavigationState next = showPanel(NavigationState{}, RailIds::Navigator);
        QVERIFY(!next.homeActive());
        QVERIFY(next.panelShown());
    }

    void restorePanelKeepsTheDestination()
    {
        const NavigationState home;
        const NavigationState restored = restorePanel(home, RailIds::Plugins, false);
        QVERIFY(restored.homeActive());
        QCOMPARE(restored.panelId, RailIds::Plugins);
        QVERIFY(!restored.panelOpen);
        QCOMPARE(restorePanel(home, QStringLiteral("garbage"), true).panelId, RailIds::Navigator);
    }

    void navigationReadsForgivingly()
    {
        const NavigationState work = workspaceWith(RailIds::Tools, false);
        QCOMPARE(NavigationState::fromVariant(work.toVariant()), work);

        const NavigationState junk = NavigationState::fromVariant({
            {QStringLiteral("destination"), QStringLiteral("mars")},
            {QStringLiteral("panelId"), QStringLiteral("navigation.home")},
        });
        QVERIFY(junk.homeActive());
        QCOMPARE(junk.panelId, RailIds::Navigator);
        QVERIFY(junk.panelOpen);
    }

    // ---- recent items --------------------------------------------------

    void recentsAreNewestFirstAndUnique()
    {
        RecentItems recents;
        QVERIFY(recents.note(recent(QStringLiteral("chat"), QStringLiteral("a"), QStringLiteral("Alpha"))));
        QVERIFY(recents.note(recent(QStringLiteral("document"), QStringLiteral("d1"), QStringLiteral("main.cpp"))));
        QVERIFY(recents.note(recent(QStringLiteral("chat"), QStringLiteral("a"))));

        QCOMPARE(recents.items().size(), 2);
        QCOMPARE(recents.items().first().id, QStringLiteral("a"));
        // An update without a title keeps the one it had.
        QCOMPARE(recents.items().first().title, QStringLiteral("Alpha"));
        QVERIFY(recents.items().first().accessedAt > 0);
    }

    void recentsRejectInvalidItems()
    {
        RecentItems recents;
        QVERIFY(!recents.note(recent({}, QStringLiteral("a"))));
        QVERIFY(!recents.note(recent(QStringLiteral("chat"), QStringLiteral("  "))));
        QVERIFY(recents.items().isEmpty());
    }

    void recentsStayWithinCapacity()
    {
        RecentItems recents(3);
        for (int index = 0; index < 5; ++index) {
            recents.note(recent(QStringLiteral("chat"), QString::number(index)));
        }
        QCOMPARE(recents.items().size(), 3);
        QCOMPARE(recents.items().first().id, QStringLiteral("4"));
        QCOMPARE(recents.items().last().id, QStringLiteral("2"));
    }

    void recentsFilterByKind()
    {
        RecentItems recents;
        recents.note(recent(QStringLiteral("chat"), QStringLiteral("a")));
        recents.note(recent(QStringLiteral("document"), QStringLiteral("d")));
        recents.note(recent(QStringLiteral("chat"), QStringLiteral("b")));
        const QList<RecentItem> chats = recents.items(QStringLiteral("chat"), 0);
        QCOMPARE(chats.size(), 2);
        QCOMPARE(chats.first().id, QStringLiteral("b"));
        QCOMPARE(recents.items({}, 1).size(), 1);
        QVERIFY(recents.find(QStringLiteral("document"), QStringLiteral("d")));
        QVERIFY(!recents.find(QStringLiteral("chat"), QStringLiteral("d")));
    }

    void forgettingNeverNeedsTheResource()
    {
        RecentItems recents;
        recents.note(recent(QStringLiteral("chat"), QStringLiteral("a"), {}, QStringLiteral("c1")));
        recents.note(recent(QStringLiteral("document"), QStringLiteral("d"), {}, QStringLiteral("c1")));
        recents.note(recent(QStringLiteral("collection"), QStringLiteral("c1")));
        recents.note(recent(QStringLiteral("chat"), QStringLiteral("b"), {}, QStringLiteral("c2")));

        QVERIFY(recents.forget(QStringLiteral("chat"), QStringLiteral("b")));
        QVERIFY(!recents.forget(QStringLiteral("chat"), QStringLiteral("b")));
        QCOMPARE(recents.forgetCollection(QStringLiteral("c1")), 3);
        QVERIFY(recents.items().isEmpty());
    }

    void recentsKeepOnlySmallDetails()
    {
        RecentItems recents;
        RecentItem item = recent(QStringLiteral("document"), QStringLiteral("d"));
        item.details = {
            {QStringLiteral("path"), QStringLiteral("src/main.cpp")},
            {QStringLiteral("sizeBytes"), 1200},
            {QStringLiteral("file"), QVariantMap{{QStringLiteral("big"), QStringLiteral("object")}}},
        };
        item.title = QString(1000, QLatin1Char('x'));
        recents.note(item);

        const RecentItem &stored = recents.items().first();
        QCOMPARE(stored.details.value(QStringLiteral("path")).toString(), QStringLiteral("src/main.cpp"));
        QCOMPARE(stored.details.value(QStringLiteral("sizeBytes")).toInt(), 1200);
        QVERIFY(!stored.details.contains(QStringLiteral("file")));
        QVERIFY(stored.title.size() <= 240);
    }

    void recentsRoundTripAndSkipJunk()
    {
        RecentItems recents;
        recents.note(recent(QStringLiteral("chat"), QStringLiteral("a"), QStringLiteral("Alpha"), QStringLiteral("c1")));
        recents.note(recent(QStringLiteral("document"), QStringLiteral("d"), QStringLiteral("readme.md")));

        QVariantList stored = recents.toVariant();
        stored.append(QVariantMap{{QStringLiteral("kind"), QStringLiteral("chat")}}); // no id
        stored.append(stored.first()); // duplicate
        stored.append(QStringLiteral("not a map"));

        const RecentItems loaded = RecentItems::fromVariant(stored);
        QCOMPARE(loaded.items().size(), 2);
        QCOMPARE(loaded.items().first().id, QStringLiteral("d"));
        QCOMPARE(loaded.items().last().title, QStringLiteral("Alpha"));
        QCOMPARE(loaded.items().last().collectionId, QStringLiteral("c1"));
    }

    // ---- ShellStore ----------------------------------------------------

    void storeStartsOnHomeAndRemembersTheDestination()
    {
        QTemporaryDir directory;
        QSettings settings(directory.filePath(QStringLiteral("shell.ini")), QSettings::IniFormat);
        {
            ShellStore store(&settings);
            QVERIFY(store.homeActive());
            QSignalSpy changed(&store, &ShellStore::navigationChanged);
            store.activate(RailIds::Navigator);
            QVERIFY(!store.homeActive());
            QVERIFY(store.panelShown());
            QCOMPARE(changed.count(), 1);
            // Same state again publishes nothing.
            store.returnToWorkspace();
            QCOMPARE(changed.count(), 1);
        }
        ShellStore reopened(&settings);
        QCOMPARE(reopened.destination(), Destinations::Workspace);
    }

    void storeRestoresPanelsByLegacyIndex()
    {
        QTemporaryDir directory;
        QSettings settings(directory.filePath(QStringLiteral("shell.ini")), QSettings::IniFormat);
        ShellStore store(&settings);
        store.restorePanelIndex(2, false);
        QCOMPARE(store.panelId(), RailIds::Search);
        QCOMPARE(store.panelIndex(), 2);
        QVERIFY(!store.panelOpen());
        QVERIFY(store.homeActive());
    }

    void storeKeepsRecentsAndTheHomeDraft()
    {
        QTemporaryDir directory;
        QSettings settings(directory.filePath(QStringLiteral("shell.ini")), QSettings::IniFormat);
        {
            ShellStore store(&settings);
            QSignalSpy changed(&store, &ShellStore::recentItemsChanged);
            QVERIFY(store.noteRecent({
                {QStringLiteral("kind"), QStringLiteral("chat")},
                {QStringLiteral("id"), QStringLiteral("a")},
                {QStringLiteral("title"), QStringLiteral("Alpha")},
            }));
            QCOMPARE(changed.count(), 1);
            // Seeing the front item again publishes nothing.
            QVERIFY(store.noteRecent({
                {QStringLiteral("kind"), QStringLiteral("chat")},
                {QStringLiteral("id"), QStringLiteral("a")},
            }));
            QCOMPARE(changed.count(), 1);
            QVERIFY(!store.noteRecent({{QStringLiteral("kind"), QStringLiteral("chat")}}));
            store.setHomeDraft(QStringLiteral("half a thought"));
        }
        ShellStore reopened(&settings);
        QCOMPARE(reopened.recent(QStringLiteral("chat")).size(), 1);
        QCOMPARE(reopened.recentItems().first().toMap().value(QStringLiteral("title")).toString(), QStringLiteral("Alpha"));
        QCOMPARE(reopened.homeDraft(), QStringLiteral("half a thought"));
        reopened.setHomeDraft({});
        QVERIFY(!settings.contains(QStringLiteral("shell/homeDraft")));
    }
};

QTEST_GUILESS_MAIN(CoreShellTest)

#include "core_shell_test.moc"

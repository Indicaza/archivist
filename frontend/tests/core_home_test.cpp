#include "App/Domains/Home/home_store.h"
#include "Core/Domains/Home/home_items.h"
#include "Core/Domains/Home/home_modules.h"

#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace Home;

namespace
{
QStringList visibleIds(const HomeLayout &layout)
{
    QStringList ids;
    for (const ModuleEntry &entry : layout.modules()) {
        if (entry.visible) {
            ids.append(entry.id);
        }
    }
    return ids;
}

Pin pinOf(const QString &kind, const QString &id, const QString &collectionId = {})
{
    Pin pin;
    pin.target.kind = kind;
    pin.target.id = id;
    pin.target.collectionId = collectionId;
    pin.title = id;
    return pin;
}

ActivityEvent eventOf(const QString &id, const QString &key, const QString &collectionId = {})
{
    ActivityEvent event;
    event.id = id;
    event.kind = ActivityKinds::ReplyFinished;
    event.key = key;
    event.title = QStringLiteral("Reply ready");
    event.target.kind = QStringLiteral("chat");
    event.target.id = key;
    event.target.collectionId = collectionId;
    return event;
}
} // namespace

class CoreHomeTest final : public QObject
{
    Q_OBJECT

private slots:
    // ---- registry --------------------------------------------------------

    void builtInsAreRegisteredInOrder()
    {
        const ModuleRegistry registry = ModuleRegistry::withBuiltIns();
        const QList<ModuleDescriptor> types = registry.types();
        QCOMPARE(types.size(), 4);
        QCOMPARE(types.at(0).typeId, ModuleTypes::Resume);
        QCOMPARE(types.at(1).typeId, ModuleTypes::Pinned);
        QCOMPARE(types.at(2).typeId, ModuleTypes::Activity);
        QCOMPARE(types.at(3).typeId, ModuleTypes::Layouts);
        for (const ModuleDescriptor &type : types) {
            QVERIFY(type.builtIn);
            QVERIFY(type.permissions.isEmpty());
        }
    }

    void registryRefusesBadDescriptors()
    {
        ModuleRegistry registry = ModuleRegistry::withBuiltIns();
        QString error;

        ModuleDescriptor bad;
        bad.typeId = QStringLiteral("Weather");
        bad.title = QStringLiteral("Weather");
        QVERIFY(!registry.registerType(bad, &error));
        QVERIFY(!error.isEmpty());

        ModuleDescriptor duplicate;
        duplicate.typeId = ModuleTypes::Resume;
        duplicate.title = QStringLiteral("Again");
        QVERIFY(!registry.registerType(duplicate, &error));

        ModuleDescriptor future;
        future.typeId = QStringLiteral("plugin.acme.weather");
        future.title = QStringLiteral("Weather");
        future.contractVersion = kModuleContractVersion + 1;
        QVERIFY(!registry.registerType(future, &error));

        ModuleDescriptor weather;
        weather.typeId = QStringLiteral("plugin.acme.weather");
        weather.title = QStringLiteral("Weather");
        weather.defaultSpan = QStringLiteral("enormous");
        weather.resourceKind = QStringLiteral("plugin.acme.forecast");
        weather.expandViewType = QStringLiteral("plugin.acme.forecast-view");
        weather.permissions = {QStringLiteral("network.weather")};
        QVERIFY(registry.registerType(weather, &error));
        QCOMPARE(registry.find(weather.typeId)->defaultSpan, Spans::Full);
        QCOMPARE(registry.find(weather.typeId)->expandViewType, weather.expandViewType);
    }

    // ---- layout ----------------------------------------------------------

    void defaultIsBalancedAndKeepsLayoutsAvailable()
    {
        const ModuleRegistry registry = ModuleRegistry::withBuiltIns();
        const HomeLayout layout = HomeLayout::defaults(registry);
        QCOMPARE(visibleIds(layout), (QStringList{ModuleTypes::Resume, ModuleTypes::Pinned, ModuleTypes::Activity}));
        QVERIFY(layout.find(ModuleTypes::Layouts));
        QVERIFY(!layout.find(ModuleTypes::Layouts)->visible);
        QCOMPARE(layout.matchingPreset(registry), Presets::Balanced);
    }

    void presetsDifferAndAreRecognised()
    {
        const ModuleRegistry registry = ModuleRegistry::withBuiltIns();
        const HomeLayout focused = HomeLayout::preset(Presets::Focused, registry);
        const HomeLayout overview = HomeLayout::preset(Presets::Overview, registry);
        QCOMPARE(visibleIds(focused), QStringList{ModuleTypes::Resume});
        QCOMPARE(visibleIds(overview).size(), 4);
        QCOMPARE(focused.matchingPreset(registry), Presets::Focused);
        QCOMPARE(overview.matchingPreset(registry), Presets::Overview);
        QCOMPARE(HomeLayout::preset(QStringLiteral("nope"), registry), HomeLayout::defaults(registry));
    }

    void editsMoveHideAndResize()
    {
        const ModuleRegistry registry = ModuleRegistry::withBuiltIns();
        HomeLayout layout = HomeLayout::defaults(registry);

        QVERIFY(layout.move(ModuleTypes::Activity, 0));
        QCOMPARE(layout.modules().first().id, ModuleTypes::Activity);
        QVERIFY(!layout.move(ModuleTypes::Activity, -5)); // already first
        QVERIFY(layout.move(ModuleTypes::Activity, 99));
        QCOMPARE(layout.modules().last().id, ModuleTypes::Activity);
        QVERIFY(!layout.move(QStringLiteral("missing"), 0));

        QVERIFY(layout.setVisible(ModuleTypes::Pinned, false));
        QVERIFY(!layout.setVisible(ModuleTypes::Pinned, false));
        QVERIFY(layout.setSpan(ModuleTypes::Resume, Spans::Half));
        QVERIFY(!layout.setSpan(ModuleTypes::Resume, QStringLiteral("wide")));
        QCOMPARE(layout.matchingPreset(registry), Presets::Custom);

        QCOMPARE(layout.add(ModuleTypes::Layouts, registry), ModuleTypes::Layouts);
        QVERIFY(layout.find(ModuleTypes::Layouts)->visible);
        QVERIFY(layout.add(ModuleTypes::Layouts, registry).isEmpty()); // already shown
        QVERIFY(layout.add(QStringLiteral("plugin.unknown.thing"), registry).isEmpty());
    }

    void savedLayoutRoundTripsAndForgives()
    {
        ModuleRegistry registry = ModuleRegistry::withBuiltIns();
        HomeLayout layout = HomeLayout::defaults(registry);
        layout.move(ModuleTypes::Pinned, 0);
        layout.setVisible(ModuleTypes::Activity, false);
        QCOMPARE(HomeLayout::fromVariant(layout.toVariant(), registry), layout);

        // Garbage gives the default.
        QCOMPARE(HomeLayout::fromVariant({}, registry), HomeLayout::defaults(registry));
        QCOMPARE(
            HomeLayout::fromVariant({{QStringLiteral("version"), 99}, {QStringLiteral("modules"), QVariantList{}}}, registry),
            HomeLayout::defaults(registry)
        );

        // A plugin module that's missing today is kept; built-ins the saved
        // layout doesn't mention come back hidden; junk and duplicates go.
        const QVariantMap saved{
            {QStringLiteral("version"), 1},
            {QStringLiteral("modules"), QVariantList{
                QVariantMap{{QStringLiteral("typeId"), QStringLiteral("plugin.acme.weather")}, {QStringLiteral("span"), Spans::Half}},
                QVariantMap{{QStringLiteral("typeId"), ModuleTypes::Resume}},
                QVariantMap{{QStringLiteral("typeId"), ModuleTypes::Resume}},
                QVariantMap{{QStringLiteral("typeId"), QStringLiteral("NOT AN ID")}},
            }},
        };
        const HomeLayout read = HomeLayout::fromVariant(saved, registry);
        QCOMPARE(read.modules().size(), 5);
        QCOMPARE(read.modules().at(0).typeId, QStringLiteral("plugin.acme.weather"));
        QCOMPARE(read.modules().at(0).span, Spans::Half);
        QCOMPARE(read.modules().at(1).typeId, ModuleTypes::Resume);
        QVERIFY(!read.find(ModuleTypes::Pinned)->visible);
        QVERIFY(!read.find(ModuleTypes::Activity)->visible);
    }

    // ---- pins ------------------------------------------------------------

    void pinsAreOrderedUniqueAndBounded()
    {
        Pins pins(3);
        QVERIFY(pins.pin(pinOf(QStringLiteral("chat"), QStringLiteral("a"))));
        QVERIFY(pins.pin(pinOf(QStringLiteral("collection"), QStringLiteral("p1"))));
        QVERIFY(!pins.pin(pinOf(QStringLiteral("chat"), QStringLiteral("a"))));
        QVERIFY(!pins.pin(pinOf(QString(), QStringLiteral("x"))));
        QVERIFY(pins.pin(pinOf(QStringLiteral("document"), QStringLiteral("d"), QStringLiteral("p1"))));
        QVERIFY(!pins.pin(pinOf(QStringLiteral("chat"), QStringLiteral("b")))); // full

        QVERIFY(pins.move(QStringLiteral("document"), QStringLiteral("d"), 0));
        QCOMPARE(pins.items().first().target.id, QStringLiteral("d"));
        QVERIFY(pins.rename(QStringLiteral("chat"), QStringLiteral("a"), QStringLiteral("Renamed")));
        QCOMPARE(pins.items().at(pins.indexOf(QStringLiteral("chat"), QStringLiteral("a"))).title, QStringLiteral("Renamed"));

        QCOMPARE(pins.forgetCollection(QStringLiteral("p1")), 2);
        QCOMPARE(pins.items().size(), 1);
        QVERIFY(pins.unpin(QStringLiteral("chat"), QStringLiteral("a")));
        QVERIFY(!pins.unpin(QStringLiteral("chat"), QStringLiteral("a")));

        Pins round = Pins::fromVariant(QVariantList{
            pinOf(QStringLiteral("chat"), QStringLiteral("a")).toVariant(),
            pinOf(QStringLiteral("chat"), QStringLiteral("a")).toVariant(),
            QVariantMap{},
        });
        QCOMPARE(round.items().size(), 1);
    }

    // ---- activity --------------------------------------------------------

    void activityCoalescesByKeyAndKeepsItsOwnIdentity()
    {
        ActivityLog log(3);
        QVERIFY(log.note(eventOf(QStringLiteral("e1"), QStringLiteral("chat-a"))));
        QVERIFY(log.note(eventOf(QStringLiteral("e2"), QStringLiteral("chat-b"))));
        QVERIFY(log.note(eventOf(QStringLiteral("e3"), QStringLiteral("chat-a"))));
        QCOMPARE(log.items().size(), 2);
        QCOMPARE(log.items().first().id, QStringLiteral("e3"));
        // The event's id is not the conversation's.
        QCOMPARE(log.items().first().target.id, QStringLiteral("chat-a"));

        QVERIFY(log.note(eventOf(QStringLiteral("e4"), QStringLiteral("chat-c"))));
        QVERIFY(log.note(eventOf(QStringLiteral("e5"), QStringLiteral("chat-d"))));
        QCOMPARE(log.items().size(), 3);
        QVERIFY(!log.find(QStringLiteral("e2")));

        QCOMPARE(log.unreadCount(), 3);
        QCOMPARE(log.markAllRead(), 3);
        QCOMPARE(log.unreadCount(), 0);
        QVERIFY(log.dismiss(QStringLiteral("e5")));
        QVERIFY(!log.dismiss(QStringLiteral("e5")));

        ActivityEvent invalid;
        invalid.id = QStringLiteral("x");
        QVERIFY(!log.note(invalid));
    }

    void activityRoundTripsWithArtifacts()
    {
        ActivityLog log;
        ActivityEvent event = eventOf(QStringLiteral("e1"), QStringLiteral("chat-a"), QStringLiteral("p1"));
        event.artifact.kind = QStringLiteral("document");
        event.artifact.id = QStringLiteral("p1:lib:file");
        log.note(event);
        log.note(eventOf(QStringLiteral("e2"), QStringLiteral("chat-b"), QStringLiteral("p2")));

        const ActivityLog read = ActivityLog::fromVariant(log.toVariant());
        QCOMPARE(read.items().size(), 2);
        QCOMPARE(read.find(QStringLiteral("e1"))->artifact.id, QStringLiteral("p1:lib:file"));
        QVERIFY(!read.find(QStringLiteral("e2"))->artifact.isValid());

        ActivityLog forgetting = read;
        QCOMPARE(forgetting.forgetCollection(QStringLiteral("p1")), 1);
        QCOMPARE(forgetting.items().size(), 1);
    }

    // ---- store -----------------------------------------------------------

    void storePersistsEverythingAcrossRestarts()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("settings.ini"));

        {
            QSettings settings(path, QSettings::IniFormat);
            HomeStore store(&settings);
            QCOMPARE(store.presetId(), Presets::Balanced);

            QSignalSpy layoutSpy(&store, &HomeStore::layoutChanged);
            QVERIFY(store.applyPreset(Presets::Overview));
            QVERIFY(!store.applyPreset(Presets::Overview));
            QVERIFY(!store.applyPreset(QStringLiteral("nope")));
            QVERIFY(store.moveModule(ModuleTypes::Layouts, 0));
            QCOMPARE(store.presetId(), Presets::Custom);
            QCOMPARE(layoutSpy.count(), 2);

            QVERIFY(store.pin({{QStringLiteral("kind"), QStringLiteral("chat")}, {QStringLiteral("id"), QStringLiteral("c1")}, {QStringLiteral("title"), QStringLiteral("Docking")}}));
            QVERIFY(!store.pin({{QStringLiteral("kind"), QStringLiteral("chat")}, {QStringLiteral("id"), QStringLiteral("c2")}})); // no title
            QVERIFY(store.isPinned(QStringLiteral("chat"), QStringLiteral("c1")));

            const QString eventId = store.noteActivity({
                {QStringLiteral("kind"), ActivityKinds::ReplyFinished},
                {QStringLiteral("key"), QStringLiteral("reply:c1")},
                {QStringLiteral("title"), QStringLiteral("Reply ready")},
                {QStringLiteral("target"), QVariantMap{{QStringLiteral("kind"), QStringLiteral("chat")}, {QStringLiteral("id"), QStringLiteral("c1")}}},
            });
            QVERIFY(!eventId.isEmpty());
            QVERIFY(eventId != QStringLiteral("c1"));
            QCOMPARE(store.unreadActivity(), 1);
            settings.sync();
        }

        {
            QSettings settings(path, QSettings::IniFormat);
            HomeStore store(&settings);
            QCOMPARE(store.presetId(), Presets::Custom);
            QCOMPARE(store.modules().first().toMap().value(QStringLiteral("id")).toString(), ModuleTypes::Layouts);
            QCOMPARE(store.modules().first().toMap().value(QStringLiteral("title")).toString(), QStringLiteral("Layouts"));
            QCOMPARE(store.pins().size(), 1);
            QCOMPARE(store.activity().size(), 1);
            QCOMPARE(store.unreadActivity(), 1);

            store.markActivityRead();
            QCOMPARE(store.unreadActivity(), 0);
            QVERIFY(store.togglePin({{QStringLiteral("kind"), QStringLiteral("chat")}, {QStringLiteral("id"), QStringLiteral("c1")}}));
            QVERIFY(!store.isPinned(QStringLiteral("chat"), QStringLiteral("c1")));
            store.restoreDefault();
            QCOMPARE(store.presetId(), Presets::Balanced);
        }
    }

    void pluginModulesBecomeAvailableWhenRegistered()
    {
        QTemporaryDir directory;
        QSettings settings(directory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        settings.setValue(QStringLiteral("home/layout"), QStringLiteral(
            "{\"version\":1,\"modules\":[{\"id\":\"plugin.acme.weather\",\"typeId\":\"plugin.acme.weather\",\"visible\":true,\"span\":\"half\"}]}"
        ));
        HomeStore store(&settings);
        QVariantMap first = store.modules().first().toMap();
        QCOMPARE(first.value(QStringLiteral("typeId")).toString(), QStringLiteral("plugin.acme.weather"));
        QVERIFY(!first.value(QStringLiteral("available")).toBool());

        ModuleDescriptor weather;
        weather.typeId = QStringLiteral("plugin.acme.weather");
        weather.title = QStringLiteral("Weather");
        weather.permissions = {QStringLiteral("network.weather")};
        QSignalSpy layoutSpy(&store, &HomeStore::layoutChanged);
        QVERIFY(store.registerModuleType(weather));
        QCOMPARE(layoutSpy.count(), 1);
        first = store.modules().first().toMap();
        QVERIFY(first.value(QStringLiteral("available")).toBool());
        QCOMPARE(first.value(QStringLiteral("permissions")).toStringList(), weather.permissions);
    }

    void forgettingAProjectDropsItsPinsAndEvents()
    {
        QTemporaryDir directory;
        QSettings settings(directory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        HomeStore store(&settings);
        store.pin({{QStringLiteral("kind"), QStringLiteral("collection")}, {QStringLiteral("id"), QStringLiteral("p1")}, {QStringLiteral("title"), QStringLiteral("Novel")}});
        store.pin({{QStringLiteral("kind"), QStringLiteral("chat")}, {QStringLiteral("id"), QStringLiteral("c9")}, {QStringLiteral("collectionId"), QStringLiteral("p2")}, {QStringLiteral("title"), QStringLiteral("Other")}});
        store.noteActivity({
            {QStringLiteral("kind"), ActivityKinds::ReplyFinished},
            {QStringLiteral("title"), QStringLiteral("Reply ready")},
            {QStringLiteral("target"), QVariantMap{{QStringLiteral("kind"), QStringLiteral("chat")}, {QStringLiteral("id"), QStringLiteral("c1")}, {QStringLiteral("collectionId"), QStringLiteral("p1")}}},
        });
        store.forgetCollection(QStringLiteral("p1"));
        QCOMPARE(store.pins().size(), 1);
        QCOMPARE(store.activity().size(), 0);
    }
};

QTEST_GUILESS_MAIN(CoreHomeTest)
#include "core_home_test.moc"

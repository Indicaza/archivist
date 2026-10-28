#include "Core/Domains/Composer/composer_state.h"

#include <QSet>
#include <QtTest>

using namespace Composer;

namespace
{
Environment withChatAnchors(const QStringList &anchors)
{
    Environment environment;
    environment.anchorShowsChat = [anchors](const QString &id) { return anchors.contains(id); };
    return environment;
}
} // namespace

class CoreComposerTest : public QObject
{
    Q_OBJECT

private slots:
    void registryHasBuiltInModes()
    {
        ModeRegistry registry;
        QVERIFY(registry.has(Modes::Compact));
        QVERIFY(registry.has(Modes::Workbench));
        QVERIFY(registry.mode(Modes::Compact)->allows(Placements::Anchor));
        QVERIFY(registry.mode(Modes::Compact)->allows(Placements::Floating));
        QVERIFY(registry.mode(Modes::Workbench)->allows(Placements::Bottom));
        QVERIFY(!registry.mode(Modes::Workbench)->allows(Placements::Anchor));
    }

    void pluginModesRegisterWithChecks()
    {
        ModeRegistry registry;
        QString error;
        QVERIFY(!registry.registerMode(Mode{QString(), {}, {}, {}, {Placements::Bottom}}, &error));
        QVERIFY(!registry.registerMode(Mode{Modes::Compact, {}, {}, {}, {Placements::Bottom}}, &error));
        QVERIFY(!registry.registerMode(Mode{QStringLiteral("writing"), {}, {}, {}, {QStringLiteral("sideways")}}, &error));
        QVERIFY(registry.registerMode(
            Mode{QStringLiteral("writing"), QStringLiteral("Writing"), {}, {}, {Placements::Anchor, Placements::Anchor, QStringLiteral("x")}, true},
            &error));
        const Mode *writing = registry.mode(QStringLiteral("writing"));
        QVERIFY(writing);
        QVERIFY(!writing->builtIn);
        QCOMPARE(writing->placements, QStringList{Placements::Anchor});
    }

    void modeFollowsConversationThenProjectThenSpace()
    {
        ModeRegistry registry;
        State state;
        QCOMPARE(effectiveMode(state, registry, QStringLiteral("c1")), Modes::Compact);
        QCOMPARE(effectiveMode(state, registry, QStringLiteral("c1"), Modes::Workbench), Modes::Workbench);
        QVERIFY(chooseMode(state, registry, Modes::Compact));
        QCOMPARE(effectiveMode(state, registry, QStringLiteral("c1"), Modes::Workbench), Modes::Compact);
        QVERIFY(setConversationMode(state, registry, QStringLiteral("c1"), Modes::Workbench));
        QCOMPARE(effectiveMode(state, registry, QStringLiteral("c1")), Modes::Workbench);
        QCOMPARE(effectiveMode(state, registry, QStringLiteral("c2")), Modes::Compact);
        QVERIFY(setConversationMode(state, registry, QStringLiteral("c1"), QString()));
        QCOMPARE(effectiveMode(state, registry, QStringLiteral("c1")), Modes::Compact);
    }

    void unknownModesAreIgnored()
    {
        ModeRegistry registry;
        State state;
        QVERIFY(!chooseMode(state, registry, QStringLiteral("nope")));
        state.chosenMode = QStringLiteral("gone-plugin");
        state.conversationModes.insert(QStringLiteral("c1"), QStringLiteral("gone-plugin"));
        QCOMPARE(effectiveMode(state, registry, QStringLiteral("c1"), Modes::Workbench), Modes::Workbench);
        QVERIFY(normalize(state, registry, withChatAnchors({})));
        QVERIFY(state.chosenMode.isEmpty());
        QVERIFY(state.conversationModes.isEmpty());
    }

    void workbenchShowsAtBottomButKeepsThePlacement()
    {
        ModeRegistry registry;
        State state;
        QVERIFY(dockInAnchor(state, withChatAnchors({QStringLiteral("a1")}), QStringLiteral("a1")));
        QCOMPARE(effectivePlacement(state, registry, Modes::Compact).kind, Placements::Anchor);
        QCOMPARE(effectivePlacement(state, registry, Modes::Workbench).kind, Placements::Bottom);
        // Collapsing again finds the composer where the user left it.
        QCOMPARE(state.placement.anchorId, QStringLiteral("a1"));
    }

    void dockingNeedsAChatAnchor()
    {
        State state;
        QVERIFY(!dockInAnchor(state, withChatAnchors({QStringLiteral("a1")}), QStringLiteral("a2")));
        QVERIFY(!dockInAnchor(state, withChatAnchors({QStringLiteral("a1")}), QString()));
        QVERIFY(dockInAnchor(state, withChatAnchors({QStringLiteral("a1")}), QStringLiteral("a1")));
        QVERIFY(!dockInAnchor(state, withChatAnchors({QStringLiteral("a1")}), QStringLiteral("a1")));
    }

    void staleAnchorFallsBackToBottom()
    {
        ModeRegistry registry;
        State state;
        dockInAnchor(state, withChatAnchors({QStringLiteral("a1")}), QStringLiteral("a1"));
        QVERIFY(!normalize(state, registry, withChatAnchors({QStringLiteral("a1")})));
        QVERIFY(normalize(state, registry, withChatAnchors({})));
        QVERIFY(state.placement.isBottom());
    }

    void floatingRemembersWhereItCameFrom()
    {
        State state;
        const Environment environment = withChatAnchors({QStringLiteral("a1")});
        dockInAnchor(state, environment, QStringLiteral("a1"));
        QVERIFY(!floatAt(state, QRectF(10, 10, 20, 20))); // too small
        QVERIFY(floatAt(state, QRectF(100, 100, 600, 120)));
        QCOMPARE(state.placement.kind, Placements::Floating);
        QVERIFY(floatAt(state, QRectF(140, 90, 600, 120))); // moving it keeps the origin
        QCOMPARE(state.beforeFloating.anchorId, QStringLiteral("a1"));
        QVERIFY(returnFromFloating(state, environment));
        QCOMPARE(state.placement.kind, Placements::Anchor);
        QCOMPARE(state.placement.anchorId, QStringLiteral("a1"));
        QVERIFY(!returnFromFloating(state, environment));
    }

    void floatingBackToAGoneAnchorLandsAtBottom()
    {
        State state;
        dockInAnchor(state, withChatAnchors({QStringLiteral("a1")}), QStringLiteral("a1"));
        floatAt(state, QRectF(0, 0, 600, 120));
        QVERIFY(returnFromFloating(state, withChatAnchors({})));
        QVERIFY(state.placement.isBottom());
    }

    void brokenFloatingRectIsRepaired()
    {
        ModeRegistry registry;
        State state;
        state.placement = Placement{Placements::Floating, {}, QRectF(0, 0, 0, 0)};
        QVERIFY(normalize(state, registry, withChatAnchors({})));
        QVERIFY(state.placement.isBottom());
    }

    void restoreDefaultKeepsConversationPins()
    {
        ModeRegistry registry;
        State state;
        chooseMode(state, registry, Modes::Workbench);
        dockInAnchor(state, withChatAnchors({QStringLiteral("a1")}), QStringLiteral("a1"));
        setConversationMode(state, registry, QStringLiteral("c1"), Modes::Workbench);
        QVERIFY(restoreDefault(state));
        QVERIFY(state.placement.isBottom());
        QVERIFY(state.chosenMode.isEmpty());
        QCOMPARE(state.conversationModes.value(QStringLiteral("c1")).toString(), Modes::Workbench);
        QVERIFY(!restoreDefault(state));
    }

    void moveToBottomOnlyWhenElsewhere()
    {
        State state;
        QVERIFY(!moveToBottom(state));
        dockInAnchor(state, withChatAnchors({QStringLiteral("a1")}), QStringLiteral("a1"));
        QVERIFY(moveToBottom(state));
        QVERIFY(state.placement.isBottom());
    }

    void roundTripsThroughVariant()
    {
        ModeRegistry registry;
        State state;
        chooseMode(state, registry, Modes::Workbench);
        dockInAnchor(state, withChatAnchors({QStringLiteral("a1")}), QStringLiteral("a1"));
        floatAt(state, QRectF(12, 34, 640, 130));
        setConversationMode(state, registry, QStringLiteral("c1"), Modes::Compact);
        const State loaded = State::fromVariant(state.toVariant());
        QVERIFY(loaded == state);
    }

    void forgivingLoad()
    {
        QVariantMap broken{
            {QStringLiteral("placement"), QVariantMap{{QStringLiteral("kind"), QStringLiteral("anchor")}}}, // no anchor id
            {QStringLiteral("beforeFloating"), QVariantMap{{QStringLiteral("kind"), QStringLiteral("floating")}}},
            {QStringLiteral("conversationModes"), QVariantMap{{QString(), QStringLiteral("compact")}, {QStringLiteral("c1"), QString()}}},
        };
        const State state = State::fromVariant(broken);
        QVERIFY(state.placement.isBottom());
        QVERIFY(state.beforeFloating.isBottom());
        QVERIFY(state.conversationModes.isEmpty());
        QVERIFY(State::fromVariant({}).placement.isBottom());
    }

    void commandsHaveStableUniqueIds()
    {
        QSet<QString> ids;
        for (const Command &command : commands()) {
            QVERIFY(command.id.startsWith(QStringLiteral("composer.")));
            QVERIFY(!command.label.isEmpty());
            QVERIFY(!ids.contains(command.id));
            ids.insert(command.id);
        }
        QVERIFY(ids.contains(Commands::Expand));
        QVERIFY(ids.contains(Commands::Collapse));
        QVERIFY(ids.contains(Commands::DockInConversation));
        QVERIFY(ids.contains(Commands::Float));
        QVERIFY(ids.contains(Commands::Restore));
    }
};

QTEST_GUILESS_MAIN(CoreComposerTest)
#include "core_composer_test.moc"

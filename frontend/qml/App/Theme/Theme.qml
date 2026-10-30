import QtQuick
import Archivist.Services

QtObject {
    readonly property color appBg: "#171613"
    readonly property color appBgDeep: "#100f0d"
    readonly property color appText: "#e3dfd7"
    readonly property color mutedText: "#a6a093"
    readonly property color accent: "#9280bc"
    readonly property color accentBright: "#b7aad2"
    readonly property color accentSoft: "#269280bc"
    readonly property color success: "#83a77a"
    readonly property color warning: "#c29a60"
    readonly property color danger: "#c96969"

    readonly property color topbarBg: "#151411"
    readonly property color topbarBorder: "#292621"
    readonly property color surfaceBg: "#1e1c18"
    readonly property color controlSurfaceBg: "#24221d"
    readonly property color workspaceBg: "#181713"
    readonly property color workspaceBgTop: "#1b1a17"
    readonly property color workspaceBgBottom: "#171613"
    // The workspace's compact chat composer: a bubble floating over the
    // conversation, a step lighter than the page, with a soft dark scrim
    // under it so text scrolling behind fades out.
    readonly property color compactComposerBg: "#25231f"
    readonly property color compactComposerBorder: "#36322b"
    readonly property color compactComposerBorderHover: "#454037"
    readonly property color compactComposerHighlight: "#0dfff6e6"
    readonly property color compactComposerShadow: "#000000"
    readonly property int compactComposerRadius: 24
    readonly property color workspaceBgDeep: "#12110e"
    readonly property color sidebarBg: "#1c1a16"
    readonly property color railBg: "#12110f"
    readonly property color panelBorder: "#3b3730"
    readonly property color quietBorder: "#2d2a25"
    readonly property color hoverBg: "#2b2924"
    readonly property color activeBg: "#302b38"

    readonly property color userBg: "#2a2721"
    readonly property color assistantBg: "transparent"
    readonly property color systemBg: "#24211d"
    readonly property color messageSelectionBg: "#67569f"
    readonly property color messageSelectionText: "#eee8de"
    // The code theme imported from another editor (VS Code, TextMate,
    // Sublime), or empty while Archivist's own colors are in use. See
    // CodeThemeLibrary for its shape.
    readonly property var codeTheme: CodeThemes.currentTheme
    readonly property bool hasCodeTheme: codeTheme !== undefined
        && codeTheme !== null
        && codeTheme.chatSyntax !== undefined
    readonly property color codeThemeBg: hasCodeTheme ? codeTheme.background : "#201f1b"
    readonly property color codeThemeText: hasCodeTheme ? codeTheme.foreground : "#d8d3c9"

    // Code blocks take the imported theme's background, so its colors keep
    // the contrast they were designed for.
    readonly property color codeBlockBg: codeThemeBg
    readonly property color codeBlockHeaderBg: hasCodeTheme
        ? mixColors(codeThemeBg, codeThemeText, 0.06)
        : "#292720"
    readonly property color codeBlockBorder: hasCodeTheme
        ? mixColors(codeThemeBg, codeThemeText, 0.18)
        : "#454139"
    readonly property color codeBlockText: codeThemeText
    readonly property color codeBlockMutedText: hasCodeTheme
        ? mixColors(codeThemeBg, codeThemeText, 0.6)
        : "#a29b90"
    readonly property color codeBlockHoverBg: hasCodeTheme
        ? mixColors(codeThemeBg, codeThemeText, 0.1)
        : "#34312b"
    readonly property color composerBg: "#201e1a"
    readonly property color composerCardBg: "#24221d"
    readonly property color composerCardFocusBg: "#282520"
    readonly property int composerCardRadius: 10
    readonly property color composerPlaceholder: "#90897c"
    readonly property color messageBorder: "#36322b"
    readonly property color cometHotText: "#fffaf0"
    readonly property color cometWarmText: "#ece4d8"

    readonly property string titleFontFamily: "Georgia"
    readonly property string bodyFontFamily: ".AppleSystemUIFont"
    // The system UI face (SF Pro on macOS): a large x-height and open
    // shapes read best at body size.
    readonly property string chatFontFamily: Qt.platform.os === "osx"
        ? ".AppleSystemUIFont"
        : Qt.platform.os === "windows"
            ? "Segoe UI Variable Text"
            : "Noto Sans"
    readonly property string monospaceFontFamily: Qt.platform.os === "osx"
        ? "Menlo"
        : "monospace"

    // Chat transcript fonts, shared so every message block lays out with the
    // same font (and shares cached layouts).
    // Grouped assignments rather than Qt.font(): Qt.font() drops
    // preferTypoLineMetrics, which changes line heights.
    property font chatBodyFont
    chatBodyFont.family: chatFontFamily
    chatBodyFont.pixelSize: typeBody
    chatBodyFont.weight: Font.Normal
    chatBodyFont.letterSpacing: 0
    chatBodyFont.kerning: true
    chatBodyFont.contextFontMerging: true
    chatBodyFont.hintingPreference: Font.PreferNoHinting
    chatBodyFont.preferTypoLineMetrics: true

    property font chatCodeFont
    chatCodeFont.family: monospaceFontFamily
    chatCodeFont.pixelSize: px(13)
    chatCodeFont.letterSpacing: 0
    chatCodeFont.preferTypoLineMetrics: true

    // Interface size (Cmd/Ctrl + - 0), like zooming a web page: every type
    // size and bar height below is multiplied by it, so text grows
    // everywhere and the layout reflows to the same window.
    readonly property real uiScale: InterfaceScale.scale
    function px(value) {
        return Math.round(value * uiScale)
    }

    readonly property int typeMicro: px(10)
    readonly property int typeCaption: px(12)
    readonly property int typeLabel: px(13)
    readonly property int typeControl: px(14)
    readonly property int typeBody: px(16)
    readonly property int typeTitle: px(21)
    readonly property int typeDisplay: px(34)

    readonly property int textStatusSize: typeMicro
    readonly property int textMetadataSize: typeMicro
    readonly property int textCaptionSize: typeCaption
    readonly property int textSidebarLabelSize: typeCaption
    readonly property int textTabSize: typeLabel
    readonly property int textControlSize: typeControl
    readonly property int textComposerSize: typeControl
    readonly property int textBodySize: typeBody
    readonly property int textWorkbenchTitleSize: typeBody
    readonly property int textTopbarBrandSize: typeTitle
    readonly property int textPanelTitleSize: typeTitle
    readonly property int textDisplaySize: typeDisplay

    readonly property int textWeightRegular: Font.Normal
    readonly property int textWeightEmphasis: Font.DemiBold
    readonly property int textWeightStrong: Font.Bold

    readonly property real textTrackingNormal: 0.0
    readonly property real textTrackingLabel: 0.35
    readonly property real textTrackingCaps: 0.55

    readonly property real textLineHeightControl: 1.2
    readonly property real textLineHeightCompact: 1.4
    readonly property real textLineHeightBody: 1.55
    readonly property real textLineHeightReading: 1.75

    readonly property int typeFine: typeMicro
    readonly property int typeCode: typeControl
    readonly property int typeHeading: typeTitle
    readonly property real typeLineHeightCompact: textLineHeightCompact
    readonly property real typeLineHeightBody: textLineHeightBody
    readonly property real typeLineHeightReading: textLineHeightReading

    function typeSize(referenceSize) {
        if (referenceSize <= 8) {
            return typeMicro
        }

        if (referenceSize <= 10) {
            return typeCaption
        }

        if (referenceSize <= 13) {
            return typeLabel
        }

        if (referenceSize <= 17) {
            return typeBody
        }

        if (referenceSize <= 22) {
            return typeTitle
        }

        return typeDisplay
    }

    readonly property int topbarHeight: px(40)
    readonly property int topbarAvatarSize: px(28)
    readonly property int activityRailWidth: px(44)
    readonly property int statusBarHeight: px(26)
    readonly property int explorerHeaderHeight: px(40)
    readonly property int explorerMinWidth: px(200)
    readonly property int explorerDefaultWidth: px(310)
    readonly property int explorerMaxWidth: 720
    readonly property int workspaceHeaderHeight: px(42)
    readonly property int editorTabHeight: px(32)
    readonly property int editorTabMinWidth: px(120)
    readonly property int editorTabMaxWidth: px(240)
    readonly property int workspaceMinHeight: editorTabHeight
    readonly property int chatDockHeaderHeight: px(50)
    readonly property int workbenchTabHeight: px(28)
    readonly property int chatDockDefaultHeight: px(256)
    readonly property int chatDockMinHeight: px(198)
    readonly property int chatDockPanelMinWidth: px(180)
    readonly property int chatDockPanelDefaultWidth: px(320)
    readonly property int chatDockPanelMaxWidth: 520
    readonly property int chatDockComposerMinWidth: 420
    readonly property int controlBarHeight: px(36)
    readonly property int artifactDrawerWidth: 390
    readonly property int artifactDrawerHeight: 600

    readonly property int transcriptContentWidth: px(920)
    readonly property int assistantMessageWidth: px(860)
    readonly property int userMessageWidth: px(640)
    readonly property int messageHorizontalInset: 32
    readonly property int messageVerticalGap: 34
    readonly property int codeBlockMinHeight: 72
    readonly property int codeBlockMaxHeight: 420
    // How long the pointer rests on a code block before it takes focus and
    // scroll input, and how that focus moves.
    readonly property int codeBlockFocusDelay: 550
    readonly property real codeBlockFocusGrow: 2
    readonly property real codeBlockFocusSpring: 4.2
    readonly property real codeBlockFocusDamping: 0.3
    readonly property int panelCollisionGap: 28
    readonly property int resizeHandleThickness: 8
    readonly property real hoverScale: 1.04
    readonly property real hoverNeighborScale: 1.01
    readonly property real pressedScale: 0.985
    readonly property real modalSpawnScale: 0.12
    readonly property real modalOvershootScale: 1.025
    // ---- motion you can tune -------------------------------------------
    // The feel of the app's movement, in one place. Ctrl+Alt+M opens the
    // Motion panel to change these live; they're saved for next time.
    // motionSpeed scales every duration (0.5 is twice as quick). layout*
    // is how the sidebar, the bottom dock and Home open, close and push the
    // layout: how long, and how much it overshoots (0 eases in with no
    // overshoot). reflow* is the spring panels move on when they make room
    // or are moved: stiffer is quicker, more damping is less wobble.
    readonly property var motionDefaults: ({
        speed: 1.0,
        layoutDuration: 220,
        layoutBounce: 0.3,
        reflowSpring: 4.2,
        reflowDamping: 0.34,
        reflowMass: 1.0,
        stagger: 0.35,
        anticipation: 0.25
    })
    property real motionSpeed: savedMotion("speed")
    property int layoutDuration: savedMotion("layoutDuration")
    property real layoutBounce: savedMotion("layoutBounce")
    property real reflowSpring: savedMotion("reflowSpring")
    property real reflowDamping: savedMotion("reflowDamping")
    property real reflowMass: savedMotion("reflowMass")
    property real stagger: savedMotion("stagger")
    property real anticipation: savedMotion("anticipation")

    function savedMotion(key) {
        var stored = Number(WorkspaceState.value("motion/" + key, motionDefaults[key]))
        return isFinite(stored) ? stored : motionDefaults[key]
    }
    function setMotion(key, value) {
        switch (key) {
        case "speed": motionSpeed = value; break
        case "layoutDuration": layoutDuration = value; break
        case "layoutBounce": layoutBounce = value; break
        case "reflowSpring": reflowSpring = value; break
        case "reflowDamping": reflowDamping = value; break
        case "reflowMass": reflowMass = value; break
        case "stagger": stagger = value; break
        case "anticipation": anticipation = value; break
        default: return
        }
        WorkspaceState.setValue("motion/" + key, String(value))
    }
    function resetMotion() {
        for (var key in motionDefaults) {
            setMotion(key, motionDefaults[key])
        }
    }

    // The layout's own moves (sidebar, dock, Home): this long, on this
    // curve. Anticipation draws back a touch before it goes; bounce carries
    // it a little past its mark before it settles. Both 0 is a plain ease.
    readonly property int layoutMotionDuration: Math.round(layoutDuration * motionSpeed)
    readonly property var layoutCurve: [0.34, -0.5 * anticipation, 0.18, 1 + 0.5 * layoutBounce, 1, 1]

    readonly property int motionFast: Math.round(150 * motionSpeed)
    readonly property int motionHover: Math.round(170 * motionSpeed)
    readonly property int motionHoverExit: Math.round(145 * motionSpeed)
    readonly property int motionPanel: layoutMotionDuration
    readonly property int motionModalLaunch: 235
    readonly property int motionModalSettle: 85
    readonly property int motionModalCloseKick: 55
    readonly property int motionModalClose: 175
    readonly property int chatRevealFrameInterval: 16
    readonly property real chatRevealMinimumCharactersPerSecond: 34.0
    readonly property int chatRevealMaximumDuration: 18000
    readonly property int chatCometMaximumGlyphsPerFrame: 36
    readonly property int chatCometSettleDuration: 420
    readonly property int chatCometCoolingDuration: 900
    readonly property real chatCometRise: 8.0
    readonly property real chatCometStartScale: 1.14
    readonly property int chatRevealFollowDuration: 280
    readonly property int chatRevealFollowMaximumEasingTime: 160
    readonly property real motionSpring: 3.0
    readonly property real motionDamping: 0.28
    // Chat messages follow the transcript's width on the same spring as the
    // workspace panels, each kind with its own weight: your messages are
    // light and quick, replies a little heavier, system notes heaviest.
    readonly property real chatSpringMassUser: 0.8
    readonly property real chatSpringMassAssistant: 1.15
    readonly property real chatSpringMassSystem: 1.45

    // True when the system asks for less motion (macOS Reduce motion, or
    // ARCHIVIST_REDUCED_MOTION=1): chat drops its springs, launches, pulses
    // and the in-view zones, and jumps instead of gliding.
    readonly property bool reducedMotion: ChatUi.reducedMotion

    // ---- magnets: one motion language for picking up, making room,
    // docking and putting back (Orbit, Peek and Spaces will share it). The
    // pointer is always authoritative: nothing here delays where a carried
    // panel is, only how it looks getting there. With reduced motion every
    // magnet animation jumps (springs, tremor and overshoot are skipped).
    readonly property int magnetPickupDuration: 185
    readonly property real magnetPickupScale: 0.88
    // Shadow under a carried panel: how dark, and how far it drops.
    readonly property real magnetLiftShadow: 0.56
    readonly property real magnetLiftOffset: 23
    // Neighbours making room (and the content following them). The panels
    // nearest the change move first and the far ones follow, a ripple out
    // from the pointer: the farthest spring is magnetRippleSpread softer.
    readonly property real magnetReflowSpring: reflowSpring / Math.max(0.25, motionSpeed)
    readonly property real magnetReflowDamping: reflowDamping
    readonly property real magnetRippleSpread: stagger
    // A floating panel opening on its own (not dropped) pops in from this
    // share of its size, with a little overshoot.
    readonly property real magnetAppearScale: 0.9
    readonly property int magnetAppearDuration: 240
    // The landing area easing to where a drop would go.
    readonly property real magnetLandingSpring: 4.2
    readonly property real magnetLandingDamping: 0.36
    // Held small while docking, or full size where it floats.
    readonly property int magnetSpreadDuration: 140
    // Let go: it springs into place with a restrained overshoot.
    readonly property int magnetReleaseDuration: 260
    readonly property real magnetReleaseOvershoot: 0.8

    // Gravity Well orbs: a dark sphere lit from above, warm like the app.
    readonly property color orbTop: "#4b473f"
    readonly property color orbMiddle: "#2b2924"
    readonly property color orbBottom: "#121110"
    // The Dock wave: the orb under the pointer grows this much, its
    // neighbours about half as much, fading out over this many slots.
    readonly property real wellMagnification: 1.62
    readonly property real wellMagnificationReach: 1.9
    // At rest the Well all but disappears.
    readonly property real wellDormantOpacity: 0.06
    readonly property int wellRevealDuration: 160
    readonly property int wellSettleDuration: 320
    // The Genie: a panel funnelling into its sphere, and back out. Long
    // enough to read, front-loaded so it never feels slow: the pinch shows
    // on the first frame.
    readonly property int genieMinimizeDuration: 430
    readonly property int genieRestoreDuration: 360
    readonly property int genieReducedDuration: 170
    // The sphere taking it in: a hard squash, then a springy settle, a
    // flash and a ripple.
    readonly property int wellArrivalDuration: 460
    // Cancelled or refused: back where it came from, no overshoot.
    readonly property int magnetReturnDuration: 260
    // The pull felt near a dock zone: it starts this many zone-widths away
    // and grows as the pointer closes in; at its strongest the zone's edge
    // trembles by this many pixels, this fast.
    readonly property real magnetProximityReach: 2.2
    readonly property real magnetTremor: 1.0
    readonly property int magnetTremorPeriod: 110
    // The panel that receives a drop answers with this small nudge.
    readonly property real magnetReceiveNudge: 0.012
    readonly property int magnetReceiveDuration: 220
    // A tab held over a tab strip: the tabs after the insertion point
    // step aside this far.
    readonly property real magnetTabGap: 14
    // Carried, a panel is held small so it is easy to aim (at most this
    // big); a floating panel moved by its bar shrinks to this share.
    readonly property real magnetCarryMaxWidth: 340
    readonly property real magnetCarryMaxHeight: 240
    readonly property real magnetPanelMoveScale: 0.65
    readonly property real magnetPanelMoveSpring: 5.2
    readonly property real magnetPanelMoveDamping: 0.30
    // A floating panel may hang off the window, but this much of it (and
    // its header) always stays in reach.
    readonly property real magnetKeepVisible: 96
    readonly property real magnetKeepHeader: 40
    // Picked up, a panel is held by its top-left corner, this far from the
    // pointer's tip, wherever it was grabbed.
    readonly property real magnetHoldOffset: 8
    // Momentum: what is carried hangs from that corner and swings back
    // against the way it moves (degrees per px/ms, at most magnetTiltMax),
    // on a loose spring so it rocks past level when the pointer stops. A
    // flick on release glides on for magnetThrowTime ms of its speed (at
    // most magnetThrowMax px).
    readonly property real magnetTiltPerSpeed: 3.0
    readonly property real magnetTiltMax: 6
    readonly property real magnetSwingSpring: 2.6
    readonly property real magnetSwingDamping: 0.16
    function magnetSwingAngle(velocity) {
        // Moving right the hanging body trails left (clockwise); moving
        // down it trails up a little.
        var angle = (velocity.x - 0.4 * velocity.y) * magnetTiltPerSpeed
        return Math.max(-magnetTiltMax, Math.min(magnetTiltMax, angle))
    }
    readonly property real magnetThrowTime: 90
    readonly property real magnetThrowMax: 140

    // Each agent speaks in its own hue: its rail, glyph, list bullets, small
    // headings and composer chip. The id picks a hue, so it stays the same
    // across launches without being stored.
    readonly property var agentHues: [
        "#9280bc", "#6e9fbe", "#7fa77f", "#c3956a", "#b67b9c", "#8a90cc", "#5fa8a0"
    ]

    function agentHue(agentId) {
        var key = String(agentId || "")

        if (key.length === 0) {
            return accent
        }

        var hash = 0

        for (var index = 0; index < key.length; index += 1) {
            hash = (hash * 31 + key.charCodeAt(index)) % 2147483647
        }

        return agentHues[hash % agentHues.length]
    }

    function agentHueBright(hue) {
        return Qt.lighter(hue, 1.32)
    }

    // Links in the agent's hue, never darker than reads well on the page.
    function chatLinkColor(hue) {
        var bright = agentHueBright(hue)

        return bright.hslLightness < 0.74
            ? Qt.hsla(bright.hslHue, bright.hslSaturation, 0.74, 1)
            : bright
    }

    // `amount` of the way from color a to color b.
    function mixColors(a, b, amount) {
        return Qt.rgba(
            a.r + (b.r - a.r) * amount,
            a.g + (b.g - a.g) * amount,
            a.b + (b.b - a.b) * amount,
            1
        )
    }

    function withAlpha(color, alpha) {
        return Qt.rgba(color.r, color.g, color.b, alpha)
    }

    // Reading styles for chat text, laid out in C++ by ChatText.
    // About 75 characters a line, with room between lines.
    readonly property int chatReadingMeasure: 640
    readonly property real chatLineHeight: 1.7
    readonly property real chatCodeLineHeight: 1.5
    // Code may run a little past the prose, not to the panel edge.
    readonly property int chatCodeOverhang: 96
    readonly property color chatHeadingText: "#f3efe8"
    readonly property color chatInlineCodeText: "#e3c79c"
    readonly property color chatInlineCodeBg: "#2a2723"
    readonly property color chatQuoteText: "#bcb6aa"
    readonly property color chatQuoteBar: "#4a453d"
    readonly property color chatTableBorder: "#3b3730"
    readonly property color chatTableHeaderBg: "#23211d"

    function chatReadingStyle(hue) {
        var bright = agentHueBright(hue)

        return {
            lineHeight: chatLineHeight,
            headingFamily: chatFontFamily,
            headingColor: chatHeadingText,
            accent: bright,
            codeFamily: monospaceFontFamily,
            codeColor: chatInlineCodeText,
            codeBackground: chatInlineCodeBg,
            linkColor: chatLinkColor(hue),
            linkUnderline: withAlpha(hue, 0.55),
            linkArrows: false,
            bulletColor: mutedText,
            quoteColor: chatQuoteText,
            quoteBar: chatQuoteBar,
            calloutBackground: withAlpha(hue, 0.08),
            warning: warning,
            tableBorder: chatTableBorder,
            tableHeaderBackground: chatTableHeaderBg
        }
    }

    // Syntax colors for code blocks: warm and low-contrast enough to sit in
    // the dark theme, distinct enough to scan.
    readonly property var chatSyntaxStyle: hasCodeTheme
        ? Object.assign({}, archivistSyntaxStyle, codeTheme.chatSyntax)
        : archivistSyntaxStyle
    readonly property var archivistSyntaxStyle: ({
        lineHeight: chatCodeLineHeight,
        keyword: "#c4a5e0",
        string: "#b5cc8e",
        comment: "#918a7e",
        number: "#e2a47a",
        type: "#e6c88a",
        function: "#8fb6e0",
        constant: "#e2a47a",
        meta: "#87c2c9",
        added: "#a7cb8f",
        removed: "#e39191",
        addedBackground: "#1c83a77a",
        removedBackground: "#1cc96969"
    })
    readonly property color codeGutterText: hasCodeTheme
        ? (codeTheme.lineNumbers || mixColors(codeThemeBg, codeThemeText, 0.4))
        : "#6f695f"
    // Line numbers keep step with the code beside them.
    readonly property var chatCodeGutterStyle: ({ lineHeight: chatCodeLineHeight })
    readonly property int codeBlockFoldLines: 14

    // Sending: a new message launches up from the composer.
    readonly property real chatLaunchRise: 24
    readonly property real chatLaunchScale: 0.96
    readonly property real chatSpringMassTool: 1.7
    readonly property int chatToolStagger: 40

    // Home: the personal layer above every project. Warm paper-dark tones
    // stepped up from the workspace so Home reads as its own place without
    // a different palette.
    readonly property color homeCanvasTop: "#1c1a16"
    readonly property color homeCanvasBottom: "#141310"
    readonly property color homeGlow: "#0fd9c9a8"
    readonly property color homeCardBg: "#1e1c18"
    readonly property color homeCardHoverBg: "#23211c"
    readonly property color homeCardBorder: "#2c2924"
    readonly property color homeCardBorderHover: "#3e3a32"
    readonly property color homeHairline: "#26231f"
    readonly property color homeEyebrow: "#8d8679"
    readonly property color homeComposerBg: "#211f1b"
    readonly property color homeComposerFocusBorder: "#5a5068"
    readonly property color homeSendBg: "#ebe5d9"
    readonly property color homeSendHoverBg: "#f8f3ea"
    readonly property color homeSendPressedBg: "#d2cbbd"
    readonly property color homeShadow: "#66000000"
    readonly property int homeComposerRadius: 20
    readonly property int homeCardRadius: 14
    readonly property int homeContentWidth: px(1040)
    readonly property int homeComposerWidth: px(720)
    readonly property int typeEyebrow: px(11)
    readonly property real textTrackingEyebrow: 1.1
    readonly property int motionDive: 280

    readonly property int radiusSmall: 6
    readonly property int radiusMedium: 10
    readonly property int radiusLarge: 14
    readonly property int radiusPanel: 9
}

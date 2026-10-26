import QtQuick
import Archivist.Services

QtObject {
    readonly property color appBg: "#171613"
    readonly property color appBgDeep: "#100f0d"
    readonly property color appText: "#d8d2c7"
    readonly property color mutedText: "#9a9387"
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
    readonly property color codeBlockBg: "#201f1b"
    readonly property color codeBlockHeaderBg: "#292720"
    readonly property color codeBlockBorder: "#454139"
    readonly property color codeBlockText: "#cec8bd"
    readonly property color codeBlockMutedText: "#a29b90"
    readonly property color codeBlockHoverBg: "#34312b"
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
    readonly property string chatFontFamily: Qt.platform.os === "osx"
        ? "Avenir Next"
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
    chatBodyFont.letterSpacing: 0.08
    chatBodyFont.kerning: true
    chatBodyFont.contextFontMerging: true
    chatBodyFont.hintingPreference: Font.PreferNoHinting
    chatBodyFont.preferTypoLineMetrics: true

    property font chatCodeFont
    chatCodeFont.family: monospaceFontFamily
    chatCodeFont.pixelSize: typeCode
    chatCodeFont.letterSpacing: 0.08
    chatCodeFont.preferTypoLineMetrics: true

    readonly property int typeMicro: 10
    readonly property int typeCaption: 12
    readonly property int typeLabel: 13
    readonly property int typeControl: 14
    readonly property int typeBody: 16
    readonly property int typeTitle: 21
    readonly property int typeDisplay: 34

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

    readonly property int topbarHeight: 40
    readonly property int topbarAvatarSize: 28
    readonly property int activityRailWidth: 44
    readonly property int statusBarHeight: 26
    readonly property int explorerHeaderHeight: 40
    readonly property int explorerMinWidth: 200
    readonly property int explorerDefaultWidth: 310
    readonly property int explorerMaxWidth: 720
    readonly property int workspaceHeaderHeight: 42
    readonly property int editorTabHeight: 32
    readonly property int editorTabMinWidth: 120
    readonly property int editorTabMaxWidth: 240
    readonly property int workspaceMinHeight: editorTabHeight
    readonly property int chatDockHeaderHeight: 50
    readonly property int workbenchTabHeight: 28
    readonly property int chatDockDefaultHeight: 256
    readonly property int chatDockMinHeight: 198
    readonly property int chatDockPanelMinWidth: 180
    readonly property int chatDockPanelDefaultWidth: 320
    readonly property int chatDockPanelMaxWidth: 520
    readonly property int chatDockComposerMinWidth: 420
    readonly property int controlBarHeight: 36
    readonly property int artifactDrawerWidth: 390
    readonly property int artifactDrawerHeight: 600

    readonly property int transcriptContentWidth: 920
    readonly property int assistantMessageWidth: 860
    readonly property int userMessageWidth: 640
    readonly property int messageHorizontalInset: 32
    readonly property int messageVerticalGap: 34
    readonly property int codeBlockMinHeight: 72
    readonly property int codeBlockMaxHeight: 420
    // How long the pointer rests on a code block before it takes focus and
    // scroll input, and how that focus moves.
    readonly property int codeBlockFocusDelay: 550
    readonly property real codeBlockFocusGrow: 4
    readonly property real codeBlockFocusSpring: 4.2
    readonly property real codeBlockFocusDamping: 0.3
    readonly property int panelCollisionGap: 28
    readonly property int resizeHandleThickness: 8
    readonly property real hoverScale: 1.04
    readonly property real hoverNeighborScale: 1.01
    readonly property real pressedScale: 0.985
    readonly property real modalSpawnScale: 0.12
    readonly property real modalOvershootScale: 1.025
    readonly property int motionFast: 150
    readonly property int motionHover: 170
    readonly property int motionHoverExit: 145
    readonly property int motionPanel: 240
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

    function withAlpha(color, alpha) {
        return Qt.rgba(color.r, color.g, color.b, alpha)
    }

    // Reading styles for chat text, laid out in C++ by ChatText.
    readonly property int chatReadingMeasure: 700
    readonly property real chatLineHeight: 1.55
    readonly property color chatHeadingText: "#eee8de"
    readonly property color chatInlineCodeText: "#e3c79c"
    readonly property color chatInlineCodeBg: "#2a2723"
    readonly property color chatQuoteText: "#aca59a"
    readonly property color chatQuoteBar: "#4a453d"
    readonly property color chatTableBorder: "#3b3730"
    readonly property color chatTableHeaderBg: "#23211d"

    function chatReadingStyle(hue) {
        var bright = agentHueBright(hue)

        return {
            lineHeight: chatLineHeight,
            headingFamily: titleFontFamily,
            headingColor: chatHeadingText,
            accent: bright,
            codeFamily: monospaceFontFamily,
            codeColor: chatInlineCodeText,
            codeBackground: chatInlineCodeBg,
            linkColor: bright,
            linkUnderline: withAlpha(hue, 0.55),
            linkArrows: true,
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
    readonly property var chatSyntaxStyle: ({
        keyword: "#c4a5e0",
        string: "#b5cc8e",
        comment: "#7c766b",
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
    readonly property color codeGutterText: "#5d584f"
    readonly property int codeBlockFoldLines: 14

    // Sending: a new message launches up from the composer.
    readonly property real chatLaunchRise: 24
    readonly property real chatLaunchScale: 0.96
    readonly property real chatSpringMassTool: 1.7
    readonly property int chatToolStagger: 40

    readonly property int radiusSmall: 6
    readonly property int radiusMedium: 10
    readonly property int radiusLarge: 14
    readonly property int radiusPanel: 9
}

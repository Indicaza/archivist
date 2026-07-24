import QtQuick

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

    readonly property int typeFine: 10
    readonly property int typeCaption: 12
    readonly property int typeLabel: 13
    readonly property int typeCode: 14
    readonly property int typeBody: 16
    readonly property int typeHeading: 21
    readonly property int typeDisplay: 34
    readonly property real typeLineHeightCompact: 1.4
    readonly property real typeLineHeightBody: 1.55
    readonly property real typeLineHeightReading: 1.75

    function typeSize(referenceSize) {
        if (referenceSize <= 8) {
            return typeFine
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
            return typeHeading
        }

        return typeDisplay
    }

    readonly property int topbarHeight: 52
    readonly property int activityRailWidth: 44
    readonly property int statusBarHeight: 26
    readonly property int explorerHeaderHeight: 40
    readonly property int explorerMinWidth: 244
    readonly property int explorerDefaultWidth: 310
    readonly property int explorerMaxWidth: 520
    readonly property int workspaceHeaderHeight: 42
    readonly property int workspaceMinHeight: 220
    readonly property int chatDockHeaderHeight: 50
    readonly property int chatDockDefaultHeight: 256
    readonly property int chatDockMinHeight: 198
    readonly property int chatDockPanelMinWidth: 260
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

    readonly property int radiusSmall: 6
    readonly property int radiusMedium: 10
    readonly property int radiusLarge: 14
    readonly property int radiusPanel: 9
}

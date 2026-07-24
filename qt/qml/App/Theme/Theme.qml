import QtQuick

QtObject {
    readonly property color appBg: "#11100e"
    readonly property color appBgDeep: "#090908"
    readonly property color appText: "#f4eee3"
    readonly property color mutedText: "#b8ad9d"
    readonly property color accent: "#8f7adf"
    readonly property color accentBright: "#b5a8ed"
    readonly property color accentSoft: "#218f7adf"
    readonly property color success: "#7fa878"
    readonly property color warning: "#c49a5a"
    readonly property color danger: "#c85f5f"

    readonly property color topbarBg: "#12110f"
    readonly property color topbarBorder: "#24221e"
    readonly property color surfaceBg: "#1b1916"
    readonly property color controlSurfaceBg: "#211f1b"
    readonly property color workspaceBg: "#12110f"
    readonly property color workspaceBgDeep: "#0d0c0a"
    readonly property color sidebarBg: "#161411"
    readonly property color railBg: "#0b0a09"
    readonly property color panelBorder: "#3d3932"
    readonly property color quietBorder: "#2c2923"
    readonly property color hoverBg: "#2c2924"
    readonly property color activeBg: "#2a2632"

    readonly property color userBg: "#191714"
    readonly property color assistantBg: "#26231e"
    readonly property color systemBg: "#1e1c18"
    readonly property color composerBg: "#1c1a16"
    readonly property color composerPlaceholder: "#a09586"
    readonly property color messageBorder: "#302c26"

    readonly property string titleFontFamily: "Georgia"
    readonly property string bodyFontFamily: ".AppleSystemUIFont"

    readonly property int topbarHeight: 42
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
    readonly property int controlBarHeight: 36
    readonly property int artifactDrawerWidth: 390
    readonly property int artifactDrawerHeight: 600

    readonly property int transcriptContentWidth: 980
    readonly property int assistantMessageWidth: 940
    readonly property int userMessageWidth: 720
    readonly property int messageHorizontalInset: 28
    readonly property int messageVerticalGap: 18
    readonly property int panelCollisionGap: 28
    readonly property int resizeHandleThickness: 8
    readonly property real hoverScale: 1.06
    readonly property real hoverNeighborScale: 1.015
    readonly property real pressedScale: 0.955
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
    readonly property real motionSpring: 3.0
    readonly property real motionDamping: 0.28

    readonly property int radiusSmall: 6
    readonly property int radiusMedium: 10
    readonly property int radiusLarge: 14
    readonly property int radiusPanel: 9
}

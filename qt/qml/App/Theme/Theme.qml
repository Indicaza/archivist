import QtQuick

QtObject {
    readonly property color appBg: "#11100e"
    readonly property color appBgDeep: "#090908"
    readonly property color appText: "#eee8dc"
    readonly property color mutedText: "#a79d8e"
    readonly property color accent: "#8f7adf"
    readonly property color accentBright: "#b5a8ed"
    readonly property color accentSoft: "#218f7adf"
    readonly property color success: "#7fa878"
    readonly property color warning: "#c49a5a"
    readonly property color danger: "#c85f5f"

    readonly property color topbarBg: "#12110f"
    readonly property color topbarBorder: "#24221e"
    readonly property color surfaceBg: "#181713"
    readonly property color controlSurfaceBg: "#151410"
    readonly property color workspaceBg: "#151410"
    readonly property color workspaceBgDeep: "#0d0c0a"
    readonly property color sidebarBg: "#100f0d"
    readonly property color railBg: "#0c0b0a"
    readonly property color panelBorder: "#302d27"
    readonly property color quietBorder: "#24211d"
    readonly property color hoverBg: "#23211d"
    readonly property color activeBg: "#211f25"

    readonly property color userBg: "#171613"
    readonly property color assistantBg: "#211f1b"
    readonly property color systemBg: "#1b1a17"
    readonly property color composerBg: "#1d1b17"
    readonly property color composerPlaceholder: "#918779"

    readonly property string titleFontFamily: "Georgia"
    readonly property string bodyFontFamily: ".AppleSystemUIFont"

    readonly property int topbarHeight: 42
    readonly property int activityRailWidth: 38
    readonly property int statusBarHeight: 22
    readonly property int explorerHeaderHeight: 34
    readonly property int explorerMinWidth: 204
    readonly property int explorerMaxWidth: 282
    readonly property int workspaceHeaderHeight: 38
    readonly property int chatDockHeaderHeight: 42
    readonly property int chatDockBodyHeight: 172
    readonly property int artifactDrawerWidth: 278
    readonly property int artifactDrawerHeight: 430

    readonly property int transcriptContentWidth: 920
    readonly property int assistantMessageWidth: 920
    readonly property int userMessageWidth: 680
    readonly property int messageHorizontalInset: 24
    readonly property int messageVerticalGap: 14

    readonly property int radiusSmall: 6
    readonly property int radiusMedium: 10
    readonly property int radiusLarge: 14
    readonly property int radiusPanel: 9
}

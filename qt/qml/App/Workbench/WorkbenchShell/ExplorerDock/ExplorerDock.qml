import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "ExplorerItem"

Rectangle {
    id: root

    required property var theme
    required property int activeViewIndex

    signal closeRequested()

    readonly property var viewTitles: [
        "Library Explorer",
        "Archived Libraries",
        "Library Search",
        "Plugins",
        "Tools"
    ]

    color: theme.surfaceBg
    border.width: 0
    clip: true

    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: root.theme.panelBorder
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: root.theme.explorerHeaderHeight
            color: "#1a1916"

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: root.theme.quietBorder
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 9
                anchors.rightMargin: 5
                spacing: 8

                Text {
                    Layout.fillWidth: true
                    text: root.viewTitles[root.activeViewIndex]
                    color: root.theme.mutedText
                    font.pixelSize: 9
                    font.weight: Font.Bold
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 1.0
                    elide: Text.ElideRight
                }

                Button {
                    Layout.preferredWidth: 25
                    Layout.preferredHeight: 25
                    text: "‹"
                    hoverEnabled: true
                    padding: 0
                    onClicked: root.closeRequested()

                    contentItem: Text {
                        text: parent.text
                        color: parent.hovered ? root.theme.appText : root.theme.mutedText
                        font.pixelSize: 17
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: 4
                        color: parent.hovered ? root.theme.hoverBg : "transparent"
                        border.width: parent.hovered ? 1 : 0
                        border.color: root.theme.panelBorder
                    }
                }
            }
        }

        Loader {
            Layout.fillWidth: true
            Layout.fillHeight: true
            sourceComponent: root.activeViewIndex === 0 ? libraryView : placeholderView
        }
    }

    Component {
        id: libraryView

        Item {
            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 35
                    color: root.theme.controlSurfaceBg

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: root.theme.quietBorder
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 9
                        anchors.rightMargin: 6
                        spacing: 4

                        Text {
                            Layout.fillWidth: true
                            text: "Archivist"
                            color: root.theme.appText
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }

                        Repeater {
                            model: ["⌄", "⌕", "+", "✎", "▣"]

                            delegate: Button {
                                required property string modelData

                                Layout.preferredWidth: 24
                                Layout.preferredHeight: 24
                                text: modelData
                                hoverEnabled: true
                                padding: 0

                                contentItem: Text {
                                    text: parent.text
                                    color: parent.hovered ? root.theme.appText : root.theme.mutedText
                                    font.pixelSize: 12
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }

                                background: Rectangle {
                                    radius: 4
                                    color: parent.hovered ? root.theme.hoverBg : "transparent"
                                    border.width: parent.hovered ? 1 : 0
                                    border.color: root.theme.quietBorder
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 30
                    color: root.theme.surfaceBg

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: root.theme.quietBorder
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 9
                        anchors.rightMargin: 6
                        spacing: 7

                        Text {
                            text: "Archivist"
                            color: root.theme.mutedText
                            font.pixelSize: 9
                            font.weight: Font.DemiBold
                        }

                        Rectangle {
                            Layout.preferredWidth: 25
                            Layout.preferredHeight: 16
                            radius: 8
                            color: "#26231e"

                            Text {
                                anchors.centerIn: parent
                                text: "163"
                                color: root.theme.mutedText
                                font.pixelSize: 8
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                        }

                        Button {
                            Layout.preferredWidth: 23
                            Layout.preferredHeight: 23
                            text: "↻"
                            hoverEnabled: true
                            padding: 0

                            contentItem: Text {
                                text: parent.text
                                color: parent.hovered ? root.theme.appText : root.theme.mutedText
                                font.pixelSize: 14
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }

                            background: Rectangle {
                                radius: 4
                                color: parent.hovered ? root.theme.hoverBg : "transparent"
                            }
                        }
                    }
                }

                ListView {
                    id: libraryList

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.leftMargin: 7
                    Layout.rightMargin: 7
                    Layout.topMargin: 3
                    clip: true
                    spacing: 0
                    boundsBehavior: Flickable.StopAtBounds
                    cacheBuffer: 600
                    reuseItems: true

                    model: ListModel {
                        ListElement { itemTitle: "backend"; itemGlyph: "□"; itemDepth: 0; itemSelected: false; itemMuted: false; itemFolder: true; itemExpanded: true; itemWarning: false }
                        ListElement { itemTitle: "data"; itemGlyph: "□"; itemDepth: 1; itemSelected: false; itemMuted: false; itemFolder: true; itemExpanded: false; itemWarning: false }
                        ListElement { itemTitle: "src"; itemGlyph: "□"; itemDepth: 1; itemSelected: false; itemMuted: false; itemFolder: true; itemExpanded: false; itemWarning: false }
                        ListElement { itemTitle: "package.json"; itemGlyph: "{}"; itemDepth: 1; itemSelected: false; itemMuted: false; itemFolder: false; itemExpanded: false; itemWarning: false }
                        ListElement { itemTitle: "tsconfig.json"; itemGlyph: "{}"; itemDepth: 1; itemSelected: false; itemMuted: false; itemFolder: false; itemExpanded: false; itemWarning: false }
                        ListElement { itemTitle: "frontend"; itemGlyph: "□"; itemDepth: 0; itemSelected: false; itemMuted: false; itemFolder: true; itemExpanded: true; itemWarning: false }
                        ListElement { itemTitle: "src"; itemGlyph: "□"; itemDepth: 1; itemSelected: false; itemMuted: false; itemFolder: true; itemExpanded: false; itemWarning: false }
                        ListElement { itemTitle: "eslint.config.js"; itemGlyph: "{}"; itemDepth: 1; itemSelected: false; itemMuted: false; itemFolder: false; itemExpanded: false; itemWarning: false }
                        ListElement { itemTitle: "index.html"; itemGlyph: "◇"; itemDepth: 1; itemSelected: false; itemMuted: false; itemFolder: false; itemExpanded: false; itemWarning: false }
                        ListElement { itemTitle: "package.json"; itemGlyph: "{}"; itemDepth: 1; itemSelected: false; itemMuted: false; itemFolder: false; itemExpanded: false; itemWarning: false }
                        ListElement { itemTitle: "README.md"; itemGlyph: "▤"; itemDepth: 1; itemSelected: false; itemMuted: false; itemFolder: false; itemExpanded: false; itemWarning: false }
                        ListElement { itemTitle: "tsconfig.app.json"; itemGlyph: "{}"; itemDepth: 1; itemSelected: false; itemMuted: false; itemFolder: false; itemExpanded: false; itemWarning: false }
                        ListElement { itemTitle: "tsconfig.json"; itemGlyph: "{}"; itemDepth: 1; itemSelected: false; itemMuted: false; itemFolder: false; itemExpanded: false; itemWarning: false }
                        ListElement { itemTitle: "tsconfig.node.json"; itemGlyph: "{}"; itemDepth: 1; itemSelected: false; itemMuted: false; itemFolder: false; itemExpanded: false; itemWarning: false }
                        ListElement { itemTitle: "vite.config.ts"; itemGlyph: "{}"; itemDepth: 1; itemSelected: false; itemMuted: false; itemFolder: false; itemExpanded: false; itemWarning: false }
                        ListElement { itemTitle: "catalog-test.txt"; itemGlyph: "▤"; itemDepth: 0; itemSelected: false; itemMuted: true; itemFolder: false; itemExpanded: false; itemWarning: true }
                        ListElement { itemTitle: "package-lock.json"; itemGlyph: "{}"; itemDepth: 0; itemSelected: false; itemMuted: false; itemFolder: false; itemExpanded: false; itemWarning: false }
                        ListElement { itemTitle: "package.json"; itemGlyph: "{}"; itemDepth: 0; itemSelected: false; itemMuted: false; itemFolder: false; itemExpanded: false; itemWarning: false }
                        ListElement { itemTitle: "README.md"; itemGlyph: "▤"; itemDepth: 0; itemSelected: false; itemMuted: false; itemFolder: false; itemExpanded: false; itemWarning: false }
                    }

                    delegate: ExplorerItem {
                        required property string itemTitle
                        required property string itemGlyph
                        required property int itemDepth
                        required property bool itemSelected
                        required property bool itemMuted
                        required property bool itemFolder
                        required property bool itemExpanded
                        required property bool itemWarning

                        width: libraryList.width
                        theme: root.theme
                        title: itemTitle
                        glyph: itemGlyph
                        depth: itemDepth
                        selected: itemSelected
                        muted: itemMuted
                        folder: itemFolder
                        expanded: itemExpanded
                        warning: itemWarning
                    }

                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AsNeeded
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 28
                    color: root.theme.controlSurfaceBg

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        height: 1
                        color: root.theme.quietBorder
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 9
                        anchors.rightMargin: 9
                        spacing: 6

                        Text {
                            Layout.fillWidth: true
                            text: "Ready  ·  Jul 19, 11:16 AM  ·  13 missing"
                            color: root.theme.mutedText
                            font.pixelSize: 8
                            opacity: 0.72
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }

    Component {
        id: placeholderView

        Item {
            Column {
                anchors.centerIn: parent
                width: Math.max(140, parent.width - 30)
                spacing: 10

                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 46
                    height: 46
                    radius: 13
                    color: root.theme.accentSoft
                    border.width: 1
                    border.color: "#554a7b"

                    Text {
                        anchors.centerIn: parent
                        text: root.activeViewIndex === 1 ? "▤" : root.activeViewIndex === 2 ? "⌕" : root.activeViewIndex === 3 ? "◇" : "⚒"
                        color: root.theme.accentBright
                        font.pixelSize: 20
                    }
                }

                Text {
                    width: parent.width
                    text: root.viewTitles[root.activeViewIndex]
                    color: root.theme.appText
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }

                Text {
                    width: parent.width
                    text: "This surface is structurally ready and will be connected after the native Workbench is proven."
                    color: root.theme.mutedText
                    font.pixelSize: 10
                    lineHeight: 1.35
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }
            }
        }
    }
}

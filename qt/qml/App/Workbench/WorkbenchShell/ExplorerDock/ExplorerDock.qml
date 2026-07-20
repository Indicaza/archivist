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
            Layout.preferredHeight: 34
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
                anchors.margins: 8
                spacing: 8

                TextField {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 30
                    placeholderText: "Filter files"
                    color: root.theme.appText
                    placeholderTextColor: root.theme.composerPlaceholder
                    font.pixelSize: 10
                    leftPadding: 9
                    rightPadding: 9

                    background: Rectangle {
                        radius: root.theme.radiusSmall
                        color: root.theme.workspaceBgDeep
                        border.width: 1
                        border.color: parent.activeFocus ? root.theme.accent : root.theme.quietBorder
                    }
                }

                ListView {
                    id: libraryList

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 3
                    boundsBehavior: Flickable.StopAtBounds

                    model: ListModel {
                        ListElement { itemTitle: "Archivist"; itemSubtitle: "~/Projects/Startups/Archivist"; itemDepth: 0; itemSelected: true }
                        ListElement { itemTitle: "backend"; itemSubtitle: "Express · SQLite · TypeScript"; itemDepth: 1; itemSelected: false }
                        ListElement { itemTitle: "frontend"; itemSubtitle: "Electron · React · Vite"; itemDepth: 1; itemSelected: false }
                        ListElement { itemTitle: "qt"; itemSubtitle: "Qt Quick · QML · CMake"; itemDepth: 1; itemSelected: false }
                        ListElement { itemTitle: "README.md"; itemSubtitle: "Project handoff and roadmap"; itemDepth: 1; itemSelected: false }
                    }

                    delegate: ExplorerItem {
                        required property string itemTitle
                        required property string itemSubtitle
                        required property int itemDepth
                        required property bool itemSelected

                        width: libraryList.width
                        theme: root.theme
                        title: itemTitle
                        subtitle: itemSubtitle
                        depth: itemDepth
                        selected: itemSelected
                    }

                    ScrollBar.vertical: ScrollBar {}
                }

                Button {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 31
                    text: "Rescan Library"
                    hoverEnabled: true

                    contentItem: Text {
                        text: parent.text
                        color: parent.hovered ? root.theme.appText : root.theme.mutedText
                        font.pixelSize: 9
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: root.theme.radiusSmall
                        color: parent.hovered ? root.theme.hoverBg : "#1b1a17"
                        border.width: 1
                        border.color: parent.hovered ? "#4b4269" : root.theme.quietBorder
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

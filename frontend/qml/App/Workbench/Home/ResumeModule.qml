import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../Icons"

// Resume work: the projects worked in lately, as cards that go back to
// where each was left (its conversation, its tabs, its layout). Only what
// is known is shown: a project with no saved workspace says it opens fresh.
Item {
    id: root

    required property var theme
    // [{ collectionId, name, hue, chat, chatTitle, when, summary, current }]
    property var entries: []
    property string diveCollectionId: ""
    property real diveProgress: 0

    signal openRequested(var entry)
    signal pinRequested(var entry)

    readonly property int columns: width >= 900 ? 3 : width >= 560 ? 2 : 1
    // One row: the few that matter most. Every project is in the sidebar.
    readonly property var shown: entries.slice(0, columns)

    width: parent ? parent.width : 0
    implicitHeight: entries.length === 0 ? empty.implicitHeight : grid.implicitHeight

    Text {
        id: empty

        width: parent.width
        visible: root.entries.length === 0
        text: "Projects you work in will be here, ready to pick up where you left them."
        color: root.theme.mutedText
        font.family: root.theme.bodyFontFamily
        font.pixelSize: root.theme.textControlSize
        wrapMode: Text.Wrap
    }

    GridLayout {
        id: grid

        width: parent.width
        columns: root.columns
        columnSpacing: 14
        rowSpacing: 14

        Repeater {
            model: root.shown

            delegate: HomeCard {
                id: card

                required property var modelData
                required property int index

                readonly property var summary: modelData.summary || ({})
                readonly property bool diving: root.diveCollectionId === String(modelData.collectionId)

                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.maximumWidth: (grid.width - grid.columnSpacing * (root.columns - 1)) / root.columns
                Layout.preferredHeight: 138
                theme: root.theme
                emphasized: modelData.current === true
                lift: diving ? root.diveProgress : 0
                Accessible.name: "Resume " + modelData.name
                Accessible.description: modelData.chatTitle.length > 0
                    ? "Continue " + modelData.chatTitle
                    : "Open the workspace"
                onClicked: root.openRequested(modelData)

                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: cardMenu.popup()
                }

                Menu {
                    id: cardMenu

                    MenuItem {
                        text: "Open"
                        onTriggered: root.openRequested(card.modelData)
                    }
                    MenuItem {
                        text: card.modelData.pinned ? "Unpin project" : "Pin project"
                        onTriggered: root.pinRequested(card.modelData)
                    }
                }

                contentItem: Item {
                    // Project identity: its hue as a short bar.
                    Rectangle {
                        x: 18
                        y: 20
                        width: 18
                        height: 3
                        radius: 1.5
                        color: card.modelData.hue
                    }

                    Text {
                        x: 18
                        y: 32
                        width: parent.width - 36 - whenText.width - 8
                        text: card.modelData.name
                        color: root.theme.chatHeadingText
                        font.family: root.theme.titleFontFamily
                        font.pixelSize: root.theme.px(19)
                        elide: Text.ElideRight
                    }

                    Text {
                        id: whenText

                        anchors.right: parent.right
                        anchors.rightMargin: 18
                        y: 36
                        text: card.modelData.current ? "Open now" : card.modelData.when
                        color: card.modelData.current ? root.theme.accentBright : root.theme.mutedText
                        font.family: root.theme.bodyFontFamily
                        font.pixelSize: root.theme.textCaptionSize
                    }

                    Row {
                        x: 18
                        y: 66
                        width: parent.width - 36
                        spacing: 7

                        AppIcon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: "chat"
                            tone: card.lit ? "normal" : "muted"
                            iconSize: 13
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 20
                            text: card.modelData.chatTitle.length > 0
                                ? card.modelData.chatTitle
                                : "No conversations yet"
                            color: card.modelData.chatTitle.length > 0 ? root.theme.appText : root.theme.mutedText
                            font.family: root.theme.bodyFontFamily
                            font.pixelSize: root.theme.textControlSize
                            elide: Text.ElideRight
                        }
                    }

                    Rectangle {
                        x: 18
                        y: 98
                        width: parent.width - 36
                        height: 1
                        color: root.theme.homeHairline
                    }

                    Text {
                        x: 18
                        y: 109
                        width: parent.width - 36 - openHint.width - 8
                        text: {
                            var s = card.summary
                            if (!s.available) {
                                return "Opens a fresh workspace"
                            }
                            var parts = []
                            var count = Number(s.viewCount || 0)
                            parts.push(count === 1 ? "1 tab" : count + " tabs")
                            var titles = s.titles || []
                            if (titles.length > 0) {
                                parts.push(titles.slice(0, 2).join(", "))
                            }
                            if (String(s.layoutName || "").length > 0) {
                                parts.push(String(s.layoutName))
                            }
                            return parts.join("  ·  ")
                        }
                        color: root.theme.mutedText
                        font.family: root.theme.bodyFontFamily
                        font.pixelSize: root.theme.textCaptionSize
                        elide: Text.ElideRight
                    }

                    Row {
                        id: openHint

                        anchors.right: parent.right
                        anchors.rightMargin: 16
                        y: 107
                        spacing: 4
                        opacity: card.lit ? 1 : 0

                        Behavior on opacity {
                            enabled: !root.theme.reducedMotion
                            NumberAnimation { duration: root.theme.motionHover }
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Resume"
                            color: root.theme.appText
                            font.family: root.theme.bodyFontFamily
                            font.pixelSize: root.theme.textCaptionSize
                            font.weight: root.theme.textWeightEmphasis
                        }

                        AppIcon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: "arrow-right"
                            tone: "normal"
                            iconSize: 12
                        }
                    }
                }
            }
        }
    }
}

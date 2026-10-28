import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Archivist.Services 1.0
import "../../Icons"

// Pinned: conversations, projects, files and layouts the person keeps
// close, in the order they chose. Reorder by dragging the grip (Edit Home)
// or with Move up/down; unpin from the row.
Item {
    id: root

    required property var theme
    required property var home
    property bool editing: false

    signal openRequested(var pin)

    readonly property var pins: root.home.livePins

    HomeKeyedModel {
        id: pinModel

        items: root.pins
        keyOf: function(pin) { return String(pin.kind) + "\n" + String(pin.id) }
    }

    function iconFor(kind) {
        if (kind === "chat") return "chat"
        if (kind === "collection") return "folder"
        if (kind === "layout") return "grid"
        if (kind === "document") return "file-text"
        return "pin"
    }

    width: parent ? parent.width : 0
    implicitHeight: pins.length === 0 ? empty.implicitHeight : panel.implicitHeight

    HomePanel {
        id: empty

        width: parent.width
        visible: root.pins.length === 0
        theme: root.theme
        padding: 18

        Column {
            width: parent.width
            spacing: 6

            Text {
                width: parent.width
                text: "Keep things close"
                color: root.theme.appText
                font.family: root.theme.titleFontFamily
                font.pixelSize: root.theme.px(16)
            }

            Text {
                width: parent.width
                text: "Pin a conversation, project or layout from the sidebar or a card and it stays here, in your order."
                color: root.theme.mutedText
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.textCaptionSize + 1
                lineHeight: 1.25
                wrapMode: Text.Wrap
            }
        }
    }

    HomePanel {
        id: panel

        width: parent.width
        visible: root.pins.length > 0
        theme: root.theme

        ListView {
            id: list

            width: parent.width
            height: contentHeight
            interactive: false
            model: pinModel
            spacing: 0

            add: Transition {
                enabled: !root.theme.reducedMotion
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: root.theme.motionPanel }
            }
            move: Transition {
                enabled: !root.theme.reducedMotion
                NumberAnimation { properties: "y"; duration: root.theme.motionPanel; easing.type: Easing.OutCubic }
            }
            displaced: Transition {
                enabled: !root.theme.reducedMotion
                NumberAnimation { properties: "y"; duration: root.theme.motionPanel; easing.type: Easing.OutCubic }
            }

            delegate: HomeListRow {
                id: row

                required property string key
                required property int revision
                required property int index
                readonly property var modelData: { revision; return pinModel.itemFor(key) }

                width: list.width
                theme: root.theme
                iconName: root.iconFor(String(modelData.kind))
                title: String(modelData.title || "")
                subtitle: String(modelData.subtitle || "")
                onClicked: root.openRequested(modelData)

                Keys.onPressed: function(event) {
                    var alt = (event.modifiers & Qt.AltModifier) !== 0
                    if (alt && event.key === Qt.Key_Up) {
                        HomeStore.movePin(String(modelData.kind), String(modelData.id), row.index - 1)
                        event.accepted = true
                    } else if (alt && event.key === Qt.Key_Down) {
                        HomeStore.movePin(String(modelData.kind), String(modelData.id), row.index + 1)
                        event.accepted = true
                    }
                }

                leading: [
                    Item {
                        width: root.editing ? 16 : 0
                        height: 24
                        visible: root.editing

                        AppIcon {
                            anchors.centerIn: parent
                            name: "grip"
                            tone: gripArea.pressed ? "normal" : "muted"
                            iconSize: 12
                        }

                        MouseArea {
                            id: gripArea

                            anchors.fill: parent
                            anchors.margins: -6
                            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                            preventStealing: true
                            onPositionChanged: function(mouse) {
                                if (!pressed) {
                                    return
                                }
                                var point = mapToItem(list.contentItem, mouse.x, mouse.y)
                                var target = list.indexAt(10, point.y)
                                if (target >= 0 && target !== row.index) {
                                    HomeStore.movePin(String(row.modelData.kind), String(row.modelData.id), target)
                                }
                            }
                        }
                    }
                ]

                actions: [
                    HomeChip {
                        theme: root.theme
                        round: true
                        implicitHeight: 28
                        iconName: "chevron-up"
                        iconSize: 10
                        visible: row.index > 0
                        toolTipText: "Move up (⌥↑)"
                        onClicked: HomeStore.movePin(String(row.modelData.kind), String(row.modelData.id), row.index - 1)
                    },
                    HomeChip {
                        theme: root.theme
                        round: true
                        implicitHeight: 28
                        iconName: "close"
                        iconSize: 11
                        toolTipText: "Unpin"
                        onClicked: HomeStore.unpin(String(row.modelData.kind), String(row.modelData.id))
                    }
                ]
            }
        }
    }
}

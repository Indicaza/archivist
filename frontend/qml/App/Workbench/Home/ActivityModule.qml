import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Archivist.Services 1.0
import "../../Icons"

// Activity: what finished while you were elsewhere. Each event opens what it
// is about; pinning pins that thing (not the event); dismissing drops only
// the event.
Item {
    id: root

    required property var theme
    required property var home
    property int limit: 6

    signal openRequested(var event)

    readonly property var events: HomeStore.activity.slice(0, limit)

    function dotFor(event) {
        if (String(event.kind) === "reply.failed") {
            return root.theme.danger
        }
        return event.unread ? root.theme.accentBright : root.theme.withAlpha(root.theme.mutedText, 0.45)
    }

    function pinTargetOf(event) {
        var target = event.target || ({})
        return {
            kind: String(target.kind || ""),
            id: String(target.id || ""),
            collectionId: String(target.collectionId || ""),
            libraryId: String(target.libraryId || ""),
            title: String(event.detail || event.title || ""),
            subtitle: root.home.projectNameFor(String(target.collectionId || ""))
        }
    }

    width: parent ? parent.width : 0
    implicitHeight: events.length === 0 ? empty.implicitHeight : panel.implicitHeight

    HomeKeyedModel {
        id: eventModel

        items: root.events
    }

    HomePanel {
        id: empty

        width: parent.width
        visible: root.events.length === 0
        theme: root.theme
        padding: 18

        Column {
            width: parent.width
            spacing: 6

            Text {
                width: parent.width
                text: "All quiet"
                color: root.theme.appText
                font.family: root.theme.titleFontFamily
                font.pixelSize: root.theme.px(16)
            }

            Text {
                width: parent.width
                text: "When a reply finishes while you're somewhere else, it shows up here so nothing slips by."
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
        visible: root.events.length > 0
        theme: root.theme

        ListView {
            id: list

            width: parent.width
            height: contentHeight
            interactive: false
            model: eventModel

            add: Transition {
                enabled: !root.theme.reducedMotion
                ParallelAnimation {
                    NumberAnimation { property: "opacity"; from: 0; to: 1; duration: root.theme.motionPanel }
                    NumberAnimation { property: "x"; from: -8; to: 0; duration: root.theme.motionPanel; easing.type: Easing.OutCubic }
                }
            }
            remove: Transition {
                enabled: !root.theme.reducedMotion
                NumberAnimation { property: "opacity"; to: 0; duration: root.theme.motionFast }
            }
            displaced: Transition {
                enabled: !root.theme.reducedMotion
                NumberAnimation { properties: "y"; duration: root.theme.motionPanel; easing.type: Easing.OutCubic }
            }

            delegate: HomeListRow {
                id: row

                required property string key
                required property int revision
                readonly property var event: { revision; return eventModel.itemFor(key) }
                readonly property var target: event.target || ({})
                readonly property bool pinned: { HomeStore.pins; return HomeStore.isPinned(String(target.kind || ""), String(target.id || "")) }

                width: list.width
                theme: root.theme
                showDot: true
                dotColor: root.dotFor(event)
                emphasized: event.unread === true
                title: String(event.title || "")
                subtitle: {
                    var parts = []
                    if (String(event.detail || "").length > 0) parts.push(String(event.detail))
                    var project = root.home.projectNameFor(String(target.collectionId || ""))
                    if (project.length > 0) parts.push(project)
                    return parts.join("  ·  ")
                }
                trailing: root.home.relativeTime(event.occurredAt)
                onClicked: root.openRequested(event)

                actions: [
                    HomeChip {
                        theme: root.theme
                        round: true
                        implicitHeight: 28
                        iconName: "pin"
                        iconSize: 12
                        iconTone: row.pinned ? "accent" : ""
                        visible: String(row.target.kind || "").length > 0
                        toolTipText: row.pinned ? "Unpin " + String(row.event.detail || "it") : "Pin " + String(row.event.detail || "it")
                        onClicked: HomeStore.togglePin(root.pinTargetOf(row.event))
                    },
                    HomeChip {
                        theme: root.theme
                        round: true
                        implicitHeight: 28
                        iconName: "close"
                        iconSize: 11
                        toolTipText: "Dismiss"
                        onClicked: HomeStore.dismissActivity(String(row.event.id))
                    }
                ]
            }
        }
    }
}

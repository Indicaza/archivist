import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "ChatMessage"
import "JumpToLatestButton"

Rectangle {
    id: root

    required property var theme
    property real leftInset: 0

    color: theme.surfaceBg
    clip: true

    function jumpToLatest() {
        transcript.positionViewAtEnd()
    }

    function appendUserMessage(message) {
        var trimmed = message.trim()

        if (trimmed.length === 0) {
            return
        }

        messages.append({
            roleValue: "user",
            contentValue: trimmed,
            timestampValue: "Now"
        })

        messages.append({
            roleValue: "assistant",
            contentValue: "The native Qt Workspace received that message. Backend wiring comes after this visual and structural slice is stable.",
            timestampValue: "Now"
        })

        Qt.callLater(jumpToLatest)
    }

    function seedMessages() {
        if (messages.count > 0) {
            return
        }

        messages.append({
            roleValue: "system",
            contentValue: "Qt Workbench prototype · local visual data only · Express and SQLite remain untouched.",
            timestampValue: "7:42 PM"
        })

        for (var index = 0; index < 56; index += 1) {
            var userTurn = index % 2 === 0
            var cycle = Math.floor(index / 2) + 1
            var longResponse = cycle % 6 === 0

            messages.append({
                roleValue: userTurn ? "user" : "assistant",
                contentValue: userTurn
                    ? "Synthetic native transcript turn " + cycle + ". This scroll exists to exercise variable-height delegates without creating a DOM node for the entire history."
                    : longResponse
                        ? "Archivist response " + cycle + ". Qt Quick ListView creates and reuses delegates around the visible viewport, giving this migration a native virtualization foundation.\n\nThis longer paragraph deliberately changes the delegate height. The important result is that scrolling remains smooth while the surrounding Workbench keeps its shadows, borders, dock, Explorer, and artifact surfaces."
                        : "Archivist response " + cycle + ". Qt Quick ListView creates and reuses delegates around the visible viewport, giving this migration a native virtualization foundation.",
                timestampValue: "Demo " + String(index + 1)
            })
        }

        Qt.callLater(jumpToLatest)
    }

    Component.onCompleted: seedMessages()

    Rectangle {
        id: workspaceHeader

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: root.theme.workspaceHeaderHeight
        color: theme.controlSurfaceBg

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: root.theme.quietBorder
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14 + root.leftInset
            anchors.rightMargin: 14
            spacing: 8

            Text {
                text: "Archivist"
                color: root.theme.mutedText
                font.pixelSize: 9
            }

            Text {
                text: "/"
                color: root.theme.mutedText
                font.pixelSize: 9
                opacity: 0.55
            }

            Text {
                Layout.fillWidth: true
                text: "Context Compiler Test 1"
                color: root.theme.appText
                font.pixelSize: 10
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            Text {
                text: "Archivist  ·  Grumpy"
                color: root.theme.mutedText
                font.pixelSize: 8
                opacity: 0.72
            }
        }
    }

    ListView {
        id: transcript

        readonly property bool nearEnd: contentHeight <= height
            || contentY >= contentHeight - height - 90

        anchors.top: workspaceHeader.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: root.leftInset
        clip: true
        spacing: root.theme.messageVerticalGap
        topMargin: 16
        bottomMargin: 22
        cacheBuffer: 1600
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds

        model: ListModel {
            id: messages
        }

        delegate: ChatMessage {
            required property string roleValue
            required property string contentValue
            required property string timestampValue

            width: transcript.width
            theme: root.theme
            role: roleValue
            content: contentValue
            timestamp: timestampValue
        }

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }
    }

    JumpToLatestButton {
        anchors.right: parent.right
        anchors.rightMargin: 18
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 18
        theme: root.theme
        visible: !transcript.nearEnd
        opacity: visible ? 1 : 0
        z: 20
        onClicked: root.jumpToLatest()

        Behavior on opacity {
            NumberAnimation { duration: 150 }
        }
    }
}

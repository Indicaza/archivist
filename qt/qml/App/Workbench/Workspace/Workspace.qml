import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Archivist.Services 1.0
import "ChatMessage"
import "JumpToLatestButton"

Rectangle {
    id: root

    required property var theme
    property real leftObstruction: 0

    readonly property string selectedChatTitle: ChatStore.selectedChat.title || "No Chat Selected"
    readonly property bool hasSelectedChat: ChatStore.selectedChatId.length > 0
    readonly property bool hasMessages: ChatStore.messages.length > 0

    color: theme.surfaceBg
    clip: true

    function jumpToLatest() {
        if (ChatStore.messages.length === 0) {
            return
        }

        transcript.forceLayout()
        transcript.positionViewAtIndex(ChatStore.messages.length - 1, ListView.End)

        Qt.callLater(function() {
            transcript.forceLayout()
            transcript.positionViewAtEnd()
            transcript.contentY = Math.max(
                transcript.originY,
                transcript.contentHeight - transcript.height + transcript.bottomMargin
            )
            transcript.returnToBounds()
        })
    }

    Component.onCompleted: ChatStore.refresh()

    Connections {
        target: ChatStore

        function onMessagesChanged() {
            Qt.callLater(root.jumpToLatest)
        }

        function onSelectedChatIdChanged() {
            Qt.callLater(root.jumpToLatest)
        }
    }

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
            anchors.leftMargin: Math.max(14, root.leftObstruction + 14)
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
                text: root.selectedChatTitle
                color: root.theme.appText
                font.pixelSize: 10
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            Text {
                text: ChatStore.responding
                    ? "Archivist is thinking"
                    : ChatStore.lastModel.length > 0
                        ? ChatStore.lastProvider + "  ·  " + ChatStore.lastModel
                        : root.hasSelectedChat
                            ? "Ready"
                            : "Select a Chat"
                color: ChatStore.responding
                    ? root.theme.appText
                    : root.theme.mutedText
                font.pixelSize: 8
                opacity: ChatStore.responding ? 0.9 : 0.72
                elide: Text.ElideRight
            }
        }
    }

    ListView {
        id: transcript

        readonly property real bottomContentY: Math.max(
            originY,
            contentHeight - height + bottomMargin
        )
        readonly property bool nearEnd: contentHeight <= height
            || contentY >= bottomContentY - 24

        anchors.top: workspaceHeader.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: root.hasSelectedChat && root.hasMessages
        clip: true
        spacing: root.theme.messageVerticalGap
        topMargin: 16
        bottomMargin: 22
        cacheBuffer: 1600
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds
        model: ChatStore.messages

        delegate: ChatMessage {
            required property var modelData

            width: transcript.width
            theme: root.theme
            role: String(modelData.role || "system")
            content: String(modelData.content || "")
            timestamp: String(modelData.displayTimestamp || "")
            status: String(modelData.status || "complete")
            leftObstruction: root.leftObstruction
        }

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }
    }

    Column {
        anchors.centerIn: parent
        width: Math.min(460, parent.width - 80)
        spacing: 8
        visible: !transcript.visible

        Text {
            width: parent.width
            text: ChatStore.loadingChats
                ? "Loading Chats…"
                : ChatStore.loadingMessages
                    ? "Loading conversation…"
                    : ChatStore.errorMessage.length > 0
                        ? "Chat could not be loaded"
                        : !root.hasSelectedChat
                            ? "Select a Chat"
                            : "This conversation is empty"
            color: root.theme.appText
            font.pixelSize: 16
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
        }

        Text {
            width: parent.width
            text: ChatStore.errorMessage.length > 0
                ? ChatStore.errorMessage
                : !root.hasSelectedChat
                    ? "Open Chats from the command dock to choose a conversation."
                    : "Send a message below to begin."
            color: root.theme.mutedText
            font.pixelSize: 11
            lineHeight: 1.45
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
        }
    }

    JumpToLatestButton {
        anchors.right: parent.right
        anchors.rightMargin: 18
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 18
        theme: root.theme
        visible: transcript.visible && !transcript.nearEnd
        opacity: visible ? 1 : 0
        z: 20
        onClicked: root.jumpToLatest()

        Behavior on opacity {
            NumberAnimation { duration: 150 }
        }
    }
}

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
    property bool historyLoadPending: false
    property int historyAnchorIndex: -1
    property real historyAnchorOffset: 0
    property real historyAnchorContentY: 0
    property real historyAnchorContentHeight: 0
    property int historyPrependedCount: 0
    property int historyRestorePass: 0
    property bool scrollToEndPending: false
    property int scrollToEndPass: 0

    readonly property real historyPrefetchDistance: Math.max(
        6000,
        transcript.height * 6
    )

    readonly property string selectedChatTitle: ChatStore.selectedChat.title || "No Chat Selected"
    readonly property bool hasSelectedChat: ChatStore.selectedChatId.length > 0
    readonly property bool hasMessages: ChatStore.messages.length > 0

    color: theme.surfaceBg
    clip: true

    function scheduleScrollToEnd() {
        if (ChatStore.messages.length === 0) {
            return
        }

        root.scrollToEndPending = true
        root.scrollToEndPass = 0
        scrollToEndTimer.restart()
    }

    function cancelScrollToEnd() {
        scrollToEndTimer.stop()
        root.scrollToEndPending = false
        root.scrollToEndPass = 0
    }

    function positionAtEnd() {
        if (!root.scrollToEndPending || transcript.count === 0) {
            return
        }

        transcript.forceLayout()
        transcript.positionViewAtEnd()

        root.scrollToEndPass += 1

        if (root.scrollToEndPass < 3) {
            scrollToEndTimer.restart()
            return
        }

        root.scrollToEndPending = false
        root.scrollToEndPass = 0
    }

    function jumpToLatest() {
        transcript.cancelFlick()
        root.scheduleScrollToEnd()
    }

    function canPrefetchHistory() {
        return transcript.visible
            && ChatStore.hasOlderMessages
            && !ChatStore.loadingMessages
            && !ChatStore.loadingOlderMessages
            && !root.historyLoadPending
            && transcript.contentY
                <= transcript.originY + root.historyPrefetchDistance
    }

    function scheduleHistoryPrefetch() {
        if (root.canPrefetchHistory()) {
            historyPrefetchTimer.restart()
        }
    }

    function requestOlderMessages() {
        if (!root.canPrefetchHistory()) {
            return
        }

        root.historyLoadPending = true
        ChatStore.loadOlderMessages()
    }

    function captureHistoryAnchor(count) {
        root.cancelScrollToEnd()
        transcript.cancelFlick()
        transcript.forceLayout()

        const sampleY = transcript.contentY + Math.min(
            48,
            Math.max(2, transcript.height * 0.08)
        )
        const visibleIndex = transcript.indexAt(
            Math.max(1, transcript.width / 2),
            sampleY
        )
        const anchorIndex = visibleIndex >= 0 ? visibleIndex : 0
        const anchorItem = transcript.itemAtIndex(anchorIndex)

        root.historyAnchorIndex = anchorIndex
        root.historyAnchorOffset = anchorItem
            ? anchorItem.y - transcript.contentY
            : 0
        root.historyAnchorContentY = transcript.contentY
        root.historyAnchorContentHeight = transcript.contentHeight
        root.historyPrependedCount = count
        root.historyRestorePass = 0
    }

    function restoreHistoryAnchor() {
        if (!root.historyLoadPending || root.historyPrependedCount <= 0) {
            return
        }

        const targetIndex = root.historyAnchorIndex + root.historyPrependedCount

        transcript.forceLayout()

        if (targetIndex >= 0 && targetIndex < ChatStore.messages.length) {
            transcript.positionViewAtIndex(targetIndex, ListView.Beginning)
            transcript.forceLayout()

            const anchorItem = transcript.itemAtIndex(targetIndex)
            if (anchorItem) {
                transcript.contentY = anchorItem.y - root.historyAnchorOffset
            } else {
                transcript.contentY = root.historyAnchorContentY
                    + transcript.contentHeight
                    - root.historyAnchorContentHeight
            }
        } else {
            transcript.contentY = root.historyAnchorContentY
                + transcript.contentHeight
                - root.historyAnchorContentHeight
        }

        transcript.returnToBounds()
        root.historyRestorePass += 1

        if (root.historyRestorePass < 4) {
            historyRestoreTimer.restart()
            return
        }

        root.clearHistoryAnchor()
        root.scheduleHistoryPrefetch()
    }

    function clearHistoryAnchor() {
        historyRestoreTimer.stop()
        root.historyLoadPending = false
        root.historyAnchorIndex = -1
        root.historyAnchorOffset = 0
        root.historyAnchorContentY = 0
        root.historyAnchorContentHeight = 0
        root.historyPrependedCount = 0
        root.historyRestorePass = 0
    }

    Component.onCompleted: ChatStore.refresh()

    Connections {
        target: ChatStore

        function onMessagesChanged() {
            if (root.historyLoadPending) {
                return
            }

            root.scheduleScrollToEnd()
        }

        function onSelectedChatIdChanged() {
            root.clearHistoryAnchor()
            root.cancelScrollToEnd()
        }

        function onLoadingMessagesChanged() {
            if (!ChatStore.loadingMessages && ChatStore.messages.length > 0) {
                root.scheduleScrollToEnd()
            }
        }

        function onOlderMessagesWillPrepend(count) {
            root.captureHistoryAnchor(count)
        }

        function onOlderMessagesPrepended(count) {
            root.historyPrependedCount = count
            historyRestoreTimer.restart()
        }

        function onLoadingOlderMessagesChanged() {
            if (
                !ChatStore.loadingOlderMessages
                && root.historyLoadPending
                && root.historyPrependedCount <= 0
            ) {
                Qt.callLater(function() {
                    if (
                        root.historyLoadPending
                        && root.historyPrependedCount <= 0
                    ) {
                        root.clearHistoryAnchor()
                    }
                })
            }
        }
    }

    Timer {
        id: historyPrefetchTimer

        interval: 55
        repeat: false
        onTriggered: root.requestOlderMessages()
    }

    Timer {
        id: historyRestoreTimer

        interval: 16
        repeat: false
        onTriggered: root.restoreHistoryAnchor()
    }

    Timer {
        id: scrollToEndTimer

        interval: 16
        repeat: false
        onTriggered: root.positionAtEnd()
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

        readonly property bool nearEnd: count === 0 || atYEnd
        readonly property bool nearBeginning: contentY
            <= originY + root.historyPrefetchDistance

        anchors.top: workspaceHeader.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: root.hasSelectedChat && root.hasMessages
        clip: true
        spacing: root.theme.messageVerticalGap
        topMargin: 22
        bottomMargin: 22
        cacheBuffer: Math.max(8000, height * 7)
        displayMarginBeginning: 1200
        displayMarginEnd: 800
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds
        model: ChatStore.messages

        onCountChanged: {
            if (root.scrollToEndPending) {
                scrollToEndTimer.restart()
            }
        }

        onContentHeightChanged: {
            if (root.scrollToEndPending) {
                scrollToEndTimer.restart()
            }
        }

        onContentYChanged: {
            if (nearBeginning) {
                root.scheduleHistoryPrefetch()
            }
        }

        onMovementStarted: {
            root.cancelScrollToEnd()
            root.scheduleHistoryPrefetch()
        }

        onMovementEnded: root.scheduleHistoryPrefetch()

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

    }

    Rectangle {
        anchors.top: workspaceHeader.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: 7
        width: historyStatusText.implicitWidth + 18
        height: 22
        visible: ChatStore.loadingOlderMessages
        color: root.theme.controlSurfaceBg
        radius: 4
        z: 12

        Text {
            id: historyStatusText

            anchors.centerIn: parent
            text: "Loading earlier messages…"
            color: root.theme.mutedText
            font.pixelSize: 8
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

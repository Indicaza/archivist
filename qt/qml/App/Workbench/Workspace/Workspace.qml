import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Archivist.Services 1.0
import "ChatMessage"
import "JumpToLatestButton"
import "FilePreview"
import "EditorTabs"

Rectangle {
    id: root

    required property var theme

    signal contextInspectionRequested(string messageId)
    property real leftObstruction: 0
    property real previewLeftObstruction: 0
    property bool historyLoadPending: false
    property int historyAnchorIndex: -1
    property real historyAnchorOffset: 0
    property real historyAnchorContentY: 0
    property real historyAnchorContentHeight: 0
    property int historyPrependedCount: 0
    property int historyRestorePass: 0
    property bool scrollToEndPending: false
    property int scrollToEndPass: 0
    property bool revealFollowEnabled: false
    property real revealFollowTargetY: 0

    readonly property real historyPrefetchDistance: Math.max(
        6000,
        transcript.height * 6
    )

    readonly property string selectedChatTitle: ChatStore.selectedChat.title || "No Chat Selected"
    readonly property bool hasSelectedChat: ChatStore.selectedChatId.length > 0
    readonly property bool hasMessages: ChatStore.messages.length > 0
    readonly property bool previewActive: LibraryStore.selectedFileId.length > 0
    readonly property string previewPath: LibraryStore.selectedFile.relativePath
        ? String(LibraryStore.selectedFile.relativePath)
        : "Library file"
    readonly property string selectedLibraryName: LibraryStore.selectedLibrary.name
        ? String(LibraryStore.selectedLibrary.name)
        : "Library"
    readonly property real previewViewportX: (
        root.previewActive || editorTabStrip.hasTabs
    ) ? root.previewLeftObstruction : 0

    gradient: Gradient {
        GradientStop {
            position: 0.0
            color: root.theme.workspaceBgTop
        }

        GradientStop {
            position: 1.0
            color: root.theme.workspaceBgBottom
        }
    }
    clip: true

    function scheduleScrollToEnd() {
        if (ChatStore.messages.length === 0) {
            return
        }

        revealFollowAnimation.stop()
        root.revealFollowEnabled = true
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
        root.revealFollowEnabled = true
        root.scheduleScrollToEnd()
    }

    function transcriptEndY() {
        return Math.max(
            transcript.originY - transcript.topMargin,
            transcript.originY
                + transcript.contentHeight
                - transcript.height
                + transcript.bottomMargin
        )
    }

    function stopRevealFollow() {
        revealFollowAnimation.stop()
        root.revealFollowEnabled = false
    }

    function followRevealSmoothly() {
        if (
            !root.revealFollowEnabled
            || transcript.count === 0
            || transcript.dragging
            || transcript.flicking
        ) {
            return
        }

        transcript.forceLayout()
        root.revealFollowTargetY = root.transcriptEndY()

        if (root.revealFollowTargetY <= transcript.contentY + 0.5) {
            return
        }

        if (!revealFollowAnimation.running) {
            revealFollowAnimation.start()
        }
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
            root.stopRevealFollow()
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

    SmoothedAnimation {
        id: revealFollowAnimation

        target: transcript
        property: "contentY"
        to: root.revealFollowTargetY
        duration: root.theme.chatRevealFollowDuration
        velocity: -1
        maximumEasingTime: root.theme.chatRevealFollowMaximumEasingTime
        reversingMode: SmoothedAnimation.Immediate
    }

    Rectangle {
        id: workspaceHeader

        anchors.top: parent.top
        x: root.previewViewportX
        width: Math.max(0, parent.width - root.previewViewportX)
        height: editorTabStrip.hasTabs ? 34 : root.theme.workspaceHeaderHeight
        color: theme.controlSurfaceBg
        z: 50

        Behavior on x {
            SpringAnimation {
                spring: root.theme.motionSpring
                damping: root.theme.motionDamping
                epsilon: 0.2
            }
        }

        Behavior on width {
            SpringAnimation {
                spring: root.theme.motionSpring
                damping: root.theme.motionDamping
                epsilon: 0.2
            }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: editorTabStrip.hasTabs ? 2.4 : 1
            color: editorTabStrip.hasTabs
                ? editorTabStrip.activeContourColor
                : root.theme.quietBorder
        }

        RowLayout {
            anchors.fill: parent
            visible: !editorTabStrip.hasTabs
            anchors.leftMargin: root.previewActive
                ? 14
                : Math.max(14, root.leftObstruction + 14)
            anchors.rightMargin: 14
            spacing: 8

            Text {
                text: "Archivist"
                color: root.theme.mutedText
                font.pixelSize: root.theme.typeSize(10)
            }

            Text {
                text: "/"
                color: root.theme.mutedText
                font.pixelSize: root.theme.typeSize(10)
                opacity: 0.55
            }

            Text {
                Layout.fillWidth: true
                text: root.previewActive
                    ? root.selectedLibraryName + "  /  " + root.previewPath
                    : root.selectedChatTitle
                color: root.theme.appText
                font.pixelSize: root.theme.typeSize(11)
                font.weight: Font.DemiBold
                elide: Text.ElideMiddle
            }

            Text {
                text: root.previewActive
                    ? LibraryStore.loadingFilePreview
                        ? "Opening file"
                        : LibraryStore.filePreviewError.length > 0
                            ? "Preview unavailable"
                            : "Read-only preview"
                    : ChatStore.responding
                        ? "Archivist is thinking"
                        : ChatStore.lastModel.length > 0
                            ? ChatStore.lastProvider + "  ·  " + ChatStore.lastModel
                            : root.hasSelectedChat
                                ? "Ready"
                                : "Select a Chat"
                color: root.previewActive && LibraryStore.filePreviewError.length > 0
                    ? root.theme.danger
                    : ChatStore.responding && !root.previewActive
                        ? root.theme.appText
                        : root.theme.mutedText
                font.pixelSize: root.theme.typeSize(9)
                opacity: ChatStore.responding && !root.previewActive ? 0.9 : 0.72
                elide: Text.ElideRight
            }

            Button {
                id: closePreviewButton

                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
                visible: root.previewActive
                text: "×"
                hoverEnabled: true
                padding: 0
                ToolTip.visible: hovered
                ToolTip.text: "Close file preview"
                onClicked: LibraryStore.clearFilePreview()
                scale: down
                    ? root.theme.pressedScale
                    : hovered
                        ? root.theme.hoverScale
                        : 1.0

                Behavior on scale {
                    enabled: !closePreviewButton.down

                    NumberAnimation {
                        duration: root.theme.motionHover
                        easing.type: Easing.OutCubic
                    }
                }

                contentItem: Text {
                    text: parent.text
                    color: parent.hovered
                        ? root.theme.appText
                        : root.theme.mutedText
                    font.pixelSize: root.theme.typeSize(15)
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                background: Rectangle {
                    color: parent.hovered ? root.theme.hoverBg : "transparent"
                    radius: 4
                }
            }
        }

        EditorTabStrip {
            id: editorTabStrip

            anchors.fill: parent
            theme: root.theme
        }
    }

    FilePreview {
        anchors.top: workspaceHeader.bottom
        anchors.bottom: parent.bottom
        x: root.previewLeftObstruction
        width: Math.max(0, parent.width - root.previewLeftObstruction)
        visible: root.previewActive
        theme: root.theme
        file: LibraryStore.selectedFile
        preview: LibraryStore.filePreview
        loading: LibraryStore.loadingFilePreview
        errorMessage: LibraryStore.filePreviewError
        leftObstruction: 0

        Behavior on x {
            SpringAnimation {
                spring: root.theme.motionSpring
                damping: root.theme.motionDamping
                epsilon: 0.2
            }
        }

        Behavior on width {
            SpringAnimation {
                spring: root.theme.motionSpring
                damping: root.theme.motionDamping
                epsilon: 0.2
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
        visible: !root.previewActive && root.hasSelectedChat && root.hasMessages
        clip: true
        spacing: root.theme.messageVerticalGap
        topMargin: 42
        bottomMargin: 44
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
            if (!revealFollowAnimation.running) {
                root.cancelScrollToEnd()
                root.stopRevealFollow()
            }
            root.scheduleHistoryPrefetch()
        }

        onDraggingChanged: {
            if (dragging) {
                root.cancelScrollToEnd()
                root.stopRevealFollow()
            }
        }

        onFlickingChanged: {
            if (flicking) {
                root.cancelScrollToEnd()
                root.stopRevealFollow()
            }
        }

        onMovementEnded: root.scheduleHistoryPrefetch()

        delegate: ChatMessage {
            required property var modelData
            required property int index

            width: transcript.width
            theme: root.theme
            messageId: String(modelData.id || "")
            role: String(modelData.role || "system")
            content: String(modelData.content || "")
            timestamp: String(modelData.displayTimestamp || "")
            status: String(modelData.status || "complete")
            animateReveal: Boolean(modelData.animateReveal || false)
            leftObstruction: root.leftObstruction
            onContextInspectionRequested: function(messageId) {
                root.contextInspectionRequested(messageId)
            }
            onRevealProgressed: {
                if (index !== transcript.count - 1) {
                    return
                }

                root.followRevealSmoothly()
            }
            onRevealFinished: function(messageId) {
                ChatStore.finishMessageReveal(messageId)
            }
        }

    }

    Rectangle {
        anchors.top: workspaceHeader.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: 7
        width: historyStatusText.implicitWidth + 18
        height: 22
        visible: !root.previewActive && ChatStore.loadingOlderMessages
        color: root.theme.controlSurfaceBg
        radius: 4
        z: 12

        Text {
            id: historyStatusText

            anchors.centerIn: parent
            text: "Loading earlier messages…"
            color: root.theme.mutedText
            font.pixelSize: root.theme.typeSize(9)
        }
    }

    Column {
        anchors.centerIn: parent
        width: Math.min(460, parent.width - 80)
        spacing: 8
        visible: !root.previewActive && !transcript.visible

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
            font.pixelSize: root.theme.typeSize(16)
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
            font.pixelSize: root.theme.typeSize(12)
            lineHeight: root.theme.typeLineHeightBody
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

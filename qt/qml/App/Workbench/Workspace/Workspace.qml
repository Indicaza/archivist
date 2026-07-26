import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Archivist.Services 1.0
import "../../Files/FileIdentity.js" as FileIdentity
import "../../Files/Renderers"
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
    property string trackedChatViewportKey: ""
    property var pendingChatViewportState: ({})
    property bool chatViewportRestorePending: false
    property bool restoringChatViewport: false
    property int chatViewportRestorePass: 0

    readonly property real historyPrefetchDistance: Math.max(
        6000,
        transcript.height * 6
    )

    readonly property string selectedChatTitle: ChatStore.selectedChat.title || "No Chat Selected"
    readonly property bool hasSelectedChat: ChatStore.selectedChatId.length > 0
    readonly property bool hasMessages: ChatStore.messages.length > 0
    readonly property bool previewActive: LibraryStore.selectedFileId.length > 0
    readonly property bool fileTransitionActive: (
        editorTabStrip.pendingFileId.length > 0
        || (
            editorTabStrip.activeTabKind === "file"
            && !root.previewActive
        )
    )
    readonly property bool fileSurfaceActive: root.previewActive
        || root.fileTransitionActive
    readonly property string previewPath: LibraryStore.selectedFile.relativePath
        ? String(LibraryStore.selectedFile.relativePath)
        : "Library file"
    readonly property string selectedLibraryName: LibraryStore.selectedLibrary.name
        ? String(LibraryStore.selectedLibrary.name)
        : "Library"
    readonly property var previewFileIdentity: FileIdentity.resolve({
        fileName: LibraryStore.selectedFile.name
            || LibraryStore.selectedFile.relativePath
            || "",
        extension: LibraryStore.selectedFile.extension || ""
    })
    readonly property bool imagePreviewActive: root.previewActive
        && root.previewFileIdentity.preferredRendererId === "image"
    readonly property bool filePreviewStatusVisible: root.previewActive
    readonly property string filePreviewAccessLabel:
        filePreview.statusAccessLabel
    readonly property string filePreviewTypeLabel:
        filePreview.statusTypeLabel
    readonly property string filePreviewRendererLabel:
        filePreview.statusRendererLabel
    readonly property string filePreviewPath:
        filePreview.statusPath
    readonly property string filePreviewMetrics:
        filePreview.statusMetrics
    readonly property bool filePreviewAttached:
        filePreview.attachedToChat
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

    function chatViewportStateKey(chatId) {
        var collectionId = String(CollectionStore.selectedCollectionId || "")
        var targetChatId = String(chatId || "")

        if (collectionId.length === 0 || targetChatId.length === 0) {
            return ""
        }

        return "workspace/collections/"
            + collectionId
            + "/viewports/chats/"
            + targetChatId
    }

    function messageIndexForId(messageId) {
        var targetId = String(messageId || "")
        var messages = ChatStore.messages || []

        for (var index = 0; index < messages.length; index += 1) {
            if (String(messages[index].id || "") === targetId) {
                return index
            }
        }

        return -1
    }

    function transcriptAnchor() {
        if (transcript.count === 0) {
            return {
                messageId: "",
                offset: 0
            }
        }

        transcript.forceLayout()

        var sampleY = transcript.contentY + Math.min(
            48,
            Math.max(4, transcript.height * 0.08)
        )
        var index = transcript.indexAt(
            Math.max(1, transcript.width / 2),
            sampleY
        )

        if (index < 0) {
            index = Math.max(0, transcript.indexAt(
                Math.max(1, transcript.width / 2),
                transcript.contentY + transcript.height / 2
            ))
        }

        var item = transcript.itemAtIndex(index)
        var message = index >= 0 && index < ChatStore.messages.length
            ? ChatStore.messages[index]
            : ({})

        return {
            messageId: String(message.id || ""),
            offset: item ? item.y - transcript.contentY : 0
        }
    }

    function scheduleChatViewportSave() {
        if (
            root.restoringChatViewport
            || root.trackedChatViewportKey.length === 0
            || !root.hasSelectedChat
        ) {
            return
        }

        chatViewportSaveTimer.restart()
    }

    function saveChatViewportState(stateKey) {
        var key = String(stateKey || root.trackedChatViewportKey || "")

        if (
            key.length === 0
            || root.restoringChatViewport
            || transcript.count === 0
        ) {
            return
        }

        var anchor = root.transcriptAnchor()
        var endY = root.transcriptEndY()

        WorkspaceState.setValue(
            key,
            JSON.stringify({
                version: 1,
                atEnd: transcript.nearEnd,
                contentY: transcript.contentY,
                distanceFromEnd: Math.max(0, endY - transcript.contentY),
                anchorMessageId: anchor.messageId,
                anchorOffset: anchor.offset
            })
        )
    }

    function switchChatViewportState() {
        chatViewportSaveTimer.stop()

        if (root.trackedChatViewportKey.length > 0) {
            root.saveChatViewportState(root.trackedChatViewportKey)
        }

        root.trackedChatViewportKey = root.chatViewportStateKey(
            ChatStore.selectedChatId
        )
        root.pendingChatViewportState = {
            version: 1,
            atEnd: true,
            contentY: 0,
            distanceFromEnd: 0,
            anchorMessageId: "",
            anchorOffset: 0
        }

        if (root.trackedChatViewportKey.length > 0) {
            var raw = String(
                WorkspaceState.value(
                    root.trackedChatViewportKey,
                    ""
                ) || ""
            )

            if (raw.length > 0) {
                try {
                    var parsed = JSON.parse(raw)
                    if (parsed && typeof parsed === "object") {
                        root.pendingChatViewportState = parsed
                    }
                } catch (error) {
                    root.pendingChatViewportState = {
                        version: 1,
                        atEnd: true,
                        contentY: 0,
                        distanceFromEnd: 0,
                        anchorMessageId: "",
                        anchorOffset: 0
                    }
                }
            }
        }

        root.chatViewportRestorePending =
            root.trackedChatViewportKey.length > 0
        root.restoringChatViewport = root.chatViewportRestorePending
        root.chatViewportRestorePass = 0

        if (root.chatViewportRestorePending) {
            chatViewportRestoreTimer.restart()
        }
    }

    function restoreChatViewport() {
        if (!root.chatViewportRestorePending) {
            return
        }

        if (ChatStore.loadingMessages) {
            chatViewportRestoreTimer.restart()
            return
        }

        if (transcript.count === 0) {
            root.chatViewportRestorePending = false
            root.restoringChatViewport = false
            return
        }

        var state = root.pendingChatViewportState || ({})
        var shouldFollowEnd = state.atEnd === undefined
            ? true
            : Boolean(state.atEnd)

        if (shouldFollowEnd) {
            root.chatViewportRestorePending = false
            root.restoringChatViewport = false
            root.scheduleScrollToEnd()
            return
        }

        root.cancelScrollToEnd()
        root.stopRevealFollow()
        transcript.cancelFlick()
        transcript.forceLayout()

        var anchorIndex = root.messageIndexForId(state.anchorMessageId)

        if (anchorIndex >= 0) {
            transcript.positionViewAtIndex(anchorIndex, ListView.Beginning)
            transcript.forceLayout()

            var anchorItem = transcript.itemAtIndex(anchorIndex)
            if (anchorItem) {
                transcript.contentY = anchorItem.y
                    - Number(state.anchorOffset || 0)
            }
        } else {
            var distanceFromEnd = Math.max(
                0,
                Number(state.distanceFromEnd || 0)
            )
            transcript.contentY = root.transcriptEndY() - distanceFromEnd
        }

        transcript.returnToBounds()
        root.chatViewportRestorePass += 1

        if (root.chatViewportRestorePass < 4) {
            chatViewportRestoreTimer.restart()
        } else {
            root.chatViewportRestorePending = false
            root.restoringChatViewport = false
        }
    }

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

    Component.onCompleted: {
        ChatStore.refresh()
        root.switchChatViewportState()
    }

    Component.onDestruction: {
        chatViewportSaveTimer.stop()
        root.saveChatViewportState(root.trackedChatViewportKey)
        WorkspaceState.sync()
    }

    Connections {
        target: ChatStore

        function onMessagesChanged() {
            if (root.historyLoadPending) {
                return
            }

            if (root.chatViewportRestorePending) {
                chatViewportRestoreTimer.restart()
                return
            }

            root.scheduleScrollToEnd()
        }

        function onSelectedChatIdChanged() {
            root.clearHistoryAnchor()
            root.cancelScrollToEnd()
            root.stopRevealFollow()
            root.switchChatViewportState()
        }

        function onLoadingMessagesChanged() {
            if (!ChatStore.loadingMessages && ChatStore.messages.length > 0) {
                if (root.chatViewportRestorePending) {
                    chatViewportRestoreTimer.restart()
                } else {
                    root.scheduleScrollToEnd()
                }
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

    Connections {
        target: CollectionStore

        function onSelectedCollectionIdChanged() {
            root.switchChatViewportState()
        }
    }

    Timer {
        id: chatViewportSaveTimer

        interval: 180
        repeat: false
        onTriggered: root.saveChatViewportState()
    }

    Timer {
        id: chatViewportRestoreTimer

        interval: 24
        repeat: false
        onTriggered: root.restoreChatViewport()
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
        height: editorTabStrip.hasTabs ? 33 : root.theme.workspaceHeaderHeight
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
            height: editorTabStrip.hasTabs ? 0.8 : 1
            color: editorTabStrip.hasTabs
                ? editorTabStrip.activeContourColor
                : root.theme.quietBorder
            opacity: editorTabStrip.hasTabs ? 0.72 : 1
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
                    ? root.imagePreviewActive
                        ? "Image preview"
                        : LibraryStore.loadingFilePreview
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
                color: root.previewActive
                    && !root.imagePreviewActive
                    && LibraryStore.filePreviewError.length > 0
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
        id: filePreview

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

    Rectangle {
        anchors.top: workspaceHeader.bottom
        anchors.bottom: parent.bottom
        x: root.previewLeftObstruction
        width: Math.max(0, parent.width - root.previewLeftObstruction)
        visible: root.fileTransitionActive && !root.previewActive
        color: root.theme.workspaceBgDeep
        z: 30
        clip: true

        DocumentLoadingState {
            anchors.centerIn: parent
            width: Math.min(460, parent.width - 56)
            theme: root.theme
            title: "Restoring file workspace"
            detail: "Opening the renderer and restoring your zoom and reading position."
            fileLabel: editorTabStrip.activeTabPath.length > 0
                ? editorTabStrip.activeTabPath
                : editorTabStrip.activeTabTitle
        }

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
        visible: !root.fileSurfaceActive
            && root.hasSelectedChat
            && root.hasMessages
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

            root.scheduleChatViewportSave()
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

        onMovementEnded: {
            root.scheduleHistoryPrefetch()
            root.scheduleChatViewportSave()
        }

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
        visible: !root.fileSurfaceActive && ChatStore.loadingOlderMessages
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
        visible: !root.fileSurfaceActive && !transcript.visible

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

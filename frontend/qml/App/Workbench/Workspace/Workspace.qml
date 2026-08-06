import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Archivist.Services 1.0
import "../../Files/FileIdentity.js" as FileIdentity
import "../../Files/Renderers"
import "ChatMessage"
import "JumpToLatestButton"
import "FilePreview"
import "CodeEditor"
import "EditorTabs"
import "ChatViewportPolicy.js" as ChatViewportPolicy

Rectangle {
    id: root

    required property var theme

    signal contextInspectionRequested(string messageId)
    signal revealInLibraryRequested(
        string libraryId,
        string fileId,
        string relativePath
    )
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
    property bool jumpToEndPending: false
    property real scrollToEndTargetY: 0
    property int scrollToEndDuration: 140
    property int scrollToEndSettlePass: 0
    property bool revealFollowEnabled: false
    property bool programmaticScrollWrite: false
    property bool historyRestoreDeferred: false
    property string trackedChatViewportKey: ""
    property var pendingChatViewportState: ({})
    property bool chatViewportRestorePending: false
    property bool restoringChatViewport: false
    property int chatViewportRestorePass: 0
    property string editorSafetyMode: ""
    property string editorSafetyTabKey: ""
    property string editorSafetyDocumentId: ""
    property string editorSafetyTitle: ""
    property string pendingCloseTabKey: ""
    property string pendingCloseDocumentId: ""
    property string pendingEditorTargetFileId: ""
    property string pendingEditorTargetPath: ""
    property int pendingEditorTargetLine: 1
    property int pendingEditorTargetColumn: 1

    function normalizedEditorPath(value) {
        return String(value || "")
            .replace(/\\/g, "/")
            .replace(/\/+$/, "")
    }

    function openEditorLocation(
        filePath,
        lineNumber,
        columnNumber
    ) {
        var libraryId = String(
            LibraryStore.activeFileLibraryId || ""
        )
        var libraryRoot = root.normalizedEditorPath(
            LibraryStore.activeFileLibrary.rootPath || ""
        )
        var targetPath = root.normalizedEditorPath(filePath)
        var prefix = libraryRoot.length > 0
            ? libraryRoot + "/"
            : ""

        if (
            libraryId.length === 0
            || prefix.length === 0
            || targetPath.indexOf(prefix) !== 0
        ) {
            return
        }

        var relativePath = targetPath.substring(prefix.length)
        pendingEditorTargetFileId = ""
        pendingEditorTargetPath = relativePath
        pendingEditorTargetLine = Math.max(
            1, Number(lineNumber || 1)
        )
        pendingEditorTargetColumn = Math.max(
            1, Number(columnNumber || 1)
        )

        if (
            root.normalizedEditorPath(
                LibraryStore.selectedFile.relativePath || ""
            ) === relativePath
            && String(LibraryStore.activeFileLibraryId || "")
                === libraryId
        ) {
            pendingEditorTargetFileId = String(
                LibraryStore.selectedFileId || ""
            )
            Qt.callLater(function() {
                root.revealPendingEditorLocation(
                    codeEditor.stableDocumentId
                )
            })
            return
        }

        if (
            String(LibraryStore.selectedLibraryId || "")
                === libraryId
        ) {
            var files = LibraryStore.files || []

            for (var index = 0; index < files.length; index += 1) {
                var file = files[index] || ({})

                if (
                    root.normalizedEditorPath(file.relativePath)
                        !== relativePath
                ) {
                    continue
                }

                pendingEditorTargetFileId = String(file.id || "")
                LibraryStore.previewFileFromLibrary(
                    libraryId,
                    pendingEditorTargetFileId,
                    file
                )
                return
            }
        }

        LibraryStore.previewFilePathFromLibrary(
            libraryId,
            relativePath
        )
    }

    function revealPendingEditorLocation(documentId) {
        var selectedPath = root.normalizedEditorPath(
            LibraryStore.selectedFile.relativePath || ""
        )
        var pathMatches = pendingEditorTargetPath.length > 0
            && selectedPath === pendingEditorTargetPath
        var fileMatches = pendingEditorTargetFileId.length > 0
            && String(LibraryStore.selectedFileId || "")
                === pendingEditorTargetFileId

        if (!pathMatches && !fileMatches) {
            return
        }

        var lineNumber = pendingEditorTargetLine
        var columnNumber = pendingEditorTargetColumn
        pendingEditorTargetFileId = ""
        pendingEditorTargetPath = ""
        pendingEditorTargetLine = 1
        pendingEditorTargetColumn = 1

        Qt.callLater(function() {
            codeEditor.revealLocation(
                documentId,
                lineNumber,
                columnNumber
            )
        })
    }

    function openDirtyCloseDialog(
        tabKey,
        documentId,
        title
    ) {
        editorSafetyMode = "dirtyClose"
        editorSafetyTabKey = String(tabKey || "")
        editorSafetyDocumentId = String(
            documentId || ""
        )
        editorSafetyTitle = String(title || "Untitled")
        editorSafetyDialog.open()
    }

    function resetEditorSafetyTarget() {
        editorSafetyMode = ""
        editorSafetyTabKey = ""
        editorSafetyDocumentId = ""
        editorSafetyTitle = ""
    }

    function cancelEditorSafety() {
        pendingCloseTabKey = ""
        pendingCloseDocumentId = ""
        editorSafetyDialog.close()
        resetEditorSafetyTarget()
    }

    function saveAndCloseTarget() {
        pendingCloseTabKey = editorSafetyTabKey
        pendingCloseDocumentId =
            editorSafetyDocumentId
        editorSafetyDialog.close()
        codeEditor.saveDocument(
            pendingCloseDocumentId
        )
        resetEditorSafetyTarget()
    }

    function discardAndCloseTarget() {
        var tabKey = editorSafetyTabKey
        var documentId = editorSafetyDocumentId

        editorSafetyDialog.close()
        resetEditorSafetyTarget()
        codeEditor.discardDocument(documentId)
        editorTabStrip.closeTabByKey(tabKey)
    }

    readonly property real historyPrefetchDistance: Math.max(
        48,
        transcript.height * 0.06
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
    readonly property string selectedLibraryName: LibraryStore.activeFileLibrary.name
        ? String(LibraryStore.activeFileLibrary.name)
        : "Library"
    readonly property var previewFileIdentity: FileIdentity.resolve({
        fileName: LibraryStore.selectedFile.name
            || LibraryStore.selectedFile.relativePath
            || "",
        extension: LibraryStore.selectedFile.extension || ""
    })
    readonly property bool codeEditorActive: root.previewActive
        && (
            root.previewFileIdentity.category === "code"
            || root.previewFileIdentity.category === "data"
        )
    readonly property bool markdownFileActive:
        root.previewActive
        && root.previewFileIdentity.preferredRendererId
            === "markdown"
    readonly property bool markdownEditMode:
        root.markdownFileActive
        && filePreview.viewMode === "source"
    readonly property bool editorSurfaceActive:
        root.codeEditorActive
        || root.markdownEditMode
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
            || root.programmaticScrollWrite
            || scrollToEndAnimation.running
            || ChatStore.responding
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

    function clampedTranscriptContentY(value) {
        return ChatViewportPolicy.clampContentY(
            transcript.originY,
            transcript.contentHeight,
            transcript.height,
            transcript.topMargin,
            transcript.bottomMargin,
            value
        )
    }

    function placeTranscriptAnchor(index, offset) {
        if (index < 0 || index >= ChatStore.messages.length) {
            return false
        }

        transcript.forceLayout()

        var anchorItem = transcript.itemAtIndex(index)

        if (!anchorItem) {
            root.beginProgrammaticScrollWrite()
            transcript.positionViewAtIndex(index, ListView.Beginning)
            transcript.forceLayout()
            anchorItem = transcript.itemAtIndex(index)
        }

        if (!anchorItem) {
            return false
        }

        root.beginProgrammaticScrollWrite()
        transcript.contentY = root.clampedTranscriptContentY(
            anchorItem.y - Number(offset || 0)
        )
        return true
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
            root.finishChatViewportRestore()
            return
        }

        var state = root.pendingChatViewportState || ({})
        var shouldFollowEnd = state.atEnd === undefined
            ? true
            : Boolean(state.atEnd)

        if (shouldFollowEnd) {
            root.finishChatViewportRestore()
            root.scheduleScrollToEnd(true, false)
            return
        }

        root.cancelScrollToEnd()
        root.stopRevealFollow()
        transcript.cancelFlick()

        var anchorIndex = root.messageIndexForId(state.anchorMessageId)
        var anchorPlaced = root.placeTranscriptAnchor(
            anchorIndex,
            state.anchorOffset
        )

        if (!anchorPlaced) {
            var distanceFromEnd = Math.max(
                0,
                Number(state.distanceFromEnd || 0)
            )
            transcript.forceLayout()
            root.beginProgrammaticScrollWrite()
            transcript.contentY = root.clampedTranscriptContentY(
                root.transcriptEndY() - distanceFromEnd
            )
        }

        root.finishChatViewportRestore()
    }

    function finishChatViewportRestore() {
        chatViewportRestoreTimer.stop()
        root.chatViewportRestorePending = false
        root.restoringChatViewport = false
        root.chatViewportRestorePass = 0
    }

    function cancelChatViewportRestore() {
        if (
            !root.chatViewportRestorePending
            && !root.restoringChatViewport
        ) {
            return
        }

        root.finishChatViewportRestore()
    }

    function beginProgrammaticScrollWrite(holdOpen) {
        root.programmaticScrollWrite = true

        if (holdOpen === true) {
            programmaticScrollReleaseTimer.stop()
            return
        }

        programmaticScrollReleaseTimer.restart()
    }

    function takeManualScrollOwnership() {
        historyPrefetchTimer.stop()
        root.cancelScrollToEnd()
        root.stopRevealFollow()
        root.cancelChatViewportRestore()

        if (
            root.historyLoadPending
            && root.historyPrependedCount > 0
        ) {
            historyRestoreTimer.stop()
            root.historyRestoreDeferred = true
        }
    }

    function updateScrollToEndTarget() {
        transcript.forceLayout()
        root.scrollToEndTargetY = root.clampedTranscriptContentY(
            root.transcriptEndY()
        )
    }

    function startScrollToEndAnimation() {
        if (!root.scrollToEndPending || transcript.count === 0) {
            return
        }

        if (
            !root.jumpToEndPending
            && !root.shouldFollowTranscript()
        ) {
            root.cancelScrollToEnd()
            return
        }

        root.updateScrollToEndTarget()
        var distance = Math.abs(
            root.scrollToEndTargetY - transcript.contentY
        )

        if (distance <= 0.5) {
            root.finishScrollToEnd()
            return
        }

        root.scrollToEndDuration = ChatViewportPolicy.scrollDuration(
            distance,
            root.jumpToEndPending
        )
        root.beginProgrammaticScrollWrite(true)

        if (!scrollToEndAnimation.running) {
            scrollToEndAnimation.start()
        }
    }

    function scheduleScrollToEnd(forceFollow, explicitJump) {
        if (forceFollow === true) {
            root.revealFollowEnabled = true
        }

        if (explicitJump === true) {
            root.jumpToEndPending = true
            root.scrollToEndSettlePass = 0
        }

        if (
            ChatStore.messages.length === 0
            || (
                !root.jumpToEndPending
                && (
                    root.restoringChatViewport
                    || root.historyLoadPending
                    || !root.revealFollowEnabled
                )
            )
        ) {
            return
        }

        historyPrefetchTimer.stop()
        root.scrollToEndPending = true
        root.startScrollToEndAnimation()
    }

    function cancelScrollToEnd() {
        root.scrollToEndPending = false
        root.jumpToEndPending = false
        root.scrollToEndSettlePass = 0
        scrollToEndAnimation.stop()
        programmaticScrollReleaseTimer.restart()
    }

    function finishScrollToEnd() {
        if (!root.scrollToEndPending) {
            programmaticScrollReleaseTimer.restart()
            return
        }

        transcript.forceLayout()
        root.updateScrollToEndTarget()
        var remaining = Math.abs(
            root.scrollToEndTargetY - transcript.contentY
        )

        if (remaining > 0.75 && root.scrollToEndSettlePass < 3) {
            root.scrollToEndSettlePass += 1
            root.scrollToEndDuration = ChatViewportPolicy.scrollDuration(
                remaining,
                root.jumpToEndPending
            )
            root.beginProgrammaticScrollWrite(true)
            scrollToEndAnimation.start()
            return
        }

        root.beginProgrammaticScrollWrite(false)
        transcript.contentY = root.scrollToEndTargetY
        root.scrollToEndPending = false
        root.jumpToEndPending = false
        root.scrollToEndSettlePass = 0
    }

    function jumpToLatest() {
        historyPrefetchTimer.stop()
        historyRestoreTimer.stop()
        root.historyRestoreDeferred = false
        root.clearHistoryAnchor(ChatStore.loadingOlderMessages)
        root.cancelChatViewportRestore()
        transcript.cancelFlick()
        root.revealFollowEnabled = true
        root.scheduleScrollToEnd(true, true)
    }

    function transcriptEndY() {
        return ChatViewportPolicy.transcriptEndY(
            transcript.originY,
            transcript.contentHeight,
            transcript.height,
            transcript.topMargin,
            transcript.bottomMargin
        )
    }

    function shouldFollowTranscript() {
        return ChatViewportPolicy.shouldFollow({
            autoFollow: root.revealFollowEnabled,
            dragging: transcript.dragging,
            flicking: transcript.flicking,
            restoringViewport: root.restoringChatViewport,
            restoringHistory: root.historyLoadPending
        })
    }

    function stopRevealFollow() {
        root.revealFollowEnabled = false
    }

    function followRevealSmoothly() {
        if (!root.shouldFollowTranscript() || transcript.count === 0) {
            return
        }

        root.scheduleScrollToEnd(false, false)
    }

    function canPrefetchHistory() {
        return ChatViewportPolicy.shouldPrefetchHistory({
            visible: transcript.visible,
            hasOlderMessages: ChatStore.hasOlderMessages,
            nearBeginning: transcript.nearBeginning,
            loadingMessages: ChatStore.loadingMessages,
            loadingOlderMessages: ChatStore.loadingOlderMessages,
            historyLoadPending: root.historyLoadPending,
            restoringViewport: root.restoringChatViewport,
            responding: ChatStore.responding,
            autoFollow: root.revealFollowEnabled,
            scrollToEndPending: root.scrollToEndPending,
            interacting: transcript.moving
                || transcript.dragging
                || transcript.flicking
        })
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
        if (root.jumpToEndPending) {
            return
        }

        root.cancelScrollToEnd()
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

        if (transcript.moving || transcript.dragging || transcript.flicking) {
            root.historyRestoreDeferred = true
            return
        }

        const targetIndex = root.historyAnchorIndex + root.historyPrependedCount
        const anchorPlaced = root.placeTranscriptAnchor(
            targetIndex,
            root.historyAnchorOffset
        )

        if (anchorPlaced) {
            root.clearHistoryAnchor()
            root.scheduleHistoryPrefetch()
            return
        }

        root.historyRestorePass += 1

        if (root.historyRestorePass < 4) {
            historyRestoreTimer.restart()
            return
        }

        transcript.forceLayout()
        root.beginProgrammaticScrollWrite()
        transcript.contentY = root.clampedTranscriptContentY(
            root.historyAnchorContentY
                + transcript.contentHeight
                - root.historyAnchorContentHeight
        )
        root.clearHistoryAnchor()
        root.scheduleHistoryPrefetch()
    }

    function clearHistoryAnchor(keepLoadPending) {
        historyRestoreTimer.stop()
        root.historyLoadPending = keepLoadPending === true
        root.historyAnchorIndex = -1
        root.historyAnchorOffset = 0
        root.historyAnchorContentY = 0
        root.historyAnchorContentHeight = 0
        root.historyPrependedCount = 0
        root.historyRestorePass = 0
        root.historyRestoreDeferred = false
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

            if (!root.revealFollowEnabled) {
                return
            }

            if (ChatStore.responding) {
                if (root.shouldFollowTranscript()) {
                    root.scheduleScrollToEnd(false, false)
                }
                return
            }

            root.scheduleScrollToEnd(false, false)
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
                    root.scheduleScrollToEnd(true, false)
                }
            }
        }

        function onOlderMessagesWillPrepend(count) {
            if (root.jumpToEndPending) {
                root.clearHistoryAnchor(true)
                return
            }

            root.captureHistoryAnchor(count)
        }

        function onOlderMessagesPrepended(count) {
            if (root.jumpToEndPending) {
                root.clearHistoryAnchor(ChatStore.loadingOlderMessages)
                root.startScrollToEndAnimation()
                return
            }

            root.historyPrependedCount = count

            if (transcript.moving) {
                root.historyRestoreDeferred = true
                return
            }

            historyRestoreTimer.restart()
        }

        function onLoadingOlderMessagesChanged() {
            if (ChatStore.loadingOlderMessages) {
                return
            }

            if (root.jumpToEndPending) {
                root.clearHistoryAnchor(false)
                root.startScrollToEndAnimation()
                return
            }

            if (
                root.historyLoadPending
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

        interval: 280
        repeat: false
        onTriggered: root.requestOlderMessages()
    }

    Timer {
        id: historyRestoreTimer

        interval: 16
        repeat: false
        onTriggered: root.restoreHistoryAnchor()
    }

    SmoothedAnimation {
        id: scrollToEndAnimation

        target: transcript
        property: "contentY"
        to: root.scrollToEndTargetY
        duration: root.scrollToEndDuration
        velocity: -1
        maximumEasingTime: Math.min(220, root.scrollToEndDuration)
        reversingMode: SmoothedAnimation.Immediate
        onStarted: root.beginProgrammaticScrollWrite(true)
        onStopped: root.finishScrollToEnd()
    }

    Timer {
        id: programmaticScrollReleaseTimer

        interval: 32
        repeat: false
        onTriggered: root.programmaticScrollWrite = false
    }

    Rectangle {
        id: workspaceHeader

        anchors.top: parent.top
        x: root.previewViewportX
        width: Math.max(0, parent.width - root.previewViewportX)
        height: editorTabStrip.hasTabs
            ? root.theme.editorTabHeight
            : root.theme.workspaceHeaderHeight
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
            height: 1
            visible: !editorTabStrip.hasTabs
            color: root.theme.quietBorder
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
                    ? root.editorSurfaceActive
                        ? LibraryStore.savingFile
                            ? "Saving…"
                            : LibraryStore.fileSaveError.length > 0
                                ? "Save failed"
                                : LibraryStore.filePreviewError.length > 0
                                    ? "Refresh failed"
                                    : LibraryStore.loadingFilePreview
                                        ? codeEditor.initialDocumentLoaded
                                            ? "Refreshing…"
                                            : "Opening code editor"
                                        : codeEditor.dirty
                                            ? "Unsaved changes"
                                            : root.markdownEditMode
                                                ? "Markdown editor"
                                                : String(
                                                    root.previewFileIdentity
                                                        .displayLabel
                                                    || "Code"
                                                )
                                                    + " editor"
                        : root.markdownFileActive
                            ? "Markdown preview"
                            : root.imagePreviewActive
                            ? "Image preview"
                            : LibraryStore.loadingFilePreview
                            ? "Opening file"
                            : LibraryStore.filePreviewError.length > 0
                                ? "Preview unavailable"
                                : "Read-only preview"
                    : ChatStore.responding
                        ? ChatStore.runPhaseLabel
                        : ChatStore.lastModel.length > 0
                            ? ChatStore.lastProvider + "  ·  " + ChatStore.lastModel
                            : root.hasSelectedChat
                                ? "Ready"
                                : "Select a Chat"
                color: root.editorSurfaceActive
                    && (
                        LibraryStore.fileSaveError.length > 0
                        || LibraryStore.filePreviewError.length > 0
                    )
                    ? root.theme.danger
                    : root.previewActive
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
                onClicked:
                    editorTabStrip.requestCloseActiveTab()
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

            onDirtyCloseRequested: function(
                tabKey,
                documentId,
                title
            ) {
                root.openDirtyCloseDialog(
                    tabKey,
                    documentId,
                    title
                )
            }

            onRevealInLibraryRequested: function(
                libraryId,
                fileId,
                relativePath
            ) {
                root.revealInLibraryRequested(
                    libraryId,
                    fileId,
                    relativePath
                )
            }
        }
    }

    FilePreview {
        id: filePreview

        anchors.top: workspaceHeader.bottom
        anchors.bottom: parent.bottom
        x: root.previewLeftObstruction
        width: Math.max(0, parent.width - root.previewLeftObstruction)
        visible: root.previewActive
            && !root.codeEditorActive
            && !root.markdownEditMode
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

    CodeEditor {
        id: codeEditor

        anchors.top: workspaceHeader.bottom
        anchors.bottom: parent.bottom
        x: root.previewLeftObstruction
        width: Math.max(
            0,
            parent.width - root.previewLeftObstruction
        )
        visible: root.editorSurfaceActive
        theme: root.theme
        active: root.editorSurfaceActive
        markdownDocument: root.markdownFileActive
        file: LibraryStore.selectedFile
        preview: LibraryStore.filePreview
        loading: LibraryStore.loadingFilePreview
        errorMessage: LibraryStore.filePreviewError

        onMarkdownPreviewRequested: {
            filePreview.viewMode = "rendered"
        }

        onFileLocationRequested: function(
            filePath,
            lineNumber,
            columnNumber
        ) {
            root.openEditorLocation(
                filePath,
                lineNumber,
                columnNumber
            )
        }

        onDocumentReady: function(documentId) {
            root.revealPendingEditorLocation(documentId)
        }

        onDirtyStateReported: function(documentId, dirty) {
            editorTabStrip.setDocumentDirty(
                documentId,
                dirty
            )

            if (
                !dirty
                && documentId
                    === root.pendingCloseDocumentId
                && root.pendingCloseTabKey.length > 0
            ) {
                var tabKey = root.pendingCloseTabKey
                root.pendingCloseTabKey = ""
                root.pendingCloseDocumentId = ""
                editorTabStrip.closeTabByKey(tabKey)
            }
        }

        onSaveFailed: function(
            documentId,
            message
        ) {
            root.pendingCloseTabKey = ""
            root.pendingCloseDocumentId = ""
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

    Dialog {
        id: editorSafetyDialog

        parent: Overlay.overlay
        modal: true
        focus: true
        width: Math.min(
            460,
            Math.max(320, root.width - 48)
        )
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        padding: 0
        closePolicy: Popup.CloseOnEscape

        onRejected: root.cancelEditorSafety()

        background: Rectangle {
            color: root.theme.controlSurfaceBg
            radius: 8
            border.width: 1
            border.color: root.theme.panelBorder
        }

        contentItem: ColumnLayout {
            spacing: 10

            Item {
                Layout.preferredHeight: 6
            }

            Text {
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                text: "Save changes to “"
                    + root.editorSafetyTitle
                    + "”?"
                color: root.theme.appText
                font.pixelSize: root.theme.typeSize(14)
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
            }

            Text {
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                text: "Your changes will be lost if you don’t save them."
                color: root.theme.mutedText
                font.pixelSize: root.theme.typeSize(11)
                wrapMode: Text.Wrap
            }

            Item {
                Layout.preferredHeight: 4
            }
        }

        footer: Rectangle {
            implicitHeight: 58
            color: "transparent"

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                color: root.theme.quietBorder
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 8

                Button {
                    text: "Don’t Save"
                    onClicked: root.discardAndCloseTarget()
                }

                Item {
                    Layout.fillWidth: true
                }

                Button {
                    text: "Cancel"
                    onClicked: root.cancelEditorSafety()
                }

                Button {
                    text: "Save"
                    highlighted: true
                    enabled: !LibraryStore.savingFile
                    onClicked: root.saveAndCloseTarget()
                }
            }
        }
    }

    Connections {
        target: LibraryStore

        function onFileRenamed(
            fileId,
            relativePath,
            name
        ) {
            var documentId = String(
                CollectionStore.selectedCollectionId || ""
            )
                + ":"
                + String(
                    LibraryStore.selectedLibraryId || ""
                )
                + ":"
                + String(fileId || "")
            editorTabStrip.updateFileTab(
                documentId,
                name,
                relativePath
            )
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

        readonly property real distanceFromEnd: ChatViewportPolicy.distanceFromEnd(
            originY,
            contentHeight,
            height,
            topMargin,
            bottomMargin,
            contentY
        )
        readonly property bool nearEnd: count === 0
            || ChatViewportPolicy.isNearEnd(
                originY,
                contentHeight,
                height,
                topMargin,
                bottomMargin,
                contentY,
                96
            )
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
        reuseItems: false
        boundsBehavior: Flickable.StopAtBounds
        model: ChatStore.messages

        onCountChanged: {
            if (root.scrollToEndPending) {
                root.updateScrollToEndTarget()
                root.startScrollToEndAnimation()
            }
        }

        onContentHeightChanged: {
            if (root.scrollToEndPending) {
                root.updateScrollToEndTarget()
                root.startScrollToEndAnimation()
                return
            }

            if (root.shouldFollowTranscript()) {
                root.scheduleScrollToEnd(false, false)
            }
        }

        onContentYChanged: {
            if (nearBeginning) {
                root.scheduleHistoryPrefetch()
            }

            root.scheduleChatViewportSave()
        }

        onMovementStarted: {
            root.takeManualScrollOwnership()
        }

        onDraggingChanged: {
            if (dragging) {
                root.takeManualScrollOwnership()
            }
        }

        onFlickingChanged: {
            if (flicking) {
                root.takeManualScrollOwnership()
            }
        }

        onMovementEnded: {
            if (
                root.historyRestoreDeferred
                && root.historyLoadPending
                && root.historyPrependedCount > 0
            ) {
                root.historyRestoreDeferred = false
                historyRestoreTimer.restart()
                root.scheduleChatViewportSave()
                return
            }
            if (
                ChatViewportPolicy.shouldEnableFollow({
                    autoFollow: root.revealFollowEnabled,
                    nearEnd: transcript.nearEnd,
                    restoringViewport: root.restoringChatViewport,
                    restoringHistory: root.historyLoadPending
                })
            ) {
                root.revealFollowEnabled = true
            }

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
            content: String(modelData.id || "")
                    === ChatStore.activeRunAssistantMessageId
                ? ChatStore.activeRunContent
                : String(modelData.content || "")
            timestamp: String(modelData.displayTimestamp || "")
            status: String(modelData.status || "complete")
            progressLabel: String(modelData.id || "")
                    === ChatStore.activeRunAssistantMessageId
                ? ChatStore.runPhaseLabel
                : String(modelData.role || "") === "assistant"
                    && String(modelData.status || "") === "streaming"
                    ? "Starting Run…"
                    : ""
            activity: String(modelData.id || "")
                    === ChatStore.activeRunAssistantMessageId
                ? ChatStore.runActivity
                : ({})
            attachedFiles: String(modelData.role || "") === "assistant"
                    && String(modelData.status || "") === "streaming"
                ? ChatStore.attachments
                : []
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

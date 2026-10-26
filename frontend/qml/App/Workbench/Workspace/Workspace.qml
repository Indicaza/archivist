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
    property int scrollToEndSettlePass: 0
    property bool programmaticScrollWrite: false
    // Animated jump to latest (see startAnimatedJump).
    property real jumpProgress: 0
    property real jumpStartY: 0
    property real jumpLaunch: 0
    property real jumpTotal: 0
    property bool jumpCutDone: true

    // The pinned time label shown while scrolling.
    property string pinnedSectionLabel: ""
    property bool pinnedSectionShown: false
    // Messages that arrived while the reader was scrolled up.
    property int unreadCount: 0
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
    readonly property var diagnosticsFlags: ArchivistDiagnostics || ({})
    readonly property bool chatViewportTraceEnabled: Boolean(
        diagnosticsFlags.chatViewportTrace
    )
    readonly property bool chatDisableHistoryPrefetch: Boolean(
        diagnosticsFlags.chatDisableHistoryPrefetch
    )
    readonly property bool chatDisableViewportSave: Boolean(
        diagnosticsFlags.chatDisableViewportSave
    )
    readonly property bool chatPlainMessages: Boolean(
        diagnosticsFlags.chatPlainMessages
    )
    property real chatTracePreviousContentY: 0
    property int chatTracePreviousCount: 0

    function roundedTraceValue(value) {
        var number = Number(value || 0)
        return isFinite(number)
            ? Math.round(number * 100) / 100
            : 0
    }

    function traceChatViewport(eventName, details) {
        if (!root.chatViewportTraceEnabled) {
            return
        }

        var payload = {
            chatId: String(ChatStore.selectedChatId || ""),
            contentY: root.roundedTraceValue(transcript.contentY),
            originY: root.roundedTraceValue(transcript.originY),
            contentHeight: root.roundedTraceValue(transcript.contentHeight),
            viewportHeight: root.roundedTraceValue(transcript.height),
            minimumY: root.roundedTraceValue(
                transcript.originY - transcript.topMargin
            ),
            maximumY: root.roundedTraceValue(
                ChatViewportPolicy.transcriptEndY(
                    transcript.originY,
                    transcript.contentHeight,
                    transcript.height,
                    transcript.topMargin,
                    transcript.bottomMargin
                )
            ),
            verticalVelocity: root.roundedTraceValue(
                transcript.verticalVelocity
            ),
            moving: transcript.moving,
            dragging: transcript.dragging,
            flicking: transcript.flicking,
            count: transcript.count,
            nearBeginning: transcript.nearBeginning,
            nearEnd: transcript.nearEnd,
            historyLoadPending: root.historyLoadPending,
            historyPrependedCount: root.historyPrependedCount,
            restoringViewport: root.restoringChatViewport,
            scrollToEndPending: root.scrollToEndPending,
            programmaticScrollWrite: root.programmaticScrollWrite
        }
        var values = details || ({})

        for (var key in values) {
            payload[key] = values[key]
        }

        RuntimeTrace.emitEvent("chat.viewport", eventName, payload)
    }

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
    readonly property color chatHue: root.theme.agentHue(
        String(ChatStore.selectedChat.agentId || "")
    )
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

        var beforeContentY = transcript.contentY
        var beforeOriginY = transcript.originY
        var beforeContentHeight = transcript.contentHeight
        transcript.forceLayout()
        root.traceChatViewport("viewport.anchor.forceLayout", {
            beforeContentY: root.roundedTraceValue(beforeContentY),
            beforeOriginY: root.roundedTraceValue(beforeOriginY),
            beforeContentHeight: root.roundedTraceValue(
                beforeContentHeight
            )
        })

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
        if (root.chatDisableViewportSave) {
            return
        }

        if (
            root.restoringChatViewport
            || root.programmaticScrollWrite
            || root.scrollToEndPending
            || jumpAnimation.running
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

        root.traceChatViewport("viewport.save.begin", {
            stateKey: key
        })
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
        root.traceChatViewport("viewport.save.end", {
            stateKey: key,
            anchorMessageId: anchor.messageId,
            anchorOffset: root.roundedTraceValue(anchor.offset)
        })
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
        var shouldRestoreEnd = state.atEnd === undefined
            ? true
            : Boolean(state.atEnd)

        if (shouldRestoreEnd) {
            root.finishChatViewportRestore()
            root.scheduleScrollToEnd(false)
            return
        }

        root.cancelScrollToEnd()
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
        root.traceChatViewport("viewport.programmatic.begin", {
            holdOpen: holdOpen === true
        })
        root.programmaticScrollWrite = true

        if (holdOpen === true) {
            programmaticScrollReleaseTimer.stop()
            return
        }

        programmaticScrollReleaseTimer.restart()
    }

    function takeManualScrollOwnership() {
        historyPrefetchTimer.stop()
        jumpAnimation.stop()
        root.cancelScrollToEnd()
        root.cancelChatViewportRestore()

        if (
            root.historyLoadPending
            && root.historyPrependedCount > 0
        ) {
            historyRestoreTimer.stop()
            root.historyRestoreDeferred = true
        }
    }

    function placeTranscriptAtEnd() {
        if (!root.scrollToEndPending || transcript.count === 0) {
            return
        }

        transcript.forceLayout()
        root.beginProgrammaticScrollWrite(true)
        transcript.positionViewAtIndex(
            transcript.count - 1,
            ListView.End
        )
        transcript.forceLayout()

        var targetY = root.clampedTranscriptContentY(
            root.transcriptEndY()
        )
        var beforeY = transcript.contentY
        transcript.contentY = targetY

        root.traceChatViewport("navigation.end.place", {
            previousContentY: root.roundedTraceValue(beforeY),
            targetY: root.roundedTraceValue(targetY),
            settlePass: root.scrollToEndSettlePass,
            explicitJump: root.jumpToEndPending
        })
        scrollToEndSettleTimer.restart()
    }

    function scheduleScrollToEnd(explicitJump) {
        if (explicitJump === true) {
            root.jumpToEndPending = true
        }

        if (
            ChatStore.messages.length === 0
            || (
                !root.jumpToEndPending
                && (
                    root.restoringChatViewport
                    || root.historyLoadPending
                )
            )
        ) {
            return
        }

        historyPrefetchTimer.stop()
        root.scrollToEndPending = true
        root.scrollToEndSettlePass = 0
        root.traceChatViewport("navigation.end.schedule", {
            explicitJump: root.jumpToEndPending
        })
        root.placeTranscriptAtEnd()
    }

    function cancelScrollToEnd() {
        root.scrollToEndPending = false
        root.jumpToEndPending = false
        root.scrollToEndSettlePass = 0
        scrollToEndSettleTimer.stop()
        programmaticScrollReleaseTimer.restart()
    }

    function finishScrollToEnd() {
        if (!root.scrollToEndPending || transcript.count === 0) {
            programmaticScrollReleaseTimer.restart()
            return
        }

        transcript.forceLayout()

        var targetY = root.clampedTranscriptContentY(
            root.transcriptEndY()
        )
        var remaining = Math.abs(targetY - transcript.contentY)

        root.traceChatViewport("navigation.end.settle", {
            targetY: root.roundedTraceValue(targetY),
            remaining: root.roundedTraceValue(remaining),
            settlePass: root.scrollToEndSettlePass
        })

        if (remaining > 0.75 && root.scrollToEndSettlePass < 4) {
            root.scrollToEndSettlePass += 1
            root.placeTranscriptAtEnd()
            return
        }

        root.beginProgrammaticScrollWrite(false)
        transcript.contentY = targetY
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
        root.startAnimatedJump()
    }

    // The jump-to-latest button scrolls fast instead of teleporting. The
    // animation follows the live end of the transcript every frame (rows
    // near the end may still settle), then hands over to the exact end
    // placement below.
    function startAnimatedJump() {
        jumpAnimation.stop()

        if (transcript.count === 0 || root.theme.reducedMotion) {
            root.scheduleScrollToEnd(true)
            return
        }

        transcript.forceLayout()
        var endY = root.clampedTranscriptContentY(root.transcriptEndY())
        var distance = endY - transcript.contentY

        if (distance <= 1) {
            root.scheduleScrollToEnd(true)
            return
        }

        var plan = ChatViewportPolicy.jumpPlan(distance, transcript.height)
        root.beginProgrammaticScrollWrite(true)
        root.jumpToEndPending = true
        root.jumpStartY = transcript.contentY
        root.jumpLaunch = plan.launch
        root.jumpTotal = plan.launch + plan.travel
        root.jumpCutDone = plan.launch <= 0
        root.jumpProgress = 0
        jumpAnimation.duration = plan.duration
        jumpAnimation.easing.type = plan.launch > 0
            ? Easing.OutQuart
            : Easing.OutCubic
        root.traceChatViewport("navigation.jump.start", {
            distance: root.roundedTraceValue(distance),
            launch: root.roundedTraceValue(plan.launch),
            travel: root.roundedTraceValue(plan.travel),
            duration: plan.duration
        })
        jumpAnimation.restart()
    }

    function stepAnimatedJump() {
        var travelled = root.jumpTotal * root.jumpProgress

        if (!root.jumpCutDone && travelled < root.jumpLaunch) {
            transcript.contentY = root.jumpStartY + travelled
            return
        }

        if (!root.jumpCutDone) {
            // Moving fast enough that a cut to a few screens above the end
            // reads as motion, not a jump.
            root.jumpCutDone = true
            transcript.positionViewAtIndex(transcript.count - 1, ListView.End)
        }

        var endY = root.clampedTranscriptContentY(root.transcriptEndY())
        transcript.contentY = root.clampedTranscriptContentY(
            endY - (root.jumpTotal - travelled)
        )
    }

    function updatePinnedSection() {
        var row = transcript.itemAt(
            transcript.width / 2,
            transcript.contentY + transcript.topMargin + 4
        )
        var label = row && row.timeSection ? String(row.timeSection) : ""

        if (label.length === 0) {
            return
        }

        root.pinnedSectionLabel = label
        root.pinnedSectionShown = true
        pinnedSectionHideTimer.restart()
    }

    // Retry sends the prompt behind a reply again.
    function retryReply(messageId) {
        if (ChatStore.responding) {
            return
        }

        var messages = ChatStore.messages || []
        var found = false

        for (var index = messages.length - 1; index >= 0; index -= 1) {
            var message = messages[index] || ({})

            if (!found) {
                found = String(message.id || "") === String(messageId)
                continue
            }

            if (String(message.role || "") === "user") {
                ChatStore.sendMessage(String(message.content || ""))
                return
            }
        }
    }

    function sendSuggestion(prompt) {
        if (!ChatStore.responding && root.hasSelectedChat) {
            ChatStore.sendMessage(prompt)
        }
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

    function canPrefetchHistory() {
        if (root.chatDisableHistoryPrefetch) {
            return false
        }

        return ChatViewportPolicy.shouldPrefetchHistory({
            visible: transcript.visible,
            hasOlderMessages: ChatStore.hasOlderMessages,
            nearBeginning: transcript.nearBeginning,
            loadingMessages: ChatStore.loadingMessages,
            loadingOlderMessages: ChatStore.loadingOlderMessages,
            historyLoadPending: root.historyLoadPending,
            restoringViewport: root.restoringChatViewport,
            responding: ChatStore.responding,
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

        root.traceChatViewport("history.request")
        root.historyLoadPending = true
        ChatStore.loadOlderMessages()
    }

    function captureHistoryAnchor(count) {
        if (root.jumpToEndPending) {
            return
        }

        root.traceChatViewport("history.anchor.capture.begin", {
            prependCount: count
        })
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
        root.traceChatViewport("history.anchor.capture.end", {
            anchorIndex: root.historyAnchorIndex,
            anchorOffset: root.roundedTraceValue(
                root.historyAnchorOffset
            ),
            prependCount: count
        })
    }

    function restoreHistoryAnchor() {
        if (!root.historyLoadPending || root.historyPrependedCount <= 0) {
            return
        }

        if (transcript.moving || transcript.dragging || transcript.flicking) {
            root.historyRestoreDeferred = true
            return
        }

        root.traceChatViewport("history.anchor.restore.begin", {
            anchorIndex: root.historyAnchorIndex,
            prependCount: root.historyPrependedCount,
            restorePass: root.historyRestorePass
        })
        const targetIndex = root.historyAnchorIndex + root.historyPrependedCount
        const anchorPlaced = root.placeTranscriptAnchor(
            targetIndex,
            root.historyAnchorOffset
        )

        if (anchorPlaced) {
            root.traceChatViewport("history.anchor.restore.placed", {
                targetIndex: targetIndex
            })
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
        root.chatTracePreviousContentY = transcript.contentY
        root.chatTracePreviousCount = transcript.count
        root.traceChatViewport("trace.config", {
            disableHistoryPrefetch: root.chatDisableHistoryPrefetch,
            disableViewportSave: root.chatDisableViewportSave,
            plainMessages: root.chatPlainMessages
        })
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
            }
        }

        function onSelectedChatIdChanged() {
            jumpAnimation.stop()
            root.unreadCount = 0
            root.clearHistoryAnchor()
            root.cancelScrollToEnd()
            root.switchChatViewportState()
        }

        function onLoadingMessagesChanged() {
            if (!ChatStore.loadingMessages && ChatStore.messages.length > 0) {
                if (root.chatViewportRestorePending) {
                    chatViewportRestoreTimer.restart()
                } else {
                    root.scheduleScrollToEnd(false)
                }
            }
        }

        function onOlderMessagesWillPrepend(count) {
            root.traceChatViewport("history.willPrepend", {
                prependCount: count
            })
            if (root.jumpToEndPending) {
                root.clearHistoryAnchor(true)
                return
            }

            root.captureHistoryAnchor(count)
        }

        function onOlderMessagesPrepended(count) {
            root.traceChatViewport("history.prepended", {
                prependCount: count
            })
            if (root.jumpToEndPending) {
                root.clearHistoryAnchor(ChatStore.loadingOlderMessages)
                root.placeTranscriptAtEnd()
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
            root.traceChatViewport("history.loading.changed", {
                loading: ChatStore.loadingOlderMessages
            })
            if (ChatStore.loadingOlderMessages) {
                return
            }

            if (root.jumpToEndPending) {
                root.clearHistoryAnchor(false)
                root.placeTranscriptAtEnd()
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

    NumberAnimation {
        id: jumpAnimation

        target: root
        property: "jumpProgress"
        from: 0
        to: 1
        onFinished: root.scheduleScrollToEnd(true)
    }

    onJumpProgressChanged: {
        if (jumpAnimation.running) {
            root.stepAnimatedJump()
        }
    }

    Timer {
        id: scrollToEndSettleTimer

        interval: 24
        repeat: false
        onTriggered: root.finishScrollToEnd()
    }

    Timer {
        id: programmaticScrollReleaseTimer

        interval: 32
        repeat: false
        onTriggered: root.programmaticScrollWrite = false
    }

    Timer {
        id: movementTraceTimer

        interval: 32
        repeat: true
        running: root.chatViewportTraceEnabled && transcript.moving
        onTriggered: root.traceChatViewport("movement.sample")
    }

    Timer {
        id: settleTraceTimer

        property int tick: 0
        interval: 16
        repeat: true
        onTriggered: {
            tick += 1

            if (
                tick === 1
                || tick === 2
                || tick === 4
                || tick === 8
                || tick === 16
                || tick === 32
            ) {
                root.traceChatViewport("settle.sample", {
                    elapsedMs: tick * interval
                })
            }

            if (tick >= 32) {
                stop()
            }
        }
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
        cacheBuffer: Math.max(1200, Math.ceil(height * 2))
        displayMarginBeginning: 0
        displayMarginEnd: 0
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds
        boundsMovement: Flickable.StopAtBounds
        model: ChatStore.messageModel

        onCountChanged: {
            var previousCount = root.chatTracePreviousCount
            root.chatTracePreviousCount = count
            root.traceChatViewport("count.changed", {
                previousCount: previousCount,
                nextCount: count
            })

            if (
                count > previousCount
                && previousCount > 0
                && !nearEnd
                && !root.historyLoadPending
                && !ChatStore.loadingMessages
                && !ChatStore.loadingOlderMessages
            ) {
                root.unreadCount += count - previousCount
            }

            if (root.scrollToEndPending) {
                Qt.callLater(root.placeTranscriptAtEnd)
                return
            }

            if (jumpAnimation.running) {
                return
            }

            if (
                ChatStore.responding
                && !root.historyLoadPending
                && !root.chatViewportRestorePending
                && !root.restoringChatViewport
                && count > 0
            ) {
                root.beginProgrammaticScrollWrite()
                transcript.positionViewAtIndex(
                    count - 1,
                    ListView.Contain
                )
            }
        }

        onNearEndChanged: {
            if (nearEnd) {
                root.unreadCount = 0
            }
        }

        onContentHeightChanged: {
            root.traceChatViewport("contentHeight.changed")

            if (root.scrollToEndPending) {
                scrollToEndSettleTimer.restart()
            }
        }

        onOriginYChanged: {
            root.traceChatViewport("originY.changed")

            if (root.scrollToEndPending) {
                scrollToEndSettleTimer.restart()
            }
        }

        onContentYChanged: {
            var previousContentY = root.chatTracePreviousContentY
            root.chatTracePreviousContentY = contentY

            if (
                root.chatViewportTraceEnabled
                && !moving
                && Math.abs(contentY - previousContentY) > 0.5
            ) {
                root.traceChatViewport("contentY.idle.changed", {
                    previousContentY: root.roundedTraceValue(
                        previousContentY
                    )
                })
            }

            if (nearBeginning) {
                root.scheduleHistoryPrefetch()
            }

            root.scheduleChatViewportSave()

            if (moving || jumpAnimation.running) {
                root.updatePinnedSection()
            }
        }

        onMovementStarted: {
            settleTraceTimer.stop()
            root.traceChatViewport("movement.started")
            root.takeManualScrollOwnership()
        }

        onDraggingChanged: {
            root.traceChatViewport("dragging.changed", {
                active: dragging
            })
            if (dragging) {
                root.takeManualScrollOwnership()
            }
        }

        onFlickingChanged: {
            root.traceChatViewport("flicking.changed", {
                active: flicking
            })
            if (flicking) {
                root.takeManualScrollOwnership()
            }
        }

        onMovementEnded: {
            root.traceChatViewport("movement.ended")

            if (root.chatViewportTraceEnabled) {
                settleTraceTimer.tick = 0
                settleTraceTimer.restart()
            }

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
            root.scheduleHistoryPrefetch()
            root.scheduleChatViewportSave()
        }

        delegate: ChatMessage {
            required property var messageData
            required property int index
            required property string timeSection
            required property string sectionBreakAfter

            width: transcript.width
            theme: root.theme
            message: messageData || ({})
            activeRunAssistantMessageId:
                ChatStore.activeRunAssistantMessageId
            // Only the streaming message reads the live Run values, so a
            // flush re-evaluates one delegate instead of every row.
            readonly property bool isActiveRunMessage: String(
                messageData && messageData.id || ""
            ) === ChatStore.activeRunAssistantMessageId
            activeRunContent: isActiveRunMessage
                ? ChatStore.activeRunContent
                : ""
            activeRunProgressLabel: isActiveRunMessage
                ? ChatStore.runPhaseLabel
                : ""
            activeRunActivity: isActiveRunMessage
                ? ChatStore.runActivity
                : ({})
            activeRunAttachments: ChatStore.attachments
            editProposalBusy: ChatStore.mutatingEditProposal
                && String(
                    messageData && messageData.editProposal
                        ? messageData.editProposal.id || ""
                        : ""
                ) === ChatStore.mutatingEditProposalId
            leftObstruction: root.leftObstruction
            hue: messageData && messageData.agentId
                ? root.theme.agentHue(String(messageData.agentId))
                : root.chatHue
            lastInTranscript: index === transcript.count - 1
            breakAfter: sectionBreakAfter
            retryAvailable: lastInTranscript && !ChatStore.responding
            diagnosticTraceEnabled: root.chatViewportTraceEnabled
            diagnosticPlainContent: root.chatPlainMessages
            onDiagnosticEvent: function(eventName, details) {
                var payload = details || ({})
                payload.delegateIndex = index
                root.traceChatViewport(eventName, payload)
            }
            onContextInspectionRequested: function(messageId) {
                root.contextInspectionRequested(messageId)
            }
            onRetryRequested: function(messageId) {
                root.retryReply(messageId)
            }
            onEditApproveAllRequested: function(proposalId) {
                ChatStore.approveEditProposal(proposalId)
            }
            onEditApproveSelectedRequested: function(proposalId, operationIds) {
                ChatStore.approveSelectedEditProposal(proposalId, operationIds)
            }
            onEditRejectRequested: function(proposalId) {
                ChatStore.rejectEditProposal(proposalId)
            }
            onEditUndoRequested: function(proposalId) {
                ChatStore.undoEditProposal(proposalId)
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

    // Empty state: the agent's glyph, a line of guidance and, for a new
    // conversation, three prompts drawn from the open library.
    readonly property bool emptyConversation: !root.fileSurfaceActive
        && !transcript.visible
        && root.hasSelectedChat
        && !ChatStore.loadingChats
        && !ChatStore.loadingMessages
        && ChatStore.errorMessage.length === 0
    readonly property var chatSuggestions: root.emptyConversation
        ? root.buildSuggestions()
        : []

    function buildSuggestions() {
        var library = String(root.selectedLibraryName || "")
        var files = library.length > 0 ? (LibraryStore.files || []) : []
        var recent = ""
        var recentTime = -1

        for (var index = 0; index < files.length && index < 2000; index += 1) {
            var file = files[index] || ({})
            var path = String(file.relativePath || file.name || "")

            if (path.length === 0) {
                continue
            }

            var stamp = Date.parse(String(
                file.modifiedAt || file.updatedAt || file.indexedAt || ""
            ))
            var time = isNaN(stamp) ? 0 : stamp

            if (time > recentTime) {
                recentTime = time
                recent = path
            }
        }

        var recentName = recent.split("/").pop()

        return [
            library.length > 0
                ? "Summarize what's in " + library
                : "Help me plan a new project",
            recentName.length > 0
                ? "Walk me through " + recentName
                : "Explain a tricky concept step by step",
            library.length > 0
                ? "What should I pick up next in " + library + "?"
                : "Draft an outline for a document"
        ]
    }

    Column {
        anchors.centerIn: parent
        width: Math.min(460, parent.width - 80)
        spacing: 8
        visible: !root.fileSurfaceActive && !transcript.visible

        Item {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 64
            height: root.emptyConversation ? 82 : 0
            visible: root.emptyConversation

            Rectangle {
                id: emptyGlyph

                property real float: 0

                anchors.horizontalCenter: parent.horizontalCenter
                y: 2 - 3 * float
                width: 60
                height: 60
                radius: 19
                color: root.theme.withAlpha(root.chatHue, 0.14)
                border.width: 1
                border.color: root.theme.withAlpha(root.chatHue, 0.36)

                SequentialAnimation on float {
                    loops: Animation.Infinite
                    running: root.emptyConversation && !root.theme.reducedMotion

                    NumberAnimation { to: 1; duration: 1600; easing.type: Easing.InOutSine }
                    NumberAnimation { to: 0; duration: 1600; easing.type: Easing.InOutSine }
                }

                Text {
                    anchors.centerIn: parent
                    text: "✣"
                    color: root.theme.agentHueBright(root.chatHue)
                    font.pixelSize: 28
                    font.weight: Font.Bold
                }
            }
        }

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
                            : "Start the conversation"
            color: root.theme.appText
            font.family: root.emptyConversation
                ? root.theme.titleFontFamily
                : root.theme.bodyFontFamily
            font.pixelSize: root.theme.typeSize(root.emptyConversation ? 20 : 16)
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
        }

        Text {
            width: parent.width
            text: ChatStore.errorMessage.length > 0
                ? ChatStore.errorMessage
                : !root.hasSelectedChat
                    ? "Open Chats from the command dock to choose a conversation."
                    : "Ask anything below, or start with one of these."
            color: root.theme.mutedText
            font.pixelSize: root.theme.typeSize(12)
            lineHeight: root.theme.typeLineHeightBody
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
        }

        Item {
            width: parent.width
            height: root.emptyConversation ? 10 : 0
        }

        Repeater {
            model: root.chatSuggestions

            delegate: Rectangle {
                id: suggestion

                required property string modelData
                required property int index
                readonly property bool hovered: suggestionMouse.containsMouse
                property real arrival: root.theme.reducedMotion ? 1 : 0

                width: parent.width
                height: 40
                radius: 12
                color: hovered
                    ? Qt.tint(root.theme.controlSurfaceBg, root.theme.withAlpha(root.chatHue, 0.18))
                    : root.theme.surfaceBg
                border.width: 1
                border.color: hovered
                    ? root.theme.withAlpha(root.chatHue, 0.55)
                    : root.theme.quietBorder
                opacity: arrival
                transform: Translate {
                    y: (1 - suggestion.arrival) * 8
                }
                Accessible.role: Accessible.Button
                Accessible.name: modelData

                Behavior on color {
                    ColorAnimation { duration: 120 }
                }

                SequentialAnimation on arrival {
                    running: !root.theme.reducedMotion

                    PauseAnimation { duration: 120 + suggestion.index * 60 }
                    NumberAnimation { to: 1; duration: 260; easing.type: Easing.OutCubic }
                }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 16
                    anchors.right: suggestionArrow.left
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    text: suggestion.modelData
                    color: root.theme.appText
                    font.family: root.theme.chatFontFamily
                    font.pixelSize: root.theme.typeSize(12)
                    elide: Text.ElideRight
                }

                Text {
                    id: suggestionArrow

                    anchors.right: parent.right
                    anchors.rightMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    text: "→"
                    color: suggestion.hovered
                        ? root.theme.agentHueBright(root.chatHue)
                        : root.theme.mutedText
                    font.pixelSize: root.theme.typeSize(12)
                }

                MouseArea {
                    id: suggestionMouse

                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    enabled: !ChatStore.responding
                    onClicked: root.sendSuggestion(suggestion.modelData)
                }
            }
        }
    }

    // While you scroll, the time of the stretch under the top edge pins
    // there, then fades once the transcript settles. It floats over the
    // transcript, so it never moves a row.
    TimeDivider {
        id: pinnedSection

        x: transcript.x
        y: transcript.y + 6
        width: transcript.width
        theme: root.theme
        section: root.pinnedSectionLabel
        lines: false
        opacity: root.pinnedSectionShown ? 1 : 0
        visible: opacity > 0.01 && transcript.visible
        z: 19

        Behavior on opacity {
            NumberAnimation {
                duration: root.pinnedSectionShown ? 120 : 320
                easing.type: Easing.OutCubic
            }
        }
    }

    Timer {
        id: pinnedSectionHideTimer

        interval: 900
        onTriggered: root.pinnedSectionShown = false
    }

    JumpToLatestButton {
        anchors.right: parent.right
        anchors.rightMargin: 18
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 18
        theme: root.theme
        hue: root.chatHue
        unreadCount: root.unreadCount
        visible: transcript.visible && !transcript.nearEnd
        opacity: visible ? 1 : 0
        z: 20
        onClicked: root.jumpToLatest()

        Behavior on opacity {
            NumberAnimation { duration: 150 }
        }
    }
}

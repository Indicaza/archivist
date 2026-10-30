import QtQuick
import Archivist.Services 1.0
import "ActivityRail"
import "ExplorerDock"
import "SidePanelResizeHandle"
import "StatusBar"
import "MotionTuner"
import "../Workspace"
import "../Dock"
import "../WorkbenchDock"
import "../ArtifactDrawer"
import "../CompactChat"
import "../Home"

Rectangle {
    id: root

    required property var theme

    // The side panel follows Shell (rail ids, Home); its state is still
    // saved per project here, by the panel's old index.
    readonly property int activeViewIndex: Shell.panelIndex
    readonly property bool explorerOpen: Shell.panelOpen
    // Home covers the workspace, which stays as it was underneath: same
    // size, same panels, nothing rebuilt. 1 is Home fully shown.
    property real homeReveal: Shell.homeActive ? 1 : 0
    readonly property bool workspaceHidden: homeReveal >= 1
    property bool homeSending: false
    property string pendingChatId: ""
    property var pendingDocument: null
    // How the conversation sits in the workspace. The workspace store owns
    // it (saved per project, placement moves are undo steps): the mode is
    // "compact", a ChatGPT-style composer over the workspace, or
    // "workbench", the chat, terminal and problems panel at the bottom; a
    // conversation can keep its own. Placement is separate: the compact
    // composer sits at the bottom or docked in a chat anchor. A plugin mode
    // presents as the compact composer until it brings its own surface.
    readonly property var composerState: Docking.composer
    readonly property string chatMode: {
        root.composerState
        return Docking.composerMode(String(ChatStore.selectedChatId || ""))
    }
    readonly property bool compactChat: chatMode !== "workbench"
    readonly property var compactPlacement: {
        root.composerState
        return Docking.composerPlacement("compact")
    }
    readonly property string composerAnchorId: compactPlacement.kind === "anchor"
        ? String(compactPlacement.anchorId || "")
        : ""
    // The conversation keeps its own mode.
    readonly property bool modeRemembered: {
        var pins = root.composerState.conversationModes || ({})
        var chatId = String(ChatStore.selectedChatId || "")
        return chatId.length > 0 && pins[chatId] !== undefined
    }
    // 0 is the docked workbench, 1 the compact composer; in between, one
    // morphs into the other.
    property real compactProgress: compactChat ? 1 : 0
    readonly property bool resizingExplorer: explorerResizeHandle.resizing
    property bool resizingDock: false
    property real explorerWidth: theme.explorerDefaultWidth
    property real dockHeight: theme.chatDockDefaultHeight
    // The bottom panel folded down to its tabs. It stays the place for chat
    // and terminals; folding only gives the workspace the room.
    property bool dockCollapsed: false
    property bool workspaceStateRestored: false
    property string workspaceScopeId: ""

    readonly property real maximumExplorerWidth: Math.max(
        theme.explorerMinWidth,
        Math.min(
            theme.explorerMaxWidth,
            width - theme.activityRailWidth - 420
        )
    )
    readonly property real clampedExplorerWidth: Math.min(
        maximumExplorerWidth,
        Math.max(theme.explorerMinWidth, explorerWidth)
    )
    property real explorerExtent: explorerOpen ? clampedExplorerWidth : 0
    // Home's sidebar is a side panel like the Explorer: same slot, same
    // edge to drag. Its width and whether it's open are one setting for
    // every project. While the Explorer is open it keeps its width, so
    // going Home swaps one panel for the other in place; otherwise it
    // slides out of the rail like any panel.
    property bool homeSidebarOpen: String(WorkspaceState.value("shell/homeSidebarOpen", "true")) !== "false"
    property real homeSidebarWidth: Number(WorkspaceState.value("shell/homeSidebarWidth", theme.explorerDefaultWidth))
        || theme.explorerDefaultWidth
    readonly property bool resizingHomeSidebar: homeSidebarResizeHandle.resizing
    readonly property real clampedHomeSidebarWidth: Math.min(
        maximumExplorerWidth,
        Math.max(theme.explorerMinWidth, homeSidebarWidth)
    )
    property real homeSidebarExtent: homeSidebarOpen && (Shell.homeActive || explorerOpen)
        ? clampedHomeSidebarWidth
        : 0
    readonly property real maximumDockHeight: Math.max(
        theme.chatDockMinHeight,
        height - theme.statusBarHeight - theme.workspaceMinHeight
    )
    readonly property real clampedDockHeight: dockCollapsed
        ? theme.workbenchTabHeight
        : Math.min(
            maximumDockHeight,
            Math.max(theme.chatDockMinHeight, dockHeight)
        )
    readonly property real centeredContentLeft: Math.max(
        theme.messageHorizontalInset,
        (width - theme.transcriptContentWidth) / 2
    )
    // Where the sidebar's edge is drawn. A bouncy close can overshoot past
    // zero; the edge never goes behind the rail.
    readonly property real explorerRight: theme.activityRailWidth + Math.max(0, explorerExtent)
    readonly property bool explorerEncroachesWorkspace: explorerOpen
        && explorerRight + theme.panelCollisionGap >= centeredContentLeft
    readonly property real attachedDockX: explorerRight
    // The compact composer floats over the conversation: centred on the
    // chat pane when one is showing (else on the workspace), just above
    // the status bar. The workspace runs underneath it.
    readonly property var chatFrame: workspaceLoader.item && workspaceLoader.item.chatFrame
        ? workspaceLoader.item.chatFrame
        : null
    readonly property bool compactOnChat: chatFrame !== null && chatFrame.width >= 360
    // Docked in a chat anchor: under that anchor's messages, sized to it.
    readonly property var dockedFrame: workspaceLoader.item && workspaceLoader.item.composerAnchorFrame
        ? workspaceLoader.item.composerAnchorFrame
        : null
    readonly property bool compactDocked: dockedFrame !== null && dockedFrame.width >= 300 && dockedFrame.height >= 160
    readonly property real compactAreaX: compactDocked ? dockedFrame.x : compactOnChat ? chatFrame.x : attachedDockX
    readonly property real compactAreaWidth: compactDocked
        ? dockedFrame.width
        : compactOnChat ? chatFrame.width : width - attachedDockX
    readonly property real compactAreaBottom: compactDocked
        ? dockedFrame.y + dockedFrame.height
        : height - theme.statusBarHeight
    readonly property real compactGap: compactDocked ? 12 : 18
    readonly property real compactSideMargin: compactDocked ? 14 : 28
    readonly property real compactWidth: Math.max(
        280,
        Math.min(theme.homeComposerWidth, compactAreaWidth - 2 * compactSideMargin)
    )
    readonly property real compactX: compactAreaX + Math.max(compactDocked ? 10 : 16, (compactAreaWidth - compactWidth) / 2)
    readonly property real compactHeight: compactComposer.implicitHeight
    // What the conversation keeps clear under its last message.
    readonly property real compactOverlay: compactHeight + compactGap + 14
    readonly property real compactY: compactAreaBottom - compactGap - compactHeight
    // A move between places glides; following a sidebar or a splitter
    // stays immediate.
    property bool composerMoving: false
    readonly property real compactEase: Math.max(0, Math.min(1, compactProgress))
    readonly property real workspaceLeftObstruction: explorerEncroachesWorkspace
        ? explorerRight
        : theme.activityRailWidth
    readonly property real workspacePreviewLeftObstruction: explorerRight
    // The bottom dock's height as drawn (it animates), so the workspace's
    // bottom edge stays on the dock's top edge every frame.
    readonly property real workspaceBottomObstruction: lerp(chatDock.height, 0, compactEase)

    function lerp(from, to, t) {
        return from + (to - from) * t
    }

    // A stage of the morph: 0 until `start`, 1 from `end`.
    function phase(t, start, end) {
        return Math.max(0, Math.min(1, (t - start) / (end - start)))
    }

    // The user picked a mode. A conversation that keeps its own mode keeps
    // this one instead.
    function setChatMode(mode) {
        var next = String(mode || "")
        if (chatMode === next) {
            return
        }
        var chatId = String(ChatStore.selectedChatId || "")
        var changed = modeRemembered
            ? Docking.setConversationComposerMode(chatId, next)
            : Docking.setComposerMode(next)
        if (!changed) {
            return
        }
        if (next !== "workbench") {
            Qt.callLater(function() {
                var workspace = workspaceLoader.item
                if (workspace && workspace.followSelectedChat && ChatStore.selectedChatId.length > 0) {
                    workspace.followSelectedChat()
                }
                compactComposer.focusInput()
            })
        } else {
            chatDock.activeView = "chat"
            dockCollapsed = false
        }
        reportCommandSurface()
    }

    function toggleChatMode() {
        setChatMode(compactChat ? "workbench" : "compact")
    }

    function rememberModeForConversation(remember) {
        var chatId = String(ChatStore.selectedChatId || "")
        if (chatId.length > 0) {
            Docking.setConversationComposerMode(chatId, remember ? chatMode : "")
        }
    }

    // The chat anchor "Dock in conversation" means: where the live chat is.
    function conversationAnchorId() {
        var workspace = workspaceLoader.item
        var row = workspace && workspace.liveChatRow ? workspace.liveChatRow : ({})
        return String(row.anchorId || "")
    }

    function dockComposerInConversation() {
        var anchorId = conversationAnchorId()
        if (anchorId.length > 0 && Docking.dockComposer(anchorId)) {
            if (!compactChat) {
                setChatMode("compact")
            }
            Qt.callLater(compactComposer.focusInput)
        }
    }

    function moveComposerToBottom() {
        if (Docking.moveComposerToBottom() && !compactChat) {
            setChatMode("compact")
        }
    }

    function restoreComposer() {
        Docking.restoreComposer()
        if (compactChat) {
            Qt.callLater(compactComposer.focusInput)
        }
    }

    // A negative y opens the menu upward from there, its right edge at x
    // (the options button); otherwise at the pointer.
    function openComposerMenu(source, x, y) {
        if (y < 0) {
            composerMenu.upwardFrom = y
            composerMenu.popup(source, x - composerMenu.width, y - composerMenu.height)
        } else {
            composerMenu.upwardFrom = 0
            composerMenu.popup(source, x, y)
        }
    }

    onCompactPlacementChanged: {
        if (theme.reducedMotion) {
            return
        }
        composerMoving = true
        composerMoveTimer.restart()
    }

    Timer {
        id: composerMoveTimer

        interval: Math.round(260 * root.theme.motionSpeed)
        onTriggered: root.composerMoving = false
    }

    Behavior on compactProgress {
        enabled: !root.theme.reducedMotion
        NumberAnimation {
            duration: root.theme.motionPanel + 120
            easing.type: Easing.InOutCubic
        }
    }

    Behavior on homeReveal {
        enabled: !root.theme.reducedMotion
        NumberAnimation {
            duration: root.theme.motionPanel
            easing.type: Easing.OutCubic
        }
    }

    Behavior on explorerExtent {
        enabled: !root.resizingExplorer
        NumberAnimation {
            duration: root.theme.motionPanel
            easing.type: Easing.BezierSpline
            easing.bezierCurve: root.theme.layoutCurve
        }
    }

    Behavior on homeSidebarExtent {
        enabled: !root.resizingHomeSidebar
        NumberAnimation {
            duration: root.theme.motionPanel
            easing.type: Easing.BezierSpline
            easing.bezierCurve: root.theme.layoutCurve
        }
    }

    onResizingHomeSidebarChanged: {
        if (!resizingHomeSidebar) {
            WorkspaceState.setValue("shell/homeSidebarWidth", String(Math.round(homeSidebarWidth)))
        }
    }

    function scopeIdForCollection(collectionId) {
        return String(collectionId || "")
    }

    function workspaceStateKey(scopeId, suffix) {
        return "workspace/collections/"
            + String(scopeId || "")
            + "/shell/"
            + String(suffix || "")
    }

    function scheduleWorkspaceStateSave() {
        if (
            !workspaceStateRestored
            || workspaceScopeId.length === 0
        ) {
            return
        }

        workspaceStateSaveTimer.restart()
    }

    function saveWorkspaceState() {
        if (
            !workspaceStateRestored
            || workspaceScopeId.length === 0
        ) {
            return
        }

        WorkspaceState.setValue(
            workspaceStateKey(workspaceScopeId, "activeViewIndex"),
            activeViewIndex
        )
        // By id too: the person's own tabs have no index.
        WorkspaceState.setValue(
            workspaceStateKey(workspaceScopeId, "panelId"),
            Shell.panelId
        )
        WorkspaceState.setValue(
            workspaceStateKey(workspaceScopeId, "explorerOpen"),
            explorerOpen
        )
        WorkspaceState.setValue(
            workspaceStateKey(workspaceScopeId, "explorerWidth"),
            explorerWidth
        )
        WorkspaceState.setValue(
            workspaceStateKey(workspaceScopeId, "dockHeight"),
            dockHeight
        )
        WorkspaceState.setValue(
            workspaceStateKey(workspaceScopeId, "dockCollapsed"),
            dockCollapsed
        )
    }

    function restoreWorkspaceState(scopeId) {
        var targetScopeId = String(scopeId || "")

        if (targetScopeId.length === 0) {
            return
        }

        workspaceStateRestored = false

        var missingValue = "__archivist_missing__"
        var scopedActiveView = WorkspaceState.value(
            workspaceStateKey(targetScopeId, "activeViewIndex"),
            missingValue
        )
        var migrateLegacyState = String(scopedActiveView) === missingValue
            && !Boolean(
                WorkspaceState.value(
                    "workspace/collectionShellMigrated",
                    false
                )
            )

        function readStateValue(suffix, legacyKey, fallback) {
            var scopedValue = WorkspaceState.value(
                workspaceStateKey(targetScopeId, suffix),
                missingValue
            )

            if (String(scopedValue) !== missingValue) {
                return scopedValue
            }

            if (migrateLegacyState) {
                return WorkspaceState.value(legacyKey, fallback)
            }

            return fallback
        }

        workspaceScopeId = targetScopeId
        Shell.restorePanelIndex(
            Math.max(
                0,
                Math.min(
                    4,
                    Number(
                        readStateValue(
                            "activeViewIndex",
                            "workspace/shell/activeViewIndex",
                            0
                        )
                    )
                )
            ),
            Boolean(
                readStateValue(
                    "explorerOpen",
                    "workspace/shell/explorerOpen",
                    true
                )
            )
        )
        var savedPanelId = String(WorkspaceState.value(workspaceStateKey(targetScopeId, "panelId"), "") || "")
        if (savedPanelId.length > 0 && Shell.panelIds.indexOf(savedPanelId) >= 0) {
            Shell.restorePanel(savedPanelId, Shell.panelOpen)
        }
        explorerWidth = Number(
            readStateValue(
                "explorerWidth",
                "workspace/shell/explorerWidth",
                theme.explorerDefaultWidth
            )
        )
        dockHeight = Number(
            readStateValue(
                "dockHeight",
                "workspace/shell/dockHeight",
                theme.chatDockDefaultHeight
            )
        )

        dockCollapsed = Boolean(
            WorkspaceState.value(
                workspaceStateKey(targetScopeId, "dockCollapsed"),
                false
            )
        )

        workspaceStateRestored = true

        if (migrateLegacyState) {
            saveWorkspaceState()
            WorkspaceState.setValue(
                "workspace/collectionShellMigrated",
                true
            )
        }
    }

    function switchCollectionWorkspace() {
        var nextScopeId = scopeIdForCollection(
            CollectionStore.selectedCollectionId
        )

        if (
            nextScopeId.length === 0
            || nextScopeId === workspaceScopeId
        ) {
            return
        }

        if (workspaceStateRestored) {
            workspaceStateSaveTimer.stop()
            saveWorkspaceState()
        }

        restoreWorkspaceState(nextScopeId)
    }

    function resetExplorerWidth() {
        explorerWidth = theme.explorerDefaultWidth
    }

    function resetDockHeight() {
        dockHeight = theme.chatDockDefaultHeight
    }

    function setHomeSidebarOpen(open) {
        homeSidebarOpen = open
        WorkspaceState.setValue("shell/homeSidebarOpen", open ? "true" : "false")
    }

    function resetHomeSidebarWidth() {
        homeSidebarWidth = theme.explorerDefaultWidth
        WorkspaceState.setValue("shell/homeSidebarWidth", String(homeSidebarWidth))
    }

    function resizeHomeSidebarTo(pointerX) {
        homeSidebarWidth = Math.min(
            maximumExplorerWidth,
            Math.max(
                theme.explorerMinWidth,
                pointerX - theme.activityRailWidth
            )
        )
    }

    function resizeExplorerTo(pointerX) {
        explorerWidth = Math.min(
            maximumExplorerWidth,
            Math.max(
                theme.explorerMinWidth,
                pointerX - theme.activityRailWidth
            )
        )
    }

    function resizeDockTo(pointerY) {
        dockHeight = Math.min(
            maximumDockHeight,
            Math.max(
                theme.chatDockMinHeight,
                height - theme.statusBarHeight - pointerY
            )
        )
    }

    Timer {
        id: workspaceStateSaveTimer

        interval: 220
        repeat: false
        onTriggered: root.saveWorkspaceState()
    }

    Component.onCompleted: {
        switchCollectionWorkspace()
        migrateChatMode(Docking.collectionId)
        if (Shell.homeActive) {
            Qt.callLater(homeView.focusComposer)
        }
    }
    Component.onDestruction: {
        workspaceStateSaveTimer.stop()
        saveWorkspaceState()
        WorkspaceState.sync()
    }

    onActiveViewIndexChanged: scheduleWorkspaceStateSave()

    Connections {
        target: Shell

        function onNavigationChanged() {
            root.scheduleWorkspaceStateSave()
        }
    }
    onExplorerOpenChanged: scheduleWorkspaceStateSave()
    onExplorerWidthChanged: scheduleWorkspaceStateSave()
    onChatModeChanged: reportCommandSurface()
    onDockHeightChanged: scheduleWorkspaceStateSave()
    onDockCollapsedChanged: {
        scheduleWorkspaceStateSave()
        reportCommandSurface()
    }

    // Agents need to know whether the conversation is on screen.
    function reportCommandSurface() {
        // The compact composer is the conversation, open.
        Docking.setCommandSurfaceState(
            compactChat ? false : dockCollapsed,
            compactChat ? "chat" : chatDock.activeView
        )
    }

    // The mode used to be saved by the shell; the store has it now. A
    // project's old choice moves over once.
    function migrateChatMode(collectionId) {
        var scopeId = String(collectionId || "")
        if (scopeId.length === 0) {
            return
        }
        var key = workspaceStateKey(scopeId, "chatMode")
        var missing = "__archivist_missing__"
        var saved = String(WorkspaceState.value(key, missing))
        if (saved === missing) {
            return
        }
        if (String(Docking.composer.chosenMode || "").length === 0) {
            Docking.setComposerMode(saved === "compact" ? "compact" : "workbench")
        }
        WorkspaceState.remove(key)
    }

    Connections {
        target: Docking

        function onCollectionIdChanged() {
            root.migrateChatMode(Docking.collectionId)
        }

        // An agent asked for the chat or terminal ("beside our
        // conversation", "bring up the terminal").
        function onCommandSurfaceRequested(view, open) {
            // The terminal and problems live in the workbench.
            if (open && view !== "chat" && root.compactChat) {
                root.setChatMode("workbench")
            }
            if (view === "chat" || view === "terminal" || view === "problems") {
                chatDock.activeView = view
            }
            root.dockCollapsed = !open
        }
    }

    Connections {
        target: CollectionStore

        function onSelectedCollectionIdChanged() {
            root.switchCollectionWorkspace()
        }

        function onWorkspaceScopeChanged() {
            LibraryStore.refresh()
            ChatStore.refresh()
            Qt.callLater(root.finishPendingOpen)
        }
    }

    color: theme.workspaceBg
    clip: true

    readonly property bool legacyWorkspace: typeof ArchivistDiagnostics !== "undefined"
        && Boolean(ArchivistDiagnostics.legacyWorkspace)
    readonly property real workspaceAreaHeight: height
        - statusBar.height
        - root.workspaceBottomObstruction

    function revealFileInLibrary(libraryId, fileId, relativePath) {
        Shell.showPanel("navigation.navigator")
        explorerDock.revealFile(libraryId, fileId, relativePath)
    }

    // ---- Home ------------------------------------------------------------

    // Back to the workspace with the conversation in front.
    function showConversation() {
        Shell.returnToWorkspace()
        chatDock.activeView = "chat"
        root.dockCollapsed = false
        Qt.callLater(function() {
            var workspace = workspaceLoader.item
            if (workspace && workspace.followSelectedChat) {
                workspace.followSelectedChat()
            }
        })
    }

    function startConversationFromHome(text, libraryId, agentId) {
        if (root.homeSending || String(libraryId || "").length === 0) {
            return
        }
        if (libraryId !== String(LibraryStore.selectedLibraryId || "")) {
            LibraryStore.selectLibrary(libraryId)
        }
        root.homeSending = true
        ChatStore.startChat(
            libraryId,
            String(agentId || CollectionStore.scope.defaultAgentId || ""),
            text
        )
        // Refused before it started (another write in flight): keep the draft.
        if (!ChatStore.mutating) {
            root.homeSending = false
        }
    }

    function openChat(chat) {
        if (!chat || !chat.id) {
            return
        }
        if (ChatStore.responding && String(chat.id) !== String(ChatStore.selectedChatId)) {
            // A reply is streaming; switching now would cut it off screen.
            root.showConversation()
            return
        }
        var libraryId = String(chat.libraryId || "")
        if (libraryId.length > 0 && libraryId !== String(LibraryStore.selectedLibraryId)) {
            LibraryStore.selectLibrary(libraryId)
        }
        if (String(chat.id) !== String(ChatStore.selectedChatId)) {
            LibraryStore.clearFilePreview()
            ChatStore.selectChat(String(chat.id))
        }
        root.showConversation()
    }

    function openChatFromHome(chat, collectionId) {
        var target = String(collectionId || "")
        if (target.length > 0 && target !== String(CollectionStore.selectedCollectionId)) {
            root.pendingChatId = String(chat.id)
            root.pendingDocument = null
            CollectionStore.selectCollection(target)
            Shell.returnToWorkspace()
            return
        }
        root.openChat(chat)
    }

    function openProjectFromHome(collectionId) {
        if (String(collectionId) !== String(CollectionStore.selectedCollectionId)) {
            root.pendingChatId = ""
            root.pendingDocument = null
            CollectionStore.selectCollection(collectionId)
        }
        Shell.returnToWorkspace()
    }

    function openDocument(item) {
        var details = item.details || ({})
        var existing = Docking.viewsForResource("document", String(item.id))
        Shell.returnToWorkspace()
        if (existing.length > 0) {
            Docking.activateView(existing[0])
            return
        }
        var libraryId = String(item.libraryId || "")
        var fileId = String(details.fileId || "")
        var file = libraryId === String(LibraryStore.selectedLibraryId)
            ? ({})
            : ({
                id: fileId,
                name: String(details.name || item.title || ""),
                relativePath: String(details.path || ""),
                extension: String(details.extension || ""),
                mimeType: String(details.mimeType || ""),
                languageId: String(details.languageId || "")
            })
        LibraryStore.previewFileFromLibrary(libraryId, fileId, file)
    }

    function openRecentFromHome(item) {
        if (!item || item.kind !== "document") {
            return
        }
        var target = String(item.collectionId || "")
        if (target.length > 0 && target !== String(CollectionStore.selectedCollectionId)) {
            root.pendingDocument = item
            root.pendingChatId = ""
            CollectionStore.selectCollection(target)
            Shell.returnToWorkspace()
            return
        }
        root.openDocument(item)
    }

    // A project switch from Home finished: open what was asked for there.
    function finishPendingOpen() {
        if (root.pendingChatId.length > 0) {
            var chats = ChatStore.chats || []
            for (var index = 0; index < chats.length; index += 1) {
                if (String(chats[index].id) === root.pendingChatId) {
                    root.pendingChatId = ""
                    root.openChat(chats[index])
                    return
                }
            }
            root.pendingChatId = ""
        }
        if (root.pendingDocument) {
            var item = root.pendingDocument
            root.pendingDocument = null
            root.openDocument(item)
        }
    }

    // A layout chosen on Home: back to the project, presented that way.
    function applyLayoutFromHome(layoutId) {
        if (!CollectionStore.selectedCollectionId) {
            return
        }
        Shell.returnToWorkspace()
        Docking.applyLayout(String(layoutId))
    }

    function addFolderFromHome() {
        Shell.showPanel("navigation.navigator")
        explorerDock.openLibraryFolderDialog()
    }

    function setupProjectFromHome() {
        Shell.showPanel("navigation.navigator")
    }

    RecentActivityTracker {}

    Connections {
        target: ChatStore

        function onChatCreated(chat) {
            if (!root.homeSending) {
                return
            }
            root.homeSending = false
            Shell.homeDraft = ""
            root.showConversation()
        }

        function onErrorMessageChanged() {
            if (root.homeSending && String(ChatStore.errorMessage || "").length > 0) {
                root.homeSending = false
            }
        }

        function onMutatingChanged() {
            // Finished without a chat (the write failed): the draft stays.
            if (root.homeSending && !ChatStore.mutating) {
                Qt.callLater(function() {
                    if (root.homeSending && !ChatStore.mutating) {
                        root.homeSending = false
                    }
                })
            }
        }
    }

    // The Motion panel: tune how the app moves, live.
    Shortcut {
        objectName: "motion.tuner"
        sequences: ["Ctrl+Alt+Shift+M"]
        context: Qt.ApplicationShortcut
        onActivated: root.motionTunerOpen = !root.motionTunerOpen
    }
    property bool motionTunerOpen: false

    Loader {
        active: root.motionTunerOpen
        anchors.right: parent.right
        anchors.rightMargin: 16
        anchors.bottom: parent.bottom
        anchors.bottomMargin: statusBar.height + 16
        z: 200

        sourceComponent: MotionTuner {
            theme: root.theme
            onToggleSidebarRequested: Shell.setPanelOpen(!Shell.panelOpen)
            onToggleDockRequested: root.toggleChatMode()
            onCloseRequested: root.motionTunerOpen = false
        }
    }

    Shortcut {
        sequences: ["Ctrl+Shift+H"]
        context: Qt.ApplicationShortcut
        onActivated: Shell.toggleHome()
    }

    // composer.toggle: expand into the workbench or collapse to chat.
    Shortcut {
        objectName: "composer.toggle"
        sequences: ["Ctrl+J"]
        context: Qt.ApplicationShortcut
        enabled: !Shell.homeActive
        onActivated: root.toggleChatMode()
    }

    // composer.restore: back to the bottom, in the space's default mode.
    Shortcut {
        objectName: "composer.restore"
        sequences: ["Ctrl+Shift+J"]
        context: Qt.ApplicationShortcut
        enabled: !Shell.homeActive
        onActivated: root.restoreComposer()
    }

    onWorkspaceHiddenChanged: {
        if (!workspaceHidden) {
            return
        }
        Qt.callLater(function() {
            if (Shell.homeActive) {
                homeView.focusComposer()
            }
        })
    }

    Loader {
        id: workspaceLoader

        x: 0
        y: 0
        width: parent.width
        height: root.workspaceAreaHeight
        sourceComponent: root.legacyWorkspace ? legacyWorkspaceComponent : dockWorkspaceComponent
        visible: !root.workspaceHidden
        // No motion of its own: it follows the dock's animated edge.
    }

    Component {
        id: dockWorkspaceComponent

        DockWorkspace {
            theme: root.theme
            // The sidebar's edge as drawn, every frame of its animation.
            leftInset: root.explorerRight
            bottomOverlay: root.compactChat ? root.compactOverlay : 0
            composerAnchorId: root.composerAnchorId
            overlayHost: floatingHost
            // The Gravity Well keeps clear of the composer.
            composerRect: compactComposer.visible && compactComposer.opacity > 0.5
                ? Qt.rect(compactComposer.x, compactComposer.y, compactComposer.width, compactComposer.height)
                : Qt.rect(0, 0, 0, 0)
            onContextInspectionRequested: function(messageId) {
                artifactDrawer.openForMessage(messageId)
            }
            onRevealInLibraryRequested: function(libraryId, fileId, relativePath) {
                root.revealFileInLibrary(libraryId, fileId, relativePath)
            }
        }
    }

    Component {
        id: legacyWorkspaceComponent

        Workspace {
            theme: root.theme
            leftObstruction: root.workspaceLeftObstruction
            previewLeftObstruction: root.workspacePreviewLeftObstruction
            onContextInspectionRequested: function(messageId) {
                artifactDrawer.openForMessage(messageId)
            }
            onRevealInLibraryRequested: function(libraryId, fileId, relativePath) {
                root.revealFileInLibrary(libraryId, fileId, relativePath)
            }
        }
    }

    // Floating panels from the docking workspace, and what a drag carries:
    // above the sidebar, the composer and the drawers.
    Item {
        id: floatingHost

        objectName: "shell.floatingHost"
        x: 0
        y: 0
        width: parent.width
        height: parent.height - statusBar.height
        visible: !root.workspaceHidden
        z: 65
    }

    ActivityRail {
        id: activityRail

        x: 0
        y: 0
        height: parent.height - statusBar.height
        theme: root.theme
        z: 20
    }

    HomeView {
        id: homeView

        x: root.theme.activityRailWidth
        y: 0
        width: parent.width - root.theme.activityRailWidth
        height: parent.height - statusBar.height
        theme: root.theme
        sending: root.homeSending
        visible: root.homeReveal > 0
        reveal: root.homeReveal
        leftInset: Math.max(0, root.homeSidebarExtent)
        sidebarShown: root.homeSidebarOpen
        enabled: Shell.homeActive
        z: 40

        onStartConversationRequested: function(text, libraryId, agentId) {
            root.startConversationFromHome(text, libraryId, agentId)
        }
        onLayoutRequested: function(layoutId) {
            root.applyLayoutFromHome(layoutId)
        }
        onChatRequested: function(chat, collectionId) {
            root.openChatFromHome(chat, collectionId)
        }
        onProjectRequested: function(collectionId) {
            root.openProjectFromHome(collectionId)
        }
        onRecentRequested: function(item) {
            root.openRecentFromHome(item)
        }
        onAddFolderRequested: root.addFolderFromHome()
        onSetupProjectRequested: root.setupProjectFromHome()
        onBackRequested: Shell.returnToWorkspace()
        onSidebarRequested: function(allProjects) {
            root.setHomeSidebarOpen(true)
            if (allProjects) {
                homeSidebar.allProjects = true
            }
        }
    }

    HomeSidebar {
        id: homeSidebar

        x: root.theme.activityRailWidth
        y: 0
        width: root.homeSidebarExtent
        height: parent.height - statusBar.height
        visible: width > 0.5 && root.homeReveal > 0
        opacity: root.homeReveal
        enabled: Shell.homeActive
        clip: true
        theme: root.theme
        home: homeView
        z: 45

        onNewChatRequested: {
            Shell.homeDraft = ""
            homeView.focusComposer()
        }
        onChatRequested: function(chat, collectionId) {
            root.openChatFromHome(chat, collectionId)
        }
        onProjectRequested: function(collectionId) {
            homeView.diveCollectionId = collectionId
            root.openProjectFromHome(collectionId)
        }
        onPinOpenRequested: function(pin) {
            homeView.openLink(pin)
        }
        onSetupProjectRequested: root.setupProjectFromHome()
        onCloseRequested: root.setHomeSidebarOpen(false)
    }

    SidePanelResizeHandle {
        id: homeSidebarResizeHandle

        x: root.theme.activityRailWidth + root.homeSidebarExtent - width / 2
        y: 0
        height: parent.height - statusBar.height
        visible: Shell.homeActive && root.homeSidebarOpen
        theme: root.theme
        coordinateItem: root
        z: 46
        onResizeRequested: function(pointerX) {
            root.resizeHomeSidebarTo(pointerX)
        }
        onResetRequested: root.resetHomeSidebarWidth()
    }

    // The sidebar slides out from behind the rail at its full width, so its
    // rows never reflow while it opens or closes; this slot only reveals
    // as much of it as is out.
    Item {
        id: explorerSlot

        x: theme.activityRailWidth
        y: 0
        width: Math.max(0, root.explorerExtent)
        height: parent.height - statusBar.height
        visible: !root.workspaceHidden
        clip: true
        z: 15

        ExplorerDock {
            id: explorerDock

            // Past its width (a bouncy open overshooting) it stretches a
            // little rather than leave a gap by the rail.
            x: Math.min(0, explorerSlot.width - width)
            y: 0
            width: Math.max(root.clampedExplorerWidth, explorerSlot.width)
            height: parent.height
            // Drawn once at startup even when closed (see explorerWarm), so
            // the first open has nothing to build.
            visible: explorerSlot.width > 0.5 || root.explorerWarm
            opacity: root.explorerOpen || root.explorerWarm ? 1 : 0
            theme: root.theme
            activeViewIndex: root.activeViewIndex
            dockWorkspace: !root.legacyWorkspace && workspaceLoader.item && workspaceLoader.item.registerSlotZone ? workspaceLoader.item : null
            onCloseRequested: Shell.setPanelOpen(false)

            Behavior on opacity {
                NumberAnimation { duration: root.theme.motionFast }
            }
        }
    }

    // For the first moments after launch the sidebar is drawn (out of
    // sight) whether or not it's open: its rows, icons and text are built
    // and uploaded then, not on the frame it's first opened.
    property bool explorerWarm: true
    Timer {
        interval: 1200
        running: true
        onTriggered: root.explorerWarm = false
    }

    SidePanelResizeHandle {
        id: explorerResizeHandle

        x: root.explorerRight - width / 2
        y: 0
        height: parent.height - statusBar.height
        visible: root.explorerOpen && !root.workspaceHidden
        theme: root.theme
        coordinateItem: root
        z: 25
        onResizeRequested: function(pointerX) {
            root.resizeExplorerTo(pointerX)
        }
        onResetRequested: root.resetExplorerWidth()
    }

    WorkbenchDock {
        id: chatDock

        x: root.attachedDockX
        y: parent.height - statusBar.height - height
        width: parent.width - root.attachedDockX
        height: root.clampedDockHeight
        theme: root.theme
        collectionId:
            CollectionStore.selectedCollectionId
        collapsed: root.dockCollapsed
        // Fades out first as it folds into the compact composer, and back
        // in last as it unfolds.
        opacity: 1 - root.phase(root.compactEase, 0, 0.4)
        visible: !root.workspaceHidden && root.compactEase < 1
        enabled: !root.compactChat
        z: 30

        onCompactRequested: root.setChatMode("compact")
        onOptionsRequested: function(source, x, y) {
            root.openComposerMenu(source, x, y)
        }
        onCollapseToggleRequested: root.dockCollapsed = !root.dockCollapsed
        onExpandRequested: root.dockCollapsed = false
        onActiveViewChanged: root.reportCommandSurface()
        Component.onCompleted: root.reportCommandSurface()
        onMessageSubmitted: function(message) {
            ChatStore.sendMessage(message)
        }

        Behavior on height {
            enabled: !root.resizingDock

            NumberAnimation {
                duration: root.theme.motionPanel
                easing.type: Easing.BezierSpline
                easing.bezierCurve: root.theme.layoutCurve
            }
        }
    }

    Item {
        id: dockResizeHandle

        x: chatDock.x
        y: chatDock.y - height / 2
        width: chatDock.width
        height: root.theme.resizeHandleThickness
        visible: !root.dockCollapsed && !root.workspaceHidden && root.compactEase === 0
        z: 35

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width
            height: dockResizeArea.containsMouse || dockResizeArea.pressed ? 2 : 1
            color: dockResizeArea.containsMouse || dockResizeArea.pressed
                ? root.theme.accent
                : root.theme.panelBorder
            opacity: dockResizeArea.containsMouse || dockResizeArea.pressed
                ? 0.9
                : 0.58

            Behavior on color {
                ColorAnimation { duration: root.theme.motionFast }
            }
        }

        MouseArea {
            id: dockResizeArea

            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.SplitVCursor
            onPressed: root.resizingDock = true
            onReleased: root.resizingDock = false
            onCanceled: root.resizingDock = false
            onPositionChanged: function(mouse) {
                if (!pressed) {
                    return
                }

                const point = mapToItem(root, mouse.x, mouse.y)
                root.resizeDockTo(point.y)
            }
            onDoubleClicked: root.resetDockHeight()
        }
    }

    // The surface between the two chat modes: the dock's panel shrinking
    // into the composer's card (or growing out of it). Only shown while
    // the morph runs; each side fades its content over it.
    Rectangle {
        id: chatMorph

        readonly property real t: root.compactEase

        visible: !root.workspaceHidden && t > 0 && t < 1
        x: root.lerp(chatDock.x, root.compactX, t)
        y: root.lerp(chatDock.y, root.compactY, t)
        width: root.lerp(chatDock.width, root.compactWidth, t)
        height: root.lerp(chatDock.height, root.compactHeight, t)
        radius: root.lerp(0, root.theme.homeComposerRadius, t)
        color: Qt.rgba(
            root.lerp(root.theme.surfaceBg.r, compactComposer.surfaceColor.r, t),
            root.lerp(root.theme.surfaceBg.g, compactComposer.surfaceColor.g, t),
            root.lerp(root.theme.surfaceBg.b, compactComposer.surfaceColor.b, t),
            1
        )
        border.width: 1
        border.color: root.theme.compactComposerBorder
        // Above the workspace, below the side panel (15) and the dock (30).
        z: 12
    }

    CompactChatComposer {
        id: compactComposer

        x: root.compactX
        y: root.compactY
        width: root.compactWidth
        theme: root.theme
        opacity: root.phase(root.compactEase, 0.55, 1)
        // Settles into place as it appears.
        scale: root.lerp(0.985, 1, root.phase(root.compactEase, 0.55, 1))
        transformOrigin: Item.Bottom
        visible: !root.workspaceHidden && root.compactEase > 0
        enabled: root.compactChat
        // Below the side panel (15): the sidebar covers the bubble and its
        // shadow as it moves, never the other way round.
        z: 13

        targetChatId: root.compactDocked ? root.dockedFrame.chatId : ""
        targetChatTitle: root.compactDocked && workspaceLoader.item && workspaceLoader.item.chatTitle
            ? workspaceLoader.item.chatTitle(root.dockedFrame.chatId)
            : ""

        onExpandRequested: root.setChatMode("workbench")
        onOptionsRequested: function(source, x, y) {
            root.openComposerMenu(source, x, y)
        }
        // Docked, it talks to the chat its anchor shows.
        onActivated: {
            if (compactComposer.targetPending && workspaceLoader.item && workspaceLoader.item.makeChatLive) {
                workspaceLoader.item.makeChatLive(root.dockedFrame.viewId)
            }
        }

        Behavior on x { enabled: root.composerMoving; NumberAnimation { duration: Math.round(220 * root.theme.motionSpeed); easing.type: Easing.OutCubic } }
        Behavior on y { enabled: root.composerMoving; NumberAnimation { duration: Math.round(220 * root.theme.motionSpeed); easing.type: Easing.OutCubic } }
        Behavior on width { enabled: root.composerMoving; NumberAnimation { duration: Math.round(220 * root.theme.motionSpeed); easing.type: Easing.OutCubic } }
    }

    // Where the composer sits and what it is. Each item's objectName is
    // its command id (Docking.composerCommands).
    DockMenu {
        id: composerMenu

        theme: root.theme
        width: 268
        // Never cut off by the window's edge.
        margins: 8
        // Opened upward: its bottom stays here as its items lay out.
        property real upwardFrom: 0
        onHeightChanged: {
            if (upwardFrom < 0 && visible) {
                y = upwardFrom - height
            }
        }

        DockMenuItem {
            objectName: "composer.place.anchor"
            theme: root.theme
            text: "Dock in conversation"
            // From the workbench too: it collapses into the conversation.
            visible: root.conversationAnchorId().length > 0
                && !(root.compactChat && root.compactDocked && root.composerAnchorId === root.conversationAnchorId())
            onTriggered: root.dockComposerInConversation()
        }
        DockMenuItem {
            objectName: "composer.place.bottom"
            theme: root.theme
            text: "Move to bottom"
            visible: root.compactChat && root.compactPlacement.kind !== "bottom"
            onTriggered: root.moveComposerToBottom()
        }
        DockMenuItem {
            objectName: "composer.expand"
            theme: root.theme
            text: "Expand workbench"
            shortcutText: Qt.platform.os === "osx" ? "⌘J" : "Ctrl+J"
            visible: root.compactChat
            onTriggered: root.setChatMode("workbench")
        }
        DockMenuItem {
            objectName: "composer.collapse"
            theme: root.theme
            text: "Collapse to chat"
            shortcutText: Qt.platform.os === "osx" ? "⌘J" : "Ctrl+J"
            visible: !root.compactChat
            onTriggered: root.setChatMode("compact")
        }
        // Modes a plugin registered, beside the two built in.
        Instantiator {
            model: (Docking.composerModes || []).filter(function(mode) { return !mode.builtIn })

            delegate: DockMenuItem {
                required property var modelData

                objectName: "composer.mode." + modelData.id
                theme: root.theme
                text: modelData.label
                checkable: true
                checked: root.chatMode === modelData.id
                onTriggered: {
                    root.setChatMode(modelData.id)
                    // The store decides; keep showing what it says.
                    checked = Qt.binding(function() { return root.chatMode === modelData.id })
                }
            }

            onObjectAdded: function(index, object) { composerMenu.insertItem(4 + index, object) }
            onObjectRemoved: function(index, object) { composerMenu.removeItem(object) }
        }
        DockMenuSeparator { theme: root.theme }
        DockMenuItem {
            objectName: "composer.remember"
            theme: root.theme
            text: "Keep this view for this conversation"
            checkable: true
            checked: root.modeRemembered
            enabled: String(ChatStore.selectedChatId || "").length > 0
            onTriggered: {
                root.rememberModeForConversation(!root.modeRemembered)
                checked = Qt.binding(function() { return root.modeRemembered })
            }
        }
        DockMenuItem {
            objectName: "composer.restore"
            theme: root.theme
            text: "Restore default position"
            shortcutText: Qt.platform.os === "osx" ? "⇧⌘J" : "Ctrl+Shift+J"
            onTriggered: root.restoreComposer()
        }
    }

    MouseArea {
        id: artifactDrawerDismissArea

        anchors.fill: parent
        visible: artifactDrawer.open
        acceptedButtons: Qt.LeftButton
        z: 55
        onClicked: artifactDrawer.open = false
    }

    ArtifactDrawer {
        id: artifactDrawer

        x: parent.width - width - 9
        y: Math.max(18, (parent.height - statusBar.height - height) / 2)
        theme: root.theme
        // Part of the workspace: it steps aside with it for Home.
        visible: root.homeReveal < 1
        opacity: 1 - root.homeReveal
        z: 60
    }

    StatusBar {
        id: statusBar

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        theme: root.theme
        filePreviewActive:
            workspaceLoader.item ? workspaceLoader.item.filePreviewStatusVisible : false
        fileAccessLabel:
            workspaceLoader.item ? workspaceLoader.item.filePreviewAccessLabel : ""
        fileTypeLabel:
            workspaceLoader.item ? workspaceLoader.item.filePreviewTypeLabel : ""
        filePath:
            workspaceLoader.item ? workspaceLoader.item.filePreviewPath : ""
        fileMetrics:
            workspaceLoader.item ? workspaceLoader.item.filePreviewMetrics : ""
        fileAttached:
            workspaceLoader.item ? workspaceLoader.item.filePreviewAttached : false
        librarySelected:
            LibraryStore.selectedLibraryId.length > 0
        libraryFilesLoading:
            LibraryStore.loadingFiles
        libraryFileCount:
            LibraryStore.files.length
        z: 50
    }
}

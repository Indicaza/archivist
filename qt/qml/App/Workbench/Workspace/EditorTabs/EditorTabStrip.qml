import QtQuick
import QtQuick.Controls
import Archivist.Services 1.0

Item {
    id: root

    required property var theme

    readonly property bool hasTabs: tabs.count > 0
    property string activeTabKey: ""
    property string pendingLibraryId: ""
    property string pendingFileId: ""
    property int hoveredTabIndex: -1
    property bool tabDragActive: false
    property string draggedTabKey: ""
    property int tabDropSlot: -1
    property real tabDragPointerX: 0
    property real tabDragGrabOffsetX: 0
    property real tabDragGhostWidth: 0
    property var draggedTabData: ({})
    property bool tabDropSettling: false
    property string settlingTabKey: ""
    property int settlingDestinationIndex: -1
    property bool settlingCommitMove: false
    property real settlingTabRootX: 0
    property bool tabStateRestored: false
    property bool restoringTabState: false
    property bool restoreActiveTabPending: false
    property bool libraryCatalogObserved: false
    property bool chatCatalogObserved: false
    property string workspaceScopeId: ""
    property bool collectionWorkspaceSwitching: false
    property bool collectionScopeReady: false
    readonly property color activeContourColor: "#5f5a52"

    visible: hasTabs
    clip: false

    function fileKey(libraryId, fileId) {
        return "file:" + String(libraryId || "") + ":" + String(fileId || "")
    }

    function chatKey(chatId) {
        return "chat:" + String(chatId || "")
    }

    function tabIndexForKey(key) {
        for (var index = 0; index < tabs.count; index += 1) {
            if (String(tabs.get(index).tabKey) === String(key || "")) {
                return index
            }
        }

        return -1
    }

    function newTabInsertionIndex() {
        var activeIndex = tabIndexForKey(activeTabKey)
        return activeIndex >= 0 ? activeIndex + 1 : tabs.count
    }

    function tabSnapshot(index) {
        if (index < 0 || index >= tabs.count) {
            return ({})
        }

        var tab = tabs.get(index)
        return {
            tabKey: String(tab.tabKey || ""),
            tabType: String(tab.tabType || ""),
            fileId: String(tab.fileId || ""),
            libraryId: String(tab.libraryId || ""),
            chatId: String(tab.chatId || ""),
            title: String(tab.title || "Untitled"),
            relativePath: String(tab.relativePath || ""),
            libraryName: String(tab.libraryName || ""),
            extension: String(tab.extension || ""),
            sizeBytes: Number(tab.sizeBytes || 0),
            lineCount: Number(tab.lineCount || 0),
            agentCount: Number(tab.agentCount || 0)
        }
    }

    function scopeIdForCollection(collectionId) {
        return String(collectionId || "")
    }

    function workspaceStateKey(scopeId, suffix) {
        return "workspace/collections/"
            + String(scopeId || "")
            + "/"
            + String(suffix || "")
    }

    function tabStateArray() {
        var state = []

        for (var index = 0; index < tabs.count; index += 1) {
            state.push(tabSnapshot(index))
        }

        return state
    }

    function scheduleTabStateSave() {
        if (
            !tabStateRestored
            || restoringTabState
            || workspaceScopeId.length === 0
        ) {
            return
        }

        tabStateSaveTimer.restart()
    }

    function saveTabState() {
        if (
            !tabStateRestored
            || restoringTabState
            || workspaceScopeId.length === 0
        ) {
            return
        }

        WorkspaceState.setValue(
            workspaceStateKey(workspaceScopeId, "editorTabs"),
            JSON.stringify(tabStateArray())
        )
        WorkspaceState.setValue(
            workspaceStateKey(workspaceScopeId, "activeTabKey"),
            activeTabKey
        )
    }

    function restoreTabState(scopeId) {
        var targetScopeId = String(scopeId || "")

        if (targetScopeId.length === 0) {
            return
        }

        restoringTabState = true

        var stateKey = workspaceStateKey(targetScopeId, "editorTabs")
        var activeKey = workspaceStateKey(targetScopeId, "activeTabKey")
        var missingValue = "__archivist_missing__"
        var rawStateValue = WorkspaceState.value(stateKey, missingValue)
        var savedActiveValue = WorkspaceState.value(activeKey, missingValue)
        var migrateLegacyState = String(rawStateValue) === missingValue
            && !Boolean(
                WorkspaceState.value(
                    "workspace/collectionTabsMigrated",
                    false
                )
            )

        if (migrateLegacyState) {
            rawStateValue = WorkspaceState.value(
                "workspace/editorTabs",
                "[]"
            )
            savedActiveValue = WorkspaceState.value(
                "workspace/activeTabKey",
                ""
            )
        } else {
            if (String(rawStateValue) === missingValue) {
                rawStateValue = "[]"
            }
            if (String(savedActiveValue) === missingValue) {
                savedActiveValue = ""
            }
        }

        workspaceScopeId = targetScopeId

        var rawState = String(rawStateValue || "[]")
        var savedTabs = []

        try {
            savedTabs = JSON.parse(rawState)
        } catch (error) {
            savedTabs = []
        }

        if (!Array.isArray(savedTabs)) {
            savedTabs = []
        }

        tabs.clear()

        for (var index = 0; index < savedTabs.length && index < 100; index += 1) {
            var savedTab = savedTabs[index] || ({})
            var tabType = String(savedTab.tabType || "")
            var tabKey = String(savedTab.tabKey || "")

            if (
                (tabType !== "file" && tabType !== "chat")
                || tabKey.length === 0
                || tabIndexForKey(tabKey) >= 0
            ) {
                continue
            }

            tabs.append({
                tabKey: tabKey,
                tabType: tabType,
                fileId: String(savedTab.fileId || ""),
                libraryId: String(savedTab.libraryId || ""),
                chatId: String(savedTab.chatId || ""),
                title: String(savedTab.title || "Untitled"),
                relativePath: String(savedTab.relativePath || ""),
                libraryName: String(savedTab.libraryName || ""),
                extension: String(savedTab.extension || ""),
                sizeBytes: Number(savedTab.sizeBytes || 0),
                lineCount: Number(savedTab.lineCount || 0),
                agentCount: Number(savedTab.agentCount || 0)
            })
        }

        var savedActiveKey = String(savedActiveValue || "")

        if (tabIndexForKey(savedActiveKey) >= 0) {
            activeTabKey = savedActiveKey
        } else if (tabs.count > 0) {
            activeTabKey = String(tabs.get(0).tabKey || "")
        } else {
            activeTabKey = ""
        }

        restoreActiveTabPending = tabs.count > 0
        restoringTabState = false
        tabStateRestored = true

        if (migrateLegacyState) {
            saveTabState()
            WorkspaceState.setValue(
                "workspace/collectionTabsMigrated",
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
            || (
                tabStateRestored
                && nextScopeId === workspaceScopeId
            )
        ) {
            return
        }

        if (tabStateRestored) {
            tabStateSaveTimer.stop()
            saveTabState()
        }

        collectionWorkspaceSwitching = true
        collectionScopeReady = false
        libraryCatalogObserved = false
        chatCatalogObserved = false
        restoreActiveTabPending = false
        pendingLibraryId = ""
        pendingFileId = ""
        LibraryStore.clearFilePreview()
        restoreTabState(nextScopeId)
    }

    function finishCollectionWorkspaceSwitch() {
        collectionWorkspaceSwitching = false
    }

    function collectionWorkspaceScopeReady() {
        var selectedScopeId = scopeIdForCollection(
            CollectionStore.selectedCollectionId
        )

        if (
            selectedScopeId.length === 0
            || selectedScopeId !== workspaceScopeId
        ) {
            return
        }

        collectionScopeReady = true

        if (tabs.count === 0) {
            finishCollectionWorkspaceSwitch()
            return
        }

        Qt.callLater(root.tryRestoreActiveTab)
    }

    function listContainsId(values, id) {
        var targetId = String(id || "")
        var source = values || []

        for (var index = 0; index < source.length; index += 1) {
            if (String(source[index].id || "") === targetId) {
                return true
            }
        }

        return false
    }

    function tryRestoreActiveTab() {
        if (!restoreActiveTabPending || !collectionScopeReady) {
            return
        }

        var index = tabIndexForKey(activeTabKey)

        if (index < 0) {
            restoreActiveTabPending = false
            return
        }

        var tab = tabs.get(index)

        if (String(tab.tabType) === "chat") {
            if (ChatStore.loadingChats) {
                return
            }

            if (!listContainsId(ChatStore.chats, tab.chatId)) {
                if (chatCatalogObserved) {
                    restoreActiveTabPending = false
                }
                return
            }

            restoreActiveTabPending = false
            LibraryStore.clearFilePreview()

            if (String(ChatStore.selectedChatId) !== String(tab.chatId)) {
                ChatStore.selectChat(String(tab.chatId))
            }
        } else {
            if (LibraryStore.loadingLibraries) {
                return
            }

            if (!listContainsId(LibraryStore.libraries, tab.libraryId)) {
                if (libraryCatalogObserved) {
                    restoreActiveTabPending = false
                }
                return
            }

            if (String(LibraryStore.selectedLibraryId) !== String(tab.libraryId)) {
                LibraryStore.selectLibrary(String(tab.libraryId))
                return
            }

            if (LibraryStore.loadingFiles) {
                return
            }

            if (!listContainsId(LibraryStore.files, tab.fileId)) {
                restoreActiveTabPending = false
                return
            }

            restoreActiveTabPending = false
            LibraryStore.previewFile(String(tab.fileId))
        }

        Qt.callLater(function() {
            var activeIndex = root.tabIndexForKey(root.activeTabKey)
            if (activeIndex >= 0 && activeIndex < tabList.count) {
                tabList.positionViewAtIndex(activeIndex, ListView.Contain)
            }
        })
    }

    function tabContentXFromRootX(rootX) {
        return root.mapToItem(tabList.contentItem, rootX, 0).x
    }

    function tabPreviewDestinationIndex() {
        if (!tabDragActive || tabDropSlot < 0 || tabs.count === 0) {
            return -1
        }

        var fromIndex = tabIndexForKey(draggedTabKey)
        var destinationIndex = tabDropSlot

        if (fromIndex < 0) {
            return -1
        }

        if (fromIndex < destinationIndex) {
            destinationIndex -= 1
        }

        return Math.max(0, Math.min(tabs.count - 1, destinationIndex))
    }

    function tabShuffleOffsetForIndex(index) {
        if (!tabDragActive || index < 0 || tabs.count < 2) {
            return 0
        }

        var fromIndex = tabIndexForKey(draggedTabKey)
        var destinationIndex = tabPreviewDestinationIndex()
        var draggedSpan = Math.max(0, tabDragGhostWidth + tabList.spacing)

        if (fromIndex < 0 || destinationIndex < 0 || destinationIndex === fromIndex) {
            return 0
        }

        if (
            destinationIndex > fromIndex
            && index > fromIndex
            && index <= destinationIndex
        ) {
            return -draggedSpan
        }

        if (
            destinationIndex < fromIndex
            && index >= destinationIndex
            && index < fromIndex
        ) {
            return draggedSpan
        }

        return 0
    }

    function updateTabDropSlot(contentX) {
        if (!tabDragActive || tabs.count === 0) {
            tabDropSlot = -1
            return
        }

        var sourceIndex = tabIndexForKey(draggedTabKey)
        var sourceItem = tabList.itemAtIndex(sourceIndex)

        if (sourceIndex < 0 || !sourceItem) {
            tabDropSlot = -1
            return
        }

        var draggedCenter = contentX
            - tabDragGrabOffsetX
            + tabDragGhostWidth / 2
        var sourceCenter = sourceItem.x + sourceItem.width / 2
        var currentDestination = tabPreviewDestinationIndex()
        var destinationIndex = sourceIndex
        var hysteresis = 12

        if (draggedCenter > sourceCenter) {
            for (var rightIndex = sourceIndex + 1; rightIndex < tabs.count; rightIndex += 1) {
                var rightItem = tabList.itemAtIndex(rightIndex)

                if (!rightItem) {
                    continue
                }

                var rightCenter = rightItem.x + rightItem.width / 2
                var rightForgiveness = Math.min(
                    58,
                    Math.max(30, rightItem.width * 0.34)
                )
                var rightThreshold = rightCenter - rightForgiveness

                if (currentDestination >= rightIndex) {
                    rightThreshold -= hysteresis
                }

                if (draggedCenter >= rightThreshold) {
                    destinationIndex = rightIndex
                } else {
                    break
                }
            }
        } else if (draggedCenter < sourceCenter) {
            for (var leftIndex = sourceIndex - 1; leftIndex >= 0; leftIndex -= 1) {
                var leftItem = tabList.itemAtIndex(leftIndex)

                if (!leftItem) {
                    continue
                }

                var leftCenter = leftItem.x + leftItem.width / 2
                var leftForgiveness = Math.min(
                    58,
                    Math.max(30, leftItem.width * 0.34)
                )
                var leftThreshold = leftCenter + leftForgiveness

                if (currentDestination >= 0 && currentDestination <= leftIndex) {
                    leftThreshold += hysteresis
                }

                if (draggedCenter <= leftThreshold) {
                    destinationIndex = leftIndex
                } else {
                    break
                }
            }
        }

        tabDropSlot = destinationIndex > sourceIndex
            ? destinationIndex + 1
            : destinationIndex
    }

    function beginTabDrag(index, pointerRootX, pointerContentX, grabOffsetX, width) {
        if (tabDragActive || index < 0 || index >= tabs.count) {
            return
        }

        draggedTabData = tabSnapshot(index)
        draggedTabKey = String(draggedTabData.tabKey || "")
        tabDragPointerX = pointerRootX
        tabDragGrabOffsetX = grabOffsetX
        tabDragGhostWidth = width
        tabDragActive = true
        hoveredTabIndex = -1
        updateTabDropSlot(pointerContentX)
        forceActiveFocus()
    }

    function updateTabDrag(pointerRootX, pointerContentX) {
        if (!tabDragActive) {
            return
        }

        tabDragPointerX = pointerRootX
        updateTabDropSlot(pointerContentX)
    }

    function tabSettleTargetRootX(fromIndex, destinationIndex) {
        var sourceItem = tabList.itemAtIndex(fromIndex)

        if (!sourceItem || destinationIndex < 0) {
            return tabDragPointerX - tabDragGrabOffsetX
        }

        var targetContentX = sourceItem.x

        if (destinationIndex > fromIndex) {
            var lastShiftedItem = tabList.itemAtIndex(destinationIndex)

            if (lastShiftedItem) {
                var sourceSpan = sourceItem.width + tabList.spacing
                targetContentX = lastShiftedItem.x
                    + lastShiftedItem.width
                    + tabList.spacing
                    - sourceSpan
            }
        } else if (destinationIndex < fromIndex) {
            var firstShiftedItem = tabList.itemAtIndex(destinationIndex)

            if (firstShiftedItem) {
                targetContentX = firstShiftedItem.x
            }
        }

        return tabList.contentItem.mapToItem(root, targetContentX, 0).x
    }

    function completeTabSettle() {
        if (!tabDropSettling) {
            return
        }

        var fromIndex = tabIndexForKey(settlingTabKey)
        var destinationIndex = settlingDestinationIndex
        var shouldMove = settlingCommitMove
            && fromIndex >= 0
            && destinationIndex >= 0
            && destinationIndex !== fromIndex

        if (shouldMove) {
            tabs.move(fromIndex, destinationIndex, 1)
            scheduleTabStateSave()
        }

        tabDragActive = false
        tabDropSettling = false
        draggedTabKey = ""
        tabDropSlot = -1
        tabDragPointerX = 0
        tabDragGrabOffsetX = 0
        tabDragGhostWidth = 0
        draggedTabData = ({})
        settlingTabKey = ""
        settlingDestinationIndex = -1
        settlingCommitMove = false
        settlingTabRootX = 0
    }

    function finishTabDrag(commitMove) {
        if (!tabDragActive || tabDropSettling) {
            return
        }

        var fromIndex = tabIndexForKey(draggedTabKey)

        if (fromIndex < 0) {
            tabDropSettling = true
            completeTabSettle()
            return
        }

        var destinationIndex = fromIndex

        if (tabDropSlot >= 0 && commitMove) {
            destinationIndex = tabDropSlot

            if (fromIndex < destinationIndex) {
                destinationIndex -= 1
            }

            destinationIndex = Math.max(
                0,
                Math.min(tabs.count - 1, destinationIndex)
            )
        }

        var heldTabRootX = tabDragPointerX - tabDragGrabOffsetX
        var targetRootX = tabSettleTargetRootX(fromIndex, destinationIndex)

        tabDropSettling = true
        settlingTabKey = draggedTabKey
        settlingDestinationIndex = destinationIndex
        settlingCommitMove = commitMove
        settlingTabRootX = heldTabRootX

        if (Math.abs(targetRootX - heldTabRootX) < 0.5) {
            completeTabSettle()
            return
        }

        tabSettleAnimation.from = heldTabRootX
        tabSettleAnimation.to = targetRootX
        tabSettleAnimation.start()
    }

    function activeTabType() {
        var index = tabIndexForKey(activeTabKey)
        return index >= 0 ? String(tabs.get(index).tabType || "") : ""
    }

    function formattedSize(bytes) {
        var value = Number(bytes || 0)

        if (value < 1024) {
            return String(value) + " B"
        }

        if (value < 1024 * 1024) {
            return (value / 1024).toFixed(value < 10 * 1024 ? 1 : 0) + " KB"
        }

        return (value / (1024 * 1024)).toFixed(1) + " MB"
    }

    function glyphFor(tabType, extension) {
        if (tabType === "chat") {
            return "▱"
        }

        var suffix = String(extension || "").toLowerCase()

        if ([".ts", ".tsx", ".js", ".jsx", ".json", ".cpp", ".h", ".hpp", ".qml"].indexOf(suffix) >= 0) {
            return "{}"
        }

        if ([".md", ".txt", ".log", ".css", ".html"].indexOf(suffix) >= 0) {
            return "▤"
        }

        if ([".fbx", ".obj", ".gltf", ".glb", ".usd", ".usdz"].indexOf(suffix) >= 0) {
            return "◇"
        }

        return "·"
    }

    function captureSelectedFile() {
        var file = LibraryStore.selectedFile || ({})
        var library = LibraryStore.selectedLibrary || ({})
        var fileId = String(file.id || LibraryStore.selectedFileId || "")
        var libraryId = String(library.id || LibraryStore.selectedLibraryId || "")

        if (fileId.length === 0 || libraryId.length === 0) {
            return
        }

        var key = fileKey(libraryId, fileId)
        var index = tabIndexForKey(key)
        var title = String(file.name || file.relativePath || "Library file")
        var relativePath = String(file.relativePath || title)
        var libraryName = String(library.name || "Library")
        var extension = String(file.extension || "")
        var sizeBytes = Number(file.sizeBytes || 0)

        if (index < 0) {
            index = newTabInsertionIndex()
            tabs.insert(index, {
                tabKey: key,
                tabType: "file",
                fileId: fileId,
                libraryId: libraryId,
                chatId: "",
                title: title,
                relativePath: relativePath,
                libraryName: libraryName,
                extension: extension,
                sizeBytes: sizeBytes,
                lineCount: 0,
                agentCount: 0
            })
        } else {
            tabs.setProperty(index, "title", title)
            tabs.setProperty(index, "relativePath", relativePath)
            tabs.setProperty(index, "libraryName", libraryName)
            tabs.setProperty(index, "extension", extension)
            tabs.setProperty(index, "sizeBytes", sizeBytes)
        }

        activeTabKey = key
        pendingLibraryId = ""
        pendingFileId = ""
        scheduleTabStateSave()
        Qt.callLater(function() {
            if (index >= 0 && index < tabList.count) {
                tabList.positionViewAtIndex(index, ListView.Contain)
            }
        })
    }

    function captureSelectedChat(makeActive) {
        var chat = ChatStore.selectedChat || ({})
        var chatId = String(chat.id || ChatStore.selectedChatId || "")

        if (chatId.length === 0) {
            return
        }

        var key = chatKey(chatId)
        var index = tabIndexForKey(key)
        var title = String(chat.title || "Untitled Chat")
        var libraryName = String(chat.libraryName || "Chat")
        var agentIds = chat.agentIds || []

        if (index < 0) {
            index = newTabInsertionIndex()
            tabs.insert(index, {
                tabKey: key,
                tabType: "chat",
                fileId: "",
                libraryId: String(chat.libraryId || ""),
                chatId: chatId,
                title: title,
                relativePath: "Conversation workspace",
                libraryName: libraryName,
                extension: "",
                sizeBytes: 0,
                lineCount: 0,
                agentCount: agentIds.length
            })
        } else {
            tabs.setProperty(index, "title", title)
            tabs.setProperty(index, "libraryName", libraryName)
            tabs.setProperty(index, "agentCount", agentIds.length)
        }

        if (makeActive === false) {
            scheduleTabStateSave()
            return
        }

        activeTabKey = key
        pendingLibraryId = ""
        pendingFileId = ""
        LibraryStore.clearFilePreview()
        scheduleTabStateSave()
        Qt.callLater(function() {
            if (index >= 0 && index < tabList.count) {
                tabList.positionViewAtIndex(index, ListView.Contain)
            }
        })
    }

    function updateChatTab(chat) {
        var chatId = String(chat && chat.id ? chat.id : "")
        var index = tabIndexForKey(chatKey(chatId))

        if (index < 0) {
            return
        }

        tabs.setProperty(index, "title", String(chat.title || "Untitled Chat"))
        tabs.setProperty(index, "libraryName", String(chat.libraryName || "Chat"))
        tabs.setProperty(index, "agentCount", chat.agentIds ? chat.agentIds.length : 0)
        scheduleTabStateSave()
    }

    function updateMovedFile(fileId, relativePath) {
        var key = fileKey(LibraryStore.selectedLibraryId, fileId)
        var index = tabIndexForKey(key)

        if (index < 0) {
            return
        }

        var pathParts = String(relativePath || "").split("/")
        var title = pathParts.length > 0
            ? pathParts[pathParts.length - 1]
            : String(tabs.get(index).title || "Library file")

        tabs.setProperty(index, "title", title)
        tabs.setProperty(index, "relativePath", String(relativePath || title))
        scheduleTabStateSave()
    }

    function capturePreviewMetadata() {
        var key = fileKey(
            LibraryStore.selectedLibraryId,
            LibraryStore.selectedFileId
        )
        var index = tabIndexForKey(key)
        var preview = LibraryStore.filePreview || ({})

        if (index >= 0 && preview.lineCount !== undefined) {
            tabs.setProperty(index, "lineCount", Number(preview.lineCount || 0))
            scheduleTabStateSave()
        }
    }

    function tryOpenPending() {
        if (
            pendingLibraryId.length === 0
            || pendingFileId.length === 0
            || String(LibraryStore.selectedLibraryId) !== pendingLibraryId
        ) {
            return
        }

        var files = LibraryStore.files || []
        for (var index = 0; index < files.length; index += 1) {
            if (String(files[index].id || "") === pendingFileId) {
                var fileId = pendingFileId
                pendingLibraryId = ""
                pendingFileId = ""
                LibraryStore.previewFile(fileId)
                return
            }
        }
    }

    function activateTab(index) {
        if (index < 0 || index >= tabs.count) {
            return
        }

        var tab = tabs.get(index)
        activeTabKey = String(tab.tabKey)
        scheduleTabStateSave()

        if (String(tab.tabType) === "chat") {
            pendingLibraryId = ""
            pendingFileId = ""
            LibraryStore.clearFilePreview()

            if (String(ChatStore.selectedChatId) !== String(tab.chatId)) {
                ChatStore.selectChat(String(tab.chatId))
            }
            return
        }

        pendingLibraryId = String(tab.libraryId)
        pendingFileId = String(tab.fileId)

        if (String(LibraryStore.selectedLibraryId) !== pendingLibraryId) {
            LibraryStore.clearFilePreview()
            LibraryStore.selectLibrary(pendingLibraryId)
            return
        }

        if (String(LibraryStore.selectedFileId) === pendingFileId) {
            pendingLibraryId = ""
            pendingFileId = ""
            return
        }

        LibraryStore.previewFile(pendingFileId)
        pendingLibraryId = ""
        pendingFileId = ""
    }

    function closeTab(index) {
        if (index < 0 || index >= tabs.count) {
            return
        }

        var closingTab = tabs.get(index)
        var wasActive = String(closingTab.tabKey) === activeTabKey
        tabs.remove(index)
        scheduleTabStateSave()

        if (!wasActive) {
            return
        }

        pendingLibraryId = ""
        pendingFileId = ""

        if (tabs.count === 0) {
            activeTabKey = ""
            if (String(closingTab.tabType) === "file") {
                LibraryStore.clearFilePreview()
            }
            return
        }

        activateTab(Math.min(index, tabs.count - 1))
    }

    Keys.onEscapePressed: function(event) {
        if (!tabDragActive) {
            return
        }

        finishTabDrag(false)
        event.accepted = true
    }

    Component.onCompleted: {
        switchCollectionWorkspace()
    }

    Component.onDestruction: {
        tabStateSaveTimer.stop()
        saveTabState()
        WorkspaceState.sync()
    }

    onActiveTabKeyChanged: scheduleTabStateSave()
    onRestoreActiveTabPendingChanged: {
        if (
            collectionWorkspaceSwitching
            && collectionScopeReady
            && !restoreActiveTabPending
        ) {
            finishCollectionWorkspaceSwitch()
        }
    }

    Connections {
        target: CollectionStore

        function onSelectedCollectionIdChanged() {
            root.switchCollectionWorkspace()
        }

        function onWorkspaceScopeChanged() {
            root.collectionWorkspaceScopeReady()
        }
    }

    Connections {
        target: LibraryStore

        function onSelectedFileChanged() {
            if (root.collectionWorkspaceSwitching) {
                return
            }

            if (root.restoreActiveTabPending) {
                root.tryRestoreActiveTab()
                return
            }

            if (LibraryStore.selectedFileId.length > 0) {
                root.captureSelectedFile()
            } else if (
                root.pendingFileId.length === 0
                && root.activeTabType() === "file"
            ) {
                if (ChatStore.selectedChatId.length > 0) {
                    root.captureSelectedChat(true)
                } else {
                    root.activeTabKey = ""
                }
            }
        }

        function onFilePreviewChanged() {
            root.capturePreviewMetadata()
        }

        function onFileMoved(fileId, relativePath) {
            root.updateMovedFile(fileId, relativePath)
        }

        function onFilesChanged() {
            root.tryOpenPending()
            root.tryRestoreActiveTab()
        }

        function onLibrariesChanged() {
            root.libraryCatalogObserved = true
            Qt.callLater(root.tryRestoreActiveTab)
        }

        function onLoadingLibrariesChanged() {
            if (!LibraryStore.loadingLibraries) {
                root.libraryCatalogObserved = true
                Qt.callLater(root.tryRestoreActiveTab)
            }
        }

        function onLoadingFilesChanged() {
            if (!LibraryStore.loadingFiles) {
                Qt.callLater(root.tryRestoreActiveTab)
            }
        }

        function onSelectedLibraryIdChanged() {
            root.tryOpenPending()
            Qt.callLater(root.tryRestoreActiveTab)
        }
    }

    Connections {
        target: ChatStore

        function onSelectedChatIdChanged() {
            if (root.collectionWorkspaceSwitching) {
                return
            }

            if (root.restoreActiveTabPending) {
                root.tryRestoreActiveTab()
                return
            }

            if (ChatStore.selectedChatId.length > 0) {
                root.captureSelectedChat(true)
            }
        }

        function onChatsChanged() {
            root.chatCatalogObserved = true
            Qt.callLater(root.tryRestoreActiveTab)
        }

        function onLoadingChatsChanged() {
            if (!ChatStore.loadingChats) {
                root.chatCatalogObserved = true
                Qt.callLater(root.tryRestoreActiveTab)
            }
        }

        function onChatUpdated(chat) {
            root.updateChatTab(chat)
        }
    }

    Timer {
        id: tabStateSaveTimer

        interval: 240
        repeat: false
        onTriggered: root.saveTabState()
    }

    ListModel {
        id: tabs
        dynamicRoles: true
    }

    Rectangle {
        anchors.fill: parent
        color: root.theme.controlSurfaceBg

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 0.8
            color: root.activeContourColor
            opacity: 0.72
        }
    }

    NumberAnimation {
        id: tabSettleAnimation

        target: root
        property: "settlingTabRootX"
        duration: 620
        easing.type: Easing.OutQuint

        onStopped: root.completeTabSettle()
    }

    Timer {
        interval: 16
        repeat: true
        running: root.tabDragActive && !root.tabDropSettling

        onTriggered: {
            var localX = root.tabDragPointerX - tabList.x
            var edgeSize = 44
            var maxContentX = Math.max(0, tabList.contentWidth - tabList.width)
            var nextContentX = tabList.contentX

            if (localX < edgeSize) {
                nextContentX -= 8 * (1 - Math.max(0, localX) / edgeSize)
            } else if (localX > tabList.width - edgeSize) {
                nextContentX += 8 * (
                    1 - Math.max(0, tabList.width - localX) / edgeSize
                )
            }

            nextContentX = Math.max(0, Math.min(maxContentX, nextContentX))

            if (nextContentX !== tabList.contentX) {
                tabList.contentX = nextContentX
                root.updateTabDropSlot(
                    root.tabContentXFromRootX(root.tabDragPointerX)
                )
            }
        }
    }

    ListView {
        id: tabList

        anchors.fill: parent
        anchors.leftMargin: -1
        anchors.rightMargin: 0
        orientation: ListView.Horizontal
        spacing: -2
        clip: true
        interactive: !root.tabDragActive

        boundsBehavior: Flickable.StopAtBounds
        model: tabs

        delegate: Item {
            id: tabItem

            required property int index
            required property string tabKey
            required property string tabType
            required property string fileId
            required property string libraryId
            required property string chatId
            required property string title
            required property string relativePath
            required property string libraryName
            required property string extension
            required property real sizeBytes
            required property int lineCount
            required property int agentCount

            readonly property bool active: tabKey === root.activeTabKey
            readonly property bool hovered: tabHover.containsMouse
            readonly property bool neighborHovered: root.hoveredTabIndex >= 0
                && Math.abs(root.hoveredTabIndex - index) === 1
            readonly property bool draggingSource: root.tabDragActive
                && root.draggedTabKey === tabKey
            readonly property bool settlingSource: root.tabDropSettling
                && root.settlingTabKey === tabKey
            readonly property real shuffleOffset: root.tabShuffleOffsetForIndex(index)

            width: Math.max(
                132,
                Math.min(280, tabTitle.implicitWidth + 74)
            )
            readonly property real baseVisualHeight: active ? 28 : 24
            property real hoverProgress: draggingSource || settlingSource
                ? 0
                : hovered
                    ? 1
                    : neighborHovered
                        ? 0.34
                        : 0

            height: tabList.height
            y: 0
            transformOrigin: Item.Bottom
            scale: 1.0

            Behavior on hoverProgress {
                enabled: !tabItem.draggingSource && !tabItem.settlingSource

                NumberAnimation {
                    duration: 220
                    easing.type: Easing.OutCubic
                }
            }
            z: draggingSource
                ? 6000
                : settlingSource
                    ? 5500
                    : active
                    ? 1000
                    : hovered
                        ? 800
                        : neighborHovered
                            ? 600
                            : tabs.count - index
            opacity: 1
            transform: Translate {
                x: tabItem.settlingSource
                    ? root.tabContentXFromRootX(
                        root.settlingTabRootX
                    ) - tabItem.x
                    : tabItem.draggingSource
                        ? root.tabContentXFromRootX(
                            root.tabDragPointerX - root.tabDragGrabOffsetX
                        ) - tabItem.x
                        : tabItem.shuffleOffset

                Behavior on x {
                    enabled: root.tabDragActive
                        && !root.tabDropSettling
                        && !tabItem.draggingSource
                        && !tabItem.settlingSource

                    NumberAnimation {
                        duration: 260
                        easing.type: Easing.OutCubic
                    }
                }
            }


            Item {
                id: tabVisual

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: Math.min(tabItem.baseVisualHeight, tabList.height)
                z: 3

                transform: Scale {
                    origin.x: tabVisual.width / 2
                    origin.y: tabVisual.height
                    xScale: 1 + tabItem.hoverProgress * (
                        tabItem.active ? 0.018 : 0.022
                    )
                    yScale: 1 + tabItem.hoverProgress * (
                        tabItem.active ? 0.075 : 0.13
                    )
                }
            }

            Canvas {
                id: tabCanvas

                parent: tabVisual
                anchors.fill: parent
                property color fillColor: tabItem.active
                    ? root.theme.workspaceBg
                    : tabItem.hovered
                        ? "#302c25"
                        : "#292620"
                property color outlineColor: tabItem.active
                    ? root.activeContourColor
                    : tabItem.hovered
                        ? "#746c60"
                        : "#665f54"
                property real outlineOpacity: tabItem.active
                    ? 1.0
                    : tabItem.hovered
                        ? 1.0
                        : 0.96
                property real topInset: 0

                onFillColorChanged: requestPaint()
                onOutlineColorChanged: requestPaint()
                onOutlineOpacityChanged: requestPaint()
                onTopInsetChanged: requestPaint()
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()

                Behavior on fillColor {
                    ColorAnimation {
                        duration: 170
                        easing.type: Easing.OutCubic
                    }
                }

                Behavior on outlineColor {
                    ColorAnimation {
                        duration: 170
                        easing.type: Easing.OutCubic
                    }
                }

                onPaint: {
                    var context = getContext("2d")
                    var left = 1
                    var right = width - 1
                    var top = topInset
                    var bottom = height
                    var shoulder = 2
                    var corner = 3

                    context.reset()
                    context.beginPath()
                    context.moveTo(left, bottom)
                    context.lineTo(left + shoulder, top + corner)
                    context.quadraticCurveTo(
                        left + shoulder + 1,
                        top,
                        left + shoulder + corner,
                        top
                    )
                    context.lineTo(right - shoulder - corner, top)
                    context.quadraticCurveTo(
                        right - shoulder - 1,
                        top,
                        right - shoulder,
                        top + corner
                    )
                    context.lineTo(right, bottom)
                    context.closePath()
                    context.fillStyle = fillColor
                    context.fill()

                    context.globalAlpha = outlineOpacity
                    context.strokeStyle = outlineColor
                    context.lineWidth = tabItem.active ? 1.25 : 0.85
                    context.lineJoin = "round"
                    context.beginPath()
                    context.moveTo(left, bottom)
                    context.lineTo(left + shoulder, top + corner)
                    context.quadraticCurveTo(
                        left + shoulder + 1,
                        top,
                        left + shoulder + corner,
                        top
                    )
                    context.lineTo(right - shoulder - corner, top)
                    context.quadraticCurveTo(
                        right - shoulder - 1,
                        top,
                        right - shoulder,
                        top + corner
                    )
                    context.lineTo(right, bottom)
                    if (!tabItem.active) {
                        context.lineTo(left, bottom)
                    }
                    context.stroke()
                    context.globalAlpha = 1
                }
            }

            Text {
                id: fileGlyph

                parent: tabVisual
                y: Math.max(0, Math.min(parent.height - height, tabTitle.y))
                anchors.left: parent.left
                anchors.leftMargin: 14
                width: 16
                height: 18
                text: root.glyphFor(tabItem.tabType, tabItem.extension)
                color: tabItem.active || tabItem.hovered
                    ? root.theme.accentBright
                    : root.theme.mutedText
                opacity: tabItem.active || tabItem.hovered ? 1 : 0.92
                font.pixelSize: root.theme.typeSize(9)
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                z: 3
            }

            Text {
                id: tabTitle

                parent: tabVisual
                x: 36
                width: Math.max(0, parent.width - 75)
                y: Math.round((parent.height - height) / 2) + 1
                height: 18
                text: tabItem.title
                color: root.theme.appText
                opacity: tabItem.active
                    ? 1
                    : tabItem.hovered
                        ? 1
                        : 0.88
                font.pixelSize: root.theme.typeSize(10)
                font.weight: tabItem.active ? Font.DemiBold : Font.Medium
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                z: 3
            }

            MouseArea {
                id: tabHover

                property bool dragCandidate: false
                property real pressedRootX: 0
                property real pressedContentX: 0
                property real pressedLocalX: 0

                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                hoverEnabled: true
                preventStealing: true
                cursorShape: root.tabDragActive
                    ? Qt.ClosedHandCursor
                    : pressed
                        ? Qt.ClosedHandCursor
                        : Qt.PointingHandCursor

                onPressed: function(mouse) {
                    var rootPoint = tabItem.mapToItem(root, mouse.x, mouse.y)
                    var contentPoint = tabItem.mapToItem(
                        tabList.contentItem,
                        mouse.x,
                        mouse.y
                    )

                    dragCandidate = true
                    pressedRootX = rootPoint.x
                    pressedContentX = contentPoint.x
                    pressedLocalX = mouse.x
                }

                onPositionChanged: function(mouse) {
                    if (!pressed || !dragCandidate) {
                        return
                    }

                    var rootPoint = tabItem.mapToItem(root, mouse.x, mouse.y)
                    var contentPoint = tabItem.mapToItem(
                        tabList.contentItem,
                        mouse.x,
                        mouse.y
                    )

                    if (
                        !root.tabDragActive
                        && Math.abs(rootPoint.x - pressedRootX) >= 7
                    ) {
                        root.beginTabDrag(
                            tabItem.index,
                            rootPoint.x,
                            contentPoint.x,
                            pressedLocalX,
                            tabItem.width
                        )
                    }

                    if (
                        root.tabDragActive
                        && root.draggedTabKey === tabItem.tabKey
                    ) {
                        root.updateTabDrag(rootPoint.x, contentPoint.x)
                    }
                }

                onReleased: function(mouse) {
                    var wasDragging = root.tabDragActive
                        && root.draggedTabKey === tabItem.tabKey

                    dragCandidate = false

                    if (wasDragging) {
                        root.finishTabDrag(true)
                    } else {
                        root.activateTab(tabItem.index)
                    }
                }

                onCanceled: {
                    dragCandidate = false

                    if (
                        root.tabDragActive
                        && root.draggedTabKey === tabItem.tabKey
                    ) {
                        root.finishTabDrag(false)
                    }
                }

                onEntered: {
                    if (!root.tabDragActive) {
                        root.hoveredTabIndex = tabItem.index
                    }
                }

                onExited: {
                    if (root.hoveredTabIndex === tabItem.index) {
                        root.hoveredTabIndex = -1
                    }
                }
                z: 2
            }

            ToolTip {
                id: tabInfo

                visible: tabHover.containsMouse && !root.tabDragActive
                delay: 900
                timeout: 7000
                y: tabList.height + 8
                padding: 0

                enter: Transition {
                    ParallelAnimation {
                        NumberAnimation {
                            property: "opacity"
                            from: 0
                            to: 1
                            duration: root.theme.motionFast
                            easing.type: Easing.OutCubic
                        }

                        NumberAnimation {
                            property: "scale"
                            from: 0.96
                            to: 1
                            duration: root.theme.motionHover
                            easing.type: Easing.OutBack
                        }
                    }
                }

                exit: Transition {
                    NumberAnimation {
                        property: "opacity"
                        from: 1
                        to: 0
                        duration: root.theme.motionFast
                        easing.type: Easing.InCubic
                    }
                }

                contentItem: Item {
                    implicitWidth: 326
                    implicitHeight: infoColumn.implicitHeight + 28

                    Column {
                        id: infoColumn

                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 14
                        spacing: 9

                        Row {
                            spacing: 9

                            Rectangle {
                                width: 30
                                height: 30
                                radius: 10
                                color: root.theme.accentSoft
                                border.width: 1
                                border.color: root.theme.quietBorder

                                Text {
                                    anchors.centerIn: parent
                                    text: root.glyphFor(
                                        tabItem.tabType,
                                        tabItem.extension
                                    )
                                    color: root.theme.accentBright
                                    font.pixelSize: root.theme.typeSize(10)
                                    font.weight: Font.DemiBold
                                }
                            }

                            Column {
                                width: 255
                                spacing: 3

                                Text {
                                    width: parent.width
                                    text: tabItem.title
                                    color: root.theme.appText
                                    font.pixelSize: root.theme.typeSize(12)
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }

                                Text {
                                    width: parent.width
                                    text: tabItem.tabType === "chat"
                                        ? "Conversation workspace"
                                        : "Library file"
                                    color: root.theme.accentBright
                                    font.pixelSize: root.theme.typeSize(8)
                                    font.weight: Font.Bold
                                    font.letterSpacing: 0.45
                                }
                            }
                        }

                        Rectangle {
                            width: parent.width
                            implicitHeight: pathText.implicitHeight + 16
                            radius: 8
                            color: root.theme.controlSurfaceBg
                            border.width: 1
                            border.color: root.theme.quietBorder

                            Text {
                                id: pathText

                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: 8
                                text: tabItem.tabType === "chat"
                                    ? tabItem.libraryName
                                    : tabItem.relativePath
                                color: root.theme.mutedText
                                font.pixelSize: root.theme.typeSize(9)
                                wrapMode: Text.WrapAnywhere
                                maximumLineCount: 3
                                elide: Text.ElideRight
                            }
                        }

                        Row {
                            spacing: 6

                            Rectangle {
                                width: libraryChip.implicitWidth + 14
                                height: 22
                                radius: 11
                                color: root.theme.accentSoft

                                Text {
                                    id: libraryChip

                                    anchors.centerIn: parent
                                    text: tabItem.libraryName
                                    color: root.theme.accentBright
                                    font.pixelSize: root.theme.typeSize(8)
                                    font.weight: Font.DemiBold
                                }
                            }

                            Rectangle {
                                width: detailChip.implicitWidth + 14
                                height: 22
                                radius: 11
                                color: root.theme.controlSurfaceBg
                                border.width: 1
                                border.color: root.theme.quietBorder

                                Text {
                                    id: detailChip

                                    anchors.centerIn: parent
                                    text: tabItem.tabType === "chat"
                                        ? String(tabItem.agentCount)
                                            + (tabItem.agentCount === 1
                                                ? " Agent"
                                                : " Agents")
                                        : root.formattedSize(tabItem.sizeBytes)
                                    color: root.theme.mutedText
                                    font.pixelSize: root.theme.typeSize(8)
                                }
                            }

                            Rectangle {
                                visible: tabItem.tabType === "file"
                                    && tabItem.lineCount > 0
                                width: lineChip.implicitWidth + 14
                                height: 22
                                radius: 11
                                color: root.theme.controlSurfaceBg
                                border.width: 1
                                border.color: root.theme.quietBorder

                                Text {
                                    id: lineChip

                                    anchors.centerIn: parent
                                    text: String(tabItem.lineCount)
                                        + (tabItem.lineCount === 1
                                            ? " line"
                                            : " lines")
                                    color: root.theme.mutedText
                                    font.pixelSize: root.theme.typeSize(8)
                                }
                            }
                        }
                    }
                }

                background: Rectangle {
                    color: root.theme.surfaceBg
                    border.width: 1
                    border.color: root.theme.panelBorder
                    radius: 13

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 4
                        radius: 10
                        color: "transparent"
                        border.width: 1
                        border.color: root.theme.quietBorder
                        opacity: 0.42
                    }
                }
            }

            Button {
                id: closeTabButton

                parent: tabVisual
                y: Math.max(0, Math.min(parent.height - height, tabTitle.y - 1))
                anchors.right: parent.right
                anchors.rightMargin: 8
                width: 19
                height: 19
                z: 5
                text: "×"
                hoverEnabled: true
                padding: 0
                opacity: tabItem.active || tabItem.hovered || hovered ? 1 : 0
                onClicked: root.closeTab(tabItem.index)

                Behavior on opacity {
                    NumberAnimation { duration: root.theme.motionFast }
                }

                contentItem: Text {
                    text: parent.text
                    color: parent.hovered
                        ? root.theme.appText
                        : root.theme.mutedText
                    font.pixelSize: root.theme.typeSize(12)
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                background: Rectangle {
                    radius: width / 2
                    color: parent.hovered
                        ? root.theme.controlSurfaceBg
                        : "transparent"
                    border.width: parent.hovered ? 1 : 0
                    border.color: root.theme.quietBorder
                }
            }
        }

        ScrollBar.horizontal: ScrollBar {
            policy: ScrollBar.AlwaysOff
        }
    }
}

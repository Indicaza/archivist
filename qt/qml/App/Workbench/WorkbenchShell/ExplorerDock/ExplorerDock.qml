import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Archivist.Services 1.0
import "../../../Files/FileIdentity.js" as FileIdentity
import "ExplorerItem"
import "WorkspaceNavigator"

Rectangle {
    id: root

    required property var theme
    required property int activeViewIndex

    signal closeRequested()

    property string selectedNodeId: ""
    property string filterText: ""
    property var expandedNodeIds: ({})
    property var treeNodes: []
    property var childrenByParent: ({})
    property var filterMatchCache: ({})
    property string selectedNodePath: ""
    property int hoveredTreeIndex: -1
    property int toolbarHoverIndex: -1
    property bool rootDropActive: false
    property var treeView: null
    property int treeViewportRevision: 0
    property string workspaceCollectionId: ""
    property string treeStateLibraryId: ""
    property string pendingSelectedLibraryId: ""
    property bool collectionLibraryRestorePending: false
    property bool restoringLibraryTreeState: false
    property bool treeStateRestorePending: false
    property var pendingTreeViewport: ({
        contentY: 0,
        nodeId: "",
        offset: 0
    })
    property var filterField: null
    readonly property var scopedLibraries: filteredLibraries()

    readonly property var viewTitles: [
        "Workspace Navigator",
        "Archived Libraries",
        "Library Search",
        "Plugins",
        "Tools"
    ]

    color: theme.surfaceBg
    border.width: 0
    clip: true

    function collectionExplorerStateKey(
        collectionId,
        suffix
    ) {
        return "workspace/collections/"
            + String(collectionId || "")
            + "/explorer/"
            + String(suffix || "")
    }

    function libraryTreeStateKey(
        collectionId,
        libraryId
    ) {
        return "workspace/collections/"
            + String(collectionId || "")
            + "/libraries/"
            + String(libraryId || "")
            + "/tree"
    }

    function cloneExpandedNodes(value) {
        var result = ({})
        var sourceValue = value || ({})

        for (var key in sourceValue) {
            if (sourceValue[key] === true) {
                result[key] = true
            }
        }

        return result
    }

    function scheduleLibraryTreeStateSave() {
        if (
            restoringLibraryTreeState
            || workspaceCollectionId.length === 0
            || treeStateLibraryId.length === 0
        ) {
            return
        }

        libraryTreeStateSaveTimer.restart()
    }

    function saveLibraryTreeState(
        collectionId,
        libraryId
    ) {
        var targetCollectionId = String(collectionId || "")
        var targetLibraryId = String(libraryId || "")

        if (
            restoringLibraryTreeState
            || targetCollectionId.length === 0
            || targetLibraryId.length === 0
        ) {
            return
        }

        var state = {
            expandedNodeIds: cloneExpandedNodes(expandedNodeIds),
            selectedNodeId: String(selectedNodeId || ""),
            selectedNodePath: String(selectedNodePath || ""),
            filterText: String(filterText || ""),
            viewport: captureTreeViewport()
        }

        WorkspaceState.setValue(
            libraryTreeStateKey(
                targetCollectionId,
                targetLibraryId
            ),
            JSON.stringify(state)
        )
        WorkspaceState.setValue(
            collectionExplorerStateKey(
                targetCollectionId,
                "selectedLibraryId"
            ),
            targetLibraryId
        )
    }

    function saveCurrentLibraryTreeState() {
        libraryTreeStateSaveTimer.stop()
        saveLibraryTreeState(
            workspaceCollectionId,
            treeStateLibraryId
        )
    }

    function readLibraryTreeState(
        collectionId,
        libraryId
    ) {
        var rawState = String(
            WorkspaceState.value(
                libraryTreeStateKey(
                    collectionId,
                    libraryId
                ),
                "{}"
            ) || "{}"
        )
        var state = ({})

        try {
            state = JSON.parse(rawState)
        } catch (error) {
            state = ({})
        }

        if (!state || typeof state !== "object") {
            state = ({})
        }

        return state
    }

    function beginLibraryTreeRestore(libraryId) {
        var targetLibraryId = String(libraryId || "")

        restoringLibraryTreeState = true
        treeStateLibraryId = targetLibraryId
        selectedNodeId = ""
        selectedNodePath = ""
        expandedNodeIds = ({})
        filterText = ""
        pendingTreeViewport = ({
            contentY: 0,
            nodeId: "",
            offset: 0
        })
        treeStateRestorePending = targetLibraryId.length > 0

        if (
            workspaceCollectionId.length > 0
            && targetLibraryId.length > 0
        ) {
            var state = readLibraryTreeState(
                workspaceCollectionId,
                targetLibraryId
            )
            expandedNodeIds = cloneExpandedNodes(
                state.expandedNodeIds
            )
            selectedNodeId = String(
                state.selectedNodeId || ""
            )
            selectedNodePath = String(
                state.selectedNodePath || ""
            )
            filterText = String(state.filterText || "")
            pendingTreeViewport = state.viewport || ({
                contentY: 0,
                nodeId: "",
                offset: 0
            })
        }

        if (
            filterField
            && String(filterField.text || "") !== filterText
        ) {
            filterField.text = filterText
        }

        treeNodes = []
        childrenByParent = ({})
        visibleTree.clear()
        restoringLibraryTreeState = false
    }

    function finishPendingLibraryTreeRestore() {
        if (
            !treeStateRestorePending
            || restoringLibraryTreeState
            || LibraryStore.loadingFiles
            || String(LibraryStore.selectedLibraryId)
                !== treeStateLibraryId
        ) {
            return
        }

        restoringLibraryTreeState = true
        rebuildNodesFromFiles(false)
        var anchor = pendingTreeViewport
        treeStateRestorePending = false
        restoreTreeViewport(
            anchor,
            treeViewportRevision
        )
        restoringLibraryTreeState = false
        scheduleLibraryTreeStateSave()
    }

    function savedCollectionLibraryId(collectionId) {
        return String(
            WorkspaceState.value(
                collectionExplorerStateKey(
                    collectionId,
                    "selectedLibraryId"
                ),
                ""
            ) || ""
        )
    }

    function finishCollectionLibraryRestore(libraryId) {
        var targetLibraryId = String(libraryId || "")

        if (targetLibraryId.length === 0) {
            return
        }

        collectionLibraryRestorePending = false
        pendingSelectedLibraryId = ""

        WorkspaceState.setValue(
            collectionExplorerStateKey(
                workspaceCollectionId,
                "selectedLibraryId"
            ),
            targetLibraryId
        )

        if (treeStateLibraryId !== targetLibraryId) {
            beginLibraryTreeRestore(targetLibraryId)
        }

        Qt.callLater(
            root.finishPendingLibraryTreeRestore
        )
    }

    function tryRestoreCollectionLibrary() {
        if (
            !collectionLibraryRestorePending
            || workspaceCollectionId.length === 0
            || workspaceCollectionId
                !== String(
                    CollectionStore.selectedCollectionId || ""
                )
            || CollectionStore.loading
            || LibraryStore.loadingLibraries
        ) {
            return
        }

        var savedLibraryId = String(
            pendingSelectedLibraryId || ""
        )

        if (
            savedLibraryId.length > 0
            && CollectionStore.includesLibrary(
                savedLibraryId
            )
        ) {
            if (
                String(LibraryStore.selectedLibraryId)
                    !== savedLibraryId
            ) {
                LibraryStore.selectLibrary(savedLibraryId)
                return
            }

            finishCollectionLibraryRestore(
                savedLibraryId
            )
            return
        }

        var currentLibraryId = String(
            LibraryStore.selectedLibraryId || ""
        )

        if (
            currentLibraryId.length > 0
            && CollectionStore.includesLibrary(
                currentLibraryId
            )
        ) {
            finishCollectionLibraryRestore(
                currentLibraryId
            )
            return
        }

        var libraries = scopedLibraries || []

        if (libraries.length > 0) {
            LibraryStore.selectLibrary(
                String(libraries[0].id || "")
            )
            return
        }

        collectionLibraryRestorePending = false
        pendingSelectedLibraryId = ""
        beginLibraryTreeRestore("")
    }

    function beginCollectionLibraryRestore() {
        var nextCollectionId = String(
            CollectionStore.selectedCollectionId || ""
        )

        if (
            workspaceCollectionId.length > 0
            && workspaceCollectionId
                !== nextCollectionId
        ) {
            saveCurrentLibraryTreeState()
        }

        workspaceCollectionId = nextCollectionId
        treeStateLibraryId = ""
        pendingSelectedLibraryId = nextCollectionId.length > 0
            ? savedCollectionLibraryId(
                nextCollectionId
            )
            : ""
        collectionLibraryRestorePending =
            nextCollectionId.length > 0

        beginLibraryTreeRestore("")
        tryRestoreCollectionLibrary()
    }

    function selectLibrary(libraryId) {
        var targetLibraryId = String(libraryId || "")

        if (
            targetLibraryId.length === 0
            || targetLibraryId
                === String(
                    LibraryStore.selectedLibraryId || ""
                )
        ) {
            return
        }

        saveCurrentLibraryTreeState()
        treeStateLibraryId = ""
        LibraryStore.selectLibrary(targetLibraryId)
    }

    function glyphForFile(fileName, extension) {
        return FileIdentity.glyphFor({
            fileName: fileName,
            extension: extension
        })
    }

    function rebuildNodesFromFiles(preserveViewport) {
        var nodes = []
        var directories = ({})
        var files = LibraryStore.files || []

        for (var fileIndex = 0; fileIndex < files.length; fileIndex += 1) {
            var file = files[fileIndex]
            var relativePath = String(file.relativePath || file.name || "")
            var rawParts = relativePath.split("/")
            var parts = []

            for (var partIndex = 0; partIndex < rawParts.length; partIndex += 1) {
                if (rawParts[partIndex].length > 0) {
                    parts.push(rawParts[partIndex])
                }
            }

            if (parts.length === 0) {
                continue
            }

            var parentId = ""
            var pathParts = []

            for (var directoryIndex = 0; directoryIndex < parts.length - 1; directoryIndex += 1) {
                pathParts.push(parts[directoryIndex])
                var directoryPath = pathParts.join("/")
                var directoryId = "folder:" + directoryPath

                if (directories[directoryId] !== true) {
                    directories[directoryId] = true
                    nodes.push({
                        id: directoryId,
                        parentId: parentId,
                        title: parts[directoryIndex],
                        glyph: "□",
                        folder: true,
                        fileId: "",
                        relativePath: directoryPath,
                        muted: false,
                        warning: false
                    })
                }

                parentId = directoryId
            }

            nodes.push({
                id: "file:" + String(file.id),
                parentId: parentId,
                title: String(file.name || parts[parts.length - 1]),
                glyph: glyphForFile(file.name, file.extension),
                folder: false,
                fileId: String(file.id),
                relativePath: relativePath,
                muted: file.status !== "available",
                warning: file.status !== "available"
            })
        }

        nodes.sort(function(left, right) {
            if (left.parentId !== right.parentId) {
                return left.parentId.localeCompare(right.parentId)
            }

            if (left.folder !== right.folder) {
                return left.folder ? -1 : 1
            }

            return left.title.localeCompare(right.title)
        })

        var groupedChildren = ({})
        for (var groupedIndex = 0; groupedIndex < nodes.length; groupedIndex += 1) {
            var groupedNode = nodes[groupedIndex]
            var groupKey = groupedNode.parentId

            if (!groupedChildren[groupKey]) {
                groupedChildren[groupKey] = []
            }

            groupedChildren[groupKey].push(groupedNode)
        }

        treeNodes = nodes
        childrenByParent = groupedChildren

        var selectedStillExists = false
        for (var nodeIndex = 0; nodeIndex < nodes.length; nodeIndex += 1) {
            if (nodes[nodeIndex].id === selectedNodeId) {
                selectedStillExists = true
                selectedNodePath = String(
                    nodes[nodeIndex].relativePath
                        || nodes[nodeIndex].title
                )
                break
            }
        }

        if (!selectedStillExists) {
            selectedNodeId = ""
            selectedNodePath = ""
        }

        rebuildTree(preserveViewport)
    }

    function libraryIndexForId(libraryId) {
        var libraries = scopedLibraries || []

        for (var index = 0; index < libraries.length; index += 1) {
            if (String(libraries[index].id) === String(libraryId)) {
                return index
            }
        }

        return -1
    }

    function filteredLibraries() {
        var libraries = LibraryStore.libraries || []

        if (CollectionStore.selectedCollectionId.length === 0) {
            return []
        }

        var filtered = []
        for (var index = 0; index < libraries.length; index += 1) {
            if (CollectionStore.includesLibrary(String(libraries[index].id))) {
                filtered.push(libraries[index])
            }
        }

        return filtered
    }

    function isExpanded(nodeId) {
        return expandedNodeIds[nodeId] === true
    }

    function nodeMatches(node, query) {
        return query.length === 0
            || node.title.toLowerCase().indexOf(query) !== -1
    }

    function subtreeMatches(nodeId, query) {
        var cacheKey = nodeId + "|" + query

        if (filterMatchCache[cacheKey] !== undefined) {
            return filterMatchCache[cacheKey]
        }

        var children = childrenByParent[nodeId] || []

        for (var index = 0; index < children.length; index += 1) {
            var child = children[index]

            if (nodeMatches(child, query) || subtreeMatches(child.id, query)) {
                filterMatchCache[cacheKey] = true
                return true
            }
        }

        filterMatchCache[cacheKey] = false
        return false
    }

    function appendVisibleChildren(parentId, depth, query) {
        var children = childrenByParent[parentId] || []

        for (var index = 0; index < children.length; index += 1) {
            var node = children[index]
            var includeNode = query.length === 0
                || nodeMatches(node, query)
                || subtreeMatches(node.id, query)

            if (!includeNode) {
                continue
            }

            visibleTree.append({
                nodeId: node.id,
                itemTitle: node.title,
                itemGlyph: node.glyph,
                itemDepth: depth,
                itemSelected: selectedNodeId === node.id,
                itemMuted: node.muted === true,
                itemFolder: node.folder === true,
                itemFileId: String(node.fileId || ""),
                itemRelativePath: String(node.relativePath || ""),
                itemExpanded: node.folder === true && isExpanded(node.id),
                itemWarning: node.warning === true
            })

            if (node.folder === true && (query.length > 0 || isExpanded(node.id))) {
                appendVisibleChildren(node.id, depth + 1, query)
            }
        }
    }

    function visibleIndexForNode(nodeId) {
        for (var index = 0; index < visibleTree.count; index += 1) {
            if (String(visibleTree.get(index).nodeId) === String(nodeId || "")) {
                return index
            }
        }

        return -1
    }

    function captureTreeViewport() {
        if (!treeView || visibleTree.count === 0) {
            return ({ contentY: 0, nodeId: "", offset: 0 })
        }

        var sampleY = treeView.contentY + 2
        var index = treeView.indexAt(2, sampleY)

        if (index < 0) {
            return ({
                contentY: treeView.contentY,
                nodeId: "",
                offset: 0
            })
        }

        var item = treeView.itemAtIndex(index)
        return {
            contentY: treeView.contentY,
            nodeId: String(visibleTree.get(index).nodeId || ""),
            offset: item ? item.y - treeView.contentY : 0
        }
    }

    function restoreTreeViewport(anchor, revision) {
        if (!treeView || !anchor) {
            return
        }

        var expectedRevision = revision === undefined
            ? treeViewportRevision
            : revision

        Qt.callLater(function() {
            if (
                !root.treeView
                || expectedRevision !== root.treeViewportRevision
            ) {
                return
            }

            var index = root.visibleIndexForNode(anchor.nodeId)

            if (index >= 0) {
                root.treeView.positionViewAtIndex(index, ListView.Beginning)
                root.treeView.forceLayout()
                var item = root.treeView.itemAtIndex(index)

                if (item) {
                    root.treeView.contentY = item.y - Number(anchor.offset || 0)
                }
            } else {
                var maximumY = Math.max(
                    root.treeView.originY,
                    root.treeView.originY
                        + root.treeView.contentHeight
                        - root.treeView.height
                )
                root.treeView.contentY = Math.max(
                    root.treeView.originY,
                    Math.min(maximumY, Number(anchor.contentY || 0))
                )
            }

            root.treeView.returnToBounds()
        })
    }

    function updateVisibleSelection(previousNodeId, nextNodeId) {
        var previousIndex = visibleIndexForNode(previousNodeId)
        var nextIndex = visibleIndexForNode(nextNodeId)

        if (previousIndex >= 0) {
            visibleTree.setProperty(previousIndex, "itemSelected", false)
        }

        if (nextIndex >= 0) {
            visibleTree.setProperty(nextIndex, "itemSelected", true)
        }
    }

    function rebuildTree(preserveViewport) {
        var shouldPreserve = preserveViewport !== false
        var anchor = shouldPreserve ? captureTreeViewport() : null
        treeViewportRevision += 1
        var revision = treeViewportRevision

        hoveredTreeIndex = -1
        visibleTree.clear()
        filterMatchCache = ({})
        appendVisibleChildren("", 0, filterText.trim().toLowerCase())

        if (anchor) {
            restoreTreeViewport(anchor, revision)
        }
    }

    function moveFileToFolder(fileId, targetDirectory) {
        if (
            String(fileId || "").length === 0
            || LibraryStore.movingFile
        ) {
            return
        }

        LibraryStore.moveFile(
            String(fileId),
            String(targetDirectory || "")
        )
    }

    function activateNode(nodeId, folder, fileId) {
        var previousNodeId = selectedNodeId
        selectedNodeId = nodeId

        for (var index = 0; index < treeNodes.length; index += 1) {
            if (treeNodes[index].id === nodeId) {
                selectedNodePath = String(
                    treeNodes[index].relativePath
                        || treeNodes[index].title
                )
                break
            }
        }

        updateVisibleSelection(previousNodeId, nodeId)

        if (folder) {
            expandedNodeIds[nodeId] = !isExpanded(nodeId)
            rebuildTree(true)
        } else if (fileId.length > 0) {
            LibraryStore.previewFile(fileId)
        }

        scheduleLibraryTreeStateSave()
    }

    function collapseAll() {
        expandedNodeIds = ({})
        rebuildTree(true)
        scheduleLibraryTreeStateSave()
    }

    function expandAll() {
        var nextExpanded = ({})

        for (var index = 0; index < treeNodes.length; index += 1) {
            if (treeNodes[index].folder === true) {
                nextExpanded[treeNodes[index].id] = true
            }
        }

        expandedNodeIds = nextExpanded
        rebuildTree(true)
        scheduleLibraryTreeStateSave()
    }

    function magnifierScale(index, hoveredIndex, pressed) {
        if (pressed) {
            return theme.pressedScale
        }

        if (index === hoveredIndex) {
            return theme.hoverScale
        }

        if (hoveredIndex >= 0 && Math.abs(index - hoveredIndex) === 1) {
            return theme.hoverNeighborScale
        }

        return 1.0
    }

    function updateToolbarHover(index, hovered) {
        if (hovered) {
            toolbarHoverIndex = index
        } else if (toolbarHoverIndex === index) {
            toolbarHoverIndex = -1
        }
    }

    Component.onCompleted: {
        workspaceCollectionId = String(
            CollectionStore.selectedCollectionId || ""
        )
        pendingSelectedLibraryId =
            workspaceCollectionId.length > 0
                ? savedCollectionLibraryId(
                    workspaceCollectionId
                )
                : ""
        collectionLibraryRestorePending =
            workspaceCollectionId.length > 0

        beginLibraryTreeRestore(
            String(
                LibraryStore.selectedLibraryId || ""
            )
        )
        LibraryStore.refresh()
        Qt.callLater(
            root.tryRestoreCollectionLibrary
        )
    }

    Component.onDestruction: {
        saveCurrentLibraryTreeState()
        WorkspaceState.sync()
    }

    Connections {
        target: CollectionStore

        function onSelectedCollectionIdChanged() {
            root.beginCollectionLibraryRestore()
        }

        function onWorkspaceScopeChanged() {
            root.tryRestoreCollectionLibrary()
        }

        function onCollectionsChanged() {
            root.tryRestoreCollectionLibrary()
        }

        function onLoadingChanged() {
            if (!CollectionStore.loading) {
                root.tryRestoreCollectionLibrary()
            }
        }
    }

    Connections {
        target: LibraryStore

        function onFilesChanged() {
            if (
                LibraryStore.loadingFiles
                && (LibraryStore.files || []).length === 0
            ) {
                root.saveCurrentLibraryTreeState()
                return
            }

            if (root.treeStateRestorePending) {
                Qt.callLater(
                    root.finishPendingLibraryTreeRestore
                )
                return
            }

            root.rebuildNodesFromFiles(true)
            root.scheduleLibraryTreeStateSave()
        }

        function onSelectedLibraryIdChanged() {
            var nextLibraryId = String(
                LibraryStore.selectedLibraryId || ""
            )

            if (
                root.treeStateLibraryId.length > 0
                && root.treeStateLibraryId
                    !== nextLibraryId
            ) {
                root.saveCurrentLibraryTreeState()
            }

            if (
                root.workspaceCollectionId.length > 0
                && nextLibraryId.length > 0
                && CollectionStore.includesLibrary(
                    nextLibraryId
                )
            ) {
                WorkspaceState.setValue(
                    root.collectionExplorerStateKey(
                        root.workspaceCollectionId,
                        "selectedLibraryId"
                    ),
                    nextLibraryId
                )
            }

            root.beginLibraryTreeRestore(
                nextLibraryId
            )

            if (
                root.collectionLibraryRestorePending
                && (
                    root.pendingSelectedLibraryId.length === 0
                    || root.pendingSelectedLibraryId
                        === nextLibraryId
                )
            ) {
                root.finishCollectionLibraryRestore(
                    nextLibraryId
                )
            }
        }

        function onLibrariesChanged() {
            root.tryRestoreCollectionLibrary()
        }

        function onLoadingLibrariesChanged() {
            if (!LibraryStore.loadingLibraries) {
                root.tryRestoreCollectionLibrary()
            }
        }

        function onLoadingFilesChanged() {
            if (!LibraryStore.loadingFiles) {
                Qt.callLater(
                    root.finishPendingLibraryTreeRestore
                )
            }
        }
    }

    Timer {
        id: libraryTreeStateSaveTimer

        interval: 180
        repeat: false
        onTriggered: root.saveCurrentLibraryTreeState()
    }

    ListModel {
        id: visibleTree
    }

    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: root.theme.panelBorder
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: root.activeViewIndex === 0
                ? 0
                : root.theme.explorerHeaderHeight
            visible: root.activeViewIndex !== 0
            color: root.theme.controlSurfaceBg

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: root.theme.quietBorder
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 9
                anchors.rightMargin: 5
                spacing: 8

                Text {
                    Layout.fillWidth: true
                    text: root.viewTitles[root.activeViewIndex]
                    color: root.theme.mutedText
                    font.pixelSize: root.theme.typeSize(10)
                    font.weight: Font.Bold
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 1.0
                    elide: Text.ElideRight
                }

                Button {
                    id: closeExplorerButton

                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    text: "‹"
                    hoverEnabled: true
                    padding: 0
                    onClicked: root.closeRequested()
                    scale: down
                        ? root.theme.pressedScale
                        : hovered
                            ? root.theme.hoverScale
                            : 1.0

                    Behavior on scale {
                        enabled: !closeExplorerButton.down

                        NumberAnimation {
                            duration: root.theme.motionHover
                            easing.type: Easing.OutCubic
                        }
                    }

                    contentItem: Text {
                        text: parent.text
                        color: parent.hovered ? root.theme.appText : root.theme.mutedText
                        font.pixelSize: root.theme.typeSize(18)
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: 4
                        color: parent.hovered ? root.theme.hoverBg : "transparent"
                        border.width: parent.hovered ? 1 : 0
                        border.color: root.theme.panelBorder
                    }
                }
            }
        }

        Loader {
            Layout.fillWidth: true
            Layout.fillHeight: true
            sourceComponent: root.activeViewIndex === 0 ? libraryView : placeholderView
        }
    }

    Component {
        id: libraryView

        WorkspaceNavigator {
            theme: root.theme
            libraryContent: libraryBrowser
            onCloseRequested: root.closeRequested()
        }
    }

    Component {
        id: libraryBrowser

        Item {
            id: libraryBrowserRoot

            clip: true

            WorkspaceDragSession {
                id: workspaceDragSession
            }

            Item {
                id: fileDragProxy

                parent: Overlay.overlay
                width: Math.min(
                    240,
                    Math.max(154, dragTitle.implicitWidth + 58)
                )
                height: 30
                visible: workspaceDragSession.active
                z: 100000

                Drag.active: workspaceDragSession.active
                Drag.source: fileDragProxy
                Drag.keys: workspaceDragSession.dragKey.length > 0
                    ? [workspaceDragSession.dragKey]
                    : []
                Drag.supportedActions: Qt.MoveAction
                Drag.proposedAction: Qt.MoveAction
                Drag.hotSpot.x: 18
                Drag.hotSpot.y: height / 2

                Rectangle {
                    anchors.fill: parent
                    radius: 6
                    color: root.theme.controlSurfaceBg
                    border.width: 1
                    border.color: workspaceDragSession.dropAllowed
                        ? root.theme.accentBright
                        : root.theme.panelBorder
                    opacity: 0.97

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 8

                        Text {
                            text: String(
                                workspaceDragSession.payload.glyph || "▤"
                            )
                            color: workspaceDragSession.dropAllowed
                                ? root.theme.accentBright
                                : root.theme.mutedText
                            font.pixelSize: root.theme.typeSize(12)
                        }

                        Text {
                            id: dragTitle

                            Layout.fillWidth: true
                            text: workspaceDragSession.sourceLabel
                            color: root.theme.appText
                            font.pixelSize: root.theme.typeSize(10)
                            font.weight: Font.DemiBold
                            elide: Text.ElideMiddle
                        }
                    }
                }
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 34
                    color: root.theme.controlSurfaceBg

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: root.theme.quietBorder
                    }

                    DropArea {
                        anchors.fill: parent
                        keys: ["archivist-library-file"]
                        enabled: workspaceDragSession.active
                            && !LibraryStore.movingFile
                        z: 10

                        onEntered: function(drag) {
                            var allowed = workspaceDragSession.payloadType
                                    === "library-file"
                                && String(
                                    workspaceDragSession.payload.sourceDirectory
                                        || ""
                                ).length > 0
                            drag.accepted = allowed
                            root.rootDropActive = allowed
                            workspaceDragSession.setTarget(
                                "library-root",
                                "library-root",
                                String(
                                    LibraryStore.selectedLibrary.name
                                        || "Library"
                                ) + " root",
                                allowed
                            )
                        }

                        onPositionChanged: function(drag) {
                            drag.accepted = root.rootDropActive
                        }

                        onExited: {
                            root.rootDropActive = false
                            workspaceDragSession.clearTarget("library-root")
                        }

                        onDropped: function(drop) {
                            if (!root.rootDropActive) {
                                return
                            }

                            root.rootDropActive = false
                            root.moveFileToFolder(
                                String(workspaceDragSession.payload.id || ""),
                                ""
                            )
                            drop.acceptProposedAction()
                        }
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 9
                        anchors.rightMargin: 6
                        spacing: 4

                        ComboBox {
                            id: librarySelector

                            Layout.fillWidth: true
                            Layout.preferredHeight: 26
                            model: root.scopedLibraries
                            textRole: "name"
                            valueRole: "id"
                            enabled: !LibraryStore.loadingLibraries && count > 0
                            hoverEnabled: true
                            ToolTip.visible: hovered
                            ToolTip.text: String(
                                LibraryStore.selectedLibrary.rootPath
                                    || "Select Library"
                            )
                            leftPadding: 7
                            rightPadding: 24

                            Binding {
                                target: librarySelector
                                property: "currentIndex"
                                value: root.libraryIndexForId(LibraryStore.selectedLibraryId)
                            }

                            onActivated: function(index) {
                                var library = root.scopedLibraries[index]
                                if (library) {
                                    root.selectLibrary(
                                        String(library.id)
                                    )
                                }
                            }

                            contentItem: Text {
                                text: librarySelector.displayText.length > 0
                                    ? librarySelector.displayText
                                    : LibraryStore.loadingLibraries
                                        ? "Loading Libraries…"
                                        : "No Libraries"
                                color: root.theme.appText
                                font.pixelSize: root.theme.typeSize(12)
                                font.weight: Font.DemiBold
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }

                            indicator: Text {
                                x: parent.width - width - 7
                                y: (parent.height - height) / 2
                                text: "⌄"
                                color: root.theme.mutedText
                                font.pixelSize: root.theme.typeSize(12)
                            }

                            background: Rectangle {
                                radius: 4
                                color: parent.hovered ? root.theme.hoverBg : "transparent"
                                border.width: parent.popup.visible ? 1 : 0
                                border.color: root.theme.panelBorder
                            }

                            popup: Popup {
                                y: librarySelector.height + 3
                                width: librarySelector.width
                                implicitHeight: Math.min(contentItem.implicitHeight + 8, 260)
                                padding: 4

                                contentItem: ListView {
                                    clip: true
                                    implicitHeight: contentHeight
                                    model: librarySelector.popup.visible ? librarySelector.delegateModel : null
                                    currentIndex: librarySelector.highlightedIndex
                                    ScrollIndicator.vertical: ScrollIndicator {}
                                }

                                background: Rectangle {
                                    radius: 6
                                    color: root.theme.controlSurfaceBg
                                    border.width: 1
                                    border.color: root.theme.panelBorder
                                }
                            }

                            delegate: ItemDelegate {
                                required property int index
                                required property var modelData

                                width: librarySelector.width - 8
                                height: 36
                                highlighted: librarySelector.highlightedIndex === index

                                contentItem: Column {
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 1

                                    Text {
                                        width: parent.width
                                        text: String(modelData.name || "Library")
                                        color: root.theme.appText
                                        font.pixelSize: root.theme.typeSize(11)
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        width: parent.width
                                        text: String(modelData.rootPath || "")
                                        color: root.theme.mutedText
                                        font.pixelSize: root.theme.typeSize(9)
                                        elide: Text.ElideMiddle
                                    }
                                }

                                background: Rectangle {
                                    radius: 4
                                    color: parent.highlighted ? root.theme.hoverBg : "transparent"
                                }
                            }
                        }

                        Rectangle {
                            Layout.preferredWidth: 28
                            Layout.preferredHeight: 17
                            radius: 9
                            color: "#26231e"

                            Text {
                                anchors.centerIn: parent
                                text: LibraryStore.loadingFiles
                                    ? "…"
                                    : String(LibraryStore.files.length)
                                color: root.theme.mutedText
                                font.pixelSize: root.theme.typeSize(8)
                            }
                        }

                        Button {
                            id: collapseAllButton

                            Layout.preferredWidth: 24
                            Layout.preferredHeight: 24
                            text: "⌃"
                            hoverEnabled: true
                            padding: 0
                            ToolTip.visible: hovered
                            ToolTip.text: "Collapse all"
                            onClicked: root.collapseAll()
                            onHoveredChanged: root.updateToolbarHover(0, hovered)
                            scale: root.magnifierScale(
                                0,
                                root.toolbarHoverIndex,
                                down
                            )

                            Behavior on scale {
                                enabled: !collapseAllButton.down

                                NumberAnimation {
                                    duration: root.toolbarHoverIndex >= 0
                                        ? root.theme.motionHover
                                        : root.theme.motionHoverExit
                                    easing.type: Easing.OutCubic
                                }
                            }

                            contentItem: Text {
                                text: parent.text
                                color: parent.hovered ? root.theme.appText : root.theme.mutedText
                                font.pixelSize: root.theme.typeSize(14)
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }

                            background: Rectangle {
                                radius: 4
                                color: parent.hovered ? root.theme.hoverBg : "transparent"
                            }
                        }

                        Button {
                            id: expandAllButton

                            Layout.preferredWidth: 24
                            Layout.preferredHeight: 24
                            text: "⌄"
                            hoverEnabled: true
                            padding: 0
                            ToolTip.visible: hovered
                            ToolTip.text: "Expand all"
                            onClicked: root.expandAll()
                            onHoveredChanged: root.updateToolbarHover(1, hovered)
                            scale: root.magnifierScale(
                                1,
                                root.toolbarHoverIndex,
                                down
                            )

                            Behavior on scale {
                                enabled: !expandAllButton.down

                                NumberAnimation {
                                    duration: root.toolbarHoverIndex >= 0
                                        ? root.theme.motionHover
                                        : root.theme.motionHoverExit
                                    easing.type: Easing.OutCubic
                                }
                            }

                            contentItem: Text {
                                text: parent.text
                                color: parent.hovered ? root.theme.appText : root.theme.mutedText
                                font.pixelSize: root.theme.typeSize(14)
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }

                            background: Rectangle {
                                radius: 4
                                color: parent.hovered ? root.theme.hoverBg : "transparent"
                            }
                        }

                        Button {
                            id: refreshLibrariesButton

                            Layout.preferredWidth: 24
                            Layout.preferredHeight: 24
                            text: LibraryStore.scanning ? "…" : "↻"
                            enabled: LibraryStore.selectedLibraryId.length > 0 && !LibraryStore.scanning
                            hoverEnabled: true
                            padding: 0
                            ToolTip.visible: hovered
                            ToolTip.text: "Rescan selected Library"
                            onClicked: LibraryStore.scanSelectedLibrary()
                            onHoveredChanged: root.updateToolbarHover(2, hovered)
                            scale: root.magnifierScale(
                                2,
                                root.toolbarHoverIndex,
                                down
                            )

                            Behavior on scale {
                                enabled: !refreshLibrariesButton.down

                                NumberAnimation {
                                    duration: root.toolbarHoverIndex >= 0
                                        ? root.theme.motionHover
                                        : root.theme.motionHoverExit
                                    easing.type: Easing.OutCubic
                                }
                            }

                            contentItem: Text {
                                text: parent.text
                                color: parent.hovered ? root.theme.appText : root.theme.mutedText
                                font.pixelSize: root.theme.typeSize(15)
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }

                            background: Rectangle {
                                radius: 4
                                color: parent.hovered ? root.theme.hoverBg : "transparent"
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 34
                    color: root.theme.surfaceBg

                    TextField {
                        id: libraryFilter

                        anchors.fill: parent
                        anchors.leftMargin: 7
                        anchors.rightMargin: 7
                        anchors.topMargin: 4
                        anchors.bottomMargin: 4
                        placeholderText: "Filter files"
                        placeholderTextColor: root.theme.mutedText
                        color: root.theme.appText
                        font.pixelSize: root.theme.typeSize(11)
                        leftPadding: 8
                        rightPadding: 8
                        selectByMouse: true
                        onTextChanged: {
                            root.filterText = text

                            if (
                                !root.restoringLibraryTreeState
                            ) {
                                root.rebuildTree(false)
                                root.restoreTreeViewport(({
                                    contentY: 0,
                                    nodeId: "",
                                    offset: 0
                                }))
                                root.scheduleLibraryTreeStateSave()
                            }
                        }

                        Component.onCompleted: {
                            root.filterField = libraryFilter
                            if (
                                String(text || "")
                                    !== root.filterText
                            ) {
                                text = root.filterText
                            }
                        }
                        Component.onDestruction: {
                            if (
                                root.filterField
                                    === libraryFilter
                            ) {
                                root.filterField = null
                            }
                        }

                        background: Rectangle {
                            radius: 4
                            color: "#0f0e0c"
                            border.width: 1
                            border.color: parent.activeFocus
                                ? "#554a7b"
                                : root.theme.quietBorder
                        }
                    }
                }



                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ListView {
                    id: libraryList

                    anchors.fill: parent
                    anchors.leftMargin: 7
                    anchors.rightMargin: 7
                    anchors.topMargin: 3
                    clip: true
                    spacing: 0
                    boundsBehavior: Flickable.StopAtBounds
                    cacheBuffer: 600
                    reuseItems: true
                    model: visibleTree
                    onContentYChanged: {
                        root.scheduleLibraryTreeStateSave()
                    }

                    Component.onCompleted: root.treeView = libraryList
                    Component.onDestruction: {
                        root.saveCurrentLibraryTreeState()
                        if (root.treeView === libraryList) {
                            root.treeView = null
                        }
                    }

                    delegate: ExplorerItem {
                        required property int index
                        required property string nodeId
                        required property string itemTitle
                        required property string itemGlyph
                        required property int itemDepth
                        required property bool itemSelected
                        required property bool itemMuted
                        required property bool itemFolder
                        required property string itemFileId
                        required property string itemRelativePath
                        required property bool itemExpanded
                        required property bool itemWarning

                        width: libraryList.width
                        theme: root.theme
                        title: itemTitle
                        glyph: itemGlyph
                        depth: itemDepth
                        selected: itemSelected
                        muted: itemMuted
                        folder: itemFolder
                        expanded: itemExpanded
                        warning: itemWarning
                        dragSession: workspaceDragSession
                        dragProxy: fileDragProxy
                        dragEnabled: !LibraryStore.movingFile
                        fileId: itemFileId
                        relativePath: itemRelativePath
                        neighborHovered: root.hoveredTreeIndex >= 0
                            && Math.abs(root.hoveredTreeIndex - index) === 1
                        onHoveredChanged: {
                            if (hovered) {
                                root.hoveredTreeIndex = index
                            } else if (root.hoveredTreeIndex === index) {
                                root.hoveredTreeIndex = -1
                            }
                        }
                        onActivated: root.activateNode(nodeId, itemFolder, itemFileId)
                        onFileDropRequested: function(fileId, targetDirectory) {
                            root.moveFileToFolder(fileId, targetDirectory)
                        }
                    }

                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AsNeeded
                    }
                    }

                    Text {
                        anchors.centerIn: parent
                        width: Math.max(120, parent.width - 28)
                        visible: visibleTree.count === 0
                        text: LibraryStore.errorMessage.length > 0
                            ? LibraryStore.errorMessage + "\n\nRun npm run dev:backend if the local API is offline."
                            : LibraryStore.loadingLibraries
                                ? "Loading Libraries…"
                                : LibraryStore.loadingFiles
                                    ? "Loading files…"
                                    : LibraryStore.selectedLibraryId.length === 0
                                        ? "No Library selected"
                                        : "No cataloged files yet.\nUse Rescan Library to refresh the catalog."
                        color: LibraryStore.errorMessage.length > 0
                            ? root.theme.danger
                            : root.theme.mutedText
                        font.pixelSize: root.theme.typeSize(10)
                        lineHeight: root.theme.typeLineHeightCompact
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: root.theme.controlBarHeight
                    color: root.theme.controlSurfaceBg

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        height: 1
                        color: root.theme.quietBorder
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 9
                        anchors.rightMargin: 9
                        spacing: 6

                        Text {
                            Layout.fillWidth: true
                            text: LibraryStore.errorMessage.length > 0
                                ? LibraryStore.errorMessage
                                : workspaceDragSession.active
                                    ? workspaceDragSession.statusText
                                    : root.selectedNodePath.length > 0
                                        ? "Selected  ·  " + root.selectedNodePath
                                        : LibraryStore.movingFile
                                            ? "Moving file…"
                                            : LibraryStore.scanning
                                            ? "Scanning Library…"
                                        : LibraryStore.loadingFiles
                                            ? "Loading file catalog…"
                                            : LibraryStore.latestScan.status
                                                ? "Catalog  ·  " + String(LibraryStore.latestScan.status)
                                                : "Ready  ·  API connected"
                            color: LibraryStore.errorMessage.length > 0
                                ? root.theme.danger
                                : root.theme.mutedText
                            font.pixelSize: root.theme.typeSize(9)
                            opacity: 0.72
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }

    Component {
        id: placeholderView

        Item {
            Column {
                anchors.centerIn: parent
                width: Math.max(140, parent.width - 30)
                spacing: 10

                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 46
                    height: 46
                    radius: 13
                    color: root.theme.accentSoft
                    border.width: 1
                    border.color: "#554a7b"

                    Text {
                        anchors.centerIn: parent
                        text: root.activeViewIndex === 1 ? "▤" : root.activeViewIndex === 2 ? "⌕" : root.activeViewIndex === 3 ? "◇" : "⚒"
                        color: root.theme.accentBright
                        font.pixelSize: root.theme.typeSize(20)
                    }
                }

                Text {
                    width: parent.width
                    text: root.viewTitles[root.activeViewIndex]
                    color: root.theme.appText
                    font.pixelSize: root.theme.typeBody
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }

                Text {
                    width: parent.width
                    text: "This surface is structurally ready and will be connected after the native Workbench is proven."
                    color: root.theme.mutedText
                    font.pixelSize: root.theme.typeSize(10)
                    lineHeight: root.theme.typeLineHeightCompact
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }
            }
        }
    }
}

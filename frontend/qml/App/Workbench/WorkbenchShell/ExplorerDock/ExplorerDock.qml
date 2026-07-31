import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
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
    property int toolbarHoverIndex: -1
    property string contextNodeId: ""
    property string contextFileId: ""
    property string contextRelativePath: ""
    property string contextTitle: ""
    property bool contextFolder: false
    property string entryOperationMode: ""
    property string entryOperationParent: ""
    property string entryOperationFileId: ""
    property string entryOperationInitialName: ""
    property bool rootDropActive: false
    property var treeView: null
    property int treeViewportRevision: 0
    property string workspaceCollectionId: ""
    property string treeStateLibraryId: ""
    property string pendingSelectedLibraryId: ""
    property bool collectionLibraryRestorePending: false
    property bool restoringLibraryTreeState: false
    property bool treeStateRestorePending: false
    property string pendingCreatedLibraryId: ""
    property string pendingCreatedCollectionId: ""
    property string pendingRevealLibraryId: ""
    property string pendingRevealFileId: ""
    property string pendingRevealRelativePath: ""
    property var pendingTreeViewport: ({
        contentY: 0,
        nodeId: "",
        offset: 0
    })
    property var filterField: null
    property bool scheduledNodeReveal: false
    property bool scheduledNodeStateSave: false
    readonly property var selectedCollectionScope: CollectionStore.scope || ({})
    readonly property var selectedCollectionLibraryIds:
        selectedCollectionScope.libraryIds || []
    readonly property var scopedLibraries: filteredLibraries(
        LibraryStore.libraries,
        selectedCollectionLibraryIds,
        CollectionStore.selectedCollectionId
    )
    readonly property string libraryFlowError: LibraryStore.errorMessage.length > 0
        ? LibraryStore.errorMessage
        : pendingCreatedLibraryId.length > 0
            ? CollectionStore.errorMessage
            : ""

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

        cancelScheduledNodeRebuild()
        filterRebuildTimer.stop()
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
        cancelScheduledNodeRebuild()
        rebuildNodesFromFiles(false)
        var anchor = pendingTreeViewport
        treeStateRestorePending = false
        var revealed = applyPendingReveal()

        if (!revealed) {
            restoreTreeViewport(
                anchor,
                treeViewportRevision
            )
        }

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
        WorkspaceState.sync()

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

        collectionLibraryRestorePending = false
        pendingSelectedLibraryId = ""
        saveCurrentLibraryTreeState()
        treeStateLibraryId = ""
        WorkspaceState.setValue(
            collectionExplorerStateKey(
                workspaceCollectionId,
                "selectedLibraryId"
            ),
            targetLibraryId
        )
        WorkspaceState.sync()
        LibraryStore.selectLibrary(targetLibraryId)
    }

    function clearPendingReveal() {
        pendingRevealLibraryId = ""
        pendingRevealFileId = ""
        pendingRevealRelativePath = ""
    }

    function expandRevealParents(relativePath) {
        var normalized = String(relativePath || "")
            .split("\\").join("/")
            .replace(/^\.\//, "")
        var parts = normalized.split("/")
        parts.pop()
        var pathParts = []
        var nextExpanded = cloneExpandedNodes(expandedNodeIds)

        for (var index = 0; index < parts.length; index += 1) {
            if (parts[index].length === 0) {
                continue
            }

            pathParts.push(parts[index])
            nextExpanded["folder:" + pathParts.join("/")] = true
        }

        expandedNodeIds = nextExpanded
    }

    function applyPendingReveal() {
        if (
            pendingRevealLibraryId.length === 0
            || pendingRevealFileId.length === 0
            || LibraryStore.loadingFiles
            || String(LibraryStore.selectedLibraryId || "")
                !== pendingRevealLibraryId
        ) {
            return false
        }

        var nodeId = "file:" + pendingRevealFileId
        var nodeExists = false

        for (var index = 0; index < treeNodes.length; index += 1) {
            if (String(treeNodes[index].id || "") === nodeId) {
                nodeExists = true
                break
            }
        }

        if (!nodeExists) {
            clearPendingReveal()
            return false
        }

        expandRevealParents(pendingRevealRelativePath)
        selectedNodeId = nodeId
        selectedNodePath = pendingRevealRelativePath
        rebuildTree(false)
        clearPendingReveal()
        scheduleLibraryTreeStateSave()

        var revision = treeViewportRevision
        Qt.callLater(function() {
            if (
                !root.treeView
                || revision !== root.treeViewportRevision
            ) {
                return
            }

            var visibleIndex = root.visibleIndexForNode(nodeId)
            if (visibleIndex >= 0) {
                root.treeView.positionViewAtIndex(
                    visibleIndex,
                    ListView.Contain
                )
                root.treeView.forceLayout()
            }
        })

        return true
    }

    function revealFile(libraryId, fileId, relativePath) {
        var targetLibraryId = String(libraryId || "")
        var targetFileId = String(fileId || "")

        if (
            targetLibraryId.length === 0
            || targetFileId.length === 0
        ) {
            return
        }

        pendingRevealLibraryId = targetLibraryId
        pendingRevealFileId = targetFileId
        pendingRevealRelativePath = String(relativePath || "")

        if (
            String(LibraryStore.selectedLibraryId || "")
                !== targetLibraryId
        ) {
            selectLibrary(targetLibraryId)
            return
        }

        if (!LibraryStore.loadingFiles) {
            applyPendingReveal()
        }
    }

    function placeholderGlyphForFile(fileName, extension) {
        var suffix = String(extension || "").toLowerCase()
        if (suffix.charAt(0) === ".") {
            suffix = suffix.slice(1)
        }

        if (suffix.length === 0) {
            var name = String(fileName || "")
            var dot = name.lastIndexOf(".")
            suffix = dot >= 0 ? name.slice(dot + 1).toLowerCase() : ""
        }

        var glyphs = {
            "ts": "TS",
            "tsx": "TX",
            "js": "JS",
            "jsx": "JX",
            "qml": "Q",
            "cpp": "C+",
            "cc": "C+",
            "cxx": "C+",
            "c": "C",
            "h": "H",
            "hpp": "H+",
            "py": "PY",
            "rs": "RS",
            "go": "GO",
            "java": "JV",
            "cs": "C#",
            "swift": "SW",
            "kt": "KT",
            "md": "MD",
            "markdown": "MD",
            "json": "{}",
            "yaml": "Y",
            "yml": "Y",
            "toml": "T",
            "xml": "<>",
            "html": "<>",
            "css": "#",
            "scss": "S#",
            "less": "L#",
            "sh": "SH",
            "bash": "SH",
            "sql": "DB",
            "pdf": "P",
            "png": "IM",
            "jpg": "IM",
            "jpeg": "IM",
            "gif": "IM",
            "webp": "IM",
            "svg": "SV",
            "txt": "T"
        }

        return glyphs[suffix] || "·"
    }

    function gitStatusPriority(status) {
        switch (String(status || "")) {
        case "conflicted": return 60
        case "deleted": return 50
        case "renamed": return 40
        case "added": return 30
        case "untracked": return 20
        case "modified": return 10
        case "ignored": return 1
        default: return 0
        }
    }

    function strongerGitStatus(currentStatus, candidateStatus) {
        return gitStatusPriority(candidateStatus)
            > gitStatusPriority(currentStatus)
                ? String(candidateStatus || "")
                : String(currentStatus || "")
    }

    function gitBranchLabel() {
        var status = LibraryStore.gitStatus || ({})

        if (LibraryStore.loadingGitStatus) {
            return "Checking Git…"
        }

        if (!status.repository) {
            return "No Git repository"
        }

        var label = status.detached
            ? "Detached HEAD"
            : String(status.branch || "Git repository")
        var movement = []

        if (Number(status.ahead || 0) > 0) {
            movement.push("↑" + Number(status.ahead))
        }
        if (Number(status.behind || 0) > 0) {
            movement.push("↓" + Number(status.behind))
        }
        if (movement.length > 0) {
            label += "  " + movement.join(" ")
        }

        return label
    }

    function gitChangeCount() {
        var status = LibraryStore.gitStatus || ({})
        var counts = status.counts || ({})
        var keys = [
            "modified",
            "added",
            "deleted",
            "renamed",
            "conflicted",
            "untracked"
        ]
        var count = 0

        for (var index = 0; index < keys.length; index += 1) {
            count += Number(counts[keys[index]] || 0)
        }

        if (count > 0 || !status.entries) {
            return count
        }

        for (
            var entryIndex = 0;
            entryIndex < status.entries.length;
            entryIndex += 1
        ) {
            if (
                String(status.entries[entryIndex].status || "")
                !== "ignored"
            ) {
                count += 1
            }
        }

        return count
    }

    function gitChangeLabel() {
        var status = LibraryStore.gitStatus || ({})
        var count = gitChangeCount()

        if (!status.repository || count === 0) {
            return status.repository ? "Clean" : ""
        }

        return count === 1 ? "1 change" : String(count) + " changes"
    }

    function scheduleNodeRebuild(
        applyReveal,
        saveTreeState
    ) {
        scheduledNodeReveal =
            scheduledNodeReveal || Boolean(applyReveal)
        scheduledNodeStateSave =
            scheduledNodeStateSave || Boolean(saveTreeState)
        nodeRebuildTimer.restart()
    }

    function cancelScheduledNodeRebuild() {
        nodeRebuildTimer.stop()
        scheduledNodeReveal = false
        scheduledNodeStateSave = false
    }

    function performScheduledNodeRebuild() {
        var shouldReveal = scheduledNodeReveal
        var shouldSave = scheduledNodeStateSave

        scheduledNodeReveal = false
        scheduledNodeStateSave = false
        rebuildNodesFromFiles(true)

        if (shouldReveal) {
            applyPendingReveal()
        }

        if (shouldSave) {
            scheduleLibraryTreeStateSave()
        }
    }

    function rebuildNodesFromFiles(preserveViewport) {
        var nodes = []
        var directories = ({})
        var catalogDirectories = LibraryStore.directories || []
        var files = LibraryStore.files || []
        var gitEntries = (LibraryStore.gitStatus || ({})).entries || []
        var gitByPath = ({})
        var folderGit = ({})
        var ignoredDirectoryPaths = ({})

        function normalizedPath(value) {
            return String(value || "")
                .split("\\").join("/")
                .replace(/^\.\//, "")
                .replace(/\/{2,}/g, "/")
                .replace(/^\/+|\/+$/g, "")
        }

        function pathIsIgnored(relativePath) {
            var parts = normalizedPath(relativePath).split("/")
            var pathParts = []

            for (var index = 0; index < parts.length; index += 1) {
                if (parts[index].length === 0) {
                    continue
                }

                pathParts.push(parts[index])
                if (
                    ignoredDirectoryPaths[pathParts.join("/")]
                    === true
                ) {
                    return true
                }
            }

            return false
        }

        function aggregateFolderStatus(
            filePath,
            status,
            includesDirectory
        ) {
            var parts = normalizedPath(filePath).split("/")
            if (!includesDirectory) {
                parts.pop()
            }
            var pathParts = []
            var normalizedStatus = String(status || "")

            for (var index = 0; index < parts.length; index += 1) {
                if (parts[index].length === 0) {
                    continue
                }

                pathParts.push(parts[index])
                var directoryPath = pathParts.join("/")
                var aggregate = folderGit[directoryPath] || {
                    count: 0,
                    ignoredCount: 0,
                    statuses: ({})
                }

                if (normalizedStatus === "ignored") {
                    aggregate.ignoredCount += 1
                } else {
                    aggregate.count += 1
                    aggregate.statuses[normalizedStatus] =
                        Number(
                            aggregate.statuses[normalizedStatus]
                            || 0
                        ) + 1
                }

                folderGit[directoryPath] = aggregate
            }
        }

        function dominantFolderStatus(aggregate) {
            var value = aggregate || ({})
            var statuses = value.statuses || ({})
            var conflictedCount = Number(statuses.conflicted || 0)
            var untrackedCount = Number(statuses.untracked || 0)
            var addedCount = Number(statuses.added || 0)
            var changedCount = Number(statuses.modified || 0)
                + Number(statuses.renamed || 0)
                + Number(statuses.deleted || 0)

            if (conflictedCount > 0) {
                return "conflicted"
            }

            if (
                untrackedCount > 0
                && addedCount === 0
                && changedCount === 0
            ) {
                return "untracked"
            }

            if (
                addedCount > 0
                && untrackedCount === 0
                && changedCount === 0
            ) {
                return "added"
            }

            if (
                changedCount > 0
                || addedCount > 0
                || untrackedCount > 0
            ) {
                return "modified"
            }

            return Number(value.ignoredCount || 0) > 0
                ? "ignored"
                : ""
        }

        for (var gitIndex = 0; gitIndex < gitEntries.length; gitIndex += 1) {
            var gitEntry = gitEntries[gitIndex] || ({})
            var gitPath = normalizedPath(gitEntry.path)
            var gitState = String(gitEntry.status || "")

            if (gitPath.length === 0 || gitState.length === 0) {
                continue
            }

            gitByPath[gitPath] = strongerGitStatus(
                gitByPath[gitPath],
                gitState
            )

            if (
                gitState === "ignored"
                && gitEntry.directory === true
            ) {
                ignoredDirectoryPaths[gitPath] = true
            }

            aggregateFolderStatus(
                gitPath,
                gitState,
                gitEntry.directory === true
            )
        }

        function appendDirectoryPath(relativePath) {
            var rawParts = normalizedPath(relativePath).split("/")
            var parts = []

            for (
                var partIndex = 0;
                partIndex < rawParts.length;
                partIndex += 1
            ) {
                if (rawParts[partIndex].length > 0) {
                    parts.push(rawParts[partIndex])
                }
            }

            var parentId = ""
            var pathParts = []

            for (
                var directoryIndex = 0;
                directoryIndex < parts.length;
                directoryIndex += 1
            ) {
                pathParts.push(parts[directoryIndex])
                var directoryPath = pathParts.join("/")
                var directoryId = "folder:" + directoryPath

                if (directories[directoryId] !== true) {
                    directories[directoryId] = true
                    var aggregate = folderGit[directoryPath] || ({})
                    var folderStatus =
                        dominantFolderStatus(aggregate)
                    if (
                        folderStatus.length === 0
                        && pathIsIgnored(directoryPath)
                    ) {
                        folderStatus = "ignored"
                    }

                    nodes.push({
                        id: directoryId,
                        parentId: parentId,
                        title: parts[directoryIndex],
                        glyph: "",
                        iconId: "folder",
                        folder: true,
                        fileId: "",
                        relativePath: directoryPath,
                        muted: false,
                        warning: false,
                        gitStatus: folderStatus,
                        gitCount: Number(aggregate.count || 0)
                    })
                }

                parentId = directoryId
            }

            return parentId
        }

        for (
            var directoryCatalogIndex = 0;
            directoryCatalogIndex < catalogDirectories.length;
            directoryCatalogIndex += 1
        ) {
            appendDirectoryPath(
                String(catalogDirectories[directoryCatalogIndex] || "")
            )
        }

        for (
            var fileIndex = 0;
            fileIndex < files.length;
            fileIndex += 1
        ) {
            var file = files[fileIndex]
            var relativePath = normalizedPath(
                file.relativePath || file.name || ""
            )
            var separator = relativePath.lastIndexOf("/")
            var directoryPath = separator >= 0
                ? relativePath.slice(0, separator)
                : ""
            var parentId = appendDirectoryPath(directoryPath)

            var fileGitStatus = String(
                gitByPath[relativePath] || ""
            )
            if (
                fileGitStatus.length === 0
                && pathIsIgnored(relativePath)
            ) {
                fileGitStatus = "ignored"
            }

            nodes.push({
                id: "file:" + String(file.id),
                parentId: parentId,
                title: String(file.name || relativePath),
                glyph: placeholderGlyphForFile(
                    file.name,
                    file.extension
                ),
                iconId: FileIdentity.iconIdFor({
                    fileName: file.name,
                    extension: file.extension
                }),
                folder: false,
                fileId: String(file.id),
                relativePath: relativePath,
                muted: file.status !== "available",
                warning: file.status !== "available",
                gitStatus: fileGitStatus,
                gitCount: 0
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
        for (
            var groupedIndex = 0;
            groupedIndex < nodes.length;
            groupedIndex += 1
        ) {
            var groupedNode = nodes[groupedIndex]
            var groupKey = groupedNode.parentId

            if (!groupedChildren[groupKey]) {
                groupedChildren[groupKey] = []
            }

            groupedChildren[groupKey].push(groupedNode)
        }

        treeNodes = nodes
        childrenByParent = groupedChildren

        var nextExpandedNodeIds = ({})
        for (var expandedId in expandedNodeIds) {
            if (
                expandedNodeIds[expandedId] === true
                && directories[expandedId] === true
            ) {
                nextExpandedNodeIds[expandedId] = true
            }
        }
        expandedNodeIds = nextExpandedNodeIds

        var selectedStillExists = false
        for (
            var nodeIndex = 0;
            nodeIndex < nodes.length;
            nodeIndex += 1
        ) {
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

    function filteredLibraries(catalog, scopedLibraryIds, collectionId) {
        if (String(collectionId || "").length === 0) {
            return []
        }

        var libraries = catalog || []
        var libraryIds = scopedLibraryIds || []
        var librariesById = ({})
        var filtered = []

        for (var catalogIndex = 0; catalogIndex < libraries.length; catalogIndex += 1) {
            var library = libraries[catalogIndex]
            var libraryId = String(library.id || "")

            if (libraryId.length > 0) {
                librariesById[libraryId] = library
            }
        }

        for (var scopeIndex = 0; scopeIndex < libraryIds.length; scopeIndex += 1) {
            var scopedLibraryId = String(libraryIds[scopeIndex] || "")
            var scopedLibrary = librariesById[scopedLibraryId]

            if (scopedLibrary) {
                filtered.push(scopedLibrary)
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
                itemIconId: String(node.iconId || "file"),
                itemDepth: depth,
                itemSelected: selectedNodeId === node.id,
                itemMuted: node.muted === true,
                itemFolder: node.folder === true,
                itemFileId: String(node.fileId || ""),
                itemRelativePath: String(node.relativePath || ""),
                itemExpanded: node.folder === true && isExpanded(node.id),
                itemWarning: node.warning === true,
                itemGitStatus: String(node.gitStatus || ""),
                itemGitCount: Number(node.gitCount || 0),
                itemActive: node.folder !== true
                    && String(node.fileId || "").length > 0
                    && String(node.fileId || "")
                        === String(LibraryStore.selectedFileId || "")
                    && String(LibraryStore.activeFileLibraryId || "")
                        === String(LibraryStore.selectedLibraryId || "")
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

    function updateVisibleActiveFile() {
        var activeFileId = String(LibraryStore.selectedFileId || "")
        var activeLibraryId = String(
            LibraryStore.activeFileLibraryId || ""
        )
        var explorerLibraryId = String(
            LibraryStore.selectedLibraryId || ""
        )

        for (var index = 0; index < visibleTree.count; index += 1) {
            var item = visibleTree.get(index)
            visibleTree.setProperty(
                index,
                "itemActive",
                item.itemFolder !== true
                    && activeFileId.length > 0
                    && String(item.itemFileId || "") === activeFileId
                    && activeLibraryId === explorerLibraryId
            )
        }
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

    function directoryForPath(relativePath) {
        var normalized = String(relativePath || "")
            .split("\\")
            .join("/")
        var separator = normalized.lastIndexOf("/")
        return separator >= 0
            ? normalized.slice(0, separator)
            : ""
    }

    function suggestedDuplicateName(fileName) {
        var name = String(fileName || "")
        var dot = name.lastIndexOf(".")

        if (dot <= 0) {
            return name + " copy"
        }

        return name.slice(0, dot)
            + " copy"
            + name.slice(dot)
    }

    function openEntryOperation(mode) {
        entryOperationMode = String(mode || "")
        entryOperationFileId = contextFileId
        entryOperationParent = contextFolder
            ? contextRelativePath
            : directoryForPath(contextRelativePath)

        if (entryOperationMode === "rename") {
            entryOperationInitialName = contextTitle
        } else if (entryOperationMode === "duplicate") {
            entryOperationInitialName = suggestedDuplicateName(
                contextTitle
            )
        } else {
            entryOperationInitialName = ""
        }

        entryOperationDialog.open()
    }

    function submitEntryOperation() {
        var name = String(entryNameField.text || "").trim()

        if (name.length === 0) {
            return
        }

        switch (entryOperationMode) {
        case "newFile":
            LibraryStore.createEntry(
                entryOperationParent,
                name,
                false
            )
            break
        case "newFolder":
            LibraryStore.createEntry(
                entryOperationParent,
                name,
                true
            )
            break
        case "rename":
            LibraryStore.renameFile(
                entryOperationFileId,
                name
            )
            break
        case "duplicate":
            LibraryStore.duplicateFile(
                entryOperationFileId,
                name
            )
            break
        }

        entryOperationDialog.close()
    }

    function showNodeContext(
        nodeId,
        fileId,
        relativePath,
        title,
        folder
    ) {
        contextNodeId = String(nodeId || "")
        contextFileId = String(fileId || "")
        contextRelativePath = String(relativePath || "")
        contextTitle = String(title || "")
        contextFolder = Boolean(folder)
        fileContextMenu.popup()
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

    function openLibraryFolderDialog() {
        var collectionId = String(
            CollectionStore.selectedCollectionId || ""
        )

        if (
            collectionId.length === 0
            || LibraryStore.creatingLibrary
            || CollectionStore.mutating
        ) {
            return
        }

        pendingCreatedCollectionId = collectionId
        libraryFolderDialog.open()
    }

    function activatePendingCreatedLibrary() {
        var libraryId = String(pendingCreatedLibraryId || "")
        var collectionId = String(pendingCreatedCollectionId || "")

        if (
            libraryId.length === 0
            || collectionId.length === 0
            || collectionId !== String(
                CollectionStore.selectedCollectionId || ""
            )
            || !CollectionStore.includesLibrary(libraryId)
        ) {
            return false
        }

        LibraryStore.selectLibraryAndScan(libraryId)
        pendingCreatedLibraryId = ""
        pendingCreatedCollectionId = ""
        return true
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

    FolderDialog {
        id: libraryFolderDialog

        title: "Add Library Folder"
        acceptLabel: "Add Library"
        onAccepted: LibraryStore.createLibrary(selectedFolder)
        onRejected: {
            if (root.pendingCreatedLibraryId.length === 0) {
                root.pendingCreatedCollectionId = ""
            }
        }
    }

    Connections {
        target: CollectionStore

        function onSelectedCollectionIdChanged() {
            root.beginCollectionLibraryRestore()
        }

        function onWorkspaceScopeChanged() {
            if (root.activatePendingCreatedLibrary()) {
                return
            }

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

        function onDirectoriesChanged() {
            if (!root.treeStateRestorePending) {
                root.scheduleNodeRebuild(false, true)
            }
        }

        function onEntryCreated(
            kind,
            relativePath,
            fileId
        ) {
            var path = String(relativePath || "")
            var parentPath = root.directoryForPath(path)

            if (parentPath.length > 0) {
                root.expandedNodeIds[
                    "folder:" + parentPath
                ] = true
            }

            Qt.callLater(function() {
                root.cancelScheduledNodeRebuild()
                root.rebuildNodesFromFiles(true)
                var nodeId = String(kind) === "directory"
                    ? "folder:" + path
                    : "file:" + String(fileId || "")
                root.activateNode(
                    nodeId,
                    String(kind) === "directory",
                    String(fileId || "")
                )
            })
        }

        function onFileRenamed(
            fileId,
            relativePath,
            name
        ) {
            Qt.callLater(function() {
                root.cancelScheduledNodeRebuild()
                root.rebuildNodesFromFiles(true)
                root.selectedNodeId =
                    "file:" + String(fileId || "")
                root.selectedNodePath = String(
                    relativePath || ""
                )
                root.rebuildTree(true)
            })
        }

        function onFileDuplicated(
            fileId,
            relativePath,
            name
        ) {
            Qt.callLater(function() {
                root.cancelScheduledNodeRebuild()
                root.rebuildNodesFromFiles(true)
                root.activateNode(
                    "file:" + String(fileId || ""),
                    false,
                    String(fileId || "")
                )
            })
        }

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

            root.scheduleNodeRebuild(true, true)
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

            var restoringExpectedLibrary =
                root.collectionLibraryRestorePending
                && root.pendingSelectedLibraryId.length > 0
                && root.pendingSelectedLibraryId
                    !== nextLibraryId

            if (
                !restoringExpectedLibrary
                && root.workspaceCollectionId.length > 0
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
                WorkspaceState.sync()
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

        function onLibraryCreated(library) {
            var libraryId = String(library.id || "")
            var collectionId = String(
                root.pendingCreatedCollectionId
                    || CollectionStore.selectedCollectionId
                    || ""
            )

            if (
                libraryId.length === 0
                || collectionId.length === 0
            ) {
                return
            }

            root.pendingCreatedLibraryId = libraryId
            root.pendingCreatedCollectionId = collectionId
            CollectionStore.addLibraryToCollection(
                collectionId,
                libraryId
            )
        }

        function onLoadingLibrariesChanged() {
            if (!LibraryStore.loadingLibraries) {
                root.tryRestoreCollectionLibrary()
            }
        }

        function onLoadingFilesChanged() {
            if (LibraryStore.loadingFiles) {
                return
            }

            if (root.treeStateRestorePending) {
                Qt.callLater(
                    root.finishPendingLibraryTreeRestore
                )
                return
            }

            root.scheduleNodeRebuild(true, true)
        }

        function onSelectedFileIdChanged() {
            root.updateVisibleActiveFile()
        }

        function onActiveFileLibraryChanged() {
            root.updateVisibleActiveFile()
        }

        function onGitStatusChanged() {
            if (
                !root.treeStateRestorePending
                && !LibraryStore.loadingFiles
            ) {
                root.scheduleNodeRebuild(false, false)
            }
        }
    }

    Timer {
        id: nodeRebuildTimer

        interval: 24
        repeat: false
        onTriggered: root.performScheduledNodeRebuild()
    }

    Timer {
        id: filterRebuildTimer

        interval: 70
        repeat: false
        onTriggered: {
            root.rebuildTree(false)
            root.restoreTreeViewport(({
                contentY: 0,
                nodeId: "",
                offset: 0
            }))
            root.scheduleLibraryTreeStateSave()
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

    Menu {
        id: fileContextMenu

        MenuItem {
            text: "New File"
            enabled: !LibraryStore.mutatingEntry
            onTriggered: root.openEntryOperation("newFile")
        }

        MenuItem {
            text: "New Folder"
            enabled: !LibraryStore.mutatingEntry
            onTriggered: root.openEntryOperation("newFolder")
        }

        MenuSeparator {}

        MenuItem {
            text: "Rename"
            visible: !root.contextFolder
            enabled:
                root.contextFileId.length > 0
                && !LibraryStore.mutatingEntry
            onTriggered: root.openEntryOperation("rename")
        }

        MenuItem {
            text: "Duplicate"
            visible: !root.contextFolder
            enabled:
                root.contextFileId.length > 0
                && !LibraryStore.mutatingEntry
            onTriggered: root.openEntryOperation("duplicate")
        }

        MenuSeparator {}

        MenuItem {
            text: Qt.platform.os === "osx"
                ? "Reveal in Finder"
                : "Reveal in File Manager"
            enabled:
                root.contextRelativePath.length > 0
                && (root.contextFolder || root.contextFileId.length > 0)
                && !LibraryStore.mutatingEntry
            onTriggered: LibraryStore.revealEntry(
                root.contextRelativePath
            )
        }
    }

    Popup {
        id: entryOperationDialog

        parent: Overlay.overlay
        x: parent
            ? Math.round((parent.width - width) / 2)
            : 0
        y: parent
            ? Math.round((parent.height - height) / 2)
            : 0
        width: Math.min(
            380,
            parent ? parent.width - 48 : 380
        )
        height: 154
        padding: 0
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape

        onOpened: {
            entryNameField.text =
                root.entryOperationInitialName
            entryNameField.forceActiveFocus()
            entryNameField.selectAll()
        }

        background: Rectangle {
            color: root.theme.surfaceBg
            border.width: 1
            border.color: root.theme.panelBorder
            radius: root.theme.radiusPanel
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 10

            Text {
                Layout.fillWidth: true
                text: root.entryOperationMode === "newFile"
                    ? "New File"
                    : root.entryOperationMode === "newFolder"
                        ? "New Folder"
                        : root.entryOperationMode === "rename"
                            ? "Rename File"
                            : "Duplicate File"
                color: root.theme.appText
                font.family: root.theme.bodyFontFamily
                font.pixelSize:
                    root.theme.textPanelTitleSize
                font.weight:
                    root.theme.textWeightStrong
            }

            TextField {
                id: entryNameField

                Layout.fillWidth: true
                Layout.preferredHeight: 32
                color: root.theme.appText
                selectionColor: root.theme.accentSoft
                selectedTextColor: root.theme.appText
                placeholderText: "Name"
                placeholderTextColor: root.theme.mutedText
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.textControlSize
                selectByMouse: true
                maximumLength: 255
                onAccepted: root.submitEntryOperation()

                background: Rectangle {
                    radius: 3
                    color: root.theme.controlSurfaceBg
                    border.width: 1
                    border.color: parent.activeFocus
                        ? root.theme.accent
                        : root.theme.quietBorder
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Item {
                    Layout.fillWidth: true
                }

                Button {
                    Layout.preferredWidth: 82
                    Layout.preferredHeight: 30
                    text: "CANCEL"
                    hoverEnabled: true
                    padding: 0
                    onClicked: entryOperationDialog.close()

                    contentItem: Text {
                        text: parent.text
                        color: parent.hovered
                            ? root.theme.appText
                            : root.theme.mutedText
                        font.family:
                            root.theme.bodyFontFamily
                        font.pixelSize:
                            root.theme.textMetadataSize
                        font.weight:
                            root.theme.textWeightStrong
                        horizontalAlignment:
                            Text.AlignHCenter
                        verticalAlignment:
                            Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: 3
                        color: parent.hovered
                            ? root.theme.hoverBg
                            : "transparent"
                    }
                }

                Button {
                    Layout.preferredWidth: 82
                    Layout.preferredHeight: 30
                    enabled:
                        entryNameField.text.trim().length > 0
                        && !LibraryStore.mutatingEntry
                    text: root.entryOperationMode === "rename"
                        ? "RENAME"
                        : root.entryOperationMode === "duplicate"
                            ? "DUPLICATE"
                            : "CREATE"
                    hoverEnabled: true
                    padding: 0
                    onClicked: root.submitEntryOperation()

                    contentItem: Text {
                        text: parent.text
                        color: parent.enabled
                            ? root.theme.accentBright
                            : root.theme.mutedText
                        opacity: parent.enabled ? 1 : 0.45
                        font.family:
                            root.theme.bodyFontFamily
                        font.pixelSize:
                            root.theme.textMetadataSize
                        font.weight:
                            root.theme.textWeightStrong
                        horizontalAlignment:
                            Text.AlignHCenter
                        verticalAlignment:
                            Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: 3
                        color: parent.hovered
                            ? root.theme.activeBg
                            : root.theme.controlSurfaceBg
                        border.width: 1
                        border.color: parent.enabled
                            ? root.theme.accent
                            : root.theme.quietBorder
                    }
                }
            }
        }
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

                        LanguageIcon {
                            iconId: String(
                                workspaceDragSession.payload.iconId || "file"
                            )
                            fileName: workspaceDragSession.sourceLabel
                            tone: workspaceDragSession.dropAllowed
                                ? "accent"
                                : "muted"
                            iconSize: 16
                            accessibleLabel: workspaceDragSession.sourceLabel
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
                    Layout.preferredHeight: 30
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
                        anchors.leftMargin: 6
                        anchors.rightMargin: 6
                        spacing: 3

                        ComboBox {
                            id: librarySelector

                            Layout.fillWidth: true
                            Layout.preferredHeight: 24
                            Layout.alignment: Qt.AlignVCenter
                            model: root.scopedLibraries
                            textRole: "name"
                            valueRole: "id"
                            enabled: !LibraryStore.loadingLibraries && count > 0
                            hoverEnabled: true
                            leftPadding: 8
                            rightPadding: 22

                            ToolTip.visible: hovered && !popup.visible
                            ToolTip.text: "Libraries"
                            ToolTip.delay: 3000
                            ToolTip.timeout: 2400

                            Binding {
                                target: librarySelector
                                property: "currentIndex"
                                value: root.libraryIndexForId(LibraryStore.selectedLibraryId)
                            }

                            onActivated: function(index) {
                                var library = root.scopedLibraries[index]
                                if (library) {
                                    root.selectLibrary(String(library.id))
                                }
                            }

                            contentItem: Text {
                                text: librarySelector.displayText.length > 0
                                    ? librarySelector.displayText
                                    : LibraryStore.loadingLibraries
                                        ? "Loading Libraries…"
                                        : "No Libraries"
                                color: root.theme.appText
                                font.pixelSize: root.theme.typeSize(10)
                                font.weight: Font.DemiBold
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }

                            indicator: AppIcon {
                                anchors.right: parent.right
                                anchors.rightMargin: 7
                                anchors.verticalCenter: parent.verticalCenter
                                name: librarySelector.popup.visible
                                    ? "chevron-up"
                                    : "chevron-down"
                                tone: librarySelector.popup.visible
                                    ? "accent"
                                    : "muted"
                                iconSize: 14
                                accessibleLabel: "Choose Library"
                            }

                            background: Rectangle {
                                radius: 5
                                color: parent.popup.visible
                                    ? root.theme.activeBg
                                    : parent.hovered
                                        ? root.theme.hoverBg
                                        : root.theme.surfaceBg
                                border.width: 1
                                border.color: parent.popup.visible
                                    ? "#554a7b"
                                    : root.theme.quietBorder

                                Behavior on color {
                                    ColorAnimation {
                                        duration: root.theme.motionFast
                                    }
                                }
                            }

                            popup: Popup {
                                id: libraryPopup

                                y: librarySelector.height + 3
                                width: Math.min(
                                    Math.max(librarySelector.width + 96, 260),
                                    root.width - 18
                                )
                                height: Math.min(
                                    Math.max(1, librarySelector.count) * 28 + 8,
                                    232
                                )
                                padding: 4
                                closePolicy: Popup.CloseOnEscape
                                    | Popup.CloseOnPressOutsideParent

                                onOpened: Qt.callLater(function() {
                                    if (librarySelector.currentIndex >= 0) {
                                        libraryPopupList.positionViewAtIndex(
                                            librarySelector.currentIndex,
                                            ListView.Contain
                                        )
                                    }
                                })

                                contentItem: ListView {
                                    id: libraryPopupList

                                    clip: true
                                    model: librarySelector.popup.visible
                                        ? librarySelector.delegateModel
                                        : null
                                    currentIndex: librarySelector.highlightedIndex
                                    boundsBehavior: Flickable.StopAtBounds
                                    flickableDirection: Flickable.VerticalFlick
                                    interactive: contentHeight > height
                                    highlightMoveDuration: root.theme.motionFast

                                    ScrollBar.vertical: ScrollBar {
                                        policy: ScrollBar.AsNeeded
                                        interactive: true
                                    }
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

                                width: ListView.view
                                    ? ListView.view.width
                                    : libraryPopup.width - 8
                                height: 28
                                hoverEnabled: true
                                highlighted: librarySelector.highlightedIndex === index
                                leftPadding: 8
                                rightPadding: 8
                                topPadding: 0
                                bottomPadding: 0

                                contentItem: RowLayout {
                                    spacing: 8

                                    Text {
                                        Layout.fillWidth: true
                                        text: String(modelData.name || "Library")
                                        color: root.theme.appText
                                        font.pixelSize: root.theme.typeSize(10)
                                        font.weight: Font.DemiBold
                                        verticalAlignment: Text.AlignVCenter
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        Layout.preferredWidth: Math.min(
                                            150,
                                            Math.max(72, libraryPopup.width * 0.42)
                                        )
                                        text: String(modelData.rootPath || "")
                                        color: root.theme.mutedText
                                        font.pixelSize: root.theme.typeSize(8)
                                        horizontalAlignment: Text.AlignRight
                                        verticalAlignment: Text.AlignVCenter
                                        elide: Text.ElideMiddle
                                        opacity: 0.76
                                    }
                                }

                                background: Rectangle {
                                    radius: 4
                                    color: parent.highlighted || parent.hovered
                                        ? root.theme.hoverBg
                                        : "transparent"
                                }
                            }
                        }

                        IconButton {
                            id: addLibraryButton

                            theme: root.theme
                            Layout.preferredWidth: 24
                            Layout.preferredHeight: 24
                            Layout.alignment: Qt.AlignVCenter
                            width: 24
                            height: 24
                            circular: true
                            idleBackgroundColor: root.theme.surfaceBg
                            hoverBackgroundColor: root.theme.hoverBg
                            iconName: "add"
                            iconTone: enabled && hovered ? "accent" : "normal"
                            iconSize: 15
                            toolTipText: CollectionStore.selectedCollectionId.length === 0
                                ? "Select a Collection before adding a Library"
                                : "Add a folder as a Library"
                            enabled: CollectionStore.selectedCollectionId.length > 0
                                && !LibraryStore.creatingLibrary
                                && !CollectionStore.mutating
                            onClicked: root.openLibraryFolderDialog()
                        }


                        IconButton {
                            id: collapseAllButton

                            theme: root.theme
                            Layout.preferredWidth: 24
                            Layout.preferredHeight: 24
                            Layout.alignment: Qt.AlignVCenter
                            width: 24
                            height: 24
                            circular: true
                            idleBackgroundColor: root.theme.surfaceBg
                            hoverBackgroundColor: root.theme.hoverBg
                            iconName: "chevron-up"
                            iconTone: hovered ? "normal" : "muted"
                            iconSize: 15
                            toolTipText: "Collapse all folders"
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
                        }

                        IconButton {
                            id: expandAllButton

                            theme: root.theme
                            Layout.preferredWidth: 24
                            Layout.preferredHeight: 24
                            Layout.alignment: Qt.AlignVCenter
                            width: 24
                            height: 24
                            circular: true
                            idleBackgroundColor: root.theme.surfaceBg
                            hoverBackgroundColor: root.theme.hoverBg
                            iconName: "chevron-down"
                            iconTone: hovered ? "normal" : "muted"
                            iconSize: 15
                            toolTipText: "Expand all folders"
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
                        }

                        IconButton {
                            id: refreshLibrariesButton

                            theme: root.theme
                            Layout.preferredWidth: 24
                            Layout.preferredHeight: 24
                            Layout.alignment: Qt.AlignVCenter
                            width: 24
                            height: 24
                            circular: true
                            idleBackgroundColor: root.theme.surfaceBg
                            hoverBackgroundColor: root.theme.hoverBg
                            iconName: "refresh"
                            iconTone: hovered ? "normal" : "muted"
                            iconSize: 15
                            toolTipText: LibraryStore.scanning
                                ? "Scanning Library"
                                : "Rescan selected Library"
                            enabled: LibraryStore.selectedLibraryId.length > 0
                                && !LibraryStore.scanning
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
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 25
                    color: root.theme.surfaceBg
                    visible: LibraryStore.selectedLibraryId.length > 0

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: root.theme.quietBorder
                        opacity: 0.7
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 7
                        spacing: 6

                        Rectangle {
                            Layout.preferredWidth: 6
                            Layout.preferredHeight: 6
                            radius: 3
                            color: (LibraryStore.gitStatus || ({})).dirty
                                ? "#d7a84f"
                                : (LibraryStore.gitStatus || ({})).repository
                                    ? "#74b886"
                                    : root.theme.mutedText
                            opacity: LibraryStore.loadingGitStatus ? 0.55 : 1
                        }

                        Text {
                            Layout.fillWidth: true
                            text: root.gitBranchLabel()
                            color: root.theme.mutedText
                            font.pixelSize: root.theme.typeSize(9)
                            font.weight: Font.DemiBold
                            elide: Text.ElideMiddle
                            verticalAlignment: Text.AlignVCenter
                        }

                        Text {
                            text: root.gitChangeLabel()
                            visible: text.length > 0
                            color: (LibraryStore.gitStatus || ({})).dirty
                                ? "#d7a84f"
                                : root.theme.mutedText
                            font.pixelSize: root.theme.typeSize(8)
                            verticalAlignment: Text.AlignVCenter
                        }

                        IconButton {
                            theme: root.theme
                            Layout.preferredWidth: 20
                            Layout.preferredHeight: 20
                            width: 20
                            height: 20
                            iconName: "refresh"
                            iconTone: hovered ? "normal" : "muted"
                            iconSize: 11
                            toolTipText: "Refresh Git status"
                            enabled: !LibraryStore.loadingGitStatus
                            onClicked: LibraryStore.refreshSelectedGitStatus()
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
                        leftPadding: 30
                        rightPadding: 8
                        selectByMouse: true
                        onTextChanged: {
                            root.filterText = text

                            if (
                                !root.restoringLibraryTreeState
                            ) {
                                filterRebuildTimer.restart()
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

                        AppIcon {
                            anchors.left: parent.left
                            anchors.leftMargin: 9
                            anchors.verticalCenter: parent.verticalCenter
                            name: "search"
                            tone: parent.activeFocus ? "accent" : "muted"
                            iconSize: 13
                            accessibleLabel: "Filter files"
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
                        required property string itemIconId
                        required property int itemDepth
                        required property bool itemSelected
                        required property bool itemMuted
                        required property bool itemFolder
                        required property string itemFileId
                        required property string itemRelativePath
                        required property bool itemExpanded
                        required property bool itemWarning
                        required property string itemGitStatus
                        required property int itemGitCount
                        required property bool itemActive

                        width: libraryList.width
                        theme: root.theme
                        title: itemTitle
                        glyph: itemGlyph
                        iconId: itemIconId
                        depth: itemDepth
                        selected: itemSelected
                        active: itemActive
                        muted: itemMuted
                        folder: itemFolder
                        expanded: itemExpanded
                        warning: itemWarning
                        gitStatus: itemGitStatus
                        gitCount: itemGitCount
                        dragSession: workspaceDragSession
                        dragProxy: fileDragProxy
                        dragEnabled: !LibraryStore.movingFile
                        fileId: itemFileId
                        relativePath: itemRelativePath
                        onActivated: root.activateNode(nodeId, itemFolder, itemFileId)
                        onContextRequested: root.showNodeContext(
                            nodeId,
                            itemFileId,
                            itemRelativePath,
                            itemTitle,
                            itemFolder
                        )
                        onFileDropRequested: function(fileId, targetDirectory) {
                            root.moveFileToFolder(fileId, targetDirectory)
                        }
                    }

                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AsNeeded
                    }
                    }

                    Column {
                        anchors.centerIn: parent
                        width: Math.max(120, parent.width - 28)
                        spacing: 10
                        visible: visibleTree.count === 0

                        Text {
                            width: parent.width
                            text: root.libraryFlowError.length > 0
                                ? root.libraryFlowError
                                    + "\n\nRun npm run dev:backend if the local API is offline."
                                : LibraryStore.creatingLibrary
                                    ? "Adding Library…"
                                    : LibraryStore.loadingLibraries
                                        ? "Loading Libraries…"
                                        : LibraryStore.loadingFiles
                                            ? "Loading files…"
                                            : CollectionStore.selectedCollectionId.length === 0
                                                ? "Create or select a Collection first."
                                                : root.scopedLibraries.length === 0
                                                    ? "No Libraries in this Collection."
                                                    : LibraryStore.selectedLibraryId.length === 0
                                                        ? "Choose a Library to continue."
                                                        : "No cataloged files yet.\nUse Rescan Library to refresh the catalog."
                            color: root.libraryFlowError.length > 0
                                ? root.theme.danger
                                : root.theme.mutedText
                            font.pixelSize: root.theme.typeSize(10)
                            lineHeight: root.theme.typeLineHeightCompact
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                        }

                        Button {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: Math.min(174, parent.width)
                            height: 32
                            visible: root.libraryFlowError.length === 0
                                && CollectionStore.selectedCollectionId.length > 0
                                && root.scopedLibraries.length === 0
                            enabled: !LibraryStore.creatingLibrary
                                && !CollectionStore.mutating
                            text: LibraryStore.creatingLibrary
                                ? "Adding Library…"
                                : "Add Library Folder"
                            hoverEnabled: true
                            padding: 0
                            onClicked: root.openLibraryFolderDialog()

                            contentItem: Text {
                                text: parent.text
                                color: parent.enabled
                                    ? root.theme.appText
                                    : root.theme.mutedText
                                font.pixelSize: root.theme.typeSize(10)
                                font.weight: Font.DemiBold
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }

                            background: Rectangle {
                                radius: 5
                                color: parent.hovered
                                    ? root.theme.activeBg
                                    : root.theme.controlSurfaceBg
                                border.width: 1
                                border.color: parent.hovered
                                    ? root.theme.accent
                                    : root.theme.panelBorder
                            }
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

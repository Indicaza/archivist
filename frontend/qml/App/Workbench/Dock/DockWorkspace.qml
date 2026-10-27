import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Archivist.Services 1.0
import "../../Files/FileIdentity.js" as FileIdentity
import "../Workspace"

// The docking workspace: Anchors (tab groups) in a split layout, filled with
// views of documents, chats and terminals. Layout lives in Docking, text in
// Documents; this item draws them and turns pointer and keyboard input into
// layout operations.
Item {
    id: root

    required property var theme
    property real leftInset: 0

    signal contextInspectionRequested(string messageId)
    signal revealInLibraryRequested(string libraryId, string fileId, string relativePath)

    readonly property alias area: area
    // Set explicitly (not bound) so the per-surface models are fetched once
    // per window, not on every layout revision.
    property string surfaceId: ""
    property bool resizingSplit: false
    property bool resizingSurface: false
    readonly property bool animateLayout: !theme.reducedMotion && !resizingSplit && !resizingSurface
    readonly property bool showFocus: Docking.surfaceIds.length > 1 || anchorCount > 1
    readonly property int anchorCount: anchorRepeater.count

    // What the status bar shows about the focused document.
    readonly property var focusedView: {
        Docking.revision
        return Docking.focusedViewId.length > 0 ? Docking.view(Docking.focusedViewId) : ({})
    }
    readonly property var focusedDetails: focusedView.resource && focusedView.resource.details
        ? focusedView.resource.details
        : ({})
    readonly property bool filePreviewStatusVisible: Boolean(focusedView.resource && focusedView.resource.kind === "document")
    readonly property string filePreviewAccessLabel: {
        Documents.revision
        if (!filePreviewStatusVisible) {
            return ""
        }
        if (focusedView.typeId !== "archivist.editor") {
            return "Preview"
        }
        return Documents.isDirty(focusedView.resource.id) ? "Edited" : "Editable"
    }
    readonly property string filePreviewTypeLabel: filePreviewStatusVisible
        ? String(fileIdentity(fileRecordFor(focusedDetails)).displayLabel || "")
        : ""
    readonly property string filePreviewPath: String(focusedDetails.path || "")
    readonly property string filePreviewMetrics: {
        var size = Number(focusedDetails.sizeBytes || 0)
        if (!filePreviewStatusVisible || size <= 0) {
            return ""
        }
        return size < 1024 ? size + " B" : size < 1048576 ? (size / 1024).toFixed(1) + " KB" : (size / 1048576).toFixed(1) + " MB"
    }
    readonly property bool filePreviewAttached: false

    property var anchorItems: ({})
    property bool quietLayout: false
    property var zoomTargets: ({})

    function registerZoomTarget(viewId, item) {
        zoomTargets[viewId] = item
    }

    function unregisterZoomTarget(viewId, item) {
        if (zoomTargets[viewId] === item) {
            delete zoomTargets[viewId]
        }
    }

    // Ctrl+= / Ctrl+- / Ctrl+0: zoom the focused view only. Editors and
    // terminals keep their zoom in the view's state, so it moves with them.
    function zoomFocusedView(direction) {
        var viewId = Docking.focusedViewId
        if (viewId.length === 0) {
            return
        }
        var view = Docking.view(viewId)
        var preview = zoomTargets[viewId]
        if (view.typeId === "archivist.preview" && preview) {
            if (direction > 0) {
                preview.zoomActiveRendererIn()
            } else if (direction < 0) {
                preview.zoomActiveRendererOut()
            } else {
                preview.resetActiveRendererZoom()
            }
            return
        }
        if (view.typeId !== "archivist.editor" && view.typeId !== "archivist.terminal") {
            return
        }
        var steps = [0.5, 0.67, 0.8, 0.9, 1, 1.1, 1.25, 1.5, 1.75, 2, 2.5, 3]
        var current = Number((view.state || {}).zoom || 1)
        var index = 0
        for (var i = 0; i < steps.length; i += 1) {
            if (Math.abs(steps[i] - current) < Math.abs(steps[index] - current)) {
                index = i
            }
        }
        var next = direction === 0 ? 1 : steps[Math.max(0, Math.min(steps.length - 1, index + direction))]
        Docking.mergeViewState(viewId, { zoom: next })
        zoomBadge.show(Math.round(next * 100) + "%")
    }

    // Ctrl+Alt+= / - / 0: the size of the whole interface.
    function stepInterfaceScale(direction) {
        if (InterfaceScale.controlledByEnvironment) {
            toast.show("Interface size is set by QT_SCALE_FACTOR")
            return
        }
        var scale = direction === 0 ? InterfaceScale.reset() : InterfaceScale.step(direction)
        var label = "Interface size " + Math.round(scale * 100) + "%"
        if (InterfaceScale.restartNeeded) {
            toast.show(label + " · applies after a restart", "Restart Now", function() {
                Docking.saveNow()
                InterfaceScale.restart()
            })
        } else {
            toast.show(label)
        }
    }
    property var terminalItems: ({})
    property string preferredChatViewId: ""
    property string lastSelectionKey: ""
    property var pendingLocation: null

    // ---- registry --------------------------------------------------------

    function registerAnchor(anchorId, item) {
        var items = anchorItems
        items[anchorId] = item
        anchorItems = items
    }

    function unregisterAnchor(anchorId, item) {
        if (anchorItems[anchorId] === item) {
            var items = anchorItems
            delete items[anchorId]
            anchorItems = items
        }
    }

    function registerTerminal(viewId, item) {
        terminalItems[viewId] = item
    }

    function unregisterTerminal(viewId, item) {
        if (terminalItems[viewId] === item) {
            delete terminalItems[viewId]
        }
    }

    // ---- lookups ---------------------------------------------------------

    function libraryFor(libraryId) {
        var libraries = LibraryStore.libraries || []
        for (var index = 0; index < libraries.length; index += 1) {
            if (String(libraries[index].id || "") === String(libraryId || "")) {
                return libraries[index]
            }
        }
        return null
    }

    function libraryRootPath(libraryId) {
        var library = libraryFor(libraryId)
        return library ? String(library.rootPath || "") : ""
    }

    function worktreeLabel(resource) {
        if (!resource || !resource.libraryId) {
            return ""
        }
        var library = libraryFor(resource.libraryId)
        if (!library || String(library.worktreeOfLibraryId || "").length === 0) {
            return ""
        }
        return String(library.branch || library.worktreeBranch || library.name || "worktree")
    }

    function chatTitle(chatId) {
        var chats = ChatStore.chats || []
        for (var index = 0; index < chats.length; index += 1) {
            if (String(chats[index].id || "") === String(chatId || "")) {
                return String(chats[index].title || "Chat")
            }
        }
        return ""
    }

    function viewTitle(info) {
        if (!info || !info.viewId) {
            return ""
        }
        var resource = info.resource || ({})
        if (info.typeId === "archivist.chat") {
            var title = chatTitle(resource.id)
            return title.length > 0 ? title : String(info.title || "Chat")
        }
        if (String(info.title || "").length > 0) {
            return String(info.title)
        }
        var details = resource.details || ({})
        return String(details.name || details.path || "Untitled")
    }

    function anchorTitle(anchorId) {
        var anchor = Docking.anchor(anchorId)
        var active = anchor.activeViewId ? Docking.view(anchor.activeViewId) : null
        var count = (anchor.views || []).length
        var title = active ? viewTitle(active) : "Empty panel"
        return count > 1 ? title + "  +" + (count - 1) : title
    }

    function fileRecordFor(details) {
        details = details || ({})
        if (details.file && details.file.id) {
            return details.file
        }
        return {
            id: String(details.fileId || ""),
            name: String(details.name || ""),
            relativePath: String(details.path || ""),
            extension: String(details.extension || ""),
            mimeType: String(details.mimeType || ""),
            languageId: String(details.languageId || ""),
            sizeBytes: Number(details.sizeBytes || 0),
            status: "available"
        }
    }

    function fileIdentity(file) {
        return FileIdentity.resolve({
            fileName: String(file.name || file.relativePath || ""),
            extension: String(file.extension || ""),
            mimeType: String(file.mimeType || ""),
            languageId: String(file.languageId || "")
        })
    }

    function documentResource(libraryId, file) {
        var identity = fileIdentity(file)
        var collectionId = String(CollectionStore.selectedCollectionId || "")
        var textual = identity.category === "code"
            || identity.category === "data"
            || identity.category === "text"
            || identity.preferredRendererId === "markdown"
            || identity.preferredRendererId === "plain-text"
        return {
            kind: "document",
            id: collectionId + ":" + libraryId + ":" + String(file.id || ""),
            collectionId: collectionId,
            libraryId: String(libraryId || ""),
            details: {
                fileId: String(file.id || ""),
                name: String(file.name || ""),
                path: String(file.relativePath || ""),
                extension: String(file.extension || ""),
                mimeType: String(file.mimeType || ""),
                languageId: String(file.languageId || identity.languageId || ""),
                sizeBytes: Number(file.sizeBytes || 0),
                textual: textual,
                file: file
            }
        }
    }

    function viewTypeForFile(file) {
        var identity = fileIdentity(file)
        return identity.category === "code" || identity.category === "data"
            ? "archivist.editor"
            : "archivist.preview"
    }

    // ---- the live chat ---------------------------------------------------

    // The chat view that shows the live conversation (ChatStore has one
    // selected chat; other chat views show a card until Phase E).
    readonly property string liveChatViewId: {
        Docking.revision
        var chatId = String(ChatStore.selectedChatId || "")
        if (chatId.length === 0) {
            return ""
        }
        var candidates = Docking.viewsForResource("chat", chatId)
        var visible = []
        for (var index = 0; index < candidates.length; index += 1) {
            var row = Docking.viewRow(candidates[index])
            if (row.typeId === "archivist.chat" && row.isActive && !row.isHidden) {
                if (row.viewId === root.preferredChatViewId) {
                    return row.viewId
                }
                if (row.isFocused) {
                    visible.unshift(row.viewId)
                } else {
                    visible.push(row.viewId)
                }
            }
        }
        return visible.length > 0 ? visible[0] : ""
    }
    readonly property var liveChatRow: {
        Docking.revision
        return liveChatViewId.length > 0 ? Docking.viewRow(liveChatViewId) : ({})
    }

    function makeChatLive(viewId) {
        var view = Docking.view(viewId)
        var chatId = String((view.resource || {}).id || "")
        root.preferredChatViewId = viewId
        Docking.activateView(viewId)
        if (chatId.length > 0 && chatId !== ChatStore.selectedChatId) {
            ChatStore.selectChat(chatId)
        }
    }

    function openChat(anchorId) {
        var chatId = String(ChatStore.selectedChatId || "")
        if (chatId.length === 0) {
            var chats = ChatStore.chats || []
            if (chats.length === 0) {
                return
            }
            chatId = String(chats[0].id)
            ChatStore.selectChat(chatId)
        }
        var viewId = Docking.openView(
            "archivist.chat",
            { kind: "chat", id: chatId },
            chatTitle(chatId),
            { anchorId: anchorId || "", reveal: !anchorId }
        )
        root.preferredChatViewId = viewId
    }

    // Follows the chat picked in the side panel: reveal its view, or point
    // the chat panel at it (as before docking), or open one.
    function followSelectedChat() {
        var chatId = String(ChatStore.selectedChatId || "")
        if (chatId.length === 0) {
            return
        }
        var existing = Docking.viewsForResource("chat", chatId)
        for (var index = 0; index < existing.length; index += 1) {
            if (Docking.view(existing[index]).typeId === "archivist.chat") {
                if (root.liveChatViewId !== existing[index]) {
                    root.preferredChatViewId = existing[index]
                    Docking.activateView(existing[index])
                }
                return
            }
        }
        var current = root.preferredChatViewId.length > 0 ? Docking.view(root.preferredChatViewId) : ({})
        if (current.typeId !== "archivist.chat") {
            var chats = Docking.viewIdsOfType("archivist.chat")
            current = chats.length > 0 ? Docking.view(chats[0]) : ({})
        }
        if (current.typeId === "archivist.chat") {
            Docking.setViewResource(current.viewId, { kind: "chat", id: chatId })
            Docking.setViewTitle(current.viewId, chatTitle(chatId))
            root.preferredChatViewId = current.viewId
            Docking.activateView(current.viewId)
            return
        }
        openChat("")
    }

    // ---- files -----------------------------------------------------------

    // Follows a file picked in the side panel (or by go-to-definition).
    function followSelectedFile() {
        var fileId = String(LibraryStore.selectedFileId || "")
        var libraryId = String(LibraryStore.activeFileLibraryId || "")
        if (fileId.length === 0 || libraryId.length === 0) {
            return
        }
        var key = libraryId + ":" + fileId
        if (key === root.lastSelectionKey) {
            return
        }
        root.lastSelectionKey = key

        var file = LibraryStore.selectedFile || ({})
        if (!file.id) {
            file = { id: fileId }
        }
        var resource = documentResource(libraryId, file)
        var reveal = null

        if (root.pendingLocation
                && root.pendingLocation.libraryId === libraryId
                && String(file.relativePath || "") === root.pendingLocation.relativePath) {
            reveal = { line: root.pendingLocation.line, column: root.pendingLocation.column }
            root.pendingLocation = null
        }

        var existing = Docking.viewsForResource("document", resource.id)
        if (existing.length > 0) {
            var target = Docking.targetAnchorId
            var chosen = existing[0]
            for (var index = 0; index < existing.length; index += 1) {
                if (Docking.view(existing[index]).anchorId === target) {
                    chosen = existing[index]
                }
            }
            Docking.activateView(chosen)
            if (reveal) {
                if (Docking.view(chosen).typeId !== "archivist.editor") {
                    Docking.setViewType(chosen, "archivist.editor")
                }
                Docking.mergeViewState(chosen, { reveal: reveal })
                var anchorItem = anchorItems[Docking.view(chosen).anchorId]
                var host = anchorItem ? anchorItem.editorHost() : null
                if (host) {
                    host.openActive()
                }
            }
            return
        }

        Docking.openView(
            reveal ? "archivist.editor" : viewTypeForFile(file),
            resource,
            String(file.name || file.relativePath || "Untitled"),
            reveal ? { state: { reveal: reveal } } : ({})
        )
    }

    // Keeps the side panel's selection on the focused document and the
    // chat composer on the focused chat.
    function syncSelection(viewId) {
        var view = Docking.view(viewId)
        var resource = view.resource || ({})
        if (resource.kind === "document") {
            var details = resource.details || ({})
            var fileId = String(details.fileId || "")
            var key = String(resource.libraryId || "") + ":" + fileId
            if (fileId.length > 0
                    && (LibraryStore.activeFileLibraryId !== resource.libraryId || LibraryStore.selectedFileId !== fileId)
                    && root.libraryFor(resource.libraryId)) {
                root.lastSelectionKey = key
                LibraryStore.previewFileFromLibrary(resource.libraryId, fileId, details.file || ({}))
            }
        } else if (resource.kind === "chat" && view.typeId === "archivist.chat") {
            root.preferredChatViewId = viewId
            if (String(resource.id || "").length > 0 && resource.id !== ChatStore.selectedChatId) {
                ChatStore.selectChat(resource.id)
            }
        }
    }

    function openLocation(filePath, lineNumber, columnNumber) {
        var path = String(filePath || "").replace(/\\/g, "/")
        var libraries = LibraryStore.libraries || []
        var best = null
        for (var index = 0; index < libraries.length; index += 1) {
            var rootPath = String(libraries[index].rootPath || "").replace(/\\/g, "/").replace(/\/$/, "")
            if (rootPath.length > 0 && path.indexOf(rootPath + "/") === 0
                    && (!best || rootPath.length > best.rootPath.length)) {
                best = { id: String(libraries[index].id), rootPath: rootPath }
            }
        }
        if (!best) {
            return
        }
        root.pendingLocation = {
            libraryId: best.id,
            relativePath: path.slice(best.rootPath.length + 1),
            line: lineNumber,
            column: columnNumber
        }
        root.lastSelectionKey = ""
        LibraryStore.previewFilePathFromLibrary(best.id, root.pendingLocation.relativePath)
    }

    function revealView(viewId) {
        var resource = Docking.view(viewId).resource || ({})
        var details = resource.details || ({})
        root.revealInLibraryRequested(String(resource.libraryId || ""), String(details.fileId || ""), String(details.path || ""))
    }

    // ---- terminals -------------------------------------------------------

    function openTerminal(anchorId) {
        var libraryId = String(LibraryStore.selectedLibraryId || "")
        var collectionId = String(CollectionStore.selectedCollectionId || "")
        if (libraryId.length === 0) {
            return
        }
        var sessionId = "terminal-" + Date.now().toString(36) + "-" + Math.floor(Math.random() * 1296).toString(36)
        var library = libraryFor(libraryId)
        Docking.openView(
            "archivist.terminal",
            {
                kind: "terminal",
                id: libraryId + ":" + sessionId,
                collectionId: collectionId,
                libraryId: libraryId,
                details: { sessionId: sessionId }
            },
            library ? "Terminal · " + String(library.name || "") : "Terminal",
            { anchorId: anchorId || "", reveal: false }
        )
    }

    // A terminal's process ends with its last view.
    function killTerminalIfLast(viewId) {
        var view = Docking.view(viewId)
        if (view.typeId !== "archivist.terminal") {
            return
        }
        var resource = view.resource || ({})
        if (Docking.viewsForResource("terminal", resource.id).length > 1) {
            return
        }
        var host = terminalItems[viewId]
        if (host) {
            host.killTerminal(String((resource.details || {}).sessionId || ""))
        }
    }

    // ---- closing ---------------------------------------------------------

    function closeView(viewId) {
        killTerminalIfLast(viewId)
        var result = Docking.closeView(viewId, false)
        if (result.needsConfirmation) {
            closeDialog.ask("view", viewId, result.documents)
        }
    }

    function closeOtherViews(viewId) {
        var result = Docking.closeOtherViews(viewId, false)
        if (result.needsConfirmation) {
            closeDialog.ask("others", viewId, result.documents)
        }
    }

    function closeAnchor(anchorId) {
        var views = Docking.anchor(anchorId).views || []
        var result = Docking.closeAnchor(anchorId, false)
        if (result.needsConfirmation) {
            closeDialog.ask("anchor", anchorId, result.documents)
            return
        }
        for (var index = 0; index < views.length; index += 1) {
            delete terminalItems[views[index]]
        }
    }

    function finishClose(kind, id) {
        if (kind === "view") {
            Docking.closeView(id, true)
        } else if (kind === "others") {
            Docking.closeOtherViews(id, true)
        } else {
            Docking.closeAnchor(id, true)
        }
    }

    function splitAnchor(anchorId, edge) {
        var anchor = Docking.anchor(anchorId)
        if ((anchor.views || []).length >= 2) {
            Docking.splitView(anchor.activeViewId, anchorId, edge)
        } else {
            Docking.createAnchor(anchorId, edge)
        }
    }

    // ---- dragging --------------------------------------------------------

    function beginDrag(kind, id, title) {
        if (!Docking.beginDrag(kind, id)) {
            return false
        }
        overlay.ghostTitle = title
        var view = kind === "view" ? Docking.view(id) : Docking.view(String(Docking.anchor(id).activeViewId || ""))
        overlay.ghostIcon = !view || !view.typeId ? "grid"
            : view.typeId === "archivist.chat" ? "chat"
            : view.typeId === "archivist.terminal" ? "terminal"
            : "file-text"
        root.forceActiveFocus()
        return true
    }

    function moveDrag(point) {
        overlay.pointer = point
        if (point.x < 0 || point.y < 0 || point.x > area.width || point.y > area.height) {
            Docking.clearDragTarget()
            return
        }
        var strip = Docking.tabStripAt(root.surfaceId, point.x, point.y, theme.editorTabHeight)
        if (strip.anchorId) {
            var item = anchorItems[strip.anchorId]
            Docking.updateDragOverTabs(root.surfaceId, strip.anchorId, item ? item.tabIndexAt(strip.localX) : -1)
            return
        }
        Docking.updateDrag(root.surfaceId, point.x, point.y)
    }

    function endDrag() {
        Docking.finishDrag()
    }

    function cancelDrag() {
        Docking.cancelDrag()
    }

    // ---- migration -------------------------------------------------------

    // First docking run for a Collection: bring over the old editor tabs and
    // the open chat so nobody loses their place.
    function migrateLegacyTabs(collectionId) {
        var scopes = [collectionId]
        var desk = String(WorkspaceState.value("workspace/collections/" + collectionId + "/activeDesk", "") || "")
        if (desk.length > 0) {
            scopes.push(collectionId + "/worktrees/" + desk)
        }
        var opened = 0
        for (var scopeIndex = 0; scopeIndex < scopes.length; scopeIndex += 1) {
            var raw = WorkspaceState.value("workspace/collections/" + scopes[scopeIndex] + "/editorTabs", "[]")
            var tabs = []
            try {
                tabs = JSON.parse(String(raw || "[]"))
            } catch (error) {
                tabs = []
            }
            if (!Array.isArray(tabs)) {
                continue
            }
            for (var index = 0; index < tabs.length && index < 100; index += 1) {
                var tab = tabs[index] || ({})
                if (String(tab.tabType) === "file" && tab.fileId && tab.libraryId) {
                    var file = {
                        id: String(tab.fileId),
                        name: String(tab.title || ""),
                        relativePath: String(tab.relativePath || ""),
                        extension: String(tab.extension || ""),
                        sizeBytes: Number(tab.sizeBytes || 0),
                        status: "available"
                    }
                    Docking.openView(
                        viewTypeForFile(file),
                        documentResource(String(tab.libraryId), file),
                        String(tab.title || file.relativePath || "Untitled"),
                        { focus: false }
                    )
                    opened += 1
                }
            }
        }
        if (String(ChatStore.selectedChatId || "").length > 0) {
            openChat("")
        }
        return opened
    }

    // Unsaved edits recovered after a crash always end up in front of the
    // person: files no view shows any more get a tab in the target panel.
    function surfaceRecoveredDocuments(collectionId) {
        var recovered = Documents.recoveredDocuments || []
        var reopened = 0
        var inCollection = 0
        for (var index = 0; index < recovered.length; index += 1) {
            var documentId = String(recovered[index])
            if (documentId.indexOf(collectionId + ":") !== 0) {
                continue
            }
            inCollection += 1
            Documents.reload(documentId)
            if (Documents.holders(documentId).length > 0) {
                continue
            }
            var snapshot = Documents.snapshot(documentId)
            var path = String(snapshot.path || "")
            var name = path.split("/").pop()
            var dot = name.lastIndexOf(".")
            var file = {
                id: String(snapshot.fileId || ""),
                name: name,
                relativePath: path,
                extension: dot > 0 ? name.slice(dot) : ""
            }
            var resource = root.documentResource(String(snapshot.libraryId || ""), file)
            resource.id = documentId
            Docking.openView("archivist.editor", resource, name)
            reopened += 1
        }
        if (inCollection > 0) {
            toast.show(inCollection === 1
                ? "Recovered unsaved edits in 1 file"
                : "Recovered unsaved edits in " + inCollection + " files")
        }
    }

    function restoreCollection() {
        var collectionId = String(CollectionStore.selectedCollectionId || "")
        if (collectionId.length > 0 && collectionId !== Docking.collectionId) {
            root.lastSelectionKey = ""
            root.preferredChatViewId = ""
            Docking.setCollection(collectionId)
        }
    }

    Component.onCompleted: {
        // Buffers recovered before this page existed: let the worktree
        // badges know they are unsaved.
        var dirty = Documents.dirtyDocuments || []
        for (var index = 0; index < dirty.length; index += 1) {
            WorktreeStore.setDocumentDirty(dirty[index], String(Documents.snapshot(dirty[index]).libraryId || ""), true)
        }
        root.surfaceId = Docking.primarySurfaceId
        restoreCollection()
    }
    Component.onDestruction: Docking.saveNow()

    Connections {
        target: CollectionStore

        function onSelectedCollectionIdChanged() {
            root.restoreCollection()
        }
    }

    Connections {
        target: Docking

        function onWorkspaceRestored(collectionId, fromSnapshot) {
            root.surfaceId = Docking.primarySurfaceId
            if (!fromSnapshot) {
                root.quietLayout = true
                Docking.applyPreset("development")
                root.quietLayout = false
                root.migrateLegacyTabs(collectionId)
            }
            var chats = Docking.viewIdsOfType("archivist.chat")
            root.preferredChatViewId = chats.length > 0 ? chats[0] : ""
            Qt.callLater(root.surfaceRecoveredDocuments, collectionId)
        }

        function onRevisionChanged() {
            if (root.surfaceId !== Docking.primarySurfaceId) {
                root.surfaceId = Docking.primarySurfaceId
            }
        }

        function onViewFocused(viewId) {
            root.syncSelection(viewId)
        }

        function onOperationFailed(message) {
            toast.show(message)
        }

        function onLayoutApplied(layoutId, name) {
            if (root.quietLayout) {
                return
            }
            toast.show("Switched to " + (name.length > 0 ? name : "layout"), "Undo", function() {
                if (Docking.undoLayout()) {
                    toast.show("Layout restored")
                }
            })
        }
    }

    Connections {
        target: LibraryStore

        function onSelectedFileChanged() {
            root.followSelectedFile()
        }
    }

    Connections {
        target: ChatStore

        function onSelectedChatIdChanged() {
            root.followSelectedChat()
        }
    }

    Connections {
        target: Documents

        function onDirtyChanged(documentId, libraryId, dirty) {
            WorktreeStore.setDocumentDirty(documentId, libraryId, dirty)
        }
    }

    // ---- keyboard --------------------------------------------------------

    Shortcut { sequences: ["Ctrl+=", "Ctrl++", StandardKey.ZoomIn]; context: Qt.WindowShortcut; onActivated: root.zoomFocusedView(1) }
    Shortcut { sequences: ["Ctrl+-", StandardKey.ZoomOut]; context: Qt.WindowShortcut; onActivated: root.zoomFocusedView(-1) }
    Shortcut { sequence: "Ctrl+0"; context: Qt.WindowShortcut; onActivated: root.zoomFocusedView(0) }
    // Ctrl+Shift+= is Ctrl++ on most keyboards, so the interface size uses Alt.
    Shortcut { sequences: ["Ctrl+Alt+=", "Ctrl+Alt++"]; context: Qt.WindowShortcut; onActivated: root.stepInterfaceScale(1) }
    Shortcut { sequence: "Ctrl+Alt+-"; context: Qt.WindowShortcut; onActivated: root.stepInterfaceScale(-1) }
    Shortcut { sequence: "Ctrl+Alt+0"; context: Qt.WindowShortcut; onActivated: root.stepInterfaceScale(0) }

    Shortcut {
        sequence: "Escape"
        enabled: Docking.dragActive
        context: Qt.ApplicationShortcut
        onActivated: Docking.cancelDrag()
    }
    Shortcut {
        sequences: ["Ctrl+\\"]
        context: Qt.ApplicationShortcut
        onActivated: root.splitAnchor(Docking.focusedAnchorId, "right")
    }
    Shortcut {
        sequences: ["Ctrl+Shift+\\"]
        context: Qt.ApplicationShortcut
        onActivated: root.splitAnchor(Docking.focusedAnchorId, "bottom")
    }
    Shortcut {
        sequences: ["Ctrl+W"]
        context: Qt.ApplicationShortcut
        onActivated: {
            if (Docking.focusedViewId.length > 0) {
                root.closeView(Docking.focusedViewId)
            }
        }
    }
    Shortcut {
        sequences: ["Ctrl+Shift+Return"]
        context: Qt.ApplicationShortcut
        onActivated: Docking.toggleMaximize(Docking.focusedAnchorId)
    }
    Shortcut { sequences: ["Ctrl+Alt+Left"]; context: Qt.ApplicationShortcut; onActivated: Docking.focusNeighbor("left") }
    Shortcut { sequences: ["Ctrl+Alt+Right"]; context: Qt.ApplicationShortcut; onActivated: Docking.focusNeighbor("right") }
    Shortcut { sequences: ["Ctrl+Alt+Up"]; context: Qt.ApplicationShortcut; onActivated: Docking.focusNeighbor("top") }
    Shortcut { sequences: ["Ctrl+Alt+Down"]; context: Qt.ApplicationShortcut; onActivated: Docking.focusNeighbor("bottom") }
    Shortcut { sequences: ["Ctrl+Alt+]"]; context: Qt.ApplicationShortcut; onActivated: Docking.cycleFocus(1) }
    Shortcut { sequences: ["Ctrl+Alt+["]; context: Qt.ApplicationShortcut; onActivated: Docking.cycleFocus(-1) }
    Shortcut { sequences: ["Ctrl+PgDown"]; context: Qt.ApplicationShortcut; onActivated: Docking.cycleView(1) }
    Shortcut { sequences: ["Ctrl+PgUp"]; context: Qt.ApplicationShortcut; onActivated: Docking.cycleView(-1) }
    Shortcut {
        sequences: ["Ctrl+S"]
        context: Qt.ApplicationShortcut
        onActivated: {
            var view = Docking.view(Docking.focusedViewId)
            if (view.resource && view.resource.kind === "document") {
                Documents.save(view.resource.id)
            }
        }
    }

    // ---- drawing ---------------------------------------------------------

    Item {
        id: area

        x: root.leftInset
        y: 0
        width: Math.max(0, root.width - root.leftInset)
        height: root.height
        clip: true

        onWidthChanged: root.noteSurfaceResize()
        onHeightChanged: root.noteSurfaceResize()

        Behavior on x {
            enabled: !root.theme.reducedMotion
            SpringAnimation { spring: root.theme.motionSpring; damping: root.theme.motionDamping; epsilon: 0.2 }
        }

        Rectangle {
            anchors.fill: parent
            color: root.theme.workspaceBgDeep
        }

        Repeater {
            id: anchorRepeater

            model: Docking.anchors(root.surfaceId)

            delegate: DockAnchor {
                theme: root.theme
                workspace: root
            }
        }

        Repeater {
            model: Docking.views

            delegate: DockViewContent {
                theme: root.theme
                workspace: root
            }
        }

        // The live conversation, placed over its chat view.
        Workspace {
            id: chatSurface

            readonly property var row: root.liveChatRow
            readonly property bool placed: row.viewId !== undefined

            x: placed ? row.rectX + 1 : 0
            y: placed ? row.rectY + root.theme.editorTabHeight : 0
            width: placed ? Math.max(0, row.rectWidth - 2) : 0
            height: placed ? Math.max(0, row.rectHeight - root.theme.editorTabHeight - 1) : 0
            visible: placed && width > 1 && height > 1
            clip: true
            theme: root.theme
            dockedMode: true
            leftObstruction: 0
            previewLeftObstruction: 0

            onContextInspectionRequested: function(messageId) {
                root.contextInspectionRequested(messageId)
            }

            Behavior on x { enabled: root.animateLayout; SpringAnimation { spring: root.theme.motionSpring * 1.4; damping: root.theme.motionDamping + 0.12; epsilon: 0.25 } }
            Behavior on y { enabled: root.animateLayout; SpringAnimation { spring: root.theme.motionSpring * 1.4; damping: root.theme.motionDamping + 0.12; epsilon: 0.25 } }
            Behavior on width { enabled: root.animateLayout; SpringAnimation { spring: root.theme.motionSpring * 1.4; damping: root.theme.motionDamping + 0.12; epsilon: 0.25 } }
            Behavior on height { enabled: root.animateLayout; SpringAnimation { spring: root.theme.motionSpring * 1.4; damping: root.theme.motionDamping + 0.12; epsilon: 0.25 } }

            MouseArea {
                anchors.fill: parent
                z: 1000
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onPressed: function(mouse) {
                    Docking.focusAnchor(chatSurface.row.anchorId)
                    mouse.accepted = false
                }
            }
        }

        Repeater {
            model: Docking.splitters(root.surfaceId)

            delegate: DockSplitter {
                theme: root.theme
                workspace: root
            }
        }

        // The focus ring glides to whichever panel has focus, drawn above
        // panel content so it reads even over editors and terminals.
        Rectangle {
            id: focusRing

            readonly property var target: {
                Docking.revision
                return root.anchorItems[Docking.focusedAnchorId] || null
            }
            readonly property bool shown: root.showFocus && target !== null && target.visible
                && !Docking.dragActive && target.surfaceId === root.surfaceId

            x: target ? target.x : 0
            y: target ? target.y : 0
            width: target ? target.width : 0
            height: target ? target.height : 0
            z: 90
            color: "transparent"
            radius: 3
            border.width: 1.5
            border.color: Qt.rgba(root.theme.accentBright.r, root.theme.accentBright.g, root.theme.accentBright.b, 0.7)
            opacity: shown ? 1 : 0
            enabled: false

            Behavior on opacity { NumberAnimation { duration: root.theme.motionFast } }
            Behavior on x { enabled: root.animateLayout && focusRing.shown; SpringAnimation { spring: 5; damping: 0.42; epsilon: 0.25 } }
            Behavior on y { enabled: root.animateLayout && focusRing.shown; SpringAnimation { spring: 5; damping: 0.42; epsilon: 0.25 } }
            Behavior on width { enabled: root.animateLayout && focusRing.shown; SpringAnimation { spring: 5; damping: 0.42; epsilon: 0.25 } }
            Behavior on height { enabled: root.animateLayout && focusRing.shown; SpringAnimation { spring: 5; damping: 0.42; epsilon: 0.25 } }
        }

        DockDropOverlay {
            id: overlay

            anchors.fill: parent
            z: 100
            theme: root.theme
            workspace: root
        }
    }

    function noteSurfaceResize() {
        root.resizingSurface = true
        surfaceResizeSettle.restart()
        Docking.setSurfaceSize(root.surfaceId, area.width, area.height)
    }

    onSurfaceIdChanged: Qt.callLater(function() {
        Docking.setSurfaceSize(root.surfaceId, area.width, area.height)
    })

    Timer {
        id: surfaceResizeSettle
        interval: 160
        onTriggered: root.resizingSurface = false
    }

    // A brief zoom readout over the focused panel.
    Rectangle {
        id: zoomBadge

        property string label: ""
        readonly property var target: root.anchorItems[Docking.focusedAnchorId] || null

        function show(text) {
            label = text
            opacity = 1
            zoomBadgeTimer.restart()
        }

        x: target ? area.x + target.x + (target.width - width) / 2 : 0
        y: target ? area.y + target.y + (target.height - height) / 2 : 0
        z: 210
        width: zoomBadgeText.implicitWidth + 32
        height: 40
        radius: 12
        color: Qt.rgba(0.08, 0.075, 0.07, 0.86)
        border.width: 1
        border.color: root.theme.panelBorder
        opacity: 0
        visible: opacity > 0.01
        scale: opacity > 0.5 ? 1 : 0.92

        Behavior on opacity { NumberAnimation { duration: 140 } }
        Behavior on scale { enabled: !root.theme.reducedMotion; NumberAnimation { duration: 160; easing.type: Easing.OutBack } }

        Text {
            id: zoomBadgeText

            anchors.centerIn: parent
            text: zoomBadge.label
            color: root.theme.appText
            font.family: root.theme.bodyFontFamily
            font.pixelSize: root.theme.textPanelTitleSize - 4
            font.weight: root.theme.textWeightEmphasis
        }

        Timer {
            id: zoomBadgeTimer
            interval: 900
            onTriggered: zoomBadge.opacity = 0
        }
    }

    // Short confirmations, optionally with one action (Undo).
    Rectangle {
        id: toast

        property string message: ""
        property string actionText: ""
        property var action: null

        function show(text, actionLabel, actionCallback) {
            message = String(text || "")
            actionText = actionLabel ? String(actionLabel) : ""
            action = actionCallback || null
            opacity = 1
            y = area.y + area.height - height - 14
            toastTimer.interval = actionText.length > 0 ? 6000 : 2600
            toastTimer.restart()
        }

        onOpacityChanged: if (opacity === 0) y = area.y + area.height

        anchors.horizontalCenter: area.horizontalCenter
        y: area.y + area.height
        z: 200
        width: toastRow.implicitWidth + 28
        height: 32
        radius: 16
        color: root.theme.controlSurfaceBg
        border.width: 1
        border.color: root.theme.panelBorder
        opacity: 0
        visible: opacity > 0.01

        Behavior on opacity { NumberAnimation { duration: root.theme.motionPanel } }
        Behavior on y { enabled: !root.theme.reducedMotion; SpringAnimation { spring: 4; damping: 0.35; epsilon: 0.2 } }

        HoverHandler {
            id: toastHover
            onHoveredChanged: hovered ? toastTimer.stop() : toastTimer.restart()
        }

        Row {
            id: toastRow

            anchors.centerIn: parent
            spacing: 12

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: toast.message
                color: root.theme.appText
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.textCaptionSize
            }

            Text {
                id: toastAction

                anchors.verticalCenter: parent.verticalCenter
                visible: toast.actionText.length > 0
                text: toast.actionText
                color: toastActionArea.containsMouse ? root.theme.appText : root.theme.accentBright
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.textCaptionSize
                font.weight: root.theme.textWeightStrong

                MouseArea {
                    id: toastActionArea

                    anchors.fill: parent
                    anchors.margins: -6
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        var callback = toast.action
                        toast.opacity = 0
                        toast.action = null
                        if (callback) {
                            callback()
                        }
                    }
                }
            }
        }

        Timer {
            id: toastTimer
            interval: 2600
            onTriggered: toast.opacity = 0
        }
    }

    // Asks before closing the last view of a document with unsaved edits.
    Dialog {
        id: closeDialog

        property string closeKind: ""
        property string closeId: ""
        property var documents: []
        property var waiting: ({})

        function ask(kind, id, documentIds) {
            closeKind = kind
            closeId = id
            documents = documentIds || []
            waiting = ({})
            open()
        }

        function titleOf(documentId) {
            var snapshot = Documents.snapshot(documentId)
            var path = String(snapshot.path || "")
            return path.length > 0 ? path.split("/").pop() : documentId.split(":").pop()
        }

        readonly property bool saving: Object.keys(waiting).length > 0

        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        focus: true
        width: Math.min(440, root.width - 48)
        padding: 20
        topPadding: 18
        bottomPadding: 16
        closePolicy: Popup.CloseOnEscape

        enter: Transition {
            enabled: !root.theme.reducedMotion
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 140; easing.type: Easing.OutCubic }
            NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: 180; easing.type: Easing.OutBack }
        }
        exit: Transition {
            enabled: !root.theme.reducedMotion
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 100 }
        }

        Overlay.modal: Rectangle {
            color: "#80000000"
        }

        background: Rectangle {
            color: root.theme.controlSurfaceBg
            radius: 10
            border.width: 1
            border.color: root.theme.panelBorder
        }

        contentItem: ColumnLayout {
            spacing: 8

            Text {
                Layout.fillWidth: true
                text: closeDialog.documents.length === 1
                    ? "Save changes to " + closeDialog.titleOf(closeDialog.documents[0]) + "?"
                    : "Save changes to " + closeDialog.documents.length + " files?"
                color: root.theme.appText
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.textControlSize + 1
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
            }

            Text {
                Layout.fillWidth: true
                visible: closeDialog.documents.length > 1
                text: closeDialog.documents.map(closeDialog.titleOf).join(", ")
                color: root.theme.appText
                opacity: 0.8
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.textCaptionSize
                wrapMode: Text.Wrap
                maximumLineCount: 3
                elide: Text.ElideRight
            }

            Text {
                Layout.fillWidth: true
                text: "Your edits will be lost if you close without saving."
                wrapMode: Text.Wrap
                color: root.theme.mutedText
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.textCaptionSize
            }

            Item { Layout.preferredHeight: 8 }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                DockDialogButton {
                    theme: root.theme
                    destructive: true
                    text: "Don't Save"
                    enabled: !closeDialog.saving
                    onClicked: {
                        for (var index = 0; index < closeDialog.documents.length; index += 1) {
                            Documents.discard(closeDialog.documents[index])
                        }
                        root.finishClose(closeDialog.closeKind, closeDialog.closeId)
                        closeDialog.close()
                    }
                }

                Item { Layout.fillWidth: true }

                DockDialogButton {
                    theme: root.theme
                    text: "Cancel"
                    onClicked: {
                        closeDialog.waiting = ({})
                        closeDialog.close()
                    }
                }

                DockDialogButton {
                    id: saveButton

                    theme: root.theme
                    primary: true
                    text: closeDialog.saving ? "Saving…" : "Save"
                    enabled: !closeDialog.saving
                    Keys.onReturnPressed: clicked()
                    Keys.onEnterPressed: clicked()
                    onClicked: {
                        var waiting = ({})
                        for (var index = 0; index < closeDialog.documents.length; index += 1) {
                            waiting[closeDialog.documents[index]] = true
                        }
                        closeDialog.waiting = waiting
                        for (index = 0; index < closeDialog.documents.length; index += 1) {
                            Documents.save(closeDialog.documents[index])
                        }
                    }
                }
            }
        }

        onOpened: saveButton.forceActiveFocus()
        onClosed: waiting = ({})

        Connections {
            target: Documents

            function onDocumentSaved(documentId) {
                if (!closeDialog.waiting[documentId]) {
                    return
                }
                var waiting = closeDialog.waiting
                delete waiting[documentId]
                closeDialog.waiting = waiting
                if (Object.keys(waiting).length === 0 && closeDialog.opened) {
                    root.finishClose(closeDialog.closeKind, closeDialog.closeId)
                    closeDialog.close()
                }
            }

            function onSaveFailed(documentId, message) {
                if (closeDialog.waiting[documentId]) {
                    closeDialog.waiting = ({})
                    toast.show("Could not save: " + message)
                }
            }
        }
    }
}

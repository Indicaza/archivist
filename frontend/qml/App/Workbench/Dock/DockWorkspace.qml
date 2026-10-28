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
    // Something floats over the bottom of the workspace (the compact chat
    // composer): the conversation pads its end by this much when its pane
    // reaches the bottom, so the last message can scroll clear of it.
    property real bottomOverlay: 0
    // Where the live conversation sits, in this item's coordinates, or
    // null when no chat pane is showing. The compact composer centres on it.
    // Built from the main area's own position (it springs with the side
    // panel) plus the pane's place in it, so it follows both live. A chat
    // in a floating window has no frame here.
    readonly property var chatFrame: {
        if (!chatSurface.visible || !chatSurface.placed || chatSurface.surfaceArea !== area) {
            return null
        }
        return {
            x: area.x + chatSurface.x,
            y: area.y + chatSurface.y,
            width: chatSurface.width,
            height: chatSurface.height
        }
    }

    // The anchor the compact composer is docked in ("" at the bottom).
    property string composerAnchorId: ""
    // Where that anchor shows its chat, in this item's coordinates, plus
    // which chat it is: the docked composer sits at its bottom and talks
    // to that conversation. Null when the anchor's active tab isn't a chat
    // here, so the composer goes back to the bottom until it is again.
    readonly property var composerAnchorFrame: {
        Docking.revision
        if (composerAnchorId.length === 0) {
            return null
        }
        var activeViewId = String(Docking.anchor(composerAnchorId).activeViewId || "")
        if (activeViewId.length === 0) {
            return null
        }
        var row = Docking.viewRow(activeViewId)
        if (row.typeId !== "archivist.chat" || row.isHidden || String(row.surfaceId || "") !== root.surfaceId) {
            return null
        }
        // The live chat springs with the layout: follow it exactly.
        var live = chatSurface.placed && chatSurface.row.viewId === activeViewId && chatSurface.surfaceArea === area
        return {
            x: area.x + (live ? chatSurface.x : row.rectX + 1),
            y: area.y + (live ? chatSurface.y : row.rectY + root.theme.editorTabHeight),
            width: live ? chatSurface.width : Math.max(0, row.rectWidth - 2),
            height: live ? chatSurface.height : Math.max(0, row.rectHeight - root.theme.editorTabHeight - 1),
            viewId: activeViewId,
            chatId: String(row.resourceId || "")
        }
    }

    signal contextInspectionRequested(string messageId)
    signal revealInLibraryRequested(string libraryId, string fileId, string relativePath)

    readonly property alias area: area
    // Set explicitly (not bound) so the per-surface models are fetched once
    // per window, not on every layout revision.
    property string surfaceId: ""
    readonly property bool animateLayout: area.animateLayout
    // Panels that appear after startup (a new layout, an agent's
    // arrangement) fade in, a few tens of milliseconds apart.
    property bool startupSettled: false
    property int arrivalCount: 0
    function nextArrivalDelay() {
        var delay = Math.min(arrivalCount, 4) * 45
        arrivalCount += 1
        arrivalReset.restart()
        return delay
    }
    Timer { interval: 1200; running: true; onTriggered: root.startupSettled = true }
    Timer { id: arrivalReset; interval: 400; onTriggered: root.arrivalCount = 0 }
    readonly property bool showFocus: area.showFocus
    readonly property int anchorCount: area.anchorCount

    // Each window's DockSurfaceArea, by surface id. View content (terminals,
    // previews, the live chat) lives in whichever window shows its view.
    property var surfaceAreas: ({})
    property string dragGhostTitle: ""
    property string dragGhostIcon: "file"
    property string dragSourceSurfaceId: ""

    function registerSurfaceArea(surfaceId, item) {
        if (surfaceId.length === 0) {
            return
        }
        var areas = Object.assign({}, surfaceAreas)
        areas[surfaceId] = item
        surfaceAreas = areas
    }

    function unregisterSurfaceArea(surfaceId, item) {
        if (surfaceAreas[surfaceId] === item) {
            var areas = Object.assign({}, surfaceAreas)
            delete areas[surfaceId]
            surfaceAreas = areas
        }
    }

    function surfaceAreaOfAnchor(anchorId) {
        var item = anchorItems[anchorId]
        return item ? (surfaceAreas[item.surfaceId] || null) : null
    }

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
    // Focus moved by keyboard: show a clear ring until the pointer is used.
    property bool keyboardFocusVisible: false
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

    // ---- focus -------------------------------------------------------------

    function noteKeyboardFocus() {
        root.keyboardFocusVisible = true
    }

    function notePointerFocus() {
        if (root.keyboardFocusVisible) {
            root.keyboardFocusVisible = false
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

    function viewIconName(info) {
        var typeId = String((info || {}).typeId || "")
        if (typeId === "archivist.chat") {
            return "chat"
        }
        if (typeId === "archivist.terminal") {
            return "terminal"
        }
        if (typeId === "archivist.diff") {
            return "git-branch"
        }
        if (typeId === "archivist.welcome") {
            return "workspace"
        }
        var details = ((info || {}).resource || {}).details || ({})
        var identity = FileIdentity.resolve({
            fileName: String(details.name || details.path || info.title || ""),
            extension: String(details.extension || "")
        })
        return identity.category === "image" ? "file-image"
            : identity.preferredRendererId === "markdown" ? "file-markdown"
            : identity.preferredRendererId === "pdf" ? "file-pdf"
            : identity.category === "code" || identity.category === "data" ? "file-text"
            : "file"
    }

    function libraryLabel(libraryId) {
        var library = libraryFor(libraryId)
        if (!library) {
            return ""
        }
        var branch = String(library.branch || library.worktreeBranch || "")
        var name = String(library.name || "Library")
        return branch.length > 0 && branch !== name ? name + " · " + branch : name
    }

    // The Library a project falls back to: the main checkout of the selected
    // repository, else the first Library of the Collection.
    function defaultLibraryId() {
        var main = String(WorktreeStore.anchorLibraryId || "")
        if (main.length > 0 && libraryFor(main)) {
            return main
        }
        var libraries = LibraryStore.libraries || []
        return libraries.length > 0 ? String(libraries[0].id || "") : ""
    }

    function knownLibraryIds() {
        var libraries = LibraryStore.libraries || []
        var ids = []
        for (var index = 0; index < libraries.length; index += 1) {
            ids.push(String(libraries[index].id || ""))
        }
        return ids
    }

    // ---- shelves (switchable panels) -------------------------------------

    // Views elsewhere that could join `anchorId`'s shelf, one per resource.
    function shelfCandidates(anchorId) {
        var own = Docking.anchor(anchorId).views || []
        var held = ({})
        for (var index = 0; index < own.length; index += 1) {
            var mine = Docking.view(own[index])
            held[String(mine.typeId) + "|" + String((mine.resource || {}).kind) + "|" + String((mine.resource || {}).id)] = true
        }
        var result = []
        var anchors = Object.keys(anchorItems)
        for (var a = 0; a < anchors.length; a += 1) {
            if (anchors[a] === anchorId) {
                continue
            }
            var views = Docking.anchor(anchors[a]).views || []
            for (var v = 0; v < views.length; v += 1) {
                var info = Docking.view(views[v])
                var key = String(info.typeId) + "|" + String((info.resource || {}).kind) + "|" + String((info.resource || {}).id)
                if (held[key]) {
                    continue
                }
                held[key] = true
                result.push(info)
            }
        }
        return result
    }

    // Puts a view on a shelf. Documents and chats get a second view (the
    // original stays where it is, sharing the same buffer or conversation);
    // a terminal moves, since its session has one home.
    function addToShelf(anchorId, viewId) {
        var info = Docking.view(viewId)
        if (!info.viewId) {
            return ""
        }
        var type = Docking.viewType(info.typeId)
        if (info.typeId === "archivist.terminal" || type.allowsDuplicates === false) {
            Docking.moveView(viewId, anchorId, -1)
            Docking.activateView(viewId)
            return viewId
        }
        return Docking.openView(info.typeId, info.resource, String(info.title || ""), {
            anchorId: anchorId,
            duplicate: true,
            state: info.state || ({})
        })
    }

    function anchorTitle(anchorId) {
        var anchor = Docking.anchor(anchorId)
        var active = anchor.activeViewId ? Docking.view(anchor.activeViewId) : null
        var count = (anchor.views || []).length
        var title = active ? viewTitle(active) : "Empty panel"
        title = count > 1 ? title + "  +" + (count - 1) : title
        // A panel the person named is carried by its name.
        var name = String(anchor.name || "")
        return name.length > 0 ? name + " · " + title : title
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
        // Point a chat view at the newly selected chat, as before docking.
        // This is automatic: a panel whose content is locked keeps its chat,
        // and the conversation opens somewhere else instead.
        var candidates = Docking.viewIdsOfType("archivist.chat")
        if (root.preferredChatViewId.length > 0) {
            candidates.unshift(root.preferredChatViewId)
        }
        for (var candidate = 0; candidate < candidates.length; candidate += 1) {
            var current = Docking.view(candidates[candidate])
            if (current.typeId !== "archivist.chat") {
                continue
            }
            if (Docking.setViewResource(current.viewId, { kind: "chat", id: chatId }, "automatic")) {
                Docking.setViewTitle(current.viewId, chatTitle(chatId))
                root.preferredChatViewId = current.viewId
                Docking.activateView(current.viewId)
                return
            }
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

    // A new terminal's worktree comes from where it opens (see
    // WorkspaceModel::resolveLibrary): the panel's own default worktree, then
    // the worktree of the panel's active document, then the Explorer's
    // selection, then the project default. It is fixed from then on.
    function terminalLibraryFor(anchorId) {
        var destination = String(anchorId || "") || Docking.preferredAnchorFor("archivist.terminal")
        return Docking.resolveLibrary(
            destination,
            String(LibraryStore.selectedLibraryId || ""),
            defaultLibraryId(),
            knownLibraryIds()
        )
    }

    function openTerminal(anchorId) {
        var destination = String(anchorId || "") || Docking.preferredAnchorFor("archivist.terminal")
        var libraryId = String(terminalLibraryFor(destination).libraryId || "")
        var collectionId = String(CollectionStore.selectedCollectionId || "")
        if (libraryId.length === 0) {
            return
        }
        anchorId = destination
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
        // Say where it opened when that isn't what the Explorer shows.
        if (libraryId !== String(LibraryStore.selectedLibraryId || "")) {
            toast.show("Terminal opened in " + libraryLabel(libraryId))
        }
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
        root.dragGhostTitle = title
        var view = kind === "view" ? Docking.view(id) : Docking.view(String(Docking.anchor(id).activeViewId || ""))
        root.dragGhostIcon = !view || !view.typeId ? "grid"
            : view.typeId === "archivist.chat" ? "chat"
            : view.typeId === "archivist.terminal" ? "terminal"
            : "file-text"
        var sourceAnchor = kind === "view" ? String(view.anchorId || "") : id
        var sourceItem = anchorItems[sourceAnchor]
        root.dragSourceSurfaceId = sourceItem ? sourceItem.surfaceId : Docking.primarySurfaceId
        tearGhost.sourceSize = sourceItem
            ? Qt.size(Math.max(480, sourceItem.width), Math.max(320, sourceItem.height))
            : Qt.size(720, 480)
        root.forceActiveFocus()
        return true
    }

    // The surface under a global point: floating windows first (they sit
    // above the main window), then the main window.
    function surfaceAreaAt(globalPoint) {
        var floating = Docking.floatingSurfaceIds
        for (var index = floating.length - 1; index >= 0; index -= 1) {
            var candidate = surfaceAreas[floating[index]]
            if (candidate && candidate.containsGlobal(globalPoint)) {
                return candidate
            }
        }
        return area.containsGlobal(globalPoint) ? area : null
    }

    function screenAt(globalPoint) {
        var screens = Qt.application.screens
        for (var index = 0; index < screens.length; index += 1) {
            var screen = screens[index]
            if (globalPoint.x >= screen.virtualX && globalPoint.x < screen.virtualX + screen.width
                && globalPoint.y >= screen.virtualY && globalPoint.y < screen.virtualY + screen.height) {
                return screen
            }
        }
        return null
    }

    function moveDrag(globalPoint) {
        var target = surfaceAreaAt(globalPoint)
        for (var key in surfaceAreas) {
            surfaceAreas[key].dragHere = surfaceAreas[key] === target
        }
        tearGhost.pointer = globalPoint

        if (!target) {
            // Out on the desktop: a drop opens a new window under the pointer.
            var screen = screenAt(globalPoint)
            var size = tearGhost.sourceSize
            var frameX = globalPoint.x - 60
            var frameY = globalPoint.y - 14
            if (screen) {
                // Keep the new window on the monitor under the pointer.
                var width = Math.min(size.width, screen.width - 40)
                var height = Math.min(size.height, screen.height - 60)
                size = Qt.size(width, height)
                frameX = Math.max(screen.virtualX + 8, Math.min(frameX, screen.virtualX + screen.width - width - 8))
                frameY = Math.max(screen.virtualY + 24, Math.min(frameY, screen.virtualY + screen.height - height - 8))
            }
            tearGhost.frame = Qt.rect(frameX, frameY, size.width, size.height)
            Docking.updateDragDetached(frameX, frameY, size.width, size.height, screen ? screen.name : "")
            return
        }

        var point = target.mapFromGlobal(globalPoint.x, globalPoint.y)
        target.dragPointer = point
        var strip = Docking.tabStripAt(target.surfaceId, point.x, point.y, theme.editorTabHeight)
        if (strip.anchorId) {
            var item = anchorItems[strip.anchorId]
            Docking.updateDragOverTabs(target.surfaceId, strip.anchorId, item ? item.tabIndexAt(strip.localX) : -1)
            return
        }
        Docking.updateDrag(target.surfaceId, point.x, point.y)
    }

    function endDrag() {
        var detached = Docking.dragDetached && Docking.dropAllowed
        var title = root.dragGhostTitle
        clearDragHere()
        if (Docking.finishDrag() && detached) {
            toast.show("Opened " + (title.length > 0 ? "“" + title + "”" : "panel") + " in a new window")
        }
    }

    function cancelDrag() {
        clearDragHere()
        Docking.cancelDrag()
    }

    function clearDragHere() {
        for (var key in surfaceAreas) {
            surfaceAreas[key].dragHere = false
        }
    }

    // ---- windows ---------------------------------------------------------

    // A frame for a new window, offset from the panel it comes from.
    function frameBeside(anchorId) {
        var item = anchorItems[anchorId]
        if (!item) {
            return { x: 120, y: 120, width: 720, height: 480 }
        }
        var origin = item.mapToGlobal(0, 0)
        return {
            x: origin.x + 36,
            y: origin.y + 36,
            width: Math.max(480, item.width),
            height: Math.max(320, item.height)
        }
    }

    function detachView(viewId) {
        var view = Docking.view(viewId)
        var frame = frameBeside(String(view.anchorId || ""))
        var screen = screenAt(Qt.point(frame.x, frame.y))
        if (Docking.detachView(viewId, frame.x, frame.y, frame.width, frame.height, screen ? screen.name : "").length > 0) {
            toast.show("Opened “" + viewTitle(view) + "” in a new window")
        }
    }

    function detachAnchor(anchorId) {
        var frame = frameBeside(anchorId)
        var screen = screenAt(Qt.point(frame.x, frame.y))
        Docking.detachAnchor(anchorId, frame.x, frame.y, frame.width, frame.height, screen ? screen.name : "")
    }

    // A panel in the main window to receive tabs: the focused or target one
    // when it is there, else any.
    function mainWindowAnchor() {
        var primary = Docking.primarySurfaceId
        var preferred = [Docking.targetAnchorId, Docking.focusedAnchorId]
        for (var index = 0; index < preferred.length; index += 1) {
            var item = anchorItems[preferred[index]]
            if (item && item.surfaceId === primary) {
                return preferred[index]
            }
        }
        for (var key in anchorItems) {
            if (anchorItems[key].surfaceId === primary) {
                return key
            }
        }
        return ""
    }

    function returnToMainWindow(kind, id) {
        var destination = mainWindowAnchor()
        if (destination.length === 0) {
            return
        }
        if (kind === "view") {
            Docking.moveView(id, destination, -1)
            Docking.activateView(id)
        } else {
            Docking.moveAnchor(id, destination, "right")
            Docking.focusAnchor(id)
        }
        root.Window.window.requestActivate()
    }

    function closeFloatingWindow(surfaceId) {
        var info = Docking.surface(surfaceId)
        var count = Number(info.viewCount || 0)
        if (Docking.closeSurface(surfaceId) && count > 0) {
            toast.show(count === 1 ? "Moved 1 tab back to the main window" : "Moved " + count + " tabs back to the main window")
        }
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
                // A new collection starts with one editor area: Chat and
                // Terminal already live in the bottom panel. Layouts with
                // more panels are a choice in the layout picker.
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

        // An agent rearranged things for the person: say what, and offer
        // the way back.
        function onAgentArranged(summary) {
            toast.show(summary.length > 0 ? summary : "Rearranged the workspace", "Undo", function() {
                if (Docking.undoAgentChange()) {
                    toast.show("Put back")
                }
            })
        }

        function onAgentRestored(summary) {
            toast.show(summary)
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

    // Workspace shortcuts sleep while Home is in front, so Cmd+W there
    // never closes a tab nobody can see. Saving still works.
    readonly property bool keyboardActive: !Shell.homeActive


    Shortcut {
        sequence: "Escape"
        enabled: Docking.dragActive
        context: Qt.ApplicationShortcut
        onActivated: root.cancelDrag()
    }
    Shortcut {
        sequences: ["Ctrl+\\"]
        context: Qt.ApplicationShortcut
        enabled: root.keyboardActive
        onActivated: root.splitAnchor(Docking.focusedAnchorId, "right")
    }
    Shortcut {
        sequences: ["Ctrl+Shift+\\"]
        context: Qt.ApplicationShortcut
        enabled: root.keyboardActive
        onActivated: root.splitAnchor(Docking.focusedAnchorId, "bottom")
    }
    Shortcut {
        sequences: ["Ctrl+W"]
        context: Qt.ApplicationShortcut
        enabled: root.keyboardActive
        onActivated: {
            if (Docking.focusedViewId.length > 0) {
                root.closeView(Docking.focusedViewId)
            }
        }
    }
    Shortcut {
        sequences: ["Ctrl+Shift+Return"]
        context: Qt.ApplicationShortcut
        enabled: root.keyboardActive
        onActivated: Docking.toggleMaximize(Docking.focusedAnchorId)
    }
    Shortcut { sequences: ["Ctrl+Alt+Left"]; context: Qt.ApplicationShortcut; enabled: root.keyboardActive; onActivated: { root.noteKeyboardFocus(); Docking.focusNeighbor("left") } }
    Shortcut { sequences: ["Ctrl+Alt+Right"]; context: Qt.ApplicationShortcut; enabled: root.keyboardActive; onActivated: { root.noteKeyboardFocus(); Docking.focusNeighbor("right") } }
    Shortcut { sequences: ["Ctrl+Alt+Up"]; context: Qt.ApplicationShortcut; enabled: root.keyboardActive; onActivated: { root.noteKeyboardFocus(); Docking.focusNeighbor("top") } }
    Shortcut { sequences: ["Ctrl+Alt+Down"]; context: Qt.ApplicationShortcut; enabled: root.keyboardActive; onActivated: { root.noteKeyboardFocus(); Docking.focusNeighbor("bottom") } }
    Shortcut { sequences: ["Ctrl+Alt+]"]; context: Qt.ApplicationShortcut; enabled: root.keyboardActive; onActivated: { root.noteKeyboardFocus(); Docking.cycleFocus(1) } }
    Shortcut { sequences: ["Ctrl+Alt+["]; context: Qt.ApplicationShortcut; enabled: root.keyboardActive; onActivated: { root.noteKeyboardFocus(); Docking.cycleFocus(-1) } }
    // Next/previous tab, or next/previous item on a shelf.
    Shortcut { sequences: ["Ctrl+PgDown", "Ctrl+Alt+Shift+]"]; context: Qt.ApplicationShortcut; enabled: root.keyboardActive; onActivated: Docking.cycleView(1) }
    Shortcut { sequences: ["Ctrl+PgUp", "Ctrl+Alt+Shift+["]; context: Qt.ApplicationShortcut; enabled: root.keyboardActive; onActivated: Docking.cycleView(-1) }
    // Rename the focused panel.
    Shortcut {
        sequences: ["F2"]
        context: Qt.ApplicationShortcut
        enabled: root.keyboardActive
        onActivated: {
            var item = root.anchorItems[Docking.focusedAnchorId]
            if (item) {
                item.startRename()
            }
        }
    }
    // Open the focused shelf's picker.
    Shortcut {
        sequences: ["Ctrl+Alt+P"]
        context: Qt.ApplicationShortcut
        enabled: root.keyboardActive
        onActivated: {
            var item = root.anchorItems[Docking.focusedAnchorId]
            if (item && item.switchable) {
                item.openShelfPicker()
            }
        }
    }
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

    // An arrangement an agent prepared without applying it (the person
    // was busy, or the conversation was in the background).
    DockNoticeBar {
        id: agentSuggestionBar

        readonly property var suggestion: Docking.agentSuggestion

        x: root.leftInset
        y: 0
        width: Math.max(0, root.width - root.leftInset)
        z: 5
        theme: root.theme
        open: suggestion.summary !== undefined
        text: "Suggested layout: " + String(suggestion.summary || "")
        shortText: "Suggested layout"
        primaryText: "Apply"
        secondaryText: "Dismiss"
        onPrimaryClicked: Docking.applyAgentSuggestion()
        onSecondaryClicked: Docking.dismissAgentSuggestion()
    }

    // While an agent's arrangement is in place, one quiet line offers the
    // way back, or to keep it as the normal layout.
    DockNoticeBar {
        id: temporaryArrangementBar

        readonly property var arrangement: Docking.temporaryArrangement

        x: root.leftInset
        y: agentSuggestionBar.height
        width: Math.max(0, root.width - root.leftInset)
        z: 5
        theme: root.theme
        open: arrangement.since !== undefined && !agentSuggestionBar.open
        iconName: "workspace"
        text: "Arranged for this task" + (arrangement.summary ? ": " + String(arrangement.summary) : "")
        shortText: "Arranged for this task"
        primaryText: "Keep"
        secondaryText: "Put back"
        onPrimaryClicked: Docking.keepArrangement()
        onSecondaryClicked: Docking.putBackArrangement()
    }

    DockSurfaceArea {
        id: area

        readonly property real noticeHeight: agentSuggestionBar.height + temporaryArrangementBar.height

        x: root.leftInset
        y: noticeHeight
        width: Math.max(0, root.width - root.leftInset)
        height: Math.max(0, root.height - noticeHeight)
        theme: root.theme
        workspace: root
        surfaceId: root.surfaceId

        Behavior on x {
            enabled: !root.theme.reducedMotion
            SpringAnimation { spring: root.theme.motionSpring; damping: root.theme.motionDamping; epsilon: 0.2 }
        }
    }

    // Where view content waits while its window is being made.
    Item {
        id: viewParking

        visible: false
        width: 1
        height: 1

        // Not a Repeater: each item reparents itself into its window.
        Instantiator {
            model: Docking.views

            delegate: DockViewContent {
                theme: root.theme
                workspace: root
                parking: viewParking
            }
        }

            // The live conversation, placed over its chat view.
            Workspace {
                id: chatSurface

                readonly property var row: root.liveChatRow
                readonly property bool placed: row.viewId !== undefined
                readonly property var surfaceArea: placed ? (root.surfaceAreas[String(row.surfaceId || "")] || null) : null

                parent: surfaceArea || viewParking
                z: 1

                x: placed ? row.rectX + 1 : 0
                y: placed ? row.rectY + root.theme.editorTabHeight : 0
                width: placed ? Math.max(0, row.rectWidth - 2) : 0
                height: placed ? Math.max(0, row.rectHeight - root.theme.editorTabHeight - 1) : 0
                visible: surfaceArea !== null && width > 1 && height > 1
                clip: true
                theme: root.theme
                dockedMode: true
                leftObstruction: 0
                // Clear of the compact composer when it floats over this
                // pane: at the bottom of the window, or docked in its anchor.
                bottomOverlay: placed && surfaceArea !== null
                    && (root.composerAnchorFrame !== null
                        ? row.anchorId === root.composerAnchorId
                        : y + height >= surfaceArea.height - 4)
                    ? root.bottomOverlay
                    : 0
                previewLeftObstruction: 0

                onContextInspectionRequested: function(messageId) {
                    root.contextInspectionRequested(messageId)
                }

                Behavior on x { enabled: chatSurface.surfaceArea !== null && chatSurface.surfaceArea.animateLayout; SpringAnimation { spring: root.theme.motionSpring * 1.4; damping: root.theme.motionDamping + 0.12; epsilon: 0.25 } }
                Behavior on y { enabled: chatSurface.surfaceArea !== null && chatSurface.surfaceArea.animateLayout; SpringAnimation { spring: root.theme.motionSpring * 1.4; damping: root.theme.motionDamping + 0.12; epsilon: 0.25 } }
                Behavior on width { enabled: chatSurface.surfaceArea !== null && chatSurface.surfaceArea.animateLayout; SpringAnimation { spring: root.theme.motionSpring * 1.4; damping: root.theme.motionDamping + 0.12; epsilon: 0.25 } }
                Behavior on height { enabled: chatSurface.surfaceArea !== null && chatSurface.surfaceArea.animateLayout; SpringAnimation { spring: root.theme.motionSpring * 1.4; damping: root.theme.motionDamping + 0.12; epsilon: 0.25 } }

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

    }

    // Floating windows, one per surface besides the main window.
    Instantiator {
        model: Docking.floatingSurfaceIds

        delegate: DockFloatingWindow {
            required property string modelData

            surfaceId: modelData
            theme: root.theme
            workspace: root
            transientParent: root.Window.window
        }
    }

    // Follows the pointer outside every window while a drop would open one.
    DockTearGhost {
        id: tearGhost

        theme: root.theme
        title: root.dragGhostTitle
        icon: root.dragGhostIcon
        shown: Docking.dragActive && Docking.dragDetached
        allowed: Docking.dropAllowed
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

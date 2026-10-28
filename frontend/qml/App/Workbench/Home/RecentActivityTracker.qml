import QtQml
import Archivist.Services 1.0

// Notes what the person opens (conversations, files, projects) in
// Shell's recent items, and what finishes (replies) in Home's activity.
// References only: titles and where things live, never their content.
QtObject {
    id: root

    property string lastFocusedViewId: ""
    // The reply being watched: which conversation, and whether the person
    // stopped it (a stopped reply isn't news).
    property var runChat: null
    property string runCollectionId: ""
    property bool runCancelled: false

    function noteRunStarted() {
        var chat = ChatStore.selectedChat || ({})
        root.runChat = String(chat.id || "").length > 0 ? chat : null
        root.runCollectionId = String(CollectionStore.selectedCollectionId || "")
        root.runCancelled = false
    }

    function noteRunFinished() {
        var chat = root.runChat
        root.runChat = null
        if (!chat || root.runCancelled) {
            return
        }
        var failed = String(ChatStore.errorMessage || "").length > 0
        // Seen as it happened (the conversation in front, not Home):
        // keep it in the list, already read.
        var watching = !Shell.homeActive && String(ChatStore.selectedChatId) === String(chat.id)
        HomeStore.noteActivity({
            kind: failed ? "reply.failed" : "reply.finished",
            key: "reply:" + String(chat.id),
            title: failed ? "Reply didn't finish" : "Reply ready",
            detail: String(chat.title || "Untitled conversation"),
            unread: !watching,
            target: {
                kind: "chat",
                id: String(chat.id),
                collectionId: root.runCollectionId,
                libraryId: String(chat.libraryId || "")
            }
        })
    }

    function projectLabel(collection) {
        return collection ? String(collection.name || collection.path || "") : ""
    }

    function noteChat() {
        var chat = ChatStore.selectedChat || ({})
        var chatId = String(chat.id || "")
        if (chatId.length === 0) {
            return
        }
        Shell.noteRecent({
            kind: "chat",
            id: chatId,
            collectionId: String(CollectionStore.selectedCollectionId || ""),
            libraryId: String(chat.libraryId || ""),
            title: String(chat.title || ""),
            subtitle: String(chat.libraryName || "")
        })
    }

    function noteCollection() {
        var collectionId = String(CollectionStore.selectedCollectionId || "")
        if (collectionId.length === 0) {
            return
        }
        Shell.noteRecent({
            kind: "collection",
            id: collectionId,
            title: projectLabel(CollectionStore.selectedCollection)
        })
    }

    function noteFocusedView() {
        // Restoring a project's tabs behind Home isn't the person opening them.
        if (Shell.homeActive) {
            return
        }
        var viewId = String(Docking.focusedViewId || "")
        if (viewId.length === 0 || viewId === lastFocusedViewId) {
            return
        }
        lastFocusedViewId = viewId
        var view = Docking.view(viewId)
        var resource = view.resource || ({})
        if (resource.kind !== "document") {
            return
        }
        var details = resource.details || ({})
        var path = String(details.path || "")
        var name = String(details.name || view.title || path)
        Shell.noteRecent({
            kind: "document",
            id: String(resource.id || ""),
            collectionId: String(resource.collectionId || ""),
            libraryId: String(resource.libraryId || ""),
            title: name,
            subtitle: path,
            details: {
                fileId: String(details.fileId || ""),
                name: name,
                path: path,
                extension: String(details.extension || ""),
                mimeType: String(details.mimeType || ""),
                languageId: String(details.languageId || "")
            }
        })
    }

    property Connections chats: Connections {
        target: ChatStore

        function onSelectedChatChanged() {
            root.noteChat()
        }

        function onRespondingChanged() {
            if (ChatStore.responding) {
                root.noteRunStarted()
            } else {
                root.noteRunFinished()
            }
        }

        function onActiveRunChanged() {
            if (ChatStore.cancellingRun) {
                root.runCancelled = true
            }
        }
    }

    property Connections collections: Connections {
        target: CollectionStore

        function onSelectedCollectionChanged() {
            root.noteCollection()
        }
    }

    property Connections docking: Connections {
        target: Docking

        function onRevisionChanged() {
            root.noteFocusedView()
        }
    }

    Component.onCompleted: {
        noteCollection()
        noteChat()
    }
}

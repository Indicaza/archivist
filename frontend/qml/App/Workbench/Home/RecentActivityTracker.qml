import QtQml
import Archivist.Services 1.0

// Notes what the person opens (conversations, files, projects) in
// Shell's recent items. References only: titles and where things live,
// never their content.
QtObject {
    id: root

    property string lastFocusedViewId: ""

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

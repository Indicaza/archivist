import QtQuick
import Archivist.Services 1.0
import "../Home"

// The workspace's compact chat: Home's composer floating over the bottom
// of the workspace, ChatGPT style, talking to the selected conversation.
// The transcript stays in the workspace; Expand grows back into the
// docked chat and terminal workbench.
//
// The text is the conversation's draft (ChatDrafts), the same one the
// docked composer shows, so switching between the two never loses or
// duplicates anything. With no conversation selected, sending starts one
// in the selected folder.
FocusScope {
    id: root

    required property var theme

    readonly property string chatId: String(ChatStore.selectedChatId || "")
    readonly property string libraryId: String(LibraryStore.selectedLibraryId || "")
    readonly property bool hasChat: chatId.length > 0
    // Starting a conversation: waiting for ChatStore to create it.
    property bool starting: false
    property bool loadingDraft: false

    readonly property string blockedReason: {
        if (hasChat) {
            return ""
        }
        if (libraryId.length === 0) {
            return "Choose a folder in the Explorer to start a chat"
        }
        return ""
    }

    signal expandRequested()

    implicitHeight: composer.implicitHeight

    function draftKey(chatId) {
        return chatId.length > 0 ? chatId : "none"
    }

    function focusInput() {
        composer.focusInput()
    }

    function loadDraft() {
        loadingDraft = true
        composer.input.text = ChatDrafts.draft(chatId)
        composer.input.cursorPosition = composer.input.length
        loadingDraft = false
    }

    function send(text) {
        if (ChatStore.responding || ChatStore.mutating || ChatStore.assigningAgent) {
            return
        }
        if (hasChat) {
            ChatStore.sendMessage(text)
            composer.input.text = ""
            return
        }
        if (libraryId.length === 0) {
            return
        }
        starting = true
        ChatStore.startChat(libraryId, String(CollectionStore.scope.defaultAgentId || ""), text)
        // Refused before it started (another write in flight): keep the text.
        if (!ChatStore.mutating) {
            starting = false
        }
    }

    Component.onCompleted: loadDraft()

    HomeComposer {
        id: composer

        width: parent.width
        focus: true
        theme: root.theme
        syncHomeDraft: false
        showAdd: false
        showExpand: true
        expandToolTip: "Expand into the chat and terminal dock"
        placeholder: root.hasChat
            ? "Message " + String(ChatStore.selectedChat.title || "this chat")
            : "Start a new chat"
        blockedReason: root.blockedReason
        sending: root.starting
        streaming: ChatStore.responding
        onSubmitRequested: function(text) { root.send(text) }
        onStopRequested: ChatStore.cancelActiveRun()
        onExpandRequested: root.expandRequested()
    }

    Connections {
        target: composer.input

        function onTextChanged() {
            if (!root.loadingDraft) {
                ChatDrafts.setDraft(root.chatId, composer.input.text)
            }
        }
    }

    // The docked composer edited the same draft, or it was cleared.
    Connections {
        target: ChatDrafts

        function onDraftChanged(key) {
            if (key !== root.draftKey(root.chatId)) {
                return
            }

            var stored = ChatDrafts.draft(root.chatId)
            var text = composer.input.text
            // Blank text is stored as no draft; that's no change.
            if (stored !== text && !(stored.length === 0 && text.trim().length === 0)) {
                root.loadDraft()
            }
        }
    }

    Connections {
        target: ChatStore

        function onSelectedChatIdChanged() {
            root.loadDraft()
        }

        function onChatCreated(chat) {
            if (!root.starting) {
                return
            }
            root.starting = false
            // The first message went out with the new chat.
            ChatDrafts.clearDraft("")
        }

        function onErrorMessageChanged() {
            if (root.starting && String(ChatStore.errorMessage || "").length > 0) {
                root.starting = false
            }
        }

        function onMutatingChanged() {
            if (root.starting && !ChatStore.mutating) {
                Qt.callLater(function() {
                    if (root.starting && !ChatStore.mutating) {
                        root.starting = false
                    }
                })
            }
        }
    }
}

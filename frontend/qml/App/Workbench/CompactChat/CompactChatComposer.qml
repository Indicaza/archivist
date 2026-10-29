import QtQuick
import QtQuick.Effects
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

    objectName: "composer.compact"

    required property var theme

    readonly property string chatId: String(ChatStore.selectedChatId || "")
    readonly property string libraryId: String(LibraryStore.selectedLibraryId || "")
    readonly property bool hasChat: chatId.length > 0
    // Starting a conversation: waiting for ChatStore to create it.
    property bool starting: false
    // Who answers a conversation started from here.
    property string chosenAgentId: ""
    readonly property string agentId: hasChat
        ? String(ChatStore.selectedChat.agentId || "")
        : chosenAgentId.length > 0 ? chosenAgentId : String(CollectionStore.scope.defaultAgentId || "")
    readonly property color surfaceColor: theme.compactComposerBg
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

    // Docked in an anchor, the composer belongs to the chat that anchor
    // shows. When that isn't the selected chat (another pane was used
    // since), focusing the composer asks for it to be selected first.
    property string targetChatId: ""
    property string targetChatTitle: ""
    readonly property bool targetPending: targetChatId.length > 0 && targetChatId !== chatId

    signal expandRequested()
    signal optionsRequested(Item source, real x, real y)
    // Focus arrived: the shell makes sure it talks to the right chat.
    signal activated()

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
        if (targetPending) {
            // Never send to a chat other than the one this sits in.
            root.activated()
            return
        }
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
        ChatStore.startChat(libraryId, agentId, text)
        // Refused before it started (another write in flight): keep the text.
        if (!ChatStore.mutating) {
            starting = false
        }
    }

    Component.onCompleted: loadDraft()

    // The bubble's own shadow: a blurred copy of its shape, a little lower,
    // spreading out from its edges. Nothing else is drawn around it, so
    // the conversation (or a floating panel) shows right up to the bubble.
    Rectangle {
        id: shadowShape

        x: 0
        y: 6
        width: composer.width
        height: composer.height
        radius: root.theme.compactComposerRadius
        color: root.theme.compactComposerShadow
        visible: false
        layer.enabled: true
    }

    MultiEffect {
        x: shadowShape.x
        y: shadowShape.y
        width: shadowShape.width
        height: shadowShape.height
        source: shadowShape
        blurEnabled: true
        blur: 1
        blurMax: 40
        autoPaddingEnabled: true
        opacity: composer.focused ? 0.62 : 0.5

        Behavior on opacity {
            enabled: !root.theme.reducedMotion
            NumberAnimation { duration: root.theme.motionHover }
        }
    }

    HomeComposer {
        id: composer

        width: parent.width
        focus: true
        theme: root.theme
        syncHomeDraft: false
        floating: true
        showAdd: false
        showLibrary: false
        showExpand: true
        showOptions: true
        optionsToolTip: "Where the message box sits"
        agents: AgentStore.agents || []
        agentId: root.agentId
        expandToolTip: "Expand workbench  (" + (Qt.platform.os === "osx" ? "⌘J" : "Ctrl+J") + ")"
        placeholder: root.targetPending
            ? "Message " + (root.targetChatTitle.length > 0 ? root.targetChatTitle : "this chat")
            : root.hasChat
                ? "Message " + String(ChatStore.selectedChat.title || "this chat")
                : "Start a new chat"
        blockedReason: root.blockedReason
        sending: root.starting
        streaming: ChatStore.responding
        onSubmitRequested: function(text) { root.send(text) }
        onStopRequested: ChatStore.cancelActiveRun()
        onExpandRequested: root.expandRequested()
        onOptionsRequested: function(source) {
            // Opens upward: the composer sits at the bottom of its pane.
            root.optionsRequested(source, source.width, -6)
        }
        onFocusedChanged: {
            if (composer.focused) {
                root.activated()
            }
        }
        onAgentChosen: function(agentId) {
            if (root.hasChat) {
                ChatStore.assignAgentToSelectedChat(agentId)
            } else {
                root.chosenAgentId = agentId
            }
        }
    }

    // Right-click anywhere on the bubble: the same options.
    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: function(eventPoint) {
            root.optionsRequested(root, eventPoint.position.x, eventPoint.position.y)
        }
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

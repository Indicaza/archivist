import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Archivist.Services 1.0
import "../../Icons"
import "../Dock"

// Home: the chat-first start page. A conversation up front, then what to
// reopen (conversations, projects, files) and how to start something new.
//
// Home only presents and asks. Opening anything goes through the shell
// (signals below), which uses the same stores as the rest of the app, so
// nothing opened from here is a copy.
Rectangle {
    id: root

    required property var theme
    property bool sending: false

    signal startConversationRequested(string text, string libraryId)
    signal chatRequested(var chat, string collectionId)
    signal projectRequested(string collectionId)
    signal recentRequested(var item)
    signal addFolderRequested()
    signal setupProjectRequested()
    signal backRequested()

    property int scopeRevision: 0
    property string chosenLibraryId: ""

    readonly property bool hasProject: String(CollectionStore.selectedCollectionId || "").length > 0
    readonly property string projectName: projectLabel(CollectionStore.selectedCollection)
    readonly property var scopedLibraries: computeScopedLibraries()
    readonly property var targetLibrary: computeTargetLibrary()
    readonly property var recentChats: computeRecentChats()
    readonly property var projects: computeProjects()
    readonly property var recentFiles: {
        Shell.recentItems
        return Shell.recent("document", 6)
    }
    readonly property bool wide: width >= 900
    readonly property string blockedReason: {
        if ((CollectionStore.collections || []).length === 0) {
            return "Set up a project to start a conversation"
        }
        if (!hasProject) {
            return "Pick a project to start a conversation"
        }
        if (scopedLibraries.length === 0) {
            return "Add a folder to this project to start a conversation"
        }
        if (ChatStore.responding) {
            return "Archivist is still answering. You can send when it's done."
        }
        return ""
    }

    function focusComposer() {
        composer.focusInput()
    }

    function projectLabel(collection) {
        if (!collection) {
            return ""
        }
        return String(collection.name || collection.path || "")
    }

    function computeScopedLibraries() {
        var revision = scopeRevision
        var libraries = LibraryStore.libraries || []
        var result = []
        if (!hasProject) {
            return result
        }
        for (var index = 0; index < libraries.length; index += 1) {
            if (CollectionStore.includesLibrary(String(libraries[index].id))) {
                result.push(libraries[index])
            }
        }
        return result
    }

    function computeTargetLibrary() {
        var libraries = scopedLibraries
        var wanted = [chosenLibraryId, String(LibraryStore.selectedLibraryId || "")]
        for (var w = 0; w < wanted.length; w += 1) {
            for (var index = 0; index < libraries.length; index += 1) {
                if (wanted[w].length > 0 && String(libraries[index].id) === wanted[w]) {
                    return libraries[index]
                }
            }
        }
        return libraries.length > 0 ? libraries[0] : null
    }

    function collectionForChat(chat) {
        var collections = CollectionStore.collections || []
        var chatId = String(chat.id || "")
        var libraryId = String(chat.libraryId || "")
        if (hasProject && CollectionStore.includesChat(chatId)) {
            return CollectionStore.selectedCollection
        }
        for (var index = 0; index < collections.length; index += 1) {
            if ((collections[index].chatIds || []).indexOf(chatId) >= 0) {
                return collections[index]
            }
        }
        for (var other = 0; other < collections.length; other += 1) {
            if (libraryId.length > 0 && (collections[other].libraryIds || []).indexOf(libraryId) >= 0) {
                return collections[other]
            }
        }
        return null
    }

    function computeRecentChats() {
        var revision = scopeRevision
        var chats = ChatStore.chats || []
        var result = []
        for (var index = 0; index < chats.length && result.length < 7; index += 1) {
            var chat = chats[index]
            var collection = collectionForChat(chat)
            var parts = []
            if (collection && String(collection.id) !== String(CollectionStore.selectedCollectionId)) {
                parts.push(projectLabel(collection))
            }
            if (String(chat.libraryName || "").length > 0) {
                parts.push(String(chat.libraryName))
            }
            result.push({
                chat: chat,
                collectionId: collection ? String(collection.id) : "",
                subtitle: parts.join(" · "),
                meta: relativeTime(chat.updatedAt || chat.createdAt)
            })
        }
        return result
    }

    function computeProjects() {
        Shell.recentItems
        var collections = (CollectionStore.collections || []).slice()
        var order = {}
        var recents = Shell.recent("collection", 0)
        for (var index = 0; index < recents.length; index += 1) {
            order[String(recents[index].id)] = index
        }
        var current = String(CollectionStore.selectedCollectionId || "")
        collections.sort(function(left, right) {
            var leftId = String(left.id)
            var rightId = String(right.id)
            if (leftId === current) return -1
            if (rightId === current) return 1
            var leftRank = order[leftId] !== undefined ? order[leftId] : 1e6
            var rightRank = order[rightId] !== undefined ? order[rightId] : 1e6
            if (leftRank !== rightRank) return leftRank - rightRank
            return String(left.path || left.name).localeCompare(String(right.path || right.name))
        })
        return collections.slice(0, 6)
    }

    function relativeTime(value) {
        var time = typeof value === "number" ? value : Date.parse(String(value || ""))
        if (!isFinite(time) || time <= 0) {
            return ""
        }
        var seconds = Math.max(0, (Date.now() - time) / 1000)
        if (seconds < 60) return "now"
        if (seconds < 3600) return Math.floor(seconds / 60) + "m"
        if (seconds < 86400) return Math.floor(seconds / 3600) + "h"
        if (seconds < 86400 * 7) return Math.floor(seconds / 86400) + "d"
        return new Date(time).toLocaleDateString(Qt.locale(), "MMM d")
    }

    function fileIcon(item) {
        var details = item.details || ({})
        var extension = String(details.extension || "").toLowerCase()
        if (extension === "md" || extension === "markdown") return "file-markdown"
        if (extension === "pdf") return "file-pdf"
        if (["png", "jpg", "jpeg", "gif", "webp", "svg"].indexOf(extension) >= 0) return "file-image"
        return "file-text"
    }

    color: theme.workspaceBg

    Connections {
        target: CollectionStore

        function onWorkspaceScopeChanged() {
            root.scopeRevision += 1
        }

        function onSelectedCollectionIdChanged() {
            root.chosenLibraryId = ""
            root.scopeRevision += 1
        }
    }

    Keys.onEscapePressed: function(event) {
        if (root.hasProject) {
            root.backRequested()
            event.accepted = true
        }
    }

    Flickable {
        id: page

        anchors.fill: parent
        contentWidth: width
        contentHeight: content.implicitHeight + 96
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        ScrollBar.vertical: ScrollBar {
            policy: page.contentHeight > page.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
        }

        ColumnLayout {
            id: content

            width: Math.min(820, page.width - 64)
            x: (page.width - width) / 2
            y: Math.max(36, Math.min(page.height * 0.16, 140))
            spacing: 28

            // ---- the conversation ---------------------------------------

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 14

                RowLayout {
                    Layout.fillWidth: true
                    visible: root.hasProject

                    DockPillButton {
                        theme: root.theme
                        iconName: "chevron-left"
                        text: "Back to " + (root.projectName.length > 0 ? root.projectName : "workspace")
                        Accessible.name: text
                        ToolTip.visible: hovered
                        ToolTip.delay: 600
                        ToolTip.text: "Esc"
                        onClicked: root.backRequested()
                    }

                    Item { Layout.fillWidth: true }
                }

                Text {
                    Layout.fillWidth: true
                    text: "What are we working on?"
                    color: root.theme.chatHeadingText
                    font.family: root.theme.titleFontFamily
                    font.pixelSize: root.theme.textDisplaySize
                    wrapMode: Text.Wrap
                    Accessible.role: Accessible.Heading
                }

                Text {
                    Layout.fillWidth: true
                    text: root.hasProject
                        ? "Ask about " + root.projectName + ", or pick up where you left off."
                        : "Ask a question, open a project, or pick up where you left off."
                    color: root.theme.mutedText
                    font.family: root.theme.bodyFontFamily
                    font.pixelSize: root.theme.textControlSize
                    wrapMode: Text.Wrap
                }

                HomeComposer {
                    id: composer

                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    theme: root.theme
                    libraries: root.scopedLibraries
                    libraryId: root.targetLibrary ? String(root.targetLibrary.id) : ""
                    libraryName: root.targetLibrary ? String(root.targetLibrary.name || "") : ""
                    blockedReason: root.blockedReason
                    sending: root.sending
                    onLibraryChosen: function(libraryId) { root.chosenLibraryId = libraryId }
                    onSubmitRequested: function(text) {
                        root.startConversationRequested(text, composer.libraryId)
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: String(ChatStore.errorMessage || "").length > 0
                    text: String(ChatStore.errorMessage || "")
                    color: root.theme.danger
                    font.family: root.theme.bodyFontFamily
                    font.pixelSize: root.theme.textCaptionSize
                    wrapMode: Text.Wrap
                }

                // What to do when there's nowhere to start the conversation.
                RowLayout {
                    Layout.fillWidth: true
                    visible: (CollectionStore.collections || []).length === 0
                        || (root.hasProject && root.scopedLibraries.length === 0 && !LibraryStore.loadingLibraries)
                    spacing: 10

                    Text {
                        Layout.fillWidth: true
                        text: (CollectionStore.collections || []).length === 0
                            ? "A project is a set of folders Archivist reads and works in."
                            : "Conversations start in one of the project's folders."
                        color: root.theme.mutedText
                        font.family: root.theme.bodyFontFamily
                        font.pixelSize: root.theme.textCaptionSize
                        wrapMode: Text.Wrap
                    }

                    DockPillButton {
                        theme: root.theme
                        iconName: "add"
                        text: (CollectionStore.collections || []).length === 0 ? "Set up a project" : "Add a folder"
                        onClicked: {
                            if ((CollectionStore.collections || []).length === 0) {
                                root.setupProjectRequested()
                            } else {
                                root.addFolderRequested()
                            }
                        }
                    }
                }
            }

            // ---- what to reopen -----------------------------------------

            GridLayout {
                Layout.fillWidth: true
                columns: root.wide ? 2 : 1
                columnSpacing: 36
                rowSpacing: 24

                HomeSection {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    Layout.rowSpan: root.wide ? 2 : 1
                    theme: root.theme
                    title: "Recent conversations"
                    empty: root.recentChats.length === 0
                    emptyText: "No conversations yet. Ask something above to start one."

                    Repeater {
                        model: root.recentChats

                        delegate: HomeRow {
                            required property var modelData

                            Layout.fillWidth: true
                            theme: root.theme
                            iconName: "chat"
                            title: String(modelData.chat.title || "Untitled conversation")
                            subtitle: modelData.subtitle
                            meta: modelData.meta
                            current: String(modelData.chat.id) === String(ChatStore.selectedChatId)
                            onClicked: root.chatRequested(modelData.chat, modelData.collectionId)
                        }
                    }
                }

                HomeSection {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    theme: root.theme
                    title: "Projects"
                    actionText: "New project"
                    empty: root.projects.length === 0
                    emptyText: "No projects yet."
                    onActionRequested: root.setupProjectRequested()

                    Repeater {
                        model: root.projects

                        delegate: HomeRow {
                            required property var modelData

                            readonly property int folderCount: (modelData.libraryIds || []).length

                            Layout.fillWidth: true
                            theme: root.theme
                            iconName: "collection"
                            title: String(modelData.path || modelData.name || "Project")
                            subtitle: folderCount === 1 ? "1 folder" : folderCount + " folders"
                            meta: String(modelData.id) === String(CollectionStore.selectedCollectionId) ? "Open" : ""
                            current: String(modelData.id) === String(CollectionStore.selectedCollectionId)
                            onClicked: root.projectRequested(String(modelData.id))
                        }
                    }
                }

                HomeSection {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    theme: root.theme
                    title: "Recently opened"
                    empty: root.recentFiles.length === 0
                    emptyText: "Files you open show up here."

                    Repeater {
                        model: root.recentFiles

                        delegate: HomeRow {
                            required property var modelData

                            Layout.fillWidth: true
                            theme: root.theme
                            iconName: root.fileIcon(modelData)
                            title: String(modelData.title || "File")
                            subtitle: String(modelData.subtitle || "")
                            meta: root.relativeTime(Number(modelData.accessedAt || 0))
                            onClicked: root.recentRequested(modelData)
                        }
                    }
                }
            }
        }
    }
}

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Archivist.Services 1.0
import "../../Icons"

// Home: the chat-first start page. A greeting and one composer in the
// middle; conversations, projects and recent files in the sidebar.
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
    readonly property var allChats: computeAllChats()
    readonly property var projects: computeProjects()
    readonly property var recentFiles: {
        Shell.recentItems
        return Shell.recent("document", 6)
    }
    readonly property bool roomForSidebar: width >= 820
    property bool sidebarWanted: readSidebarWanted()
    // Narrow windows show the sidebar over the page, only when asked.
    property bool sidebarOverlayOpen: false
    readonly property bool sidebarShown: roomForSidebar ? sidebarWanted : sidebarOverlayOpen
    // Up to three conversations to pick up, from this project.
    readonly property var pickUp: chatsForProject(String(CollectionStore.selectedCollectionId || ""), 3)
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

    function readSidebarWanted() {
        return String(WorkspaceState.value("shell/homeSidebarOpen", "true")) !== "false"
    }

    function setSidebarWanted(open) {
        sidebarWanted = open
        WorkspaceState.setValue("shell/homeSidebarOpen", open ? "true" : "false")
    }

    // Every conversation, newest first, with the project it belongs to.
    function computeAllChats() {
        var revision = scopeRevision
        var chats = ChatStore.chats || []
        var result = []
        for (var index = 0; index < chats.length; index += 1) {
            var chat = chats[index]
            var collection = collectionForChat(chat)
            result.push({
                chat: chat,
                collectionId: collection ? String(collection.id) : "",
                projectName: projectLabel(collection),
                meta: relativeTime(chat.updatedAt || chat.createdAt)
            })
        }
        return result
    }

    function chatsForProject(collectionId, limit) {
        var result = []
        var chats = allChats
        for (var index = 0; index < chats.length; index += 1) {
            if (chats[index].collectionId === String(collectionId)) {
                result.push(chats[index].chat)
                if (limit > 0 && result.length >= limit) {
                    break
                }
            }
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
        return collections
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

    HomeSidebar {
        id: sidebar

        x: root.sidebarShown ? 0 : -width
        width: 272
        height: parent.height
        visible: x > -width
        z: 2
        theme: root.theme
        home: root

        onNewChatRequested: {
            root.sidebarOverlayOpen = false
            root.focusComposer()
        }
        onChatRequested: function(chat, collectionId) { root.chatRequested(chat, collectionId) }
        onProjectRequested: function(collectionId) { root.projectRequested(collectionId) }
        onRecentRequested: function(item) { root.recentRequested(item) }
        onSetupProjectRequested: root.setupProjectRequested()
        onCollapseRequested: {
            if (root.roomForSidebar) {
                root.setSidebarWanted(false)
            } else {
                root.sidebarOverlayOpen = false
            }
        }

        Behavior on x {
            enabled: !root.theme.reducedMotion
            NumberAnimation { duration: root.theme.motionPanel; easing.type: Easing.OutCubic }
        }
    }

    Item {
        id: stage

        anchors.top: parent.top
        anchors.bottom: parent.bottom
        x: root.roomForSidebar ? sidebar.x + sidebar.width : 0
        width: parent.width - x

        // ---- top bar: sidebar toggle, Home | workspace switch -------------

        IconButton {
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.top: parent.top
            anchors.topMargin: 12
            width: 30
            height: 30
            visible: !root.sidebarShown
            theme: root.theme
            iconName: "chevron-right"
            iconSize: 14
            toolTipText: "Show sidebar"
            onClicked: {
                if (root.roomForSidebar) {
                    root.setSidebarWanted(true)
                } else {
                    root.sidebarOverlayOpen = true
                }
            }
        }

        Rectangle {
            id: switcher

            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 12
            visible: root.hasProject
            width: switchRow.implicitWidth + 8
            height: 34
            radius: height / 2
            color: root.theme.controlSurfaceBg
            border.width: 1
            border.color: root.theme.quietBorder

            Row {
                id: switchRow

                anchors.centerIn: parent
                spacing: 2

                Repeater {
                    model: [
                        { key: "home", label: "Home" },
                        { key: "work", label: root.projectName.length > 0 ? root.projectName : "Workspace" }
                    ]

                    delegate: Button {
                        id: switchButton

                        required property var modelData

                        readonly property bool selected: modelData.key === "home"

                        height: 26
                        leftPadding: 16
                        rightPadding: 16
                        hoverEnabled: true
                        focusPolicy: Qt.StrongFocus
                        Accessible.name: modelData.key === "home" ? "Home" : "Back to " + modelData.label
                        ToolTip.visible: hovered && modelData.key === "work"
                        ToolTip.delay: 700
                        ToolTip.text: "Back to the workspace (Esc)"
                        onClicked: {
                            if (modelData.key === "work") {
                                root.backRequested()
                            }
                        }

                        contentItem: Text {
                            text: switchButton.modelData.label
                            color: switchButton.selected || switchButton.hovered ? root.theme.appText : root.theme.mutedText
                            font.family: root.theme.bodyFontFamily
                            font.pixelSize: root.theme.textControlSize
                            font.weight: switchButton.selected ? root.theme.textWeightEmphasis : root.theme.textWeightRegular
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }

                        background: Rectangle {
                            radius: height / 2
                            color: switchButton.selected
                                ? root.theme.activeBg
                                : switchButton.hovered ? root.theme.hoverBg : "transparent"
                            border.width: switchButton.activeFocus ? 1 : 0
                            border.color: root.theme.accent
                        }
                    }
                }
            }
        }

        // ---- greeting and composer ---------------------------------------

        ColumnLayout {
            id: center

            width: Math.min(760, stage.width - 64)
            anchors.horizontalCenter: parent.horizontalCenter
            y: Math.max(switcher.y + switcher.height + 24, stage.height * 0.40 - greeting.height - 28)
            spacing: 0

            Text {
                id: greeting

                Layout.fillWidth: true
                Layout.bottomMargin: 26
                text: root.hasProject ? "What are we working on?" : "Ready when you are."
                color: root.theme.chatHeadingText
                font.family: root.theme.titleFontFamily
                font.pixelSize: root.theme.textDisplaySize
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                Accessible.role: Accessible.Heading
            }

            HomeComposer {
                id: composer

                Layout.fillWidth: true
                theme: root.theme
                libraries: root.scopedLibraries
                libraryId: root.targetLibrary ? String(root.targetLibrary.id) : ""
                libraryName: root.targetLibrary ? String(root.targetLibrary.name || "") : ""
                projectName: root.projectName
                blockedReason: root.blockedReason
                sending: root.sending
                onLibraryChosen: function(libraryId) { root.chosenLibraryId = libraryId }
                onSubmitRequested: function(text) {
                    root.startConversationRequested(text, composer.libraryId)
                }
                onAddFolderRequested: root.addFolderRequested()
                onNewProjectRequested: root.setupProjectRequested()
            }

            Text {
                Layout.fillWidth: true
                Layout.topMargin: 10
                Layout.leftMargin: 20
                visible: String(ChatStore.errorMessage || "").length > 0
                text: String(ChatStore.errorMessage || "")
                color: root.theme.danger
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.textCaptionSize
                wrapMode: Text.Wrap
            }

            // Nowhere to start a conversation yet: one clear next step.
            HomeSidebarRow {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 18
                visible: (CollectionStore.collections || []).length === 0
                    || (root.hasProject && root.scopedLibraries.length === 0 && !LibraryStore.loadingLibraries)
                theme: root.theme
                iconName: "add"
                strong: true
                label: (CollectionStore.collections || []).length === 0
                    ? "Set up a project: pick the folders Archivist should work in"
                    : "Add a folder to " + root.projectName
                onClicked: {
                    if ((CollectionStore.collections || []).length === 0) {
                        root.setupProjectRequested()
                    } else {
                        root.addFolderRequested()
                    }
                }
            }

            // Pick up where you left off.
            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: 22
                Layout.leftMargin: 8
                Layout.rightMargin: 8
                visible: root.pickUp.length > 0 && root.blockedReason.length === 0
                spacing: 2

                Repeater {
                    model: root.pickUp

                    delegate: HomeSidebarRow {
                        required property var modelData

                        Layout.fillWidth: true
                        implicitHeight: 38
                        theme: root.theme
                        iconName: "chat"
                        label: "Continue \u201c" + String(modelData.title || "Untitled conversation") + "\u201d"
                        trailing: root.relativeTime(modelData.updatedAt || modelData.createdAt)
                        onClicked: root.chatRequested(modelData, String(CollectionStore.selectedCollectionId || ""))
                    }
                }
            }
        }
    }
}

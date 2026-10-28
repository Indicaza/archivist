import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Archivist.Services 1.0
import "../../Icons"

// Home: the personal layer above every project. A conversation up front;
// under it, the modules the person chose (Resume work, Pinned, Activity,
// Layouts, later plugins), and a sidebar to go anywhere.
//
// Home only presents and asks. Opening anything goes through the shell
// (signals below), which uses the same stores as the rest of the app, so
// nothing opened from here is a copy. Its own configuration (modules,
// pins, activity) is HomeStore; where the person is stays in Shell.
Rectangle {
    id: root

    required property var theme
    property bool sending: false
    // 1 when Home is fully in front, 0 when the workspace is. Drives the
    // zoom between the two.
    property real reveal: 1

    signal startConversationRequested(string text, string libraryId, string agentId)
    signal chatRequested(var chat, string collectionId)
    signal projectRequested(string collectionId)
    signal recentRequested(var item)
    signal layoutRequested(string layoutId)
    signal addFolderRequested()
    signal setupProjectRequested()
    signal backRequested()

    property int scopeRevision: 0
    property string chosenLibraryId: ""
    property string chosenAgentId: ""
    property bool editing: false
    // The project card that was opened: it leads the dive.
    property string diveCollectionId: ""

    readonly property bool firstRun: (CollectionStore.collections || []).length === 0
    readonly property bool hasProject: String(CollectionStore.selectedCollectionId || "").length > 0
    readonly property string projectName: projectLabel(CollectionStore.selectedCollection)
    readonly property var scopedLibraries: computeScopedLibraries()
    readonly property var targetLibrary: computeTargetLibrary()
    readonly property var allChats: computeAllChats()
    readonly property var projects: computeProjects()
    readonly property var resumeEntries: computeResumeEntries()
    // Pins whose project still exists (an archived project's pins come back
    // with it).
    readonly property var livePins: {
        var pins = HomeStore.pins || []
        var known = {}
        var collections = CollectionStore.collections || []
        for (var c = 0; c < collections.length; c += 1) {
            known[String(collections[c].id)] = true
        }
        return pins.filter(function(pin) {
            var project = pin.kind === "collection" ? String(pin.id) : String(pin.collectionId || "")
            return project.length === 0 || known[project] === true
        })
    }
    readonly property var agents: (AgentStore.agents || [])
    readonly property string agentId: {
        var wanted = [chosenAgentId, String((CollectionStore.scope || {}).defaultAgentId || "")]
        for (var w = 0; w < wanted.length; w += 1) {
            for (var index = 0; index < agents.length; index += 1) {
                if (wanted[w].length > 0 && String(agents[index].id) === wanted[w]) {
                    return wanted[w]
                }
            }
        }
        return agents.length > 0 ? String(agents[0].id) : ""
    }
    readonly property bool roomForSidebar: width >= 860
    property bool sidebarWanted: readSidebarWanted()
    // Narrow windows show the sidebar over the page, only when asked.
    property bool sidebarOverlayOpen: false
    readonly property bool sidebarShown: roomForSidebar ? sidebarWanted : sidebarOverlayOpen
    readonly property bool projectHasChats: chatsForProject(String(CollectionStore.selectedCollectionId || ""), 1).length > 0
    readonly property string blockedReason: {
        if (firstRun) {
            return "Set up a project to start a conversation"
        }
        if (!hasProject) {
            return "Pick a project to start a conversation"
        }
        if (scopedLibraries.length === 0) {
            return "Add a folder to " + projectName + " to start a conversation"
        }
        if (ChatStore.responding) {
            return "Archivist is still answering. You can send when it's done."
        }
        return ""
    }
    readonly property var visibleModules: {
        var list = HomeStore.modules || []
        var result = []
        for (var index = 0; index < list.length; index += 1) {
            if (editing || list[index].visible) {
                result.push(list[index])
            }
        }
        return result
    }
    // The page leans on its modules when it has some; otherwise the
    // conversation sits a little above the middle.
    readonly property bool compact: !firstRun && visibleModules.length > 0

    function focusComposer() {
        composer.focusInput()
    }

    function projectLabel(collection) {
        if (!collection) {
            return ""
        }
        return String(collection.name || collection.path || "")
    }

    function projectNameFor(collectionId) {
        var id = String(collectionId || "")
        var collections = CollectionStore.collections || []
        for (var index = 0; index < collections.length; index += 1) {
            if (String(collections[index].id) === id) {
                return projectLabel(collections[index])
            }
        }
        return ""
    }

    // Each project keeps one hue (from its id), used as its swatch.
    function projectHue(collectionId) {
        return root.theme.agentHue("project:" + String(collectionId || ""))
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
        var chats = (ChatStore.chats || []).slice()
        chats.sort(function(left, right) {
            return timeOf(right.updatedAt || right.createdAt) - timeOf(left.updatedAt || left.createdAt)
        })
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
        collections.sort(function(left, right) {
            var leftRank = order[String(left.id)] !== undefined ? order[String(left.id)] : 1e6
            var rightRank = order[String(right.id)] !== undefined ? order[String(right.id)] : 1e6
            if (leftRank !== rightRank) return leftRank - rightRank
            return String(left.name || left.path).localeCompare(String(right.name || right.path))
        })
        return collections
    }

    // Resume work: projects by when they were last worked in (opened, or a
    // conversation in them moved), each with what going back restores.
    function computeResumeEntries() {
        Shell.recentItems
        HomeStore.pins
        var revision = scopeRevision
        var docking = Docking.revision
        var recents = Shell.recent("collection", 0)
        var opened = {}
        for (var r = 0; r < recents.length; r += 1) {
            opened[String(recents[r].id)] = Number(recents[r].accessedAt || 0)
        }
        var current = String(CollectionStore.selectedCollectionId || "")
        var entries = []
        var collections = CollectionStore.collections || []
        for (var index = 0; index < collections.length; index += 1) {
            var collection = collections[index]
            var id = String(collection.id)
            var chats = chatsForProject(id, 1)
            var chat = chats.length > 0 ? chats[0] : null
            var chatTime = chat ? timeOf(chat.updatedAt || chat.createdAt) : 0
            var when = Math.max(opened[id] || 0, chatTime)
            if (when <= 0 && id !== current) {
                continue
            }
            entries.push({
                collectionId: id,
                name: projectLabel(collection),
                hue: projectHue(id),
                chat: chat,
                chatTitle: chat ? String(chat.title || "Untitled conversation") : "",
                time: when,
                when: relativeTime(when, true),
                current: id === current && Shell.homeActive,
                pinned: HomeStore.isPinned("collection", id),
                summary: Docking.savedWorkspaceSummary ? Docking.savedWorkspaceSummary(id) : ({})
            })
        }
        entries.sort(function(left, right) {
            if (left.current !== right.current) return left.current ? -1 : 1
            return right.time - left.time
        })
        return entries
    }

    function timeOf(value) {
        var time = typeof value === "number" ? value : Date.parse(String(value || ""))
        return isFinite(time) ? time : 0
    }

    function relativeTime(value, spelledOut) {
        var time = timeOf(value)
        if (time <= 0) {
            return ""
        }
        var seconds = Math.max(0, (Date.now() - time) / 1000)
        if (seconds < 60) return spelledOut ? "Just now" : "now"
        if (seconds < 3600) return Math.floor(seconds / 60) + (spelledOut ? " min ago" : "m")
        if (seconds < 86400) return Math.floor(seconds / 3600) + (spelledOut ? " h ago" : "h")
        if (seconds < 86400 * 2 && spelledOut) return "Yesterday"
        if (seconds < 86400 * 7) return Math.floor(seconds / 86400) + (spelledOut ? " days ago" : "d")
        return new Date(time).toLocaleDateString(Qt.locale(), "MMM d")
    }

    function greeting() {
        var hour = new Date().getHours()
        if (root.firstRun) return "Welcome to Archivist."
        if (hour < 5) return "Working late."
        if (hour < 12) return "Good morning."
        if (hour < 18) return "Good afternoon."
        if (hour < 23) return "Good evening."
        return "Working late."
    }

    function chatById(chatId) {
        var chats = ChatStore.chats || []
        for (var index = 0; index < chats.length; index += 1) {
            if (String(chats[index].id) === String(chatId)) {
                return chats[index]
            }
        }
        return null
    }

    // A pin or an activity event points at a resource; open that.
    function openLink(link) {
        var kind = String(link.kind || "")
        var id = String(link.id || "")
        if (kind === "collection") {
            root.diveCollectionId = id
            root.projectRequested(id)
        } else if (kind === "chat") {
            var chat = chatById(id)
            if (chat) {
                root.chatRequested(chat, String(link.collectionId || ""))
            } else {
                root.chatRequested({ id: id, libraryId: String(link.libraryId || "") }, String(link.collectionId || ""))
            }
        } else if (kind === "layout") {
            root.layoutRequested(id)
        } else if (kind === "document") {
            root.recentRequested({
                kind: "document",
                id: id,
                collectionId: String(link.collectionId || ""),
                libraryId: String(link.libraryId || ""),
                title: String(link.title || ""),
                details: link.details || ({})
            })
        }
    }

    function openResume(entry) {
        root.diveCollectionId = String(entry.collectionId)
        if (entry.chat && String(entry.collectionId) !== String(CollectionStore.selectedCollectionId)) {
            root.chatRequested(entry.chat, String(entry.collectionId))
        } else {
            root.projectRequested(String(entry.collectionId))
        }
    }

    function showAllProjects() {
        if (roomForSidebar) {
            setSidebarWanted(true)
        } else {
            sidebarOverlayOpen = true
        }
        sidebar.allProjects = true
    }

    function moveModule(module, step) {
        var list = HomeStore.modules || []
        for (var index = 0; index < list.length; index += 1) {
            if (list[index].id === module.id) {
                HomeStore.moveModule(module.id, index + step)
                return
            }
        }
    }

    function toggleSpan(module) {
        HomeStore.setModuleSpan(module.id, module.span === "half" ? "full" : "half")
    }

    // The canvas covers the workspace quickly and lets go of it late, so
    // the change reads as one surface moving, not two crossfading.
    color: "transparent"

    onRevealChanged: {
        if (reveal >= 1) {
            diveCollectionId = ""
        }
    }

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

    Connections {
        target: Shell

        function onHomeActiveChanged() {
            if (!Shell.homeActive) {
                root.editing = false
            }
        }
    }

    Keys.onEscapePressed: function(event) {
        if (root.editing) {
            root.editing = false
            event.accepted = true
        } else if (root.hasProject) {
            root.backRequested()
            event.accepted = true
        }
    }

    // ---- canvas -----------------------------------------------------------

    // Warm light from above, falling off down the page: tonal, not a glow.
    Rectangle {
        anchors.fill: parent
        opacity: Math.min(1, root.reveal * 1.6)
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.theme.homeCanvasTop }
            GradientStop { position: 0.55; color: root.theme.homeCanvasBottom }
            GradientStop { position: 1.0; color: root.theme.homeCanvasBottom }
        }
    }

    // ---- sidebar ----------------------------------------------------------

    HomeSidebar {
        id: sidebar

        x: (root.sidebarShown ? 0 : -width) - (1 - root.reveal) * 12
        width: root.theme.homeSidebarWidth
        height: parent.height
        visible: x > -width
        opacity: root.reveal
        z: 3
        theme: root.theme
        home: root

        onNewChatRequested: {
            root.sidebarOverlayOpen = false
            Shell.homeDraft = ""
            root.focusComposer()
        }
        onChatRequested: function(chat, collectionId) { root.chatRequested(chat, collectionId) }
        onProjectRequested: function(collectionId) {
            root.diveCollectionId = collectionId
            root.projectRequested(collectionId)
        }
        onPinOpenRequested: function(pin) { root.openLink(pin) }
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

    // Narrow windows: a scrim behind the overlaid sidebar.
    Rectangle {
        anchors.fill: parent
        z: 2
        visible: !root.roomForSidebar && root.sidebarOverlayOpen
        color: root.theme.withAlpha("#000000", 0.35)

        MouseArea {
            anchors.fill: parent
            onClicked: root.sidebarOverlayOpen = false
        }
    }

    // ---- stage ------------------------------------------------------------

    Item {
        id: stage

        anchors.top: parent.top
        anchors.bottom: parent.bottom
        x: root.roomForSidebar ? sidebar.x + sidebar.width : 0
        width: parent.width - x

        // Top bar: restrained. The sidebar toggle when it's hidden; on the
        // right, the way back to the work behind Home and Customize.
        Item {
            id: topBar

            z: 2
            width: parent.width
            height: 56
            opacity: root.reveal

            HomeChip {
                anchors.left: parent.left
                anchors.leftMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                visible: !root.sidebarShown
                theme: root.theme
                round: true
                iconName: "chevron-right"
                iconSize: 13
                toolTipText: "Show sidebar"
                onClicked: {
                    if (root.roomForSidebar) {
                        root.setSidebarWanted(true)
                    } else {
                        root.sidebarOverlayOpen = true
                    }
                }
            }

            Row {
                anchors.right: parent.right
                anchors.rightMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4

                HomeChip {
                    visible: root.hasProject && !root.editing
                    theme: root.theme
                    iconName: "arrow-right"
                    iconSize: 12
                    label: stage.width > 620 ? "Back to " + root.projectName : "Back"
                    toolTipText: "Return to " + root.projectName + " as you left it (Esc)"
                    Accessible.name: "Return to " + root.projectName
                    onClicked: root.backRequested()
                }

                // In Edit Home the toolbar at the bottom has Done.
                HomeChip {
                    visible: !root.firstRun && !root.editing
                    theme: root.theme
                    iconName: "sliders"
                    iconSize: 13
                    label: stage.width > 620 ? "Customize" : ""
                    round: stage.width <= 620
                    toolTipText: "Choose what Home shows"
                    Accessible.name: "Customize Home"
                    onClicked: root.editing = true
                }
            }
        }

        Flickable {
            id: page

            anchors.fill: parent
            contentWidth: width
            contentHeight: Math.max(height, content.y + content.height + 96 + (root.editing ? 72 : 0))
            boundsBehavior: Flickable.StopAtBounds
            clip: true

            ScrollBar.vertical: ScrollBar {
                policy: page.contentHeight > page.height + 1 ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
            }

            Item {
                id: content

                readonly property real heroTop: root.compact
                    ? Math.max(topBar.height + 8, Math.min(150, page.height * 0.13))
                    : Math.max(topBar.height + 8, page.height * 0.33 - hero.height * 0.35)

                width: Math.min(root.theme.homeContentWidth, page.width - 64)
                x: Math.round((page.width - width) / 2)
                y: heroTop
                height: column.implicitHeight
                opacity: root.reveal
                // Arriving, Home settles from a touch closer; leaving into a
                // project, it moves past (the dive).
                scale: root.theme.reducedMotion ? 1 : 1 + (1 - root.reveal) * 0.035
                transformOrigin: Item.Top

                Behavior on y {
                    enabled: !root.theme.reducedMotion
                    NumberAnimation { duration: root.theme.motionPanel; easing.type: Easing.OutCubic }
                }

                ColumnLayout {
                    id: column

                    width: parent.width
                    spacing: 0

                    // ---- hero: greeting and composer ------------------------

                    ColumnLayout {
                        id: hero

                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: Math.min(root.theme.homeComposerWidth, content.width)
                        Layout.maximumWidth: Math.min(root.theme.homeComposerWidth, content.width)
                        spacing: 0

                        Text {
                            Layout.fillWidth: true
                            Layout.bottomMargin: 10
                            text: new Date().toLocaleDateString(Qt.locale(), "dddd, MMMM d").toUpperCase()
                            color: root.theme.homeEyebrow
                            font.family: root.theme.bodyFontFamily
                            font.pixelSize: root.theme.typeEyebrow
                            font.weight: root.theme.textWeightEmphasis
                            font.letterSpacing: root.theme.textTrackingEyebrow
                            horizontalAlignment: Text.AlignHCenter
                            visible: !root.firstRun
                        }

                        Text {
                            id: greetingText

                            Layout.fillWidth: true
                            text: root.greeting()
                            color: root.theme.chatHeadingText
                            font.family: root.theme.titleFontFamily
                            font.pixelSize: root.theme.px(root.compact ? 36 : 40)
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            Accessible.role: Accessible.Heading
                        }

                        Text {
                            Layout.fillWidth: true
                            Layout.topMargin: 10
                            visible: text.length > 0
                            text: root.firstRun
                                ? "A librarian for your own files. Point it at a few folders, then ask, draft and build with them."
                                : !root.projectHasChats && root.hasProject
                                    ? "Start the first conversation in " + root.projectName + ". Answers come with the sources they used."
                                    : ""
                            color: root.theme.mutedText
                            font.family: root.theme.bodyFontFamily
                            font.pixelSize: root.theme.textControlSize + 1
                            lineHeight: 1.25
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                        }

                        HomeComposer {
                            id: composer

                            Layout.fillWidth: true
                            Layout.topMargin: 28
                            theme: root.theme
                            libraries: root.scopedLibraries
                            libraryId: root.targetLibrary ? String(root.targetLibrary.id) : ""
                            libraryName: root.targetLibrary ? String(root.targetLibrary.name || "") : ""
                            projectName: root.projectName
                            agents: root.agents
                            agentId: root.agentId
                            placeholder: root.hasProject ? "Ask anything about " + root.projectName : "Ask Archivist anything"
                            blockedReason: ChatStore.responding ? "" : root.blockedReason
                            sending: root.sending
                            streaming: ChatStore.responding
                            onLibraryChosen: function(libraryId) { root.chosenLibraryId = libraryId }
                            onAgentChosen: function(agentId) { root.chosenAgentId = agentId }
                            onSubmitRequested: function(text) {
                                root.startConversationRequested(text, composer.libraryId, root.agentId)
                            }
                            onStopRequested: ChatStore.cancelActiveRun()
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

                        // A project with no conversations yet: a few ways to
                        // begin. They only fill the composer; nothing is sent.
                        Flow {
                            Layout.fillWidth: true
                            Layout.topMargin: 16
                            visible: root.hasProject && !root.projectHasChats && root.scopedLibraries.length > 0
                            spacing: 8
                            layoutDirection: Qt.LeftToRight

                            Repeater {
                                model: [
                                    "Summarize what's in " + root.projectName,
                                    "Where is the main idea explained?",
                                    "Draft a README from these files"
                                ]

                                delegate: HomeChip {
                                    id: starter

                                    required property string modelData

                                    theme: root.theme
                                    label: modelData
                                    implicitHeight: 32
                                    background: Rectangle {
                                        radius: height / 2
                                        color: starter.hovered ? root.theme.withAlpha(root.theme.appText, 0.06) : "transparent"
                                        border.width: 1
                                        border.color: starter.activeFocus ? root.theme.accent : root.theme.homeCardBorder
                                    }
                                    onClicked: composer.setText(modelData)
                                }
                            }
                        }

                        // A project with no folders: one clear next step.
                        HomeChip {
                            Layout.alignment: Qt.AlignHCenter
                            Layout.topMargin: 16
                            visible: root.hasProject && root.scopedLibraries.length === 0 && !LibraryStore.loadingLibraries
                            theme: root.theme
                            iconName: "add"
                            label: "Add a folder to " + root.projectName
                            selected: true
                            onClicked: root.addFolderRequested()
                        }
                    }

                    // ---- first run: how Archivist works -----------------------

                    HomeFirstRun {
                        Layout.fillWidth: true
                        Layout.topMargin: 48
                        visible: root.firstRun
                        theme: root.theme
                        onSetupProjectRequested: root.setupProjectRequested()
                    }

                    // ---- modules ----------------------------------------------

                    HomeModuleGrid {
                        id: moduleGrid

                        Layout.fillWidth: true
                        Layout.topMargin: root.compact ? 56 : 40
                        visible: !root.firstRun
                        theme: root.theme
                        home: root
                        modules: root.visibleModules
                        editing: root.editing
                    }
                }
            }
        }

        // Edit Home: arrangements and a way out, pinned to the bottom.
        HomeCustomizeBar {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: root.editing ? 22 : -height - 10
            opacity: root.editing ? 1 : 0
            visible: opacity > 0
            z: 3
            theme: root.theme
            onDoneRequested: root.editing = false

            Behavior on anchors.bottomMargin {
                enabled: !root.theme.reducedMotion
                NumberAnimation { duration: root.theme.motionPanel; easing.type: Easing.OutCubic }
            }
            Behavior on opacity {
                enabled: !root.theme.reducedMotion
                NumberAnimation { duration: root.theme.motionFast }
            }
        }
    }
}

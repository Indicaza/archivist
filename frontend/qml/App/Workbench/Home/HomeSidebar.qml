import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Archivist.Services 1.0
import "../../Icons"

// Home's sidebar: start something, find something, go somewhere.
//
//   New chat, Search
//   Pinned      what the person keeps close (only when there is some)
//   Projects    each opens where it was left; the arrow shows its chats
//   Recent      a short list of conversations
//
// Exhaustive browsing (every file, every chat) stays in the workspace's
// Explorer, Search and Library panels. The sidebar only lists and asks;
// HomeView turns the requests into opening things.
Rectangle {
    id: root

    required property var theme
    // HomeView: the data helpers (chats by project, labels, hues, times).
    required property var home

    signal newChatRequested()
    signal chatRequested(var chat, string collectionId)
    signal projectRequested(string collectionId)
    signal pinOpenRequested(var pin)
    signal setupProjectRequested()
    signal collapseRequested()

    property string query: ""
    // Projects the person opened or closed in the list.
    property var expanded: ({})
    property bool allProjects: false
    // Per project: how many chats to show when open.
    property var chatLimits: ({})
    // What the context menu acts on: { kind, id, collectionId, title, subtitle }.
    property var menuTarget: ({})

    readonly property string currentProjectId: String(CollectionStore.selectedCollectionId || "")
    readonly property bool searching: query.trim().length > 0
    readonly property int projectLimit: 6
    readonly property var visibleProjects: allProjects ? home.projects : home.projects.slice(0, projectLimit)
    readonly property var recentChats: home.allChats.slice(0, 6)
    // Pins show here only when Home's Pinned module doesn't, so the same
    // list never appears twice on one screen.
    readonly property bool pinnedOnPage: {
        var modules = HomeStore.modules || []
        for (var index = 0; index < modules.length; index += 1) {
            if (modules[index].typeId === "archivist.home.pinned") {
                return modules[index].visible === true
            }
        }
        return false
    }
    readonly property var pins: pinnedOnPage ? [] : home.livePins.slice(0, 6)

    function focusSearch() {
        searchField.forceActiveFocus()
    }

    function projectExpanded(collectionId) {
        var key = String(collectionId)
        return expanded[key] === true
    }

    function toggleProject(collectionId) {
        var next = Object.assign({}, expanded)
        next[String(collectionId)] = !projectExpanded(collectionId)
        expanded = next
    }

    function chatLimit(collectionId) {
        return chatLimits[String(collectionId)] || 5
    }

    function showMoreChats(collectionId) {
        var next = Object.assign({}, chatLimits)
        next[String(collectionId)] = 25
        chatLimits = next
    }

    function matches(chat) {
        var needle = query.trim().toLowerCase()
        return needle.length === 0 || String(chat.title || "").toLowerCase().indexOf(needle) >= 0
    }

    function searchResults() {
        var result = []
        var chats = root.home.allChats
        for (var index = 0; index < chats.length && result.length < 30; index += 1) {
            if (matches(chats[index].chat)) {
                result.push(chats[index])
            }
        }
        return result
    }

    function openMenu(target) {
        menuTarget = target
        rowMenu.popup()
    }

    function iconForPin(kind) {
        if (kind === "chat") return "chat"
        if (kind === "collection") return "folder"
        if (kind === "layout") return "grid"
        if (kind === "document") return "file-text"
        return "pin"
    }

    color: theme.sidebarBg

    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: root.theme.homeHairline
    }

    Menu {
        id: rowMenu

        readonly property bool pinned: {
            HomeStore.pins
            return HomeStore.isPinned(String(root.menuTarget.kind || ""), String(root.menuTarget.id || ""))
        }

        MenuItem {
            text: "Open"
            onTriggered: {
                var target = root.menuTarget
                if (target.kind === "collection") {
                    root.projectRequested(String(target.id))
                } else {
                    root.pinOpenRequested(target)
                }
            }
        }

        MenuItem {
            text: rowMenu.pinned ? "Unpin from Home" : "Pin to Home"
            onTriggered: HomeStore.togglePin(root.menuTarget)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 12
        anchors.leftMargin: 10
        anchors.rightMargin: 11
        spacing: 2

        RowLayout {
            Layout.fillWidth: true
            spacing: 4

            HomeSidebarRow {
                Layout.fillWidth: true
                implicitHeight: 34
                theme: root.theme
                iconName: "edit"
                label: "New chat"
                strong: true
                onClicked: root.newChatRequested()
            }

            HomeChip {
                theme: root.theme
                round: true
                implicitHeight: 30
                iconName: "chevron-left"
                iconSize: 12
                toolTipText: "Hide sidebar"
                onClicked: root.collapseRequested()
            }
        }

        // Search filters every conversation by title.
        Rectangle {
            Layout.fillWidth: true
            visible: root.home.allChats.length > 0
            Layout.preferredHeight: 32
            Layout.topMargin: 2
            radius: 8
            color: searchField.activeFocus
                ? root.theme.controlSurfaceBg
                : searchHover.hovered ? root.theme.withAlpha(root.theme.appText, 0.04) : "transparent"
            border.width: searchField.activeFocus ? 1 : 0
            border.color: root.theme.panelBorder

            HoverHandler {
                id: searchHover
                cursorShape: Qt.IBeamCursor
            }

            AppIcon {
                id: searchIcon

                anchors.left: parent.left
                anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                name: "search"
                tone: searchField.activeFocus ? "normal" : "muted"
                iconSize: 14
            }

            TextField {
                id: searchField

                anchors.left: searchIcon.right
                anchors.leftMargin: 4
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: root.query
                onTextChanged: root.query = text
                placeholderText: "Search conversations"
                placeholderTextColor: Qt.darker(root.theme.appText, 1.12)
                color: root.theme.appText
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.textControlSize - 1
                Accessible.name: "Search conversations"
                background: Item {}
                Keys.onEscapePressed: function(event) {
                    if (text.length > 0) {
                        text = ""
                        event.accepted = true
                    }
                }
            }
        }

        Flickable {
            id: list

            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 8
            clip: true
            contentWidth: width
            contentHeight: sections.implicitHeight + 24
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: ScrollBar {
                policy: list.contentHeight > list.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
            }

            ColumnLayout {
                id: sections

                width: list.width
                spacing: 1

                component Eyebrow: Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    Layout.topMargin: 16
                    Layout.bottomMargin: 4
                    color: root.theme.homeEyebrow
                    font.family: root.theme.bodyFontFamily
                    font.pixelSize: root.theme.typeEyebrow
                    font.weight: root.theme.textWeightEmphasis
                    font.letterSpacing: root.theme.textTrackingEyebrow
                    Accessible.role: Accessible.Heading
                }

                // ---- search results -----------------------------------------

                Eyebrow {
                    visible: root.searching
                    Layout.topMargin: 6
                    text: resultsRepeater.count === 0 ? "NO MATCHES" : "CONVERSATIONS"
                }

                Repeater {
                    id: resultsRepeater

                    model: root.searching ? root.searchResults() : []

                    delegate: HomeSidebarRow {
                        required property var modelData

                        Layout.fillWidth: true
                        theme: root.theme
                        iconName: "chat"
                        label: String(modelData.chat.title || "Untitled conversation")
                        trailing: modelData.projectName
                        current: String(modelData.chat.id) === String(ChatStore.selectedChatId)
                        onClicked: root.chatRequested(modelData.chat, modelData.collectionId)
                    }
                }

                // ---- pinned -------------------------------------------------

                Eyebrow {
                    visible: !root.searching && root.pins.length > 0
                    Layout.topMargin: 6
                    text: "PINNED"
                }

                Repeater {
                    model: root.searching ? [] : root.pins

                    delegate: HomeSidebarRow {
                        required property var modelData

                        Layout.fillWidth: true
                        theme: root.theme
                        iconName: root.iconForPin(String(modelData.kind))
                        showSwatch: modelData.kind === "collection"
                        swatch: root.home.projectHue(String(modelData.id))
                        label: String(modelData.title || "")
                        onClicked: root.pinOpenRequested(modelData)
                        onContextMenuRequested: root.openMenu(modelData)
                    }
                }

                // ---- projects -----------------------------------------------

                Eyebrow {
                    visible: !root.searching
                    Layout.topMargin: root.pins.length > 0 ? 16 : 6
                    text: "PROJECTS"
                }

                HomeSidebarRow {
                    Layout.fillWidth: true
                    visible: !root.searching && root.home.projects.length === 0
                    theme: root.theme
                    iconName: "add"
                    label: "Set up a project"
                    onClicked: root.setupProjectRequested()
                }

                Repeater {
                    model: root.searching ? [] : root.visibleProjects

                    delegate: ColumnLayout {
                        id: projectEntry

                        required property var modelData

                        readonly property string projectId: String(modelData.id)
                        readonly property bool open: root.projectExpanded(projectId)
                        readonly property var chats: open ? root.home.chatsForProject(projectId, 0) : []
                        readonly property int limit: root.chatLimit(projectId)

                        Layout.fillWidth: true
                        spacing: 1

                        HomeSidebarRow {
                            Layout.fillWidth: true
                            theme: root.theme
                            showSwatch: true
                            swatch: root.home.projectHue(projectEntry.projectId)
                            disclosure: projectEntry.open ? 1 : 0
                            label: root.home.projectLabel(projectEntry.modelData)
                            current: projectEntry.projectId === root.currentProjectId
                            trailing: projectEntry.projectId === root.currentProjectId ? "open" : ""
                            ToolTip.visible: hovered
                            ToolTip.delay: 900
                            ToolTip.text: "Open where you left it"
                            Accessible.description: projectEntry.open ? "Expanded" : "Collapsed"
                            onClicked: root.projectRequested(projectEntry.projectId)
                            onDisclosureToggled: root.toggleProject(projectEntry.projectId)
                            onContextMenuRequested: root.openMenu({
                                kind: "collection",
                                id: projectEntry.projectId,
                                title: root.home.projectLabel(projectEntry.modelData),
                                subtitle: "Project"
                            })
                        }

                        Repeater {
                            model: projectEntry.chats.slice(0, projectEntry.limit)

                            delegate: HomeSidebarRow {
                                required property var modelData

                                Layout.fillWidth: true
                                theme: root.theme
                                indent: 24
                                label: String(modelData.title || "Untitled conversation")
                                current: String(modelData.id) === String(ChatStore.selectedChatId)
                                onClicked: root.chatRequested(modelData, projectEntry.projectId)
                                onContextMenuRequested: root.openMenu({
                                    kind: "chat",
                                    id: String(modelData.id),
                                    collectionId: projectEntry.projectId,
                                    libraryId: String(modelData.libraryId || ""),
                                    title: String(modelData.title || "Untitled conversation"),
                                    subtitle: root.home.projectLabel(projectEntry.modelData)
                                })
                            }
                        }

                        HomeSidebarRow {
                            Layout.fillWidth: true
                            visible: projectEntry.open && projectEntry.chats.length > projectEntry.limit
                            theme: root.theme
                            indent: 24
                            label: "Show " + (projectEntry.chats.length - projectEntry.limit) + " more"
                            onClicked: root.showMoreChats(projectEntry.projectId)
                        }

                        HomeSidebarRow {
                            Layout.fillWidth: true
                            visible: projectEntry.open && projectEntry.chats.length === 0
                            theme: root.theme
                            indent: 24
                            label: "No conversations yet"
                            enabled: false
                        }
                    }
                }

                HomeSidebarRow {
                    Layout.fillWidth: true
                    visible: !root.searching && root.home.projects.length > root.projectLimit
                    theme: root.theme
                    iconName: root.allProjects ? "chevron-up" : "more"
                    label: root.allProjects ? "Fewer projects" : "All projects (" + root.home.projects.length + ")"
                    onClicked: root.allProjects = !root.allProjects
                }

                // ---- recent conversations -----------------------------------

                Eyebrow {
                    visible: !root.searching && root.recentChats.length > 0
                    text: "RECENT"
                }

                Repeater {
                    model: root.searching ? [] : root.recentChats

                    delegate: HomeSidebarRow {
                        required property var modelData

                        Layout.fillWidth: true
                        theme: root.theme
                        label: String(modelData.chat.title || "Untitled conversation")
                        trailing: modelData.meta
                        current: String(modelData.chat.id) === String(ChatStore.selectedChatId)
                        ToolTip.visible: hovered && modelData.projectName.length > 0
                        ToolTip.delay: 700
                        ToolTip.text: "In " + modelData.projectName
                        onClicked: root.chatRequested(modelData.chat, modelData.collectionId)
                        onContextMenuRequested: root.openMenu({
                            kind: "chat",
                            id: String(modelData.chat.id),
                            collectionId: modelData.collectionId,
                            libraryId: String(modelData.chat.libraryId || ""),
                            title: String(modelData.chat.title || "Untitled conversation"),
                            subtitle: modelData.projectName
                        })
                    }
                }
            }
        }
    }
}

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Archivist.Services 1.0
import "../../Icons"
import "../WorkbenchShell/ExplorerDock/WorkspaceNavigator"

// Home's side panel. It sits in the shell's side panel slot, where the
// Explorer is in a project, and is built like it: the same header bar,
// band headers and rows, and the shell's drag-to-resize edge.
//
//   Header      search conversations, New chat, close
//   Pinned      what the person keeps close (only when the page doesn't
//               already show it)
//   Projects    each opens where it was left; the arrow shows its chats
//   Recent      a short list of conversations
//
// Exhaustive browsing (every file, every chat) stays in the workspace's
// panels. The sidebar only lists and asks; the shell and HomeView turn the
// requests into opening things.
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
    signal closeRequested()

    property string query: ""
    // Projects the person opened or closed in the list.
    property var expanded: ({})
    property bool allProjects: false
    // Per project: how many chats to show when open.
    property var chatLimits: ({})
    // Bands the person folded: { pinned, projects, recent }.
    property var folded: ({})
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

    function bandOpen(band) {
        return folded[band] !== true
    }

    function toggleBand(band) {
        var next = Object.assign({}, folded)
        next[band] = bandOpen(band)
        folded = next
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

    color: theme.surfaceBg

    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        z: 2
        color: root.theme.panelBorder
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

    // Laid out at no less than the panel's minimum width, so opening and
    // closing slides the panel instead of squeezing its rows.
    ColumnLayout {
        width: Math.max(root.width, root.theme.explorerMinWidth)
        height: parent.height
        spacing: 0

        // Header bar, as in the Workspace Navigator.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            color: root.theme.controlSurfaceBg

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: root.theme.quietBorder
                opacity: 0.72
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                spacing: 3

                Rectangle {
                    id: searchBox

                    Layout.fillWidth: true
                    Layout.preferredHeight: 24
                    Layout.alignment: Qt.AlignVCenter
                    radius: 5
                    color: searchHover.hovered || searchField.activeFocus
                        ? root.theme.hoverBg
                        : root.theme.surfaceBg
                    border.width: 1
                    border.color: searchField.activeFocus ? "#554a7b" : root.theme.quietBorder

                    Behavior on color {
                        ColorAnimation { duration: root.theme.motionFast }
                    }

                    HoverHandler {
                        id: searchHover
                        cursorShape: Qt.IBeamCursor
                    }

                    AppIcon {
                        id: searchIcon

                        anchors.left: parent.left
                        anchors.leftMargin: 7
                        anchors.verticalCenter: parent.verticalCenter
                        name: "search"
                        tone: searchField.activeFocus ? "accent" : "muted"
                        iconSize: 13
                        accessibleLabel: "Search conversations"
                    }

                    TextField {
                        id: searchField

                        anchors.left: searchIcon.right
                        anchors.leftMargin: 2
                        anchors.right: parent.right
                        anchors.rightMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.query
                        onTextChanged: root.query = text
                        placeholderText: "Search conversations"
                        placeholderTextColor: root.theme.mutedText
                        color: root.theme.appText
                        font.pixelSize: root.theme.typeSize(9)
                        leftPadding: 4
                        rightPadding: 4
                        topPadding: 0
                        bottomPadding: 0
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

                IconButton {
                    Layout.preferredWidth: 24
                    Layout.preferredHeight: 24
                    Layout.alignment: Qt.AlignVCenter
                    width: 24
                    height: 24
                    theme: root.theme
                    circular: true
                    idleBackgroundColor: root.theme.surfaceBg
                    hoverBackgroundColor: root.theme.hoverBg
                    iconName: "edit"
                    iconSize: 14
                    toolTipText: "New chat"
                    onClicked: root.newChatRequested()
                }

                IconButton {
                    Layout.preferredWidth: 24
                    Layout.preferredHeight: 24
                    Layout.alignment: Qt.AlignVCenter
                    width: 24
                    height: 24
                    theme: root.theme
                    circular: true
                    idleBackgroundColor: root.theme.surfaceBg
                    hoverBackgroundColor: root.theme.hoverBg
                    iconName: "chevron-left"
                    iconSize: 15
                    toolTipText: "Close sidebar"
                    onClicked: root.closeRequested()
                }
            }
        }

        Flickable {
            id: list

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: width
            contentHeight: bands.implicitHeight + 12
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: ScrollBar {
                policy: list.contentHeight > list.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
            }

            ColumnLayout {
                id: bands

                width: list.width
                spacing: 0

                // Rows under a band header, inset like the navigator's.
                component BandRows: ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 7
                    Layout.rightMargin: 7
                    Layout.topMargin: 4
                    Layout.bottomMargin: 6
                    spacing: 2
                }

                // ---- search results -----------------------------------------

                SidebarBandHeader {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 28
                    visible: root.searching
                    theme: root.theme
                    title: "RESULTS"
                    iconName: "search"
                    count: resultsRepeater.count
                    expanded: true
                }

                BandRows {
                    visible: root.searching

                    HomeSidebarRow {
                        Layout.fillWidth: true
                        visible: resultsRepeater.count === 0
                        theme: root.theme
                        label: "No conversations match"
                        enabled: false
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
                }

                // ---- pinned -------------------------------------------------

                SidebarBandHeader {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 28
                    visible: !root.searching && root.pins.length > 0
                    theme: root.theme
                    title: "PINNED"
                    iconName: "pin"
                    count: root.pins.length
                    expanded: root.bandOpen("pinned")
                    onToggleRequested: root.toggleBand("pinned")
                }

                BandRows {
                    visible: !root.searching && root.pins.length > 0 && root.bandOpen("pinned")

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
                }

                // ---- projects -----------------------------------------------

                SidebarBandHeader {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 28
                    visible: !root.searching
                    theme: root.theme
                    title: "PROJECTS"
                    iconName: "folder"
                    count: root.home.projects.length
                    expanded: root.bandOpen("projects")
                    primaryVisible: true
                    primaryToolTip: "New project"
                    onToggleRequested: root.toggleBand("projects")
                    onPrimaryRequested: root.setupProjectRequested()
                }

                BandRows {
                    visible: !root.searching && root.bandOpen("projects")

                    HomeSidebarRow {
                        Layout.fillWidth: true
                        visible: root.home.projects.length === 0
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
                            spacing: 2

                            HomeSidebarRow {
                                Layout.fillWidth: true
                                theme: root.theme
                                showSwatch: true
                                swatch: root.home.projectHue(projectEntry.projectId)
                                disclosure: projectEntry.open ? 1 : 0
                                label: root.home.projectLabel(projectEntry.modelData)
                                current: projectEntry.projectId === root.currentProjectId
                                trailing: projectEntry.projectId === root.currentProjectId ? "OPEN" : ""
                                trailingBadge: true
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
                                    indent: 22
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
                                indent: 22
                                label: "Show " + (projectEntry.chats.length - projectEntry.limit) + " more"
                                onClicked: root.showMoreChats(projectEntry.projectId)
                            }

                            HomeSidebarRow {
                                Layout.fillWidth: true
                                visible: projectEntry.open && projectEntry.chats.length === 0
                                theme: root.theme
                                indent: 22
                                label: "No conversations yet"
                                enabled: false
                            }
                        }
                    }

                    HomeSidebarRow {
                        Layout.fillWidth: true
                        visible: root.home.projects.length > root.projectLimit
                        theme: root.theme
                        iconName: root.allProjects ? "chevron-up" : "more"
                        label: root.allProjects ? "Fewer projects" : "All projects (" + root.home.projects.length + ")"
                        onClicked: root.allProjects = !root.allProjects
                    }
                }

                // ---- recent conversations -----------------------------------

                SidebarBandHeader {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 28
                    visible: !root.searching && root.recentChats.length > 0
                    theme: root.theme
                    title: "RECENT"
                    iconName: "history"
                    count: root.recentChats.length
                    expanded: root.bandOpen("recent")
                    onToggleRequested: root.toggleBand("recent")
                }

                BandRows {
                    visible: !root.searching && root.recentChats.length > 0 && root.bandOpen("recent")

                    Repeater {
                        model: root.searching ? [] : root.recentChats

                        delegate: HomeSidebarRow {
                            required property var modelData

                            Layout.fillWidth: true
                            theme: root.theme
                            iconName: "chat"
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
}

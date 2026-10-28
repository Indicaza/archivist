import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Archivist.Services 1.0
import "../../Icons"

// Home's sidebar: new chat, search, projects with their conversations,
// recent conversations and recently opened files. It only lists and asks;
// HomeView turns the requests into opening things.
Rectangle {
    id: root

    required property var theme
    // HomeView: the data helpers (chats by project, labels, times).
    required property var home

    signal newChatRequested()
    signal chatRequested(var chat, string collectionId)
    signal projectRequested(string collectionId)
    signal recentRequested(var item)
    signal setupProjectRequested()
    signal collapseRequested()

    property string query: ""
    // Projects the person opened or closed in the list; the current one
    // starts open.
    property var expanded: ({})

    readonly property string currentProjectId: String(CollectionStore.selectedCollectionId || "")
    readonly property bool searching: query.trim().length > 0

    function projectExpanded(collectionId) {
        var key = String(collectionId)
        if (expanded[key] !== undefined) {
            return expanded[key]
        }
        return key === currentProjectId
    }

    function toggleProject(collectionId) {
        var next = Object.assign({}, expanded)
        next[String(collectionId)] = !projectExpanded(collectionId)
        expanded = next
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

    color: theme.sidebarBg

    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: root.theme.quietBorder
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 10
        anchors.leftMargin: 8
        anchors.rightMargin: 9
        spacing: 2

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 4
            Layout.bottomMargin: 6
            spacing: 4

            Text {
                Layout.fillWidth: true
                text: "Home"
                color: root.theme.appText
                font.family: root.theme.titleFontFamily
                font.pixelSize: root.theme.textWorkbenchTitleSize
                elide: Text.ElideRight
            }

            IconButton {
                Layout.preferredWidth: 26
                Layout.preferredHeight: 26
                theme: root.theme
                iconName: "chevron-left"
                iconSize: 13
                toolTipText: "Hide sidebar"
                onClicked: root.collapseRequested()
            }
        }

        HomeSidebarRow {
            Layout.fillWidth: true
            theme: root.theme
            iconName: "edit"
            label: "New chat"
            strong: true
            onClicked: root.newChatRequested()
        }

        // Search filters every conversation by title.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            radius: root.theme.radiusSmall + 2
            color: searchField.activeFocus ? root.theme.controlSurfaceBg : "transparent"
            border.width: searchField.activeFocus ? 1 : 0
            border.color: root.theme.panelBorder

            AppIcon {
                id: searchIcon

                anchors.left: parent.left
                anchors.leftMargin: 10
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
                placeholderText: "Search chats"
                placeholderTextColor: root.theme.mutedText
                color: root.theme.appText
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.textControlSize
                Accessible.name: "Search chats"
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
            Layout.topMargin: 10
            clip: true
            contentWidth: width
            contentHeight: sections.implicitHeight + 16
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: ScrollBar {
                policy: list.contentHeight > list.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
            }

            ColumnLayout {
                id: sections

                width: list.width
                spacing: 2

                // ---- search results ---------------------------------------

                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: 10
                    visible: root.searching
                    text: resultsRepeater.count === 0 ? "No chats match." : "Chats"
                    color: root.theme.mutedText
                    font.family: root.theme.bodyFontFamily
                    font.pixelSize: root.theme.textCaptionSize
                    bottomPadding: 4
                }

                Repeater {
                    id: resultsRepeater

                    model: root.searching ? root.searchResults() : []

                    delegate: HomeSidebarRow {
                        required property var modelData

                        Layout.fillWidth: true
                        theme: root.theme
                        label: String(modelData.chat.title || "Untitled conversation")
                        trailing: modelData.projectName
                        current: String(modelData.chat.id) === String(ChatStore.selectedChatId)
                        onClicked: root.chatRequested(modelData.chat, modelData.collectionId)
                    }
                }

                // ---- projects ---------------------------------------------

                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: 10
                    visible: !root.searching
                    text: "Projects"
                    color: root.theme.mutedText
                    font.family: root.theme.bodyFontFamily
                    font.pixelSize: root.theme.textCaptionSize
                    bottomPadding: 4
                    Accessible.role: Accessible.Heading
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
                    model: root.searching ? [] : root.home.projects

                    delegate: ColumnLayout {
                        id: projectEntry

                        required property var modelData

                        readonly property string projectId: String(modelData.id)
                        readonly property bool open: root.projectExpanded(projectId)
                        readonly property var chats: root.home.chatsForProject(projectId, 5)

                        Layout.fillWidth: true
                        spacing: 2

                        HomeSidebarRow {
                            Layout.fillWidth: true
                            theme: root.theme
                            iconName: projectEntry.open ? "folder-open" : "folder"
                            label: root.home.projectLabel(projectEntry.modelData)
                            strong: projectEntry.projectId === root.currentProjectId
                            hoverIconName: "external-link"
                            hoverToolTip: "Open the workspace"
                            Accessible.description: projectEntry.open ? "Expanded" : "Collapsed"
                            onClicked: root.toggleProject(projectEntry.projectId)
                            onDoubleClicked: root.projectRequested(projectEntry.projectId)
                            onHoverActionRequested: root.projectRequested(projectEntry.projectId)
                        }

                        Repeater {
                            model: projectEntry.open ? projectEntry.chats : []

                            delegate: HomeSidebarRow {
                                required property var modelData

                                Layout.fillWidth: true
                                theme: root.theme
                                indent: 23
                                label: String(modelData.title || "Untitled conversation")
                                current: String(modelData.id) === String(ChatStore.selectedChatId)
                                onClicked: root.chatRequested(modelData, projectEntry.projectId)
                            }
                        }

                        HomeSidebarRow {
                            Layout.fillWidth: true
                            visible: projectEntry.open && projectEntry.chats.length === 0
                            theme: root.theme
                            indent: 23
                            label: "No chats yet"
                            enabled: false
                        }
                    }
                }

                // ---- recent conversations ---------------------------------

                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: 10
                    Layout.topMargin: 14
                    visible: !root.searching && root.home.allChats.length > 0
                    text: "Recent"
                    color: root.theme.mutedText
                    font.family: root.theme.bodyFontFamily
                    font.pixelSize: root.theme.textCaptionSize
                    bottomPadding: 4
                    Accessible.role: Accessible.Heading
                }

                Repeater {
                    model: root.searching ? [] : root.home.allChats.slice(0, 12)

                    delegate: HomeSidebarRow {
                        required property var modelData

                        Layout.fillWidth: true
                        theme: root.theme
                        label: String(modelData.chat.title || "Untitled conversation")
                        trailing: modelData.meta
                        current: String(modelData.chat.id) === String(ChatStore.selectedChatId)
                        ToolTip.visible: hovered && modelData.projectName.length > 0
                        ToolTip.delay: 700
                        ToolTip.text: modelData.projectName
                        onClicked: root.chatRequested(modelData.chat, modelData.collectionId)
                    }
                }

                // ---- recently opened files --------------------------------

                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: 10
                    Layout.topMargin: 14
                    visible: !root.searching && root.home.recentFiles.length > 0
                    text: "Recently opened"
                    color: root.theme.mutedText
                    font.family: root.theme.bodyFontFamily
                    font.pixelSize: root.theme.textCaptionSize
                    bottomPadding: 4
                    Accessible.role: Accessible.Heading
                }

                Repeater {
                    model: root.searching ? [] : root.home.recentFiles

                    delegate: HomeSidebarRow {
                        required property var modelData

                        Layout.fillWidth: true
                        theme: root.theme
                        iconName: root.home.fileIcon(modelData)
                        label: String(modelData.title || "File")
                        ToolTip.visible: hovered
                        ToolTip.delay: 700
                        ToolTip.text: String(modelData.subtitle || modelData.title || "")
                        onClicked: root.recentRequested(modelData)
                    }
                }
            }
        }
    }
}

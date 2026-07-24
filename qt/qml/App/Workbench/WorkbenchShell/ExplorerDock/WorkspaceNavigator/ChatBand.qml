import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Archivist.Services 1.0
import "../../../ChatDock/ChatEditor"
import "."

Item {
    id: root

    required property var theme
    required property bool expanded

    property bool archivedOpen: false
    property int hoveredIndex: -1

    readonly property var scopedChats: filteredChats()

    signal toggleRequested()
    signal expandRequested()

    function filteredChats() {
        var chats = ChatStore.chats || []

        if (CollectionStore.selectedCollectionId.length === 0) {
            return chats
        }

        var filtered = []
        for (var index = 0; index < chats.length; index += 1) {
            if (CollectionStore.includesChat(String(chats[index].id))) {
                filtered.push(chats[index])
            }
        }

        return filtered
    }

    function selectChat(chat) {
        if (!chat || !chat.id || ChatStore.responding) {
            return
        }

        var libraryId = String(chat.libraryId || "")
        if (
            libraryId.length > 0
            && libraryId !== String(LibraryStore.selectedLibraryId)
        ) {
            LibraryStore.selectLibrary(libraryId)
        }

        ChatStore.selectChat(String(chat.id))
    }

    function createChat() {
        if (
            LibraryStore.selectedLibraryId.length === 0
            || ChatStore.mutating
            || ChatStore.responding
        ) {
            return
        }

        ChatStore.createChat(
            LibraryStore.selectedLibraryId,
            String(CollectionStore.scope.defaultAgentId || "")
        )
    }

    Component.onCompleted: {
        ChatStore.refresh()
        ChatStore.refreshArchived()
    }

    Connections {
        target: ChatStore

        function onChatCreated(chat) {
            root.expandRequested()
            if (CollectionStore.selectedCollectionId.length > 0) {
                CollectionStore.refresh()
            }
        }

        function onChatRestored(chat) {
            if (CollectionStore.selectedCollectionId.length > 0) {
                CollectionStore.refresh()
            }
        }
    }

    SidebarBandHeader {
        id: header

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 36
        theme: root.theme
        title: "CHATS"
        glyph: "▱"
        count: ChatStore.loadingChats ? 0 : root.scopedChats.length
        expanded: root.expanded
        primaryVisible: true
        primaryEnabled: LibraryStore.selectedLibraryId.length > 0
            && !ChatStore.mutating
            && !ChatStore.responding
        primaryToolTip: LibraryStore.selectedLibraryId.length > 0
            ? "Create Chat in "
                + String(LibraryStore.selectedLibrary.name || "Library")
            : "Select a Library before creating a Chat"
        secondaryVisible: true
        secondaryEnabled: ChatStore.selectedChatId.length > 0
            && !ChatStore.mutating
            && !ChatStore.responding
        secondaryToolTip: "Manage selected Chat"
        onToggleRequested: root.toggleRequested()
        onPrimaryRequested: root.createChat()
        onSecondaryRequested: chatEditor.openForChat(
            ChatStore.selectedChat
        )
    }

    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        anchors.topMargin: 4
        anchors.bottomMargin: 4
        visible: root.expanded
        spacing: 3

        ListView {
            id: chatList

            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.scopedChats.length > 0
            clip: true
            spacing: 1
            boundsBehavior: Flickable.StopAtBounds
            model: root.scopedChats

            delegate: Item {
                id: chatDelegate

                required property int index
                required property var modelData

                width: chatList.width
                height: 43
                z: chatHover.hovered
                    ? 3
                    : chatDelegate.neighborHovered
                        ? 2
                        : 1

                readonly property bool selected: String(modelData.id)
                    === String(ChatStore.selectedChatId)
                readonly property bool neighborHovered: root.hoveredIndex >= 0
                    && Math.abs(root.hoveredIndex - index) === 1

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    height: 39
                    radius: root.theme.radiusSmall
                    color: chatTap.pressed
                        ? "#292621"
                        : chatHover.hovered
                            ? root.theme.hoverBg
                            : chatDelegate.selected
                                ? root.theme.activeBg
                                : "transparent"
                    border.width: chatDelegate.selected ? 1 : 0
                    border.color: "#554a7b"
                    scale: chatTap.pressed
                        ? root.theme.pressedScale
                        : chatHover.hovered
                            ? root.theme.hoverScale
                            : chatDelegate.neighborHovered
                                ? root.theme.hoverNeighborScale
                                : 1.0

                    Behavior on scale {
                        enabled: !chatTap.pressed

                        NumberAnimation {
                            duration: chatHover.hovered
                                || chatDelegate.neighborHovered
                                    ? root.theme.motionHover
                                    : root.theme.motionHoverExit
                            easing.type: Easing.OutCubic
                        }
                    }

                    Column {
                        anchors.left: parent.left
                        anchors.right: manageChatButton.left
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 11
                        anchors.rightMargin: 5
                        spacing: 1

                        Text {
                            width: parent.width
                            text: String(
                                chatDelegate.modelData.title || "Untitled Chat"
                            )
                            color: chatDelegate.selected || chatHover.hovered
                                ? root.theme.appText
                                : root.theme.mutedText
                            font.pixelSize: root.theme.typeSize(9)
                            font.weight: chatDelegate.selected
                                ? Font.DemiBold
                                : Font.Normal
                            elide: Text.ElideRight
                        }

                        Text {
                            width: parent.width
                            text: String(
                                chatDelegate.modelData.libraryName || "Standalone"
                            )
                            color: root.theme.mutedText
                            font.pixelSize: root.theme.typeSize(8)
                            opacity: 0.62
                            elide: Text.ElideRight
                        }
                    }

                    Button {
                        id: manageChatButton

                        anchors.right: parent.right
                        anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        width: 28
                        height: 26
                        text: "•••"
                        enabled: !ChatStore.mutating
                            && !ChatStore.responding
                        hoverEnabled: true
                        padding: 0
                        ToolTip.visible: hovered
                        ToolTip.text: "Manage Chat"
                        onClicked: chatEditor.openForChat(
                            chatDelegate.modelData
                        )

                        contentItem: Text {
                            text: parent.text
                            color: parent.hovered
                                ? root.theme.appText
                                : root.theme.mutedText
                            font.pixelSize: root.theme.typeSize(8)
                            font.weight: Font.Bold
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        background: Rectangle {
                            radius: 4
                            color: parent.hovered
                                ? root.theme.surfaceBg
                                : "transparent"
                            border.width: parent.hovered ? 1 : 0
                            border.color: root.theme.panelBorder
                        }
                    }
                }

                HoverHandler {
                    id: chatHover

                    onHoveredChanged: {
                        if (hovered) {
                            root.hoveredIndex = chatDelegate.index
                        } else if (root.hoveredIndex === chatDelegate.index) {
                            root.hoveredIndex = -1
                        }
                    }
                }

                TapHandler {
                    id: chatTap

                    enabled: !ChatStore.responding
                    onTapped: root.selectChat(chatDelegate.modelData)
                }
            }

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }
        }

        Button {
            Layout.fillWidth: true
            Layout.preferredHeight: 25
            text: (root.archivedOpen ? "▾" : "▸")
                + "  Archived  "
                + String(ChatStore.archivedChats.length)
            enabled: !ChatStore.mutating
            hoverEnabled: true
            padding: 0
            onClicked: {
                root.archivedOpen = !root.archivedOpen
                if (root.archivedOpen) {
                    ChatStore.refreshArchived()
                }
            }

            contentItem: Text {
                text: parent.text
                color: parent.hovered
                    ? root.theme.appText
                    : root.theme.mutedText
                font.pixelSize: root.theme.typeSize(8)
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
                leftPadding: 7
            }

            background: Rectangle {
                radius: 4
                color: parent.hovered
                    ? root.theme.hoverBg
                    : root.theme.controlSurfaceBg
                border.width: 1
                border.color: root.theme.quietBorder
            }
        }

        ListView {
            id: archivedChatList

            Layout.fillWidth: true
            Layout.preferredHeight: root.archivedOpen ? 78 : 0
            visible: root.archivedOpen
            clip: true
            spacing: 1
            model: ChatStore.archivedChats

            delegate: Rectangle {
                id: archivedChatItem

                required property var modelData

                width: archivedChatList.width
                height: 32
                radius: 4
                color: archivedChatHover.hovered
                    ? root.theme.hoverBg
                    : "transparent"

                Text {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: 8
                    anchors.rightMargin: 28
                    anchors.verticalCenter: parent.verticalCenter
                    text: String(
                        archivedChatItem.modelData.title || "Untitled Chat"
                    )
                    color: root.theme.mutedText
                    font.pixelSize: root.theme.typeSize(8)
                    elide: Text.ElideRight
                }

                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: "✎"
                    color: root.theme.mutedText
                    font.pixelSize: root.theme.typeSize(9)
                }

                HoverHandler {
                    id: archivedChatHover
                }

                TapHandler {
                    enabled: !ChatStore.mutating && !ChatStore.responding
                    onTapped: chatEditor.openForChat(
                        archivedChatItem.modelData
                    )
                }
            }
        }

        Text {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.scopedChats.length === 0
            text: ChatStore.errorMessage.length > 0
                ? ChatStore.errorMessage
                : ChatStore.loadingChats
                    ? "Loading Chats…"
                    : CollectionStore.selectedCollectionId.length > 0
                        ? "No Chats in this Collection."
                        : "No active Chats."
            color: ChatStore.errorMessage.length > 0
                ? root.theme.danger
                : root.theme.mutedText
            font.pixelSize: root.theme.typeSize(8)
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    ChatEditor {
        id: chatEditor

        theme: root.theme
    }
}

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Archivist.Services 1.0
import "AgentEditor"
import "ChatEditor"

Rectangle {
    id: root

    required property var theme
    property bool attached: true
    property string activePanel: "none"
    property bool archivedAgentsOpen: false
    property bool archivedChatsOpen: false
    property string attachmentNotice: ""
    property int headerHoverIndex: -1
    property int composerHoverIndex: -1
    property int hoveredChatIndex: -1
    property int hoveredAgentIndex: -1

    readonly property int attachmentCount: ChatStore.attachments.length
    readonly property string selectedChatTitle: ChatStore.selectedChat.title || "Select a Chat"
    readonly property string selectedLibraryName: ChatStore.selectedChat.libraryName
        || (ChatStore.selectedChatId.length > 0
            ? "Standalone"
            : LibraryStore.selectedLibrary.name || "No Library")
    readonly property var selectedAgent: agentForId(ChatStore.selectedChat.agentId)
    readonly property string selectedAgentName: selectedAgent && selectedAgent.name
        ? String(selectedAgent.name)
        : ChatStore.selectedChat.agentId
            ? "Unavailable"
            : "None"
    readonly property bool canSubmit: ChatStore.selectedChatId.length > 0
        && !ChatStore.responding
        && !ChatStore.assigningAgent
        && !ChatStore.mutating
        && !ChatStore.mutatingAttachment
        && composer.text.trim().length > 0

    signal dockModeToggleRequested()
    signal messageSubmitted(string message)

    function agentForId(agentId) {
        var agents = AgentStore.agents || []

        for (var index = 0; index < agents.length; index += 1) {
            if (String(agents[index].id) === String(agentId || "")) {
                return agents[index]
            }
        }

        return null
    }

    function sourceWasIncluded(attachmentId) {
        var sources = ChatStore.lastSources || []

        for (var index = 0; index < sources.length; index += 1) {
            if (String(sources[index].attachmentId || "") === String(attachmentId || "")) {
                return true
            }
        }

        return false
    }

    function submitDraft() {
        var trimmed = composer.text.trim()

        if (!root.canSubmit || trimmed.length === 0) {
            return
        }

        root.messageSubmitted(trimmed)
        composer.clear()
        composer.forceActiveFocus()
    }

    function magnifierScale(index, hoveredIndex, pressed) {
        if (pressed) {
            return theme.pressedScale
        }

        if (index === hoveredIndex) {
            return theme.hoverScale
        }

        if (hoveredIndex >= 0 && Math.abs(index - hoveredIndex) === 1) {
            return theme.hoverNeighborScale
        }

        return 1.0
    }

    function updateHoverIndex(group, index, hovered) {
        if (hovered) {
            if (group === "header") {
                headerHoverIndex = index
            } else if (group === "composer") {
                composerHoverIndex = index
            }
        } else if (group === "header" && headerHoverIndex === index) {
            headerHoverIndex = -1
        } else if (group === "composer" && composerHoverIndex === index) {
            composerHoverIndex = -1
        }
    }

    Component.onCompleted: {
        AgentStore.refresh()
        AgentStore.refreshArchived()
        ChatStore.refreshArchived()
    }

    Connections {
        target: ChatStore

        function onAttachmentAdded(attachment) {
            var path = String(
                attachment.relativePath || attachment.fileName || "Library file"
            )
            root.attachmentNotice = "Attached " + path
            attachmentNoticeTimer.restart()
        }

        function onAttachmentRemoved(attachmentId) {
            root.attachmentNotice = "Source detached"
            attachmentNoticeTimer.restart()
        }

        function onChatCreated(chat) {
            root.activePanel = "none"
            composer.forceActiveFocus()
        }
    }

    Timer {
        id: attachmentNoticeTimer
        interval: 3000
        repeat: false
        onTriggered: root.attachmentNotice = ""
    }

    color: theme.surfaceBg
    border.width: 0
    clip: true

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: root.theme.panelBorder
        z: 5
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: root.theme.chatDockHeaderHeight
            color: root.theme.controlSurfaceBg

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: root.theme.quietBorder
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 11
                anchors.rightMargin: 6
                spacing: 8

                Column {
                    Layout.fillWidth: true
                    spacing: 3

                    Text {
                        width: parent.width
                        text: root.selectedChatTitle
                        color: root.theme.appText
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }

                    Row {
                        spacing: 8

                        Text {
                            text: "▣  LIBRARY  " + root.selectedLibraryName
                            color: root.theme.mutedText
                            font.pixelSize: 9
                            font.letterSpacing: 0.25
                        }

                        Rectangle {
                            width: 1
                            height: 10
                            color: root.theme.quietBorder
                        }

                        Text {
                            text: "♙  AGENT  " + root.selectedAgentName
                            color: root.theme.mutedText
                            font.pixelSize: 9
                            font.letterSpacing: 0.25
                            elide: Text.ElideRight
                        }

                        Rectangle {
                            width: 1
                            height: 10
                            color: root.theme.quietBorder
                        }

                        Text {
                            text: "▤  SOURCES  " + String(root.attachmentCount)
                            color: root.attachmentCount > 0
                                ? root.theme.accentBright
                                : root.theme.mutedText
                            font.pixelSize: 9
                            font.letterSpacing: 0.25
                        }
                    }
                }

                Button {
                    id: manageChatButton

                    Layout.preferredWidth: 31
                    Layout.preferredHeight: 31
                    text: "✎"
                    enabled: ChatStore.selectedChatId.length > 0
                    hoverEnabled: true
                    padding: 0
                    ToolTip.visible: hovered
                    ToolTip.text: "Manage Chat"
                    onClicked: chatEditor.openForChat(ChatStore.selectedChat)
                    onHoveredChanged: root.updateHoverIndex(
                        "header",
                        0,
                        hovered
                    )
                    scale: root.magnifierScale(
                        0,
                        root.headerHoverIndex,
                        down
                    )

                    Behavior on scale {
                        NumberAnimation {
                            duration: root.headerHoverIndex >= 0
                                ? root.theme.motionHover
                                : root.theme.motionHoverExit
                            easing.type: root.headerHoverIndex >= 0
                                ? Easing.OutBack
                                : Easing.OutCubic
                        }
                    }

                    contentItem: Text {
                        text: parent.text
                        color: parent.enabled && parent.hovered
                            ? root.theme.appText
                            : root.theme.mutedText
                        font.pixelSize: 14
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        opacity: parent.enabled ? 1 : 0.45
                    }

                    background: Rectangle {
                        radius: 4
                        color: parent.enabled && parent.hovered
                            ? root.theme.hoverBg
                            : "transparent"
                        border.width: 0
                    }
                }

                Button {
                    id: dockModeButton

                    Layout.preferredWidth: 31
                    Layout.preferredHeight: 31
                    text: root.attached ? "↙" : "↗"
                    hoverEnabled: true
                    padding: 0
                    ToolTip.visible: hovered
                    ToolTip.text: root.attached ? "Center Chat Dock" : "Attach Chat Dock"
                    onClicked: root.dockModeToggleRequested()
                    onHoveredChanged: root.updateHoverIndex(
                        "header",
                        1,
                        hovered
                    )
                    scale: root.magnifierScale(
                        1,
                        root.headerHoverIndex,
                        down
                    )

                    Behavior on scale {
                        NumberAnimation {
                            duration: root.headerHoverIndex >= 0
                                ? root.theme.motionHover
                                : root.theme.motionHoverExit
                            easing.type: root.headerHoverIndex >= 0
                                ? Easing.OutBack
                                : Easing.OutCubic
                        }
                    }

                    contentItem: Text {
                        text: parent.text
                        color: parent.hovered ? root.theme.appText : root.theme.mutedText
                        font.pixelSize: 15
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: 4
                        color: parent.hovered ? root.theme.hoverBg : "transparent"
                        border.width: 0
                    }
                }

                Repeater {
                    model: ["chats", "agents"]

                    delegate: Button {
                        id: dockTabButton

                        required property int index
                        required property string modelData

                        Layout.preferredWidth: 58
                        Layout.preferredHeight: 31
                        text: modelData === "chats" ? "▱  Chats" : "♙  Agents"
                        hoverEnabled: true
                        padding: 0
                        onClicked: root.activePanel = root.activePanel === modelData
                            ? "none"
                            : modelData
                        onHoveredChanged: root.updateHoverIndex(
                            "header",
                            index + 2,
                            hovered
                        )

                        contentItem: Text {
                            text: parent.text
                            color: root.activePanel === modelData || parent.hovered
                                ? root.theme.appText
                                : root.theme.mutedText
                            font.pixelSize: 10
                            font.weight: Font.DemiBold
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        background: Item {
                            Rectangle {
                                anchors.fill: parent
                                color: root.activePanel === modelData
                                    ? "#211f1c"
                                    : dockTabButton.hovered
                                        ? root.theme.hoverBg
                                        : "transparent"
                            }

                            Rectangle {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: 1
                                visible: root.activePanel === modelData
                                color: root.theme.appText
                                opacity: 0.52
                            }
                        }

                        scale: root.magnifierScale(
                            index + 2,
                            root.headerHoverIndex,
                            down
                        )

                        Behavior on scale {
                            NumberAnimation {
                                duration: root.headerHoverIndex >= 0
                                    ? root.theme.motionHover
                                    : root.theme.motionHoverExit
                                easing.type: root.headerHoverIndex >= 0
                                    ? Easing.OutBack
                                    : Easing.OutCubic
                            }
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: composer.activeFocus ? "#1a1815" : root.theme.composerBg

                Behavior on color {
                    ColorAnimation { duration: 140 }
                }

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: ChatStore.selectedChatId.length > 0 ? 38 : 0
                        visible: ChatStore.selectedChatId.length > 0
                        color: root.theme.sidebarBg

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 1
                            color: root.theme.quietBorder
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 8
                            spacing: 8

                            Text {
                                text: "SOURCES  " + String(root.attachmentCount) + " ATTACHED"
                                color: root.attachmentCount > 0
                                    ? root.theme.accentBright
                                    : root.theme.mutedText
                                font.pixelSize: 8
                                font.weight: Font.Bold
                                font.letterSpacing: 0.55
                                opacity: 0.82
                            }

                            Text {
                                visible: !ChatStore.loadingAttachments
                                    && root.attachmentCount === 0
                                Layout.fillWidth: true
                                text: "No files attached — preview a Library file to add one."
                                color: root.theme.mutedText
                                font.pixelSize: 9
                                opacity: 0.66
                                elide: Text.ElideRight
                            }

                            ListView {
                                id: attachmentList

                                Layout.fillWidth: true
                                Layout.preferredHeight: 26
                                visible: root.attachmentCount > 0
                                orientation: ListView.Horizontal
                                spacing: 6
                                clip: true
                                model: ChatStore.attachments

                                delegate: Rectangle {
                                    id: attachmentChip

                                    required property var modelData

                                    readonly property string sourcePath: String(
                                        modelData.libraryName || "Library"
                                    ) + " / " + String(
                                        modelData.relativePath || modelData.fileName || "Library file"
                                    )
                                    readonly property bool includedInLastResponse: root.sourceWasIncluded(
                                        modelData.id
                                    )

                                    width: Math.min(
                                        190,
                                        Math.max(90, attachmentLabel.implicitWidth + 30)
                                    )
                                    height: 24
                                    color: root.theme.controlSurfaceBg
                                    border.width: 1
                                    border.color: attachmentChip.includedInLastResponse
                                        ? root.theme.accentBright
                                        : root.theme.quietBorder
                                    radius: 4

                                    Text {
                                        id: attachmentLabel

                                        anchors.left: parent.left
                                        anchors.right: removeAttachmentButton.left
                                        anchors.leftMargin: 8
                                        anchors.rightMargin: 4
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: (attachmentChip.includedInLastResponse ? "✓  " : "▤  ")
                                            + attachmentChip.sourcePath
                                        color: root.theme.appText
                                        font.pixelSize: 9
                                        elide: Text.ElideMiddle
                                    }

                                    Button {
                                        id: removeAttachmentButton

                                        anchors.right: parent.right
                                        anchors.rightMargin: 3
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 20
                                        height: 20
                                        text: "×"
                                        enabled: !ChatStore.responding
                                            && !ChatStore.mutating
                                            && !ChatStore.mutatingAttachment
                                        hoverEnabled: true
                                        padding: 0
                                        ToolTip.visible: hovered
                                        ToolTip.text: "Remove attached source"
                                        onClicked: ChatStore.removeAttachment(
                                            String(attachmentChip.modelData.id)
                                        )

                                        contentItem: Text {
                                            text: parent.text
                                            color: parent.enabled && parent.hovered
                                                ? root.theme.appText
                                                : root.theme.mutedText
                                            font.pixelSize: 11
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                            opacity: parent.enabled ? 1 : 0.45
                                        }

                                        background: Rectangle {
                                            color: parent.enabled && parent.hovered
                                                ? root.theme.hoverBg
                                                : "transparent"
                                            radius: 3
                                        }
                                    }
                                }
                            }

                            Text {
                                visible: ChatStore.loadingAttachments
                                text: "Loading…"
                                color: root.theme.mutedText
                                font.pixelSize: 9
                                opacity: 0.7
                            }
                        }
                    }

                    TextArea {
                        id: composer

                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        enabled: ChatStore.selectedChatId.length > 0
                            && !ChatStore.responding
                            && !ChatStore.mutating
                            && !ChatStore.mutatingAttachment
                        placeholderText: ChatStore.responding
                            ? "Archivist is thinking…"
                            : ChatStore.selectedChatId.length > 0
                                ? "Message " + root.selectedChatTitle + "…"
                                : "Select a Chat to begin…"
                        placeholderTextColor: root.theme.composerPlaceholder
                        color: root.theme.appText
                        selectionColor: "#5a554b"
                        selectedTextColor: "#ffffff"
                        font.family: root.theme.bodyFontFamily
                        font.pixelSize: 14
                        wrapMode: TextEdit.Wrap
                        leftPadding: 15
                        rightPadding: 15
                        topPadding: 14
                        bottomPadding: 10

                        Keys.onPressed: function(event) {
                            var returnPressed = event.key === Qt.Key_Return
                                || event.key === Qt.Key_Enter
                            var shiftPressed = (event.modifiers & Qt.ShiftModifier) !== 0

                            if (returnPressed && !shiftPressed) {
                                root.submitDraft()
                                event.accepted = true
                            }
                        }

                        background: Rectangle {
                            color: "transparent"
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: root.theme.controlBarHeight
                        color: root.theme.sidebarBg

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            height: 1
                            color: root.theme.quietBorder
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 9
                            anchors.rightMargin: 7
                            spacing: 5

                            Repeater {
                                model: ["☷", "☰", "</>"]

                                delegate: Button {
                                    required property int index
                                    required property string modelData

                                    Layout.preferredWidth: 27
                                    Layout.preferredHeight: 27
                                    text: modelData
                                    hoverEnabled: true
                                    padding: 0
                                    onHoveredChanged: root.updateHoverIndex(
                                        "composer",
                                        index,
                                        hovered
                                    )
                                    scale: root.magnifierScale(
                                        index,
                                        root.composerHoverIndex,
                                        down
                                    )

                                    Behavior on scale {
                                        NumberAnimation {
                                            duration: root.composerHoverIndex >= 0
                                                ? root.theme.motionHover
                                                : root.theme.motionHoverExit
                                            easing.type: root.composerHoverIndex >= 0
                                                ? Easing.OutBack
                                                : Easing.OutCubic
                                        }
                                    }

                                    contentItem: Text {
                                        text: parent.text
                                        color: parent.hovered
                                            ? root.theme.appText
                                            : root.theme.mutedText
                                        font.pixelSize: modelData === "</>" ? 10 : 14
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                    }

                                    background: Rectangle {
                                        radius: 4
                                        color: parent.hovered ? root.theme.hoverBg : "transparent"
                                        border.width: 0
                                    }
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                text: ChatStore.errorMessage.length > 0
                                    ? ChatStore.errorMessage
                                    : root.attachmentNotice.length > 0
                                        ? root.attachmentNotice
                                        : ChatStore.mutatingAttachment
                                            ? "Updating attached sources…"
                                            : ChatStore.responding
                                            ? "Archivist is thinking…"
                                            : ChatStore.selectedChatId.length === 0
                                                ? "Select a Chat"
                                                : ChatStore.lastSources.length > 0
                                                    ? "Included: "
                                                        + String(
                                                            ChatStore.lastSources[0].libraryName
                                                            || "Library"
                                                        )
                                                        + " / "
                                                        + String(
                                                            ChatStore.lastSources[0].relativePath
                                                            || ChatStore.lastSources[0].fileName
                                                            || "attached source"
                                                        )
                                                        + (ChatStore.lastSources.length > 1
                                                            ? "  +" + String(ChatStore.lastSources.length - 1)
                                                            : "")
                                                    : "Enter to send  ·  Shift+Enter for newline"
                                color: ChatStore.errorMessage.length > 0
                                    ? root.theme.danger
                                    : root.attachmentNotice.length > 0
                                        ? root.theme.success
                                        : root.theme.mutedText
                                font.pixelSize: 9
                                opacity: 0.78
                                elide: Text.ElideRight
                            }

                            Button {
                                id: sendButton

                                Layout.preferredWidth: 74
                                Layout.preferredHeight: 30
                                text: ChatStore.responding ? "Working" : "➤  Send"
                                enabled: root.canSubmit
                                hoverEnabled: true
                                padding: 0
                                onClicked: root.submitDraft()
                                onHoveredChanged: root.updateHoverIndex(
                                    "composer",
                                    3,
                                    hovered
                                )
                                scale: root.magnifierScale(
                                    3,
                                    root.composerHoverIndex,
                                    down
                                )

                                Behavior on scale {
                                    NumberAnimation {
                                        duration: root.composerHoverIndex >= 0
                                            ? root.theme.motionHover
                                            : root.theme.motionHoverExit
                                        easing.type: root.composerHoverIndex >= 0
                                            ? Easing.OutBack
                                            : Easing.OutCubic
                                    }
                                }

                                contentItem: Text {
                                    text: parent.text
                                    color: parent.enabled
                                        ? root.theme.appText
                                        : root.theme.mutedText
                                    font.pixelSize: 10
                                    font.weight: Font.Bold
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }

                                background: Rectangle {
                                    radius: 4
                                    color: parent.enabled
                                        ? parent.hovered
                                            ? "#302d28"
                                            : "#28251f"
                                        : "#1f1d19"
                                    border.width: 0
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: root.activePanel === "none"
                    ? 0
                    : Math.min(280, root.width * 0.32)
                Layout.fillHeight: true
                visible: root.activePanel !== "none"
                color: root.theme.sidebarBg
                clip: true

                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 1
                    color: root.theme.quietBorder
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 8
                    anchors.topMargin: 9
                    anchors.bottomMargin: 9
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true

                        Text {
                            Layout.fillWidth: true
                            text: root.activePanel === "chats" ? "CHATS" : "AGENTS"
                            color: root.theme.mutedText
                            font.pixelSize: 10
                            font.weight: Font.Bold
                            font.letterSpacing: 0.7
                        }

                        Text {
                            text: root.activePanel === "chats"
                                ? ChatStore.loadingChats ? "Loading" : String(ChatStore.chats.length)
                                : AgentStore.loading ? "Loading" : String(AgentStore.agents.length)
                            color: root.theme.mutedText
                            font.pixelSize: 9
                            opacity: 0.65
                        }

                        Button {
                            visible: root.activePanel === "chats"
                                || root.activePanel === "agents"
                            Layout.preferredWidth: 24
                            Layout.preferredHeight: 24
                            text: "+"
                            enabled: root.activePanel === "chats"
                                ? LibraryStore.selectedLibraryId.length > 0
                                    && !ChatStore.mutating
                                    && !ChatStore.responding
                                : !AgentStore.mutating
                            hoverEnabled: true
                            padding: 0
                            ToolTip.visible: hovered
                            ToolTip.text: root.activePanel === "chats"
                                ? LibraryStore.selectedLibraryId.length > 0
                                    ? "Create Chat in "
                                        + String(LibraryStore.selectedLibrary.name || "Library")
                                    : "Select a Library before creating a Chat"
                                : "Create Agent"
                            onClicked: {
                                if (root.activePanel === "chats") {
                                    ChatStore.createChat(LibraryStore.selectedLibraryId)
                                } else {
                                    agentEditor.openForCreate()
                                }
                            }

                            contentItem: Text {
                                text: parent.text
                                color: parent.enabled && parent.hovered
                                    ? root.theme.appText
                                    : root.theme.mutedText
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }

                            background: Rectangle {
                                color: parent.hovered
                                    ? root.theme.hoverBg
                                    : "transparent"
                                border.width: 1
                                border.color: parent.hovered
                                    ? root.theme.panelBorder
                                    : "transparent"
                                radius: 4
                            }
                        }
                    }

                    ListView {
                        id: chatList

                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: root.activePanel === "chats"
                        spacing: 5
                        clip: true
                        model: ChatStore.chats

                        delegate: Rectangle {
                            id: chatItem

                            required property int index
                            required property var modelData

                            readonly property bool selected: String(modelData.id)
                                === ChatStore.selectedChatId
                            readonly property bool neighborHovered: root.hoveredChatIndex >= 0
                                && Math.abs(root.hoveredChatIndex - index) === 1

                            x: 14
                            width: Math.max(0, chatList.width - 28)
                            height: 48
                            radius: root.theme.radiusSmall
                            color: chatTap.pressed
                                ? "#292621"
                                : chatHover.hovered
                                    ? root.theme.hoverBg
                                    : chatItem.selected
                                        ? "#211f1c"
                                        : root.theme.controlSurfaceBg
                            border.width: 1
                            border.color: chatHover.hovered
                                ? root.theme.panelBorder
                                : chatItem.selected
                                    ? "#554a7b"
                                    : root.theme.quietBorder
                            transformOrigin: Item.Center
                            scale: chatTap.pressed
                                ? root.theme.pressedScale
                                : chatHover.hovered
                                    ? root.theme.hoverScale
                                    : chatItem.neighborHovered
                                        ? root.theme.hoverNeighborScale
                                        : 1.0
                            z: chatHover.hovered
                                ? 3
                                : chatItem.neighborHovered
                                    ? 2
                                    : 1

                            Behavior on scale {
                                NumberAnimation {
                                    duration: chatHover.hovered || chatItem.neighborHovered
                                        ? root.theme.motionHover
                                        : root.theme.motionHoverExit
                                    easing.type: chatHover.hovered || chatItem.neighborHovered
                                        ? Easing.OutBack
                                        : Easing.OutCubic
                                }
                            }

                            Behavior on color {
                                ColorAnimation { duration: root.theme.motionFast }
                            }

                            Rectangle {
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                width: 3
                                visible: chatItem.selected
                                color: root.theme.accentBright
                                opacity: 0.78
                            }

                            Column {
                                anchors.left: parent.left
                                anchors.right: chatEditButton.left
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.leftMargin: 12
                                anchors.rightMargin: 7
                                spacing: 3

                                Text {
                                    width: parent.width
                                    text: String(modelData.title || "Untitled Chat")
                                    color: root.theme.appText
                                    font.pixelSize: 10
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }

                                Text {
                                    width: parent.width
                                    text: String(modelData.libraryName || "Standalone")
                                    color: root.theme.mutedText
                                    font.pixelSize: 8
                                    opacity: 0.72
                                    elide: Text.ElideRight
                                }
                            }

                            Button {
                                id: chatEditButton

                                anchors.right: parent.right
                                anchors.rightMargin: 7
                                anchors.verticalCenter: parent.verticalCenter
                                width: 58
                                height: 27
                                text: "Manage"
                                enabled: !ChatStore.mutating && !ChatStore.responding
                                hoverEnabled: true
                                padding: 0
                                ToolTip.visible: hovered
                                ToolTip.text: "Manage Chat"
                                onClicked: chatEditor.openForChat(chatItem.modelData)

                                contentItem: Text {
                                    text: parent.text
                                    color: parent.enabled && parent.hovered
                                        ? root.theme.appText
                                        : root.theme.mutedText
                                    font.pixelSize: 8
                                    font.weight: Font.DemiBold
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }

                                background: Rectangle {
                                    color: parent.hovered
                                        ? root.theme.hoverBg
                                        : root.theme.surfaceBg
                                    border.width: 1
                                    border.color: parent.hovered
                                        ? "#6557a0"
                                        : root.theme.panelBorder
                                    radius: root.theme.radiusSmall
                                }
                            }

                            HoverHandler {
                                id: chatHover

                                onHoveredChanged: {
                                    if (hovered) {
                                        root.hoveredChatIndex = chatItem.index
                                    } else {
                                        if (root.hoveredChatIndex === chatItem.index) {
                                            root.hoveredChatIndex = -1
                                        }
                                    }
                                }
                            }

                            TapHandler {
                                id: chatTap
                                enabled: !ChatStore.responding
                                onTapped: {
                                    ChatStore.selectChat(String(chatItem.modelData.id))
                                    root.activePanel = "none"
                                }
                            }
                        }
                    }

                    Button {
                        visible: root.activePanel === "chats"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 28
                        text: (root.archivedChatsOpen ? "▾" : "▸")
                            + "  Archived  "
                            + String(ChatStore.archivedChats.length)
                        enabled: !ChatStore.mutating
                        hoverEnabled: true
                        padding: 0
                        onClicked: {
                            root.archivedChatsOpen = !root.archivedChatsOpen
                            if (root.archivedChatsOpen) {
                                ChatStore.refreshArchived()
                            }
                        }

                        contentItem: Text {
                            text: parent.text
                            color: parent.hovered
                                ? root.theme.appText
                                : root.theme.mutedText
                            font.pixelSize: 8
                            font.weight: Font.DemiBold
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: 7
                        }

                        background: Rectangle {
                            color: parent.hovered
                                ? root.theme.hoverBg
                                : root.theme.controlSurfaceBg
                            border.width: 1
                            border.color: root.theme.quietBorder
                            radius: 4
                        }
                    }

                    ListView {
                        id: archivedChatList

                        Layout.fillWidth: true
                        Layout.preferredHeight: root.activePanel === "chats"
                            && root.archivedChatsOpen
                                ? 116
                                : 0
                        visible: root.activePanel === "chats" && root.archivedChatsOpen
                        spacing: 2
                        clip: true
                        model: ChatStore.archivedChats

                        delegate: Rectangle {
                            id: archivedChatItem

                            required property var modelData

                            width: archivedChatList.width
                            height: 34
                            color: archivedChatHover.hovered
                                ? root.theme.hoverBg
                                : "transparent"

                            Text {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.leftMargin: 9
                                anchors.rightMargin: 30
                                anchors.verticalCenter: parent.verticalCenter
                                text: String(archivedChatItem.modelData.title || "Untitled Chat")
                                color: root.theme.mutedText
                                font.pixelSize: 9
                                elide: Text.ElideRight
                            }

                            Text {
                                anchors.right: parent.right
                                anchors.rightMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                text: "✎"
                                color: root.theme.mutedText
                                font.pixelSize: 10
                            }

                            HoverHandler {
                                id: archivedChatHover
                            }

                            TapHandler {
                                enabled: !ChatStore.mutating && !ChatStore.responding
                                onTapped: chatEditor.openForChat(archivedChatItem.modelData)
                            }
                        }

                        footer: Text {
                            width: archivedChatList.width
                            height: ChatStore.loadingArchivedChats ? 28 : 0
                            visible: ChatStore.loadingArchivedChats
                            text: "Loading archived Chats…"
                            color: root.theme.mutedText
                            font.pixelSize: 8
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }

                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: root.activePanel === "agents"

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 6

                            Text {
                                Layout.fillWidth: true
                                visible: AgentStore.errorMessage.length > 0
                                text: AgentStore.errorMessage
                                color: root.theme.danger
                                font.pixelSize: 8
                                wrapMode: Text.Wrap
                            }

                            Text {
                                Layout.fillWidth: true
                                visible: ChatStore.selectedChatId.length === 0
                                text: "Select a Chat before assigning an Agent."
                                color: root.theme.mutedText
                                font.pixelSize: 8
                                wrapMode: Text.Wrap
                            }

                            Text {
                                Layout.fillWidth: true
                                visible: ChatStore.assigningAgent
                                text: "Updating Agent assignment…"
                                color: root.theme.accentBright
                                font.pixelSize: 8
                            }

                            ListView {
                                id: agentList

                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                visible: AgentStore.agents.length > 0
                                spacing: 5
                                clip: true
                                model: AgentStore.agents

                                delegate: Rectangle {
                                    id: agentItem

                                    required property int index
                                    required property var modelData

                                    readonly property bool assigned: String(modelData.id)
                                        === String(ChatStore.selectedChat.agentId || "")
                                    readonly property bool neighborHovered: root.hoveredAgentIndex >= 0
                                        && Math.abs(root.hoveredAgentIndex - index) === 1

                                    x: 14
                                    width: Math.max(0, agentList.width - 28)
                                    height: 56
                                    radius: root.theme.radiusSmall
                                    color: agentTap.pressed
                                        ? "#292621"
                                        : agentHover.hovered
                                            ? root.theme.hoverBg
                                            : assigned
                                                ? "#211f1c"
                                                : root.theme.controlSurfaceBg
                                    border.width: 1
                                    border.color: agentHover.hovered
                                        ? root.theme.panelBorder
                                        : agentItem.assigned
                                            ? "#554a7b"
                                            : root.theme.quietBorder
                                    transformOrigin: Item.Center
                                    scale: agentTap.pressed
                                        ? root.theme.pressedScale
                                        : agentHover.hovered
                                            ? root.theme.hoverScale
                                            : agentItem.neighborHovered
                                                ? root.theme.hoverNeighborScale
                                                : 1.0
                                    z: agentHover.hovered
                                        ? 3
                                        : agentItem.neighborHovered
                                            ? 2
                                            : 1

                                    Behavior on scale {
                                        NumberAnimation {
                                            duration: agentHover.hovered || agentItem.neighborHovered
                                                ? root.theme.motionHover
                                                : root.theme.motionHoverExit
                                            easing.type: agentHover.hovered || agentItem.neighborHovered
                                                ? Easing.OutBack
                                                : Easing.OutCubic
                                        }
                                    }

                                    Behavior on color {
                                        ColorAnimation { duration: root.theme.motionFast }
                                    }

                                    Rectangle {
                                        anchors.left: parent.left
                                        anchors.top: parent.top
                                        anchors.bottom: parent.bottom
                                        width: 3
                                        visible: agentItem.assigned
                                        color: root.theme.accentBright
                                        opacity: 0.78
                                    }

                                    Item {
                                        id: assignmentArea

                                        anchors.left: parent.left
                                        anchors.right: editAgentButton.left
                                        anchors.top: parent.top
                                        anchors.bottom: parent.bottom

                                        Column {
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.verticalCenter: parent.verticalCenter
                                            anchors.leftMargin: 12
                                            anchors.rightMargin: 7
                                            spacing: 3

                                            Text {
                                                width: parent.width
                                                text: String(agentItem.modelData.name || "Unnamed Agent")
                                                    + (agentItem.assigned ? "  ✓" : "")
                                                color: root.theme.appText
                                                font.pixelSize: 10
                                                font.weight: Font.DemiBold
                                                elide: Text.ElideRight
                                            }

                                            Text {
                                                width: parent.width
                                                text: agentItem.modelData.description
                                                    || (agentItem.modelData.generation
                                                        ? String(agentItem.modelData.generation.model || "")
                                                        : "")
                                                color: root.theme.mutedText
                                                font.pixelSize: 9
                                                opacity: 0.72
                                                elide: Text.ElideRight
                                            }
                                        }
                                        TapHandler {
                                            id: agentTap
                                            enabled: ChatStore.selectedChatId.length > 0
                                                && !ChatStore.responding
                                                && !ChatStore.assigningAgent
                                                && !ChatStore.mutating
                                                && !agentItem.assigned
                                            onTapped: ChatStore.assignAgentToSelectedChat(
                                                String(agentItem.modelData.id)
                                            )
                                        }
                                    }

                                    Button {
                                        id: editAgentButton

                                        anchors.right: parent.right
                                        anchors.rightMargin: 7
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 46
                                        height: 27
                                        text: "Edit"
                                        enabled: !AgentStore.mutating
                                        hoverEnabled: true
                                        padding: 0
                                        ToolTip.visible: hovered
                                        ToolTip.text: "Edit Agent"
                                        onClicked: agentEditor.openForEdit(agentItem.modelData)

                                        contentItem: Text {
                                            text: parent.text
                                            color: parent.enabled && parent.hovered
                                                ? root.theme.appText
                                                : root.theme.mutedText
                                            font.pixelSize: 8
                                            font.weight: Font.DemiBold
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                        }

                                        background: Rectangle {
                                            color: parent.hovered
                                                ? root.theme.hoverBg
                                                : root.theme.surfaceBg
                                            border.width: 1
                                            border.color: parent.hovered
                                                ? "#6557a0"
                                                : root.theme.panelBorder
                                            radius: root.theme.radiusSmall
                                        }
                                    }

                                    HoverHandler {
                                        id: agentHover

                                        onHoveredChanged: {
                                            if (hovered) {
                                                root.hoveredAgentIndex = agentItem.index
                                            } else {
                                                if (root.hoveredAgentIndex === agentItem.index) {
                                                    root.hoveredAgentIndex = -1
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            Button {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 28
                                text: (root.archivedAgentsOpen ? "▾" : "▸")
                                    + "  Archived  "
                                    + String(AgentStore.archivedAgents.length)
                                enabled: !AgentStore.mutating
                                hoverEnabled: true
                                padding: 0
                                onClicked: {
                                    root.archivedAgentsOpen = !root.archivedAgentsOpen
                                    if (root.archivedAgentsOpen) {
                                        AgentStore.refreshArchived()
                                    }
                                }

                                contentItem: Text {
                                    text: parent.text
                                    color: parent.hovered
                                        ? root.theme.appText
                                        : root.theme.mutedText
                                    font.pixelSize: 8
                                    font.weight: Font.DemiBold
                                    verticalAlignment: Text.AlignVCenter
                                    leftPadding: 7
                                }

                                background: Rectangle {
                                    color: parent.hovered
                                        ? root.theme.hoverBg
                                        : root.theme.controlSurfaceBg
                                    border.width: 1
                                    border.color: root.theme.quietBorder
                                    radius: 4
                                }
                            }

                            ListView {
                                id: archivedAgentList

                                Layout.fillWidth: true
                                Layout.preferredHeight: root.archivedAgentsOpen ? 116 : 0
                                visible: root.archivedAgentsOpen
                                spacing: 2
                                clip: true
                                model: AgentStore.archivedAgents

                                delegate: Rectangle {
                                    id: archivedAgentItem

                                    required property var modelData

                                    width: archivedAgentList.width
                                    height: 34
                                    color: archivedAgentHover.hovered
                                        ? root.theme.hoverBg
                                        : "transparent"

                                    Text {
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.leftMargin: 9
                                        anchors.rightMargin: 30
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: String(archivedAgentItem.modelData.name || "Unnamed Agent")
                                        color: root.theme.mutedText
                                        font.pixelSize: 9
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        anchors.right: parent.right
                                        anchors.rightMargin: 8
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: "✎"
                                        color: root.theme.mutedText
                                        font.pixelSize: 10
                                    }

                                    HoverHandler {
                                        id: archivedAgentHover
                                    }

                                    TapHandler {
                                        enabled: !AgentStore.mutating
                                        onTapped: agentEditor.openForEdit(archivedAgentItem.modelData)
                                    }
                                }

                                footer: Text {
                                    width: archivedAgentList.width
                                    height: AgentStore.loadingArchived ? 28 : 0
                                    visible: AgentStore.loadingArchived
                                    text: "Loading archived Agents…"
                                    color: root.theme.mutedText
                                    font.pixelSize: 8
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                visible: !AgentStore.loading && AgentStore.agents.length === 0
                                text: "No active Agents found."
                                color: root.theme.mutedText
                                font.pixelSize: 8
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }
                }
            }
        }
    }

    Connections {
        target: AgentStore

        function onAgentDeleted(agentId, reassignedChatCount) {
            if (reassignedChatCount > 0) {
                ChatStore.refresh()
            }
        }
    }

    ChatEditor {
        id: chatEditor

        theme: root.theme
    }

    AgentEditor {
        id: agentEditor

        theme: root.theme
    }
}

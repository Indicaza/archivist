import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Archivist.Services 1.0
import "AgentEditor"

Rectangle {
    id: root

    required property var theme
    property bool attached: true
    property string activePanel: "none"
    property bool archivedAgentsOpen: false

    readonly property string selectedChatTitle: ChatStore.selectedChat.title || "Select a Chat"
    readonly property string selectedLibraryName: LibraryStore.selectedLibrary.name || "Standalone"
    readonly property var selectedAgent: agentForId(ChatStore.selectedChat.agentId)
    readonly property string selectedAgentName: selectedAgent && selectedAgent.name
        ? String(selectedAgent.name)
        : ChatStore.selectedChat.agentId
            ? "Unavailable"
            : "None"
    readonly property bool canSubmit: ChatStore.selectedChatId.length > 0
        && !ChatStore.responding
        && !ChatStore.assigningAgent
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

    function submitDraft() {
        var trimmed = composer.text.trim()

        if (!root.canSubmit || trimmed.length === 0) {
            return
        }

        root.messageSubmitted(trimmed)
        composer.clear()
        composer.forceActiveFocus()
    }

    Component.onCompleted: {
        AgentStore.refresh()
        AgentStore.refreshArchived()
    }

    color: theme.surfaceBg
    border.width: 0
    clip: true

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: root.theme.quietBorder
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
                            font.pixelSize: 8
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
                            font.pixelSize: 8
                            font.letterSpacing: 0.25
                            elide: Text.ElideRight
                        }
                    }
                }

                Button {
                    Layout.preferredWidth: 31
                    Layout.preferredHeight: 31
                    text: "✎"
                    enabled: ChatStore.selectedChatId.length > 0
                    hoverEnabled: true
                    padding: 0
                    ToolTip.visible: hovered
                    ToolTip.text: "Manage Chat"

                    contentItem: Text {
                        text: parent.text
                        color: parent.enabled && parent.hovered
                            ? root.theme.appText
                            : root.theme.mutedText
                        font.pixelSize: 12
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
                    Layout.preferredWidth: 31
                    Layout.preferredHeight: 31
                    text: root.attached ? "↙" : "↗"
                    hoverEnabled: true
                    padding: 0
                    ToolTip.visible: hovered
                    ToolTip.text: root.attached ? "Center Chat Dock" : "Attach Chat Dock"
                    onClicked: root.dockModeToggleRequested()

                    contentItem: Text {
                        text: parent.text
                        color: parent.hovered ? root.theme.appText : root.theme.mutedText
                        font.pixelSize: 13
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

                        required property string modelData

                        Layout.preferredWidth: 58
                        Layout.preferredHeight: 31
                        text: modelData === "chats" ? "▱  Chats" : "♙  Agents"
                        hoverEnabled: true
                        padding: 0
                        onClicked: root.activePanel = root.activePanel === modelData
                            ? "none"
                            : modelData

                        contentItem: Text {
                            text: parent.text
                            color: root.activePanel === modelData || parent.hovered
                                ? root.theme.appText
                                : root.theme.mutedText
                            font.pixelSize: 9
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

                        scale: down ? 0.985 : 1.0

                        Behavior on scale {
                            NumberAnimation { duration: 90; easing.type: Easing.OutCubic }
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

                    TextArea {
                        id: composer

                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        enabled: ChatStore.selectedChatId.length > 0 && !ChatStore.responding
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
                        font.pixelSize: 13
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
                        Layout.preferredHeight: 36
                        color: "#171512"

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
                                    required property string modelData

                                    Layout.preferredWidth: 27
                                    Layout.preferredHeight: 27
                                    text: modelData
                                    hoverEnabled: true
                                    padding: 0

                                    contentItem: Text {
                                        text: parent.text
                                        color: parent.hovered
                                            ? root.theme.appText
                                            : root.theme.mutedText
                                        font.pixelSize: modelData === "</>" ? 9 : 12
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
                                    : ChatStore.responding
                                        ? "Archivist is thinking…"
                                        : ChatStore.selectedChatId.length === 0
                                            ? "Select a Chat"
                                            : "Enter to send  ·  Shift+Enter for newline"
                                color: ChatStore.errorMessage.length > 0
                                    ? root.theme.danger
                                    : root.theme.mutedText
                                font.pixelSize: 8
                                opacity: 0.78
                                elide: Text.ElideRight
                            }

                            Button {
                                Layout.preferredWidth: 68
                                Layout.preferredHeight: 28
                                text: ChatStore.responding ? "Working" : "➤  Send"
                                enabled: root.canSubmit
                                hoverEnabled: true
                                padding: 0
                                onClicked: root.submitDraft()

                                contentItem: Text {
                                    text: parent.text
                                    color: parent.enabled
                                        ? root.theme.appText
                                        : root.theme.mutedText
                                    font.pixelSize: 9
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
                    : Math.min(250, root.width * 0.32)
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
                            font.pixelSize: 9
                            font.weight: Font.Bold
                            font.letterSpacing: 0.7
                        }

                        Text {
                            text: root.activePanel === "chats"
                                ? ChatStore.loadingChats ? "Loading" : String(ChatStore.chats.length)
                                : AgentStore.loading ? "Loading" : String(AgentStore.agents.length)
                            color: root.theme.mutedText
                            font.pixelSize: 8
                            opacity: 0.65
                        }

                        Button {
                            visible: root.activePanel === "agents"
                            Layout.preferredWidth: 24
                            Layout.preferredHeight: 24
                            text: "+"
                            enabled: !AgentStore.mutating
                            hoverEnabled: true
                            padding: 0
                            ToolTip.visible: hovered
                            ToolTip.text: "Create Agent"
                            onClicked: agentEditor.openForCreate()

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
                        spacing: 1
                        clip: true
                        model: ChatStore.chats

                        delegate: Rectangle {
                            id: chatItem

                            required property var modelData

                            width: chatList.width
                            height: 31
                            radius: 0
                            color: chatTap.pressed
                                ? "#292621"
                                : chatHover.hovered
                                    ? root.theme.hoverBg
                                    : String(modelData.id) === ChatStore.selectedChatId
                                        ? "#211f1c"
                                        : "transparent"

                            Rectangle {
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                width: 2
                                visible: String(modelData.id) === ChatStore.selectedChatId
                                color: root.theme.appText
                                opacity: 0.45
                            }

                            Text {
                                anchors.fill: parent
                                anchors.leftMargin: 9
                                anchors.rightMargin: 6
                                text: String(modelData.title || "Untitled Chat")
                                color: root.theme.appText
                                font.pixelSize: 9
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }

                            HoverHandler {
                                id: chatHover
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
                                spacing: 2
                                clip: true
                                model: AgentStore.agents

                                delegate: Rectangle {
                                    id: agentItem

                                    required property var modelData

                                    readonly property bool assigned: String(modelData.id)
                                        === String(ChatStore.selectedChat.agentId || "")

                                    width: agentList.width
                                    height: 48
                                    color: agentTap.pressed
                                        ? "#292621"
                                        : agentHover.hovered
                                            ? root.theme.hoverBg
                                            : assigned
                                                ? "#211f1c"
                                                : "transparent"

                                    Rectangle {
                                        anchors.left: parent.left
                                        anchors.top: parent.top
                                        anchors.bottom: parent.bottom
                                        width: 2
                                        visible: agentItem.assigned
                                        color: root.theme.accentBright
                                        opacity: 0.7
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
                                            anchors.leftMargin: 9
                                            anchors.rightMargin: 5
                                            spacing: 3

                                            Text {
                                                width: parent.width
                                                text: String(agentItem.modelData.name || "Unnamed Agent")
                                                    + (agentItem.assigned ? "  ✓" : "")
                                                color: root.theme.appText
                                                font.pixelSize: 9
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
                                                font.pixelSize: 8
                                                opacity: 0.72
                                                elide: Text.ElideRight
                                            }
                                        }

                                        HoverHandler {
                                            id: agentHover
                                        }

                                        TapHandler {
                                            id: agentTap
                                            enabled: ChatStore.selectedChatId.length > 0
                                                && !ChatStore.responding
                                                && !ChatStore.assigningAgent
                                                && !agentItem.assigned
                                            onTapped: ChatStore.assignAgentToSelectedChat(
                                                String(agentItem.modelData.id)
                                            )
                                        }
                                    }

                                    Button {
                                        id: editAgentButton

                                        anchors.right: parent.right
                                        anchors.rightMargin: 5
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 27
                                        height: 27
                                        text: "✎"
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
                                            font.pixelSize: 11
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                        }

                                        background: Rectangle {
                                            color: parent.hovered
                                                ? root.theme.hoverBg
                                                : "transparent"
                                            radius: 4
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

    AgentEditor {
        id: agentEditor

        theme: root.theme
    }
}

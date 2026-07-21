import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Archivist.Services 1.0

Popup {
    id: editorRoot

    required property var theme
    property var editingChat: ({})
    property bool confirmingArchive: false
    property bool confirmingDelete: false

    readonly property bool archived: editingChat
        && editingChat.archivedAt !== undefined
        && editingChat.archivedAt !== null
    readonly property bool busy: ChatStore.mutating
        || ChatStore.responding
        || ChatStore.assigningAgent
    readonly property var selectedAgent: agentField.currentIndex >= 0
        && agentField.currentIndex < AgentStore.agents.length
            ? AgentStore.agents[agentField.currentIndex]
            : null
    readonly property bool changed: titleField.text.trim() !== String(editingChat.title || "")
        || String(agentField.currentValue || "") !== String(editingChat.agentId || "")
    readonly property bool canSave: !archived
        && !busy
        && titleField.text.trim().length > 0
        && titleField.text.trim().length <= 120
        && String(agentField.currentValue || "").length > 0
        && changed

    parent: Overlay.overlay
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    width: Math.min(620, parent ? parent.width - 48 : 620)
    height: Math.min(510, parent ? parent.height - 48 : 510)
    padding: 0
    modal: true
    focus: true
    closePolicy: busy ? Popup.NoAutoClose : Popup.CloseOnEscape

    Overlay.modal: Rectangle {
        color: "#aa090908"
    }

    background: Rectangle {
        color: editorRoot.theme.surfaceBg
        border.width: 1
        border.color: editorRoot.theme.panelBorder
        radius: editorRoot.theme.radiusPanel
    }

    function agentIndexForId(agentId) {
        var agents = AgentStore.agents || []

        for (var index = 0; index < agents.length; index += 1) {
            if (String(agents[index].id) === String(agentId || "")) {
                return index
            }
        }

        return -1
    }

    function displayDate(value) {
        var date = new Date(String(value || ""))

        if (isNaN(date.getTime())) {
            return "Unknown"
        }

        return Qt.formatDateTime(date, "MMM d, yyyy  h:mm AP")
    }

    function clearConfirmations() {
        confirmingArchive = false
        confirmingDelete = false
    }

    function openForChat(chat) {
        if (!chat || !chat.id) {
            return
        }

        ChatStore.clearError()
        editingChat = chat
        confirmingArchive = false
        confirmingDelete = false
        titleField.text = String(chat.title || "")
        agentField.currentIndex = agentIndexForId(chat.agentId)
        open()
        titleField.forceActiveFocus()
        titleField.selectAll()
    }

    function save() {
        if (!canSave) {
            return
        }

        ChatStore.updateChat(String(editingChat.id), {
            title: titleField.text.trim(),
            agentId: String(agentField.currentValue)
        })
    }

    function archiveChat() {
        if (busy || archived) {
            return
        }

        if (!confirmingArchive) {
            confirmingArchive = true
            confirmingDelete = false
            return
        }

        ChatStore.archiveChat(String(editingChat.id))
    }

    function deleteChat() {
        if (busy || !archived) {
            return
        }

        if (!confirmingDelete) {
            confirmingDelete = true
            confirmingArchive = false
            return
        }

        ChatStore.deleteChat(String(editingChat.id))
    }

    Connections {
        target: ChatStore

        function onChatUpdated(chat) {
            if (
                editorRoot.visible
                && String(chat.id) === String(editorRoot.editingChat.id)
            ) {
                editorRoot.close()
            }
        }

        function onChatArchived(chat) {
            if (
                editorRoot.visible
                && String(chat.id) === String(editorRoot.editingChat.id)
            ) {
                editorRoot.close()
            }
        }

        function onChatRestored(chat) {
            if (
                editorRoot.visible
                && String(chat.id) === String(editorRoot.editingChat.id)
            ) {
                editorRoot.close()
            }
        }

        function onChatDeleted(chatId) {
            if (
                editorRoot.visible
                && String(chatId) === String(editorRoot.editingChat.id)
            ) {
                editorRoot.close()
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 72
            color: "#1a1916"

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: editorRoot.theme.quietBorder
            }

            Column {
                anchors.left: parent.left
                anchors.leftMargin: 18
                anchors.right: closeButton.left
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 3

                Text {
                    text: editorRoot.archived ? "ARCHIVED CHAT" : "CHAT MANAGEMENT"
                    color: editorRoot.archived
                        ? "#d8bd92"
                        : editorRoot.theme.accentBright
                    font.pixelSize: 9
                    font.weight: Font.Bold
                    font.letterSpacing: 0.8
                }

                Text {
                    width: parent.width
                    text: String(editorRoot.editingChat.title || "Chat")
                    color: editorRoot.theme.appText
                    font.family: editorRoot.theme.titleFontFamily
                    font.pixelSize: 21
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
            }

            Button {
                id: closeButton

                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                width: 32
                height: 32
                text: "×"
                enabled: !editorRoot.busy
                hoverEnabled: true
                padding: 0
                onClicked: editorRoot.close()

                contentItem: Text {
                    text: parent.text
                    color: parent.hovered
                        ? editorRoot.theme.appText
                        : editorRoot.theme.mutedText
                    font.pixelSize: 18
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                background: Rectangle {
                    color: parent.hovered ? editorRoot.theme.hoverBg : "transparent"
                    radius: 4
                }
            }
        }

        ScrollView {
            id: editorScroll

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            ColumnLayout {
                width: editorScroll.availableWidth
                spacing: 14

                Item {
                    Layout.preferredHeight: 2
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 18
                    Layout.rightMargin: 18
                    spacing: 6

                    Text {
                        text: "Chat name"
                        color: editorRoot.theme.mutedText
                        font.pixelSize: 9
                        font.weight: Font.DemiBold
                    }

                    TextField {
                        id: titleField

                        Layout.fillWidth: true
                        Layout.preferredHeight: 38
                        enabled: !editorRoot.archived && !editorRoot.busy
                        maximumLength: 120
                        color: editorRoot.theme.appText
                        selectionColor: "#5a554b"
                        selectedTextColor: "#ffffff"
                        font.pixelSize: 12
                        leftPadding: 11
                        rightPadding: 54
                        onTextChanged: editorRoot.clearConfirmations()

                        background: Rectangle {
                            color: titleField.activeFocus
                                ? "#211f1b"
                                : editorRoot.theme.controlSurfaceBg
                            border.width: 1
                            border.color: titleField.activeFocus
                                ? "#5a4d8c"
                                : editorRoot.theme.quietBorder
                            radius: 4
                        }

                        Text {
                            anchors.right: parent.right
                            anchors.rightMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            text: titleField.text.length + "/120"
                            color: editorRoot.theme.mutedText
                            font.pixelSize: 8
                            opacity: 0.7
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 18
                    Layout.rightMargin: 18
                    visible: !editorRoot.archived
                    spacing: 6

                    Text {
                        text: "Assigned Agent"
                        color: editorRoot.theme.mutedText
                        font.pixelSize: 9
                        font.weight: Font.DemiBold
                    }

                    ComboBox {
                        id: agentField

                        Layout.fillWidth: true
                        Layout.preferredHeight: 38
                        enabled: !editorRoot.busy && AgentStore.agents.length > 0
                        model: AgentStore.agents
                        textRole: "name"
                        valueRole: "id"
                        onCurrentIndexChanged: editorRoot.clearConfirmations()

                        contentItem: Text {
                            leftPadding: 11
                            rightPadding: 30
                            text: parent.displayText
                            color: editorRoot.theme.appText
                            font.pixelSize: 11
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }

                        background: Rectangle {
                            color: editorRoot.theme.controlSurfaceBg
                            border.width: 1
                            border.color: parent.activeFocus
                                ? "#5a4d8c"
                                : editorRoot.theme.quietBorder
                            radius: 4
                        }

                        delegate: ItemDelegate {
                            required property var modelData
                            required property int index

                            width: agentField.width
                            height: 34
                            text: String(modelData.name || "Unnamed Agent")
                            highlighted: agentField.highlightedIndex === index

                            contentItem: Text {
                                text: parent.text
                                color: editorRoot.theme.appText
                                font.pixelSize: 10
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }

                            background: Rectangle {
                                color: parent.highlighted
                                    ? editorRoot.theme.hoverBg
                                    : editorRoot.theme.controlSurfaceBg
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: editorRoot.selectedAgent
                            ? String(
                                editorRoot.selectedAgent.description
                                || (editorRoot.selectedAgent.profession
                                    ? editorRoot.selectedAgent.profession.mission
                                    : "")
                                || "This Agent controls future responses."
                            )
                            : "This Chat references an unavailable Agent."
                        color: editorRoot.selectedAgent
                            ? editorRoot.theme.mutedText
                            : editorRoot.theme.danger
                        font.pixelSize: 9
                        lineHeight: 1.35
                        wrapMode: Text.Wrap
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: 18
                    Layout.rightMargin: 18
                    implicitHeight: metadataRow.implicitHeight + 22
                    color: editorRoot.theme.controlSurfaceBg
                    border.width: 1
                    border.color: editorRoot.theme.quietBorder
                    radius: 5

                    RowLayout {
                        id: metadataRow

                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 11
                        spacing: 18

                        Column {
                            Layout.fillWidth: true
                            spacing: 3

                            Text {
                                text: "CREATED"
                                color: editorRoot.theme.mutedText
                                font.pixelSize: 8
                                font.weight: Font.Bold
                                font.letterSpacing: 0.5
                            }

                            Text {
                                text: editorRoot.displayDate(editorRoot.editingChat.createdAt)
                                color: editorRoot.theme.appText
                                font.pixelSize: 9
                            }
                        }

                        Column {
                            Layout.fillWidth: true
                            spacing: 3

                            Text {
                                text: "LAST ACTIVITY"
                                color: editorRoot.theme.mutedText
                                font.pixelSize: 8
                                font.weight: Font.Bold
                                font.letterSpacing: 0.5
                            }

                            Text {
                                text: editorRoot.displayDate(editorRoot.editingChat.updatedAt)
                                color: editorRoot.theme.appText
                                font.pixelSize: 9
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: 18
                    Layout.rightMargin: 18
                    implicitHeight: noticeText.implicitHeight + 18
                    visible: editorRoot.confirmingArchive || editorRoot.confirmingDelete
                    color: editorRoot.confirmingDelete
                        ? "#2b1717"
                        : "#2a2419"
                    border.width: 1
                    border.color: editorRoot.confirmingDelete
                        ? "#6f3636"
                        : "#675638"
                    radius: 5

                    Text {
                        id: noticeText

                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 9
                        text: editorRoot.confirmingDelete
                            ? "Delete permanently? The conversation and every message inside it will be removed. This cannot be undone."
                            : "Archive this Chat? It will leave the active list but can be restored later."
                        color: editorRoot.confirmingDelete
                            ? "#e2b5b5"
                            : "#ddc9a7"
                        font.pixelSize: 9
                        lineHeight: 1.35
                        wrapMode: Text.Wrap
                    }
                }

                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: 18
                    Layout.rightMargin: 18
                    visible: ChatStore.errorMessage.length > 0
                    text: ChatStore.errorMessage
                    color: editorRoot.theme.danger
                    font.pixelSize: 9
                    wrapMode: Text.Wrap
                }

                Item {
                    Layout.preferredHeight: 2
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 62
            color: "#171613"

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                color: editorRoot.theme.quietBorder
            }

            Row {
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8

                Button {
                    width: editorRoot.archived ? 82 : 92
                    height: 34
                    text: editorRoot.archived
                        ? "Restore"
                        : editorRoot.confirmingArchive
                            ? "Confirm"
                            : "Archive"
                    enabled: !editorRoot.busy
                    hoverEnabled: true
                    onClicked: {
                        if (editorRoot.archived) {
                            ChatStore.restoreChat(String(editorRoot.editingChat.id))
                        } else {
                            editorRoot.archiveChat()
                        }
                    }

                    contentItem: Text {
                        text: parent.text
                        color: editorRoot.archived
                            ? editorRoot.theme.success
                            : "#d8bd92"
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        color: parent.hovered
                            ? editorRoot.archived
                                ? "#1f2a1e"
                                : "#30291d"
                            : editorRoot.theme.controlSurfaceBg
                        border.width: 1
                        border.color: editorRoot.archived
                            ? "#3f603c"
                            : "#675638"
                        radius: 4
                    }
                }

                Button {
                    visible: editorRoot.archived
                    width: editorRoot.confirmingDelete ? 118 : 96
                    height: 34
                    text: editorRoot.confirmingDelete ? "Confirm Delete" : "Delete"
                    enabled: !editorRoot.busy
                    hoverEnabled: true
                    onClicked: editorRoot.deleteChat()

                    contentItem: Text {
                        text: parent.text
                        color: "#e2b5b5"
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        color: parent.hovered ? "#321b1b" : "#271717"
                        border.width: 1
                        border.color: "#6f3636"
                        radius: 4
                    }
                }
            }

            Row {
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8

                Button {
                    width: 74
                    height: 34
                    text: editorRoot.archived ? "Close" : "Cancel"
                    enabled: !editorRoot.busy
                    hoverEnabled: true
                    onClicked: editorRoot.close()

                    contentItem: Text {
                        text: parent.text
                        color: editorRoot.theme.appText
                        font.pixelSize: 10
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        color: parent.hovered
                            ? editorRoot.theme.hoverBg
                            : editorRoot.theme.controlSurfaceBg
                        border.width: 1
                        border.color: editorRoot.theme.quietBorder
                        radius: 4
                    }
                }

                Button {
                    visible: !editorRoot.archived
                    width: 92
                    height: 34
                    text: ChatStore.mutating ? "Saving…" : "Save Chat"
                    enabled: editorRoot.canSave
                    hoverEnabled: true
                    onClicked: editorRoot.save()

                    contentItem: Text {
                        text: parent.text
                        color: parent.enabled
                            ? editorRoot.theme.appText
                            : editorRoot.theme.mutedText
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        color: parent.enabled
                            ? parent.hovered
                                ? "#554a7b"
                                : "#463d68"
                            : "#24211d"
                        border.width: 1
                        border.color: parent.enabled
                            ? "#6a5c99"
                            : editorRoot.theme.quietBorder
                        radius: 4
                    }
                }
            }
        }
    }
}

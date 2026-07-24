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
    property bool resizingPanel: false
    property real panelWidth: theme.chatDockPanelDefaultWidth
    readonly property var scopedChats: filteredChats()
    readonly property var orderedAgents: collectionOrderedAgents()

    readonly property int attachmentCount: ChatStore.attachments.length
    readonly property real panelMaximumWidth: Math.max(
        180,
        Math.min(
            theme.chatDockPanelMaxWidth,
            width - theme.chatDockComposerMinWidth - theme.resizeHandleThickness
        )
    )
    readonly property real panelMinimumWidth: Math.min(
        theme.chatDockPanelMinWidth,
        panelMaximumWidth
    )
    readonly property real clampedPanelWidth: Math.min(
        panelMaximumWidth,
        Math.max(panelMinimumWidth, panelWidth)
    )
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

    function filteredChats() {
        var scope = CollectionStore.scope
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

    function collectionOrderedAgents() {
        var scope = CollectionStore.scope
        var agents = AgentStore.agents || []

        if (CollectionStore.selectedCollectionId.length === 0) {
            return agents
        }

        var rosterIds = scope.agentIds || []
        var ordered = []
        var used = ({})

        for (var rosterIndex = 0; rosterIndex < rosterIds.length; rosterIndex += 1) {
            for (var agentIndex = 0; agentIndex < agents.length; agentIndex += 1) {
                if (String(agents[agentIndex].id) === String(rosterIds[rosterIndex])) {
                    ordered.push(agents[agentIndex])
                    used[String(agents[agentIndex].id)] = true
                    break
                }
            }
        }

        for (var index = 0; index < agents.length; index += 1) {
            if (used[String(agents[index].id)] !== true) {
                ordered.push(agents[index])
            }
        }

        return ordered
    }

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

    function resetPanelWidth() {
        panelWidth = theme.chatDockPanelDefaultWidth
    }

    function resizePanelTo(pointerX) {
        panelWidth = Math.min(
            panelMaximumWidth,
            Math.max(panelMinimumWidth, width - pointerX)
        )
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
                        font.pixelSize: root.theme.typeSize(14)
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }

                    Row {
                        spacing: 8

                        Text {
                            text: "▣  LIBRARY  " + root.selectedLibraryName
                            color: root.theme.mutedText
                            font.pixelSize: root.theme.typeSize(9)
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
                            font.pixelSize: root.theme.typeSize(9)
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
                            font.pixelSize: root.theme.typeSize(9)
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
                        enabled: !manageChatButton.down

                        NumberAnimation {
                            duration: root.headerHoverIndex >= 0
                                ? root.theme.motionHover
                                : root.theme.motionHoverExit
                            easing.type: Easing.OutCubic
                        }
                    }

                    contentItem: Text {
                        text: parent.text
                        color: parent.enabled && parent.hovered
                            ? root.theme.appText
                            : root.theme.mutedText
                        font.pixelSize: root.theme.typeSize(14)
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
                        enabled: !dockModeButton.down

                        NumberAnimation {
                            duration: root.headerHoverIndex >= 0
                                ? root.theme.motionHover
                                : root.theme.motionHoverExit
                            easing.type: Easing.OutCubic
                        }
                    }

                    contentItem: Text {
                        text: parent.text
                        color: parent.hovered ? root.theme.appText : root.theme.mutedText
                        font.pixelSize: root.theme.typeSize(15)
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
                            font.pixelSize: root.theme.typeSize(10)
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
                            enabled: !dockTabButton.down

                            NumberAnimation {
                                duration: root.headerHoverIndex >= 0
                                    ? root.theme.motionHover
                                    : root.theme.motionHoverExit
                                easing.type: Easing.OutCubic
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
                                font.pixelSize: root.theme.typeSize(8)
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
                                font.pixelSize: root.theme.typeSize(9)
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
                                        font.pixelSize: root.theme.typeSize(9)
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
                                            font.pixelSize: root.theme.typeSize(11)
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
                                font.pixelSize: root.theme.typeSize(9)
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
                        selectionColor: root.theme.messageSelectionBg
                        selectedTextColor: root.theme.messageSelectionText
                        font.family: root.theme.bodyFontFamily
                        font.pixelSize: root.theme.typeSize(14)
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
                                    id: composerToolButton

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
                                        enabled: !composerToolButton.down

                                        NumberAnimation {
                                            duration: root.composerHoverIndex >= 0
                                                ? root.theme.motionHover
                                                : root.theme.motionHoverExit
                                            easing.type: Easing.OutCubic
                                        }
                                    }

                                    contentItem: Text {
                                        text: parent.text
                                        color: parent.hovered
                                            ? root.theme.appText
                                            : root.theme.mutedText
                                        font.pixelSize: root.theme.typeSize(
                                            modelData === "</>" ? 10 : 14
                                        )
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
                                font.pixelSize: root.theme.typeSize(9)
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
                                    enabled: !sendButton.down

                                    NumberAnimation {
                                        duration: root.composerHoverIndex >= 0
                                            ? root.theme.motionHover
                                            : root.theme.motionHoverExit
                                        easing.type: Easing.OutCubic
                                    }
                                }

                                contentItem: Text {
                                    text: parent.text
                                    color: parent.enabled
                                        ? root.theme.appText
                                        : root.theme.mutedText
                                    font.pixelSize: root.theme.typeSize(10)
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

            Item {
                id: panelResizeHandle

                Layout.preferredWidth: root.activePanel === "none"
                    ? 0
                    : root.theme.resizeHandleThickness
                Layout.fillHeight: true
                visible: root.activePanel !== "none"
                z: 8

                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 1
                    height: parent.height
                    color: root.theme.quietBorder
                    opacity: 0.82
                }

                Rectangle {
                    anchors.centerIn: parent
                    width: 3
                    height: panelResizeArea.containsMouse || panelResizeArea.pressed
                        ? 32
                        : 18
                    color: panelResizeArea.containsMouse || panelResizeArea.pressed
                        ? root.theme.accent
                        : root.theme.panelBorder
                    radius: 2
                    opacity: panelResizeArea.containsMouse || panelResizeArea.pressed
                        ? 0.95
                        : 0.68

                    Behavior on color {
                        ColorAnimation { duration: root.theme.motionFast }
                    }

                    Behavior on height {
                        NumberAnimation {
                            duration: root.theme.motionFast
                            easing.type: Easing.OutCubic
                        }
                    }
                }

                MouseArea {
                    id: panelResizeArea

                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.SplitHCursor
                    onPressed: root.resizingPanel = true
                    onReleased: root.resizingPanel = false
                    onCanceled: root.resizingPanel = false
                    onPositionChanged: function(mouse) {
                        if (!pressed) {
                            return
                        }

                        const point = mapToItem(root, mouse.x, mouse.y)
                        root.resizePanelTo(point.x)
                    }
                    onDoubleClicked: root.resetPanelWidth()
                }
            }

            Rectangle {
                id: workbenchPanel

                Layout.preferredWidth: root.activePanel === "none"
                    ? 0
                    : root.clampedPanelWidth
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
                    anchors.rightMargin: 10
                    anchors.topMargin: 8
                    anchors.bottomMargin: 8
                    spacing: 7

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 34
                        color: root.theme.controlSurfaceBg
                        border.width: 1
                        border.color: root.theme.quietBorder
                        radius: root.theme.radiusSmall

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 21
                            anchors.rightMargin: 5
                            spacing: 8

                            Text {
                                Layout.fillWidth: true
                                text: root.activePanel === "chats" ? "CHATS" : "AGENTS"
                                color: root.theme.appText
                                font.pixelSize: root.theme.typeSize(9)
                                font.weight: Font.Bold
                                font.letterSpacing: 0.9
                            }

                            Rectangle {
                                Layout.preferredWidth: panelCountLabel.implicitWidth + 12
                                Layout.preferredHeight: 20
                                color: root.theme.activeBg
                                border.width: 1
                                border.color: root.theme.quietBorder
                                radius: 10

                                Text {
                                    id: panelCountLabel

                                    anchors.centerIn: parent
                                    text: root.activePanel === "chats"
                                        ? ChatStore.loadingChats
                                            ? "…"
                                            : String(root.scopedChats.length)
                                        : AgentStore.loading
                                            ? "…"
                                            : String(root.orderedAgents.length)
                                    color: root.theme.mutedText
                                    font.pixelSize: root.theme.typeSize(8)
                                    font.weight: Font.DemiBold
                                }
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
                                        ChatStore.createChat(
                                            LibraryStore.selectedLibraryId,
                                            String(
                                                CollectionStore.scope.defaultAgentId
                                                    || ""
                                            )
                                        )
                                    } else {
                                        agentEditor.openForCreate()
                                    }
                                }

                                contentItem: Text {
                                    text: parent.text
                                    color: parent.enabled && parent.hovered
                                        ? root.theme.appText
                                        : root.theme.mutedText
                                    font.pixelSize: root.theme.typeSize(14)
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
                    }

                    ListView {
                        id: chatList

                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: root.activePanel === "chats"
                        spacing: 1
                        topMargin: 4
                        bottomMargin: 4
                        clip: true
                        model: root.scopedChats

                        delegate: Item {
                            id: chatDelegate

                            required property int index
                            required property var modelData

                            width: chatList.width
                            height: 58
                            z: chatHover.hovered
                                ? 3
                                : chatDelegate.neighborHovered
                                    ? 2
                                    : 1

                            readonly property bool selected: String(modelData.id)
                                === ChatStore.selectedChatId
                            readonly property bool neighborHovered: root.hoveredChatIndex >= 0
                                && Math.abs(root.hoveredChatIndex - index) === 1

                            Rectangle {
                                id: chatItem

                                readonly property int index: chatDelegate.index
                                readonly property var modelData: chatDelegate.modelData
                                readonly property bool selected: chatDelegate.selected
                                readonly property bool neighborHovered: chatDelegate.neighborHovered

                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.leftMargin: 11
                                anchors.rightMargin: 11
                                anchors.verticalCenter: parent.verticalCenter
                                height: 48
                                radius: root.theme.radiusMedium
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

                                Behavior on scale {
                                    enabled: !chatTap.pressed

                                    NumberAnimation {
                                        duration: chatHover.hovered || chatItem.neighborHovered
                                            ? root.theme.motionHover
                                            : root.theme.motionHoverExit
                                        easing.type: Easing.OutCubic
                                    }
                                }

                                Behavior on color {
                                    enabled: !chatTap.pressed
                                    ColorAnimation { duration: root.theme.motionFast }
                                }

                                Column {
                                    anchors.left: parent.left
                                    anchors.right: chatEditButton.left
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.leftMargin: 14
                                    anchors.rightMargin: 8
                                    spacing: 3

                                    Text {
                                        width: parent.width
                                        text: String(chatItem.modelData.title || "Untitled Chat")
                                        color: root.theme.appText
                                        font.pixelSize: root.theme.typeSize(10)
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        width: parent.width
                                        text: String(chatItem.modelData.libraryName || "Standalone")
                                        color: root.theme.mutedText
                                        font.pixelSize: root.theme.typeSize(8)
                                        opacity: 0.72
                                        elide: Text.ElideRight
                                    }
                                }

                                Button {
                                    id: chatEditButton

                                    anchors.right: parent.right
                                    anchors.rightMargin: 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 58
                                    height: 28
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
                                        font.pixelSize: root.theme.typeSize(8)
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
                                        } else if (root.hoveredChatIndex === chatItem.index) {
                                            root.hoveredChatIndex = -1
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
                            font.pixelSize: root.theme.typeSize(8)
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
                                font.pixelSize: root.theme.typeSize(9)
                                elide: Text.ElideRight
                            }

                            Text {
                                anchors.right: parent.right
                                anchors.rightMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                text: "✎"
                                color: root.theme.mutedText
                                font.pixelSize: root.theme.typeSize(10)
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
                            font.pixelSize: root.theme.typeSize(8)
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
                                font.pixelSize: root.theme.typeSize(8)
                                wrapMode: Text.Wrap
                            }

                            Text {
                                Layout.fillWidth: true
                                visible: ChatStore.selectedChatId.length === 0
                                text: "Select a Chat before assigning an Agent."
                                color: root.theme.mutedText
                                font.pixelSize: root.theme.typeSize(8)
                                wrapMode: Text.Wrap
                            }

                            Text {
                                Layout.fillWidth: true
                                visible: ChatStore.assigningAgent
                                text: "Updating Agent assignment…"
                                color: root.theme.accentBright
                                font.pixelSize: root.theme.typeSize(8)
                            }

                            ListView {
                                id: agentList

                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                visible: root.orderedAgents.length > 0
                                spacing: 1
                                topMargin: 4
                                bottomMargin: 4
                                clip: true
                                model: root.orderedAgents

                                delegate: Item {
                                    id: agentDelegate

                                    required property int index
                                    required property var modelData

                                    width: agentList.width
                                    height: 66
                                    z: agentHover.hovered
                                        ? 3
                                        : agentDelegate.neighborHovered
                                            ? 2
                                            : 1

                                    readonly property bool assigned: String(modelData.id)
                                        === String(ChatStore.selectedChat.agentId || "")
                                    readonly property bool neighborHovered: root.hoveredAgentIndex >= 0
                                        && Math.abs(root.hoveredAgentIndex - index) === 1

                                    Rectangle {
                                        id: agentItem

                                        readonly property int index: agentDelegate.index
                                        readonly property var modelData: agentDelegate.modelData
                                        readonly property bool assigned: agentDelegate.assigned
                                        readonly property bool neighborHovered: agentDelegate.neighborHovered

                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.leftMargin: 11
                                        anchors.rightMargin: 11
                                        anchors.verticalCenter: parent.verticalCenter
                                        height: 56
                                        radius: root.theme.radiusMedium
                                        color: agentTap.pressed
                                            ? "#292621"
                                            : agentHover.hovered
                                                ? root.theme.hoverBg
                                                : agentItem.assigned
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

                                        Behavior on scale {
                                            enabled: !agentTap.pressed

                                            NumberAnimation {
                                                duration: agentHover.hovered || agentItem.neighborHovered
                                                    ? root.theme.motionHover
                                                    : root.theme.motionHoverExit
                                                easing.type: Easing.OutCubic
                                            }
                                        }

                                        Behavior on color {
                                            enabled: !agentTap.pressed
                                            ColorAnimation { duration: root.theme.motionFast }
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
                                                anchors.leftMargin: 14
                                                anchors.rightMargin: 8
                                                spacing: 3

                                                Text {
                                                    width: parent.width
                                                    text: String(agentItem.modelData.name || "Unnamed Agent")
                                                        + (agentItem.assigned ? "  ✓" : "")
                                                    color: root.theme.appText
                                                    font.pixelSize: root.theme.typeSize(10)
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
                                                    font.pixelSize: root.theme.typeSize(9)
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
                                            anchors.rightMargin: 8
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: 46
                                            height: 28
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
                                                font.pixelSize: root.theme.typeSize(8)
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
                                                } else if (root.hoveredAgentIndex === agentItem.index) {
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
                                    font.pixelSize: root.theme.typeSize(8)
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
                                        font.pixelSize: root.theme.typeSize(9)
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        anchors.right: parent.right
                                        anchors.rightMargin: 8
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: "✎"
                                        color: root.theme.mutedText
                                        font.pixelSize: root.theme.typeSize(10)
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
                                    font.pixelSize: root.theme.typeSize(8)
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                visible: !AgentStore.loading && root.orderedAgents.length === 0
                                text: "No active Agents found."
                                color: root.theme.mutedText
                                font.pixelSize: root.theme.typeSize(8)
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

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Archivist.Services 1.0
import "../../Files/FileIdentity.js" as FileIdentity
import "AgentEditor"
import "ChatAgentPicker"
import "ChatEditor"

Rectangle {
    id: root

    required property var theme
    property bool attached: true
    property string activePanel: "none"
    property bool archivedAgentsOpen: false
    property bool archivedChatsOpen: false
    property int collectionScopeRevision: 0
    property bool attachNextCreatedAgent: false
    property string pendingCreatedChatId: ""
    property string attachmentNotice: ""
    property string agentSwitchNotice: ""
    property real agentSwitchPulse: 0
    property int headerHoverIndex: -1
    property int composerHoverIndex: -1
    property bool resizingPanel: false
    property real panelWidth: theme.chatDockPanelDefaultWidth
    readonly property var attachedAgents: chatAttachedAgents()
    readonly property var scopedChats: filteredChats()

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
        var revision = collectionScopeRevision
        var chats = ChatStore.chats || []

        if (
            String(
                CollectionStore.selectedCollectionId || ""
            ).length === 0
        ) {
            return []
        }

        var filtered = []

        for (
            var index = 0;
            index < chats.length;
            index += 1
        ) {
            if (
                CollectionStore.includesChat(
                    String(chats[index].id || "")
                )
            ) {
                filtered.push(chats[index])
            }
        }

        return filtered
    }

    function chatAgentCount(chat) {
        return chat && chat.agentIds
            ? chat.agentIds.length
            : 0
    }

    function selectChat(chat) {
        if (
            !chat
            || !chat.id
            || ChatStore.responding
        ) {
            return
        }

        var libraryId = String(chat.libraryId || "")

        if (
            libraryId.length > 0
            && libraryId
                !== String(LibraryStore.selectedLibraryId)
        ) {
            LibraryStore.selectLibrary(libraryId)
        }

        LibraryStore.clearFilePreview()
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
            String(
                CollectionStore.scope.defaultAgentId || ""
            )
        )
    }

    function chatAttachedAgents() {
        var agents = AgentStore.agents || []
        var rosterIds = ChatStore.selectedChat.agentIds || []
        var attached = []

        for (var rosterIndex = 0; rosterIndex < rosterIds.length; rosterIndex += 1) {
            for (var agentIndex = 0; agentIndex < agents.length; agentIndex += 1) {
                if (String(agents[agentIndex].id) === String(rosterIds[rosterIndex])) {
                    attached.push(agents[agentIndex])
                    break
                }
            }
        }

        return attached
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

    function attachedAgentIndex(agentId) {
        var agents = attachedAgents || []

        for (var index = 0; index < agents.length; index += 1) {
            if (String(agents[index].id) === String(agentId || "")) {
                return index
            }
        }

        return -1
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

    function openAgentPicker() {
        if (ChatStore.selectedChatId.length > 0) {
            agentPicker.openPicker()
        }
    }

    function createAttachedAgent() {
        if (ChatStore.selectedChatId.length === 0) {
            return
        }

        attachNextCreatedAgent = true
        pendingCreatedChatId = String(ChatStore.selectedChatId)
        agentEditor.openForCreate()
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
        ChatStore.refresh()
        ChatStore.refreshArchived()
    }

    Connections {
        target: CollectionStore

        function onSelectedCollectionIdChanged() {
            root.collectionScopeRevision += 1
        }

        function onWorkspaceScopeChanged() {
            root.collectionScopeRevision += 1
            Qt.callLater(ChatStore.refresh)
        }
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
            root.activePanel = "chats"
            composer.forceActiveFocus()

            if (
                CollectionStore.selectedCollectionId.length > 0
            ) {
                CollectionStore.refresh()
            }
        }

        function onChatRestored(chat) {
            if (
                CollectionStore.selectedCollectionId.length > 0
            ) {
                CollectionStore.refresh()
            }
        }

        function onAgentAssigned(agentId) {
            root.agentSwitchNotice = "Now speaking with "
                + root.selectedAgentName
            agentSwitchNoticeTimer.restart()
            agentSwitchAnimation.restart()
            Qt.callLater(function() {
                var index = root.attachedAgentIndex(agentId)
                if (index >= 0 && agentList.count > 0) {
                    agentList.positionViewAtIndex(
                        index,
                        ListView.Contain
                    )
                }
            })
        }
    }

    Timer {
        id: attachmentNoticeTimer
        interval: 3000
        repeat: false
        onTriggered: root.attachmentNotice = ""
    }

    Timer {
        id: agentSwitchNoticeTimer
        interval: 2600
        repeat: false
        onTriggered: root.agentSwitchNotice = ""
    }

    SequentialAnimation {
        id: agentSwitchAnimation

        PropertyAction {
            target: root
            property: "agentSwitchPulse"
            value: 0
        }

        NumberAnimation {
            target: root
            property: "agentSwitchPulse"
            to: 1
            duration: 170
            easing.type: Easing.OutCubic
        }

        NumberAnimation {
            target: root
            property: "agentSwitchPulse"
            to: 0
            duration: 720
            easing.type: Easing.OutCubic
        }
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
            Layout.preferredHeight: 36
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
                anchors.leftMargin: 9
                anchors.rightMargin: 6
                spacing: 6

                Text {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 90
                    text: root.selectedChatTitle
                    color: root.theme.appText
                    font.family: root.theme.chatFontFamily
                    font.pixelSize: root.theme.textWorkbenchTitleSize
                    font.weight: root.theme.textWeightEmphasis
                    font.letterSpacing: root.theme.textTrackingNormal
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                }

                ListView {
                    id: attachmentList

                    visible: root.attachmentCount > 0
                    Layout.preferredWidth: visible
                        ? Math.min(260, contentWidth)
                        : 0
                    Layout.maximumWidth: 260
                    Layout.preferredHeight: 28
                    orientation: ListView.Horizontal
                    spacing: 4
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: ChatStore.attachments

                    delegate: Item {
                        id: sourceItem

                        required property int index
                        required property var modelData

                        readonly property string sourceName: String(
                            modelData.fileName
                                || modelData.relativePath
                                || "Library file"
                        ).split("/").pop()
                        readonly property string sourceRelativePath:
                            String(
                                modelData.relativePath
                                    || modelData.fileName
                                    || sourceName
                            )
                        readonly property string sourceLibrary:
                            String(modelData.libraryName || "Library")
                        readonly property string sourceGlyph:
                            FileIdentity.glyphFor({
                                fileName: sourceName,
                                extension:
                                    modelData.extension || ""
                            })
                        readonly property string sourceType:
                            FileIdentity.displayLabelFor({
                                fileName: sourceName,
                                extension:
                                    modelData.extension || ""
                            })
                        readonly property bool includedInLastResponse:
                            root.sourceWasIncluded(modelData.id)

                        width: 30
                        height: 28

                        Rectangle {
                            id: sourceTile

                            anchors.centerIn: parent
                            width: 26
                            height: 26
                            radius: 6
                            color:
                                sourceItem.includedInLastResponse
                                    ? root.theme.activeBg
                                    : sourceHover.hovered
                                        ? root.theme.hoverBg
                                        : root.theme.surfaceBg
                            border.width: 1
                            border.color:
                                sourceItem.includedInLastResponse
                                    ? root.theme.accent
                                    : sourceHover.hovered
                                        ? root.theme.panelBorder
                                        : root.theme.quietBorder

                            Behavior on color {
                                ColorAnimation {
                                    duration:
                                        root.theme.motionFast
                                }
                            }

                            Text {
                                anchors.centerIn: parent
                                text: sourceItem.sourceGlyph
                                color:
                                    sourceItem.includedInLastResponse
                                        ? root.theme.accentBright
                                        : sourceHover.hovered
                                            ? root.theme.appText
                                            : root.theme.mutedText
                                font.pixelSize:
                                    root.theme.typeSize(10)
                                font.weight: Font.DemiBold
                            }
                        }

                        HoverHandler {
                            id: sourceHover
                        }

                        Button {
                            id: removeSourceButton

                            anchors.right: parent.right
                            anchors.top: parent.top
                            width: 14
                            height: 14
                            visible: sourceHover.hovered
                            enabled: !ChatStore.responding
                                && !ChatStore.mutating
                                && !ChatStore.mutatingAttachment
                            text: "×"
                            hoverEnabled: true
                            padding: 0
                            z: 3
                            ToolTip.visible: hovered
                            ToolTip.text: "Remove source"
                            onClicked:
                                ChatStore.removeAttachment(
                                    String(
                                        sourceItem.modelData.id
                                    )
                                )

                            contentItem: Text {
                                text: parent.text
                                color:
                                    parent.enabled
                                    && parent.hovered
                                        ? root.theme.appText
                                        : root.theme.mutedText
                                font.pixelSize:
                                    root.theme.typeSize(9)
                                font.weight: Font.Bold
                                horizontalAlignment:
                                    Text.AlignHCenter
                                verticalAlignment:
                                    Text.AlignVCenter
                                opacity:
                                    parent.enabled ? 1 : 0.45
                            }

                            background: Rectangle {
                                radius: 7
                                color: parent.hovered
                                    ? root.theme.hoverBg
                                    : root.theme.surfaceBg
                                border.width: 1
                                border.color:
                                    root.theme.quietBorder
                            }
                        }

                        ToolTip {
                            id: sourceInfo

                            visible: sourceHover.hovered
                                && !removeSourceButton.hovered
                            delay: 700
                            timeout: 7000
                            y: sourceItem.height + 7
                            padding: 0

                            enter: Transition {
                                ParallelAnimation {
                                    NumberAnimation {
                                        property: "opacity"
                                        from: 0
                                        to: 1
                                        duration:
                                            root.theme.motionFast
                                        easing.type:
                                            Easing.OutCubic
                                    }

                                    NumberAnimation {
                                        property: "scale"
                                        from: 0.96
                                        to: 1
                                        duration:
                                            root.theme.motionHover
                                        easing.type:
                                            Easing.OutBack
                                    }
                                }
                            }

                            exit: Transition {
                                NumberAnimation {
                                    property: "opacity"
                                    from: 1
                                    to: 0
                                    duration:
                                        root.theme.motionFast
                                    easing.type:
                                        Easing.InCubic
                                }
                            }

                            contentItem: Item {
                                implicitWidth: 310
                                implicitHeight:
                                    sourceInfoColumn.implicitHeight
                                        + 26

                                Column {
                                    id: sourceInfoColumn

                                    anchors.left:
                                        parent.left
                                    anchors.right:
                                        parent.right
                                    anchors.top:
                                        parent.top
                                    anchors.margins: 13
                                    spacing: 9

                                    Row {
                                        spacing: 9

                                        Rectangle {
                                            width: 30
                                            height: 30
                                            radius: 9
                                            color:
                                                root.theme.accentSoft
                                            border.width: 1
                                            border.color:
                                                root.theme.quietBorder

                                            Text {
                                                anchors.centerIn:
                                                    parent
                                                text:
                                                    sourceItem.sourceGlyph
                                                color:
                                                    root.theme.accentBright
                                                font.pixelSize:
                                                    root.theme.typeSize(
                                                        10
                                                    )
                                                font.weight:
                                                    Font.DemiBold
                                            }
                                        }

                                        Column {
                                            width: 245
                                            spacing: 3

                                            Text {
                                                width:
                                                    parent.width
                                                text:
                                                    sourceItem.sourceName
                                                color:
                                                    root.theme.appText
                                                font.pixelSize:
                                                    root.theme.typeSize(
                                                        12
                                                    )
                                                font.weight:
                                                    Font.DemiBold
                                                elide:
                                                    Text.ElideRight
                                            }

                                            Text {
                                                width:
                                                    parent.width
                                                text:
                                                    sourceItem.sourceType
                                                color:
                                                    root.theme.accentBright
                                                font.pixelSize:
                                                    root.theme.typeSize(
                                                        8
                                                    )
                                                font.weight:
                                                    Font.Bold
                                                font.letterSpacing:
                                                    0.4
                                                elide:
                                                    Text.ElideRight
                                            }
                                        }
                                    }

                                    Rectangle {
                                        width: parent.width
                                        implicitHeight:
                                            sourcePathText.implicitHeight
                                                + 16
                                        radius: 7
                                        color:
                                            root.theme.controlSurfaceBg
                                        border.width: 1
                                        border.color:
                                            root.theme.quietBorder

                                        Text {
                                            id: sourcePathText

                                            anchors.left:
                                                parent.left
                                            anchors.right:
                                                parent.right
                                            anchors.top:
                                                parent.top
                                            anchors.margins: 8
                                            text:
                                                sourceItem.sourceRelativePath
                                            color:
                                                root.theme.mutedText
                                            font.pixelSize:
                                                root.theme.typeSize(
                                                    9
                                                )
                                            wrapMode:
                                                Text.WrapAnywhere
                                            maximumLineCount: 3
                                            elide:
                                                Text.ElideRight
                                        }
                                    }

                                    Row {
                                        spacing: 7

                                        Text {
                                            text:
                                                sourceItem.sourceLibrary
                                            color:
                                                root.theme.mutedText
                                            font.pixelSize:
                                                root.theme.typeSize(
                                                    8
                                                )
                                            font.weight:
                                                Font.DemiBold
                                        }

                                        Text {
                                            text: "·"
                                            color:
                                                root.theme.quietBorder
                                            font.pixelSize:
                                                root.theme.typeSize(
                                                    8
                                                )
                                        }

                                        Text {
                                            text:
                                                sourceItem.includedInLastResponse
                                                    ? "Used in last response"
                                                    : "Attached source"
                                            color:
                                                sourceItem.includedInLastResponse
                                                    ? root.theme.accentBright
                                                    : root.theme.mutedText
                                            font.pixelSize:
                                                root.theme.typeSize(
                                                    8
                                                )
                                            font.weight:
                                                Font.DemiBold
                                        }
                                    }
                                }
                            }

                            background: Rectangle {
                                color: root.theme.surfaceBg
                                border.width: 1
                                border.color:
                                    root.theme.panelBorder
                                radius: 12
                            }
                        }
                    }
                }

                Text {
                    visible: ChatStore.loadingAttachments
                    text: "…"
                    color: root.theme.mutedText
                    font.pixelSize: root.theme.typeSize(10)
                }

                Button {
                    id: dockModeButton

                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    text: root.attached ? "↙" : "↗"
                    hoverEnabled: true
                    padding: 0
                    ToolTip.visible: hovered
                    ToolTip.text: root.attached
                        ? "Center Chat Dock"
                        : "Attach Chat Dock"
                    onClicked: root.dockModeToggleRequested()
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
                        enabled: !dockModeButton.down

                        NumberAnimation {
                            duration:
                                root.headerHoverIndex >= 0
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
                        font.pixelSize:
                            root.theme.typeSize(14)
                        horizontalAlignment:
                            Text.AlignHCenter
                        verticalAlignment:
                            Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: 5
                        color: parent.hovered
                            ? root.theme.hoverBg
                            : "transparent"
                    }
                }

                Button {
                    id: chatsTabButton

                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    text: "▱"
                    hoverEnabled: true
                    padding: 0
                    ToolTip.visible: hovered
                    ToolTip.text: "Chats"
                    onClicked:
                        root.activePanel =
                            root.activePanel === "chats"
                                ? "none"
                                : "chats"
                    onHoveredChanged: root.updateHoverIndex(
                        "header",
                        1,
                        hovered
                    )

                    contentItem: Text {
                        text: parent.text
                        color:
                            root.activePanel === "chats"
                            || parent.hovered
                                ? root.theme.appText
                                : root.theme.mutedText
                        font.pixelSize:
                            root.theme.textControlSize
                        font.weight:
                            root.theme.textWeightEmphasis
                        horizontalAlignment:
                            Text.AlignHCenter
                        verticalAlignment:
                            Text.AlignVCenter
                    }

                    background: Item {
                        Rectangle {
                            anchors.fill: parent
                            radius: 3
                            color: chatsTabButton.hovered
                                ? root.theme.hoverBg
                                : "transparent"
                        }

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 1
                            visible:
                                root.activePanel === "chats"
                            color: root.theme.appText
                            opacity: 0.62
                        }
                    }

                    scale: root.magnifierScale(
                        1,
                        root.headerHoverIndex,
                        down
                    )

                    Behavior on scale {
                        enabled: !chatsTabButton.down

                        NumberAnimation {
                            duration:
                                root.headerHoverIndex >= 0
                                    ? root.theme.motionHover
                                    : root.theme.motionHoverExit
                            easing.type: Easing.OutCubic
                        }
                    }
                }

                Button {
                    id: agentsTabButton

                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    text: "♙"
                    hoverEnabled: true
                    padding: 0
                    ToolTip.visible: hovered
                    ToolTip.text: "Agents"
                    onClicked:
                        root.activePanel =
                            root.activePanel === "agents"
                                ? "none"
                                : "agents"
                    onHoveredChanged: root.updateHoverIndex(
                        "header",
                        2,
                        hovered
                    )

                    contentItem: Text {
                        text: parent.text
                        color:
                            root.activePanel === "agents"
                            || parent.hovered
                                ? root.theme.appText
                                : root.theme.mutedText
                        font.pixelSize:
                            root.theme.typeSize(13)
                        font.weight: Font.DemiBold
                        horizontalAlignment:
                            Text.AlignHCenter
                        verticalAlignment:
                            Text.AlignVCenter
                    }

                    background: Item {
                        Rectangle {
                            anchors.fill: parent
                            radius: 3
                            color: agentsTabButton.hovered
                                ? root.theme.hoverBg
                                : "transparent"
                        }

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 1
                            visible:
                                root.activePanel === "agents"
                            color: root.theme.appText
                            opacity: 0.62
                        }
                    }

                    scale: root.magnifierScale(
                        2,
                        root.headerHoverIndex,
                        down
                    )

                    Behavior on scale {
                        enabled: !agentsTabButton.down

                        NumberAnimation {
                            duration:
                                root.headerHoverIndex >= 0
                                    ? root.theme.motionHover
                                    : root.theme.motionHoverExit
                            easing.type: Easing.OutCubic
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

                Rectangle {
                    anchors.fill: parent
                    color: "transparent"
                    border.width: root.agentSwitchPulse > 0.01 ? 2 : 0
                    border.color: root.theme.accent
                    opacity: root.agentSwitchPulse * 0.72
                    z: 4
                }

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

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
                        font.family: root.theme.chatFontFamily
                        font.pixelSize: root.theme.textComposerSize
                        font.weight: root.theme.textWeightRegular
                        font.letterSpacing: root.theme.textTrackingNormal
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
                                    : root.agentSwitchNotice.length > 0
                                        ? root.agentSwitchNotice
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
                                    : root.agentSwitchNotice.length > 0
                                        ? root.theme.accentBright
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
                    anchors.leftMargin: 4
                    anchors.rightMargin: 4
                    anchors.topMargin: 3
                    anchors.bottomMargin: 3
                    spacing: 0

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 28
                        color: "transparent"

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
                            anchors.leftMargin: 7
                            anchors.rightMargin: 3
                            spacing: 7

                            Text {
                                text:
                                    root.activePanel === "chats"
                                        ? "CHATS"
                                        : "AGENTS"
                                color: root.theme.appText
                                font.pixelSize:
                                    root.theme.textSidebarLabelSize
                                font.weight:
                                    root.theme.textWeightStrong
                                font.letterSpacing:
                                    root.theme.textTrackingLabel
                            }

                            Text {
                                Layout.fillWidth: true
                                text:
                                    root.activePanel === "chats"
                                        ? ChatStore.loadingChats
                                            ? "…"
                                            : String(
                                                root.scopedChats.length
                                            )
                                        : AgentStore.loading
                                            ? "…"
                                            : String(
                                                root.attachedAgents.length
                                            )
                                color: root.theme.mutedText
                                font.pixelSize:
                                    root.theme.textMetadataSize
                                opacity: 0.76
                            }

                            Button {
                                Layout.preferredWidth: 22
                                Layout.preferredHeight: 22
                                text: "+"
                                enabled:
                                    root.activePanel === "chats"
                                        ? LibraryStore
                                            .selectedLibraryId
                                            .length > 0
                                            && !ChatStore.mutating
                                            && !ChatStore.responding
                                        : ChatStore
                                            .selectedChatId
                                            .length > 0
                                            && !AgentStore.mutating
                                            && !ChatStore
                                                .assigningAgent
                                hoverEnabled: true
                                padding: 0
                                ToolTip.visible: hovered
                                ToolTip.text:
                                    root.activePanel === "chats"
                                        ? LibraryStore
                                            .selectedLibraryId
                                            .length > 0
                                            ? "Create Chat"
                                            : "Select a Library first"
                                        : ChatStore
                                            .selectedChatId
                                            .length > 0
                                            ? "Add or create Agent"
                                            : "Select a Chat first"
                                onClicked: {
                                    if (
                                        root.activePanel === "chats"
                                    ) {
                                        root.createChat()
                                    } else {
                                        root.openAgentPicker()
                                    }
                                }

                                contentItem: Text {
                                    text: parent.text
                                    color:
                                        parent.enabled
                                        && parent.hovered
                                            ? root.theme.appText
                                            : root.theme.mutedText
                                    font.pixelSize:
                                        root.theme.textControlSize
                                    horizontalAlignment:
                                        Text.AlignHCenter
                                    verticalAlignment:
                                        Text.AlignVCenter
                                    opacity:
                                        parent.enabled ? 1 : 0.42
                                }

                                background: Rectangle {
                                    radius: 3
                                    color: parent.hovered
                                        ? root.theme.hoverBg
                                        : "transparent"
                                }
                            }
                        }
                    }

                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: root.activePanel === "chats"

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 6

                            Text {
                                Layout.fillWidth: true
                                visible:
                                    ChatStore.errorMessage.length > 0
                                text: ChatStore.errorMessage
                                color: root.theme.danger
                                font.pixelSize:
                                    root.theme.textMetadataSize
                                wrapMode: Text.Wrap
                            }

                            Text {
                                Layout.fillWidth: true
                                visible: ChatStore.loadingChats
                                text: "Loading Chats…"
                                color: root.theme.mutedText
                                font.pixelSize:
                                    root.theme.textMetadataSize
                            }

                            ListView {
                                id: chatList

                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                visible:
                                    root.scopedChats.length > 0
                                spacing: 1
                                topMargin: 4
                                bottomMargin: 4
                                clip: true
                                boundsBehavior:
                                    Flickable.StopAtBounds
                                model: root.scopedChats

                                delegate: Rectangle {
                                    id: chatRow

                                    required property int index
                                    required property var modelData

                                    readonly property bool selected:
                                        String(modelData.id)
                                            === String(
                                                ChatStore.selectedChatId
                                            )
                                    readonly property int agentCount:
                                        root.chatAgentCount(modelData)

                                    width: chatList.width
                                    height: 30
                                    radius: 0
                                    color: chatTap.pressed
                                        ? root.theme.controlPressedBg
                                        : chatHover.hovered
                                            ? root.theme.hoverBg
                                            : selected
                                                ? root.theme.activeBg
                                                : "transparent"

                                    Behavior on color {
                                        ColorAnimation {
                                            duration:
                                                root.theme.motionFast
                                        }
                                    }

                                    Text {
                                        id: chatGlyph

                                        anchors.left: parent.left
                                        anchors.leftMargin: 8
                                        anchors.verticalCenter:
                                            parent.verticalCenter
                                        width: 16
                                        text: "▱"
                                        color: chatRow.selected
                                            ? root.theme.accentBright
                                            : root.theme.mutedText
                                        font.pixelSize:
                                            root.theme.textCaptionSize
                                        horizontalAlignment:
                                            Text.AlignHCenter
                                    }

                                    Text {
                                        anchors.left: chatGlyph.right
                                        anchors.right:
                                            chatMeta.left
                                        anchors.leftMargin: 5
                                        anchors.rightMargin: 6
                                        anchors.verticalCenter:
                                            parent.verticalCenter
                                        text: String(
                                            chatRow.modelData.title
                                                || "Untitled Chat"
                                        )
                                        color: chatRow.selected
                                            || chatHover.hovered
                                            ? root.theme.appText
                                            : root.theme.mutedText
                                        font.pixelSize:
                                            root.theme.textControlSize
                                        font.weight: chatRow.selected
                                            ? root.theme
                                                .textWeightEmphasis
                                            : root.theme
                                                .textWeightRegular
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        id: chatMeta

                                        anchors.right:
                                            manageChatButton.left
                                        anchors.rightMargin: 4
                                        anchors.verticalCenter:
                                            parent.verticalCenter
                                        visible:
                                            chatRow.agentCount > 0
                                        text: String(
                                            chatRow.agentCount
                                        )
                                        color: root.theme.mutedText
                                        font.pixelSize:
                                            root.theme.textMetadataSize
                                        opacity: 0.66
                                    }

                                    Button {
                                        id: manageChatButton

                                        anchors.right: parent.right
                                        anchors.rightMargin: 3
                                        anchors.verticalCenter:
                                            parent.verticalCenter
                                        width: 24
                                        height: 24
                                        text: "•••"
                                        enabled:
                                            !ChatStore.mutating
                                            && !ChatStore.responding
                                        hoverEnabled: true
                                        padding: 0
                                        ToolTip.visible: hovered
                                        ToolTip.text: "Open Chat settings"
                                        onClicked:
                                            chatEditor.openForChat(
                                                chatRow.modelData
                                            )

                                        contentItem: Text {
                                            text: parent.text
                                            color:
                                                parent.enabled
                                                && parent.hovered
                                                    ? root.theme.appText
                                                    : root.theme
                                                        .mutedText
                                            font.pixelSize:
                                                root.theme
                                                    .textMetadataSize
                                            font.weight:
                                                root.theme
                                                    .textWeightStrong
                                            horizontalAlignment:
                                                Text.AlignHCenter
                                            verticalAlignment:
                                                Text.AlignVCenter
                                            opacity:
                                                parent.enabled ? 1 : 0.42
                                        }

                                        background: Rectangle {
                                            radius: 3
                                            color: parent.hovered
                                                ? root.theme.surfaceBg
                                                : "transparent"
                                        }
                                    }

                                    Item {
                                        anchors.left: parent.left
                                        anchors.right:
                                            manageChatButton.left
                                        anchors.top: parent.top
                                        anchors.bottom: parent.bottom

                                        TapHandler {
                                            id: chatTap

                                            enabled:
                                                !ChatStore.responding
                                            onTapped:
                                                root.selectChat(
                                                    chatRow.modelData
                                                )
                                        }
                                    }

                                    HoverHandler {
                                        id: chatHover
                                    }
                                }

                                ScrollBar.vertical: ScrollBar {
                                    policy:
                                        ScrollBar.AsNeeded
                                }
                            }

                            Button {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 24
                                text: (
                                    root.archivedChatsOpen
                                        ? "▾"
                                        : "▸"
                                ) + "  Archived  "
                                    + String(
                                        ChatStore.archivedChats.length
                                    )
                                enabled: !ChatStore.mutating
                                hoverEnabled: true
                                padding: 0
                                onClicked: {
                                    root.archivedChatsOpen =
                                        !root.archivedChatsOpen

                                    if (
                                        root.archivedChatsOpen
                                    ) {
                                        ChatStore.refreshArchived()
                                    }
                                }

                                contentItem: Text {
                                    text: parent.text
                                    color: parent.hovered
                                        ? root.theme.appText
                                        : root.theme.mutedText
                                    font.pixelSize:
                                        root.theme.textMetadataSize
                                    font.weight:
                                        root.theme
                                            .textWeightEmphasis
                                    verticalAlignment:
                                        Text.AlignVCenter
                                    leftPadding: 7
                                }

                                background: Rectangle {
                                    color: parent.hovered
                                        ? root.theme.hoverBg
                                        : "transparent"
                                    radius: 0
                                }
                            }

                            ListView {
                                id: archivedChatList

                                Layout.fillWidth: true
                                Layout.preferredHeight:
                                    root.archivedChatsOpen
                                        ? Math.min(
                                            112,
                                            contentHeight
                                        )
                                        : 0
                                visible: root.archivedChatsOpen
                                clip: true
                                spacing: 1
                                model: ChatStore.archivedChats

                                delegate: Rectangle {
                                    id: archivedChatRow

                                    required property var modelData

                                    width: archivedChatList.width
                                    height: 28
                                    color:
                                        archivedChatHover.hovered
                                            ? root.theme.hoverBg
                                            : "transparent"

                                    Text {
                                        anchors.left: parent.left
                                        anchors.leftMargin: 8
                                        anchors.right:
                                            archivedChatManage.left
                                        anchors.rightMargin: 5
                                        anchors.verticalCenter:
                                            parent.verticalCenter
                                        text: String(
                                            archivedChatRow.modelData.title
                                                || "Untitled Chat"
                                        )
                                        color:
                                            root.theme.mutedText
                                        font.pixelSize:
                                            root.theme.textMetadataSize
                                        elide: Text.ElideRight
                                    }

                                    Button {
                                        id: archivedChatManage

                                        anchors.right: parent.right
                                        anchors.rightMargin: 3
                                        anchors.verticalCenter:
                                            parent.verticalCenter
                                        width: 24
                                        height: 24
                                        text: "•••"
                                        enabled:
                                            !ChatStore.mutating
                                            && !ChatStore.responding
                                        hoverEnabled: true
                                        padding: 0
                                        ToolTip.visible: hovered
                                        ToolTip.text:
                                            "Open Chat settings"
                                        onClicked:
                                            chatEditor.openForChat(
                                                archivedChatRow.modelData
                                            )

                                        contentItem: Text {
                                            text: parent.text
                                            color:
                                                parent.hovered
                                                    ? root.theme.appText
                                                    : root.theme.mutedText
                                            font.pixelSize:
                                                root.theme
                                                    .textMetadataSize
                                            font.weight:
                                                root.theme
                                                    .textWeightStrong
                                            horizontalAlignment:
                                                Text.AlignHCenter
                                            verticalAlignment:
                                                Text.AlignVCenter
                                        }

                                        background: Rectangle {
                                            radius: 3
                                            color: parent.hovered
                                                ? root.theme.surfaceBg
                                                : "transparent"
                                        }
                                    }

                                    HoverHandler {
                                        id: archivedChatHover
                                    }
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                visible:
                                    !ChatStore.loadingChats
                                    && root.scopedChats.length === 0
                                text:
                                    CollectionStore
                                        .selectedCollectionId
                                        .length === 0
                                        ? "Select a Collection to view Chats."
                                        : "No Chats in this Collection."
                                color: root.theme.mutedText
                                font.pixelSize:
                                    root.theme.textMetadataSize
                                horizontalAlignment:
                                    Text.AlignHCenter
                                verticalAlignment:
                                    Text.AlignVCenter
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
                                visible: root.attachedAgents.length > 0
                                spacing: 1
                                topMargin: 4
                                bottomMargin: 4
                                clip: true
                                model: root.attachedAgents

                                delegate: Rectangle {
                                    id: agentRow

                                    required property int index
                                    required property var modelData

                                    readonly property bool assigned:
                                        String(modelData.id)
                                            === String(
                                                ChatStore
                                                    .selectedChat
                                                    .agentId
                                                || ""
                                            )

                                    width: agentList.width
                                    height: 30
                                    radius: 0
                                    color: agentTap.pressed
                                        ? root.theme.controlPressedBg
                                        : agentHover.hovered
                                            ? root.theme.hoverBg
                                            : assigned
                                                ? root.theme.activeBg
                                                : "transparent"

                                    Behavior on color {
                                        ColorAnimation {
                                            duration:
                                                root.theme.motionFast
                                        }
                                    }

                                    Text {
                                        id: agentGlyph

                                        anchors.left: parent.left
                                        anchors.leftMargin: 8
                                        anchors.verticalCenter:
                                            parent.verticalCenter
                                        width: 16
                                        text: "♙"
                                        color: agentRow.assigned
                                            ? root.theme.accentBright
                                            : root.theme.mutedText
                                        font.pixelSize:
                                            root.theme.textCaptionSize
                                        horizontalAlignment:
                                            Text.AlignHCenter
                                    }

                                    Text {
                                        anchors.left: agentGlyph.right
                                        anchors.right:
                                            manageAgentButton.left
                                        anchors.leftMargin: 5
                                        anchors.rightMargin: 6
                                        anchors.verticalCenter:
                                            parent.verticalCenter
                                        text: String(
                                            agentRow.modelData.name
                                                || "Unnamed Agent"
                                        )
                                        color: agentRow.assigned
                                            || agentHover.hovered
                                            ? root.theme.appText
                                            : root.theme.mutedText
                                        font.pixelSize:
                                            root.theme.textControlSize
                                        font.weight: agentRow.assigned
                                            ? root.theme
                                                .textWeightEmphasis
                                            : root.theme
                                                .textWeightRegular
                                        elide: Text.ElideRight
                                    }

                                    Button {
                                        id: manageAgentButton

                                        anchors.right: parent.right
                                        anchors.rightMargin: 3
                                        anchors.verticalCenter:
                                            parent.verticalCenter
                                        width: 24
                                        height: 24
                                        text: "•••"
                                        enabled:
                                            !AgentStore.mutating
                                        hoverEnabled: true
                                        padding: 0
                                        ToolTip.visible: hovered
                                        ToolTip.text:
                                            "Open Agent settings"
                                        onClicked:
                                            agentEditor.openForEdit(
                                                agentRow.modelData
                                            )

                                        contentItem: Text {
                                            text: parent.text
                                            color:
                                                parent.enabled
                                                && parent.hovered
                                                    ? root.theme.appText
                                                    : root.theme
                                                        .mutedText
                                            font.pixelSize:
                                                root.theme
                                                    .textMetadataSize
                                            font.weight:
                                                root.theme
                                                    .textWeightStrong
                                            horizontalAlignment:
                                                Text.AlignHCenter
                                            verticalAlignment:
                                                Text.AlignVCenter
                                            opacity:
                                                parent.enabled ? 1 : 0.42
                                        }

                                        background: Rectangle {
                                            radius: 3
                                            color: parent.hovered
                                                ? root.theme.surfaceBg
                                                : "transparent"
                                        }
                                    }

                                    Item {
                                        anchors.left: parent.left
                                        anchors.right:
                                            manageAgentButton.left
                                        anchors.top: parent.top
                                        anchors.bottom: parent.bottom

                                        TapHandler {
                                            id: agentTap

                                            enabled:
                                                ChatStore
                                                    .selectedChatId
                                                    .length > 0
                                                && !ChatStore.responding
                                                && !ChatStore
                                                    .assigningAgent
                                                && !ChatStore.mutating
                                                && !agentRow.assigned
                                            onTapped:
                                                ChatStore
                                                    .assignAgentToSelectedChat(
                                                        String(
                                                            agentRow
                                                                .modelData
                                                                .id
                                                        )
                                                    )
                                        }
                                    }

                                    HoverHandler {
                                        id: agentHover
                                    }
                                }
                            }

                            Button {
                                visible: false
                                Layout.fillWidth: true
                                Layout.preferredHeight: 0
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
                                Layout.preferredHeight: 0
                                visible: false
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
                                visible: !AgentStore.loading && root.attachedAgents.length === 0
                                text: ChatStore.selectedChatId.length > 0
                                    ? "No Agents are attached to this Chat."
                                    : "Select a Chat to view its Agents."
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

    ChatEditor {
        id: chatEditor

        theme: root.theme
    }

    Connections {
        target: AgentStore

        function onAgentDeleted(agentId, reassignedChatCount) {
            ChatStore.refresh()
        }

        function onAgentCreated(agent) {
            if (
                root.attachNextCreatedAgent
                && root.pendingCreatedChatId === String(ChatStore.selectedChatId)
            ) {
                ChatStore.attachAgentToSelectedChat(String(agent.id || ""))
            }

            root.attachNextCreatedAgent = false
            root.pendingCreatedChatId = ""
        }
    }

    ChatAgentPicker {
        id: agentPicker

        theme: root.theme
        attachedAgentIds: ChatStore.selectedChat.agentIds || []
        chatTitle: String(ChatStore.selectedChat.title || "")
        onAgentSelected: function(agentId) {
            ChatStore.attachAgentToSelectedChat(agentId)
        }
        onCreateRequested: root.createAttachedAgent()
    }

    AgentEditor {
        id: agentEditor

        theme: root.theme
        onClosed: {
            Qt.callLater(function() {
                if (!AgentStore.mutating) {
                    root.attachNextCreatedAgent = false
                    root.pendingCreatedChatId = ""
                }
            })
        }
    }
}

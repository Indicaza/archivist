import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../../Files/FileIdentity.js" as FileIdentity

// The message composer: a raised card with a focus ring in the agent's hue,
// the agent chip, attached sources, and the Send button that turns into Stop
// while a reply runs.
Item {
    id: root

    required property var theme
    // The chat store (ChatStore in the app) and the dock that owns the
    // composer's hover state and notices.
    required property var store
    required property var dock

    // Unsent text per conversation (ChatDrafts in the app). Each chat keeps
    // its own draft: switching chats swaps the text, and nothing typed for
    // one conversation is ever sent to another.
    property var drafts: null
    property string attachmentPopId: ""
    property string draftChatId: ""
    property bool loadingDraft: false

    readonly property alias input: composer
    readonly property color hue: theme.agentHue(store.selectedChat.agentId)
    readonly property color hueBright: theme.agentHueBright(hue)
    readonly property bool motion: !theme.reducedMotion
    readonly property bool hasChat: String(store.selectedChatId || "").length > 0
    readonly property bool stopMode: store.responding === true
    readonly property var attachments: store.attachments || []

    property real focusRing: composer.activeFocus ? 2 : 0
    property real squash: 0

    signal submitRequested()
    signal agentChipClicked()

    function loadDraft() {
        var chatId = String(store.selectedChatId || "")

        loadingDraft = true
        draftChatId = chatId
        composer.text = drafts && chatId.length > 0 ? drafts.draft(chatId) : ""
        composer.cursorPosition = composer.length
        loadingDraft = false
    }

    function saveDraft() {
        if (loadingDraft || !drafts || draftChatId.length === 0) {
            return
        }

        drafts.setDraft(draftChatId, composer.text)
    }

    Connections {
        target: root.store

        function onSelectedChatIdChanged() {
            root.loadDraft()
        }
    }

    // The same draft changed elsewhere (the compact composer, or cleared).
    Connections {
        target: root.drafts
        ignoreUnknownSignals: true

        function onDraftChanged(key) {
            var expected = root.draftChatId.length > 0 ? root.draftChatId : "none"

            if (key !== expected || root.draftChatId.length === 0) {
                return
            }

            var stored = root.drafts.draft(root.draftChatId)
            // Blank text is stored as no draft; that's no change.
            if (stored !== composer.text
                    && !(stored.length === 0 && composer.text.trim().length === 0)) {
                root.loadDraft()
            }
        }
    }

    Component.onCompleted: loadDraft()

    // A small press on the card when a message leaves it.
    function playSubmit() {
        if (!motion) {
            return
        }

        submitAnimation.restart()
    }

    function statusText() {
        if (String(store.errorMessage || "").length > 0) {
            return store.errorMessage
        }

        if (dock.agentSwitchNotice.length > 0) {
            return dock.agentSwitchNotice
        }

        if (dock.attachmentNotice.length > 0) {
            return dock.attachmentNotice
        }

        if (store.mutatingAttachment) {
            return "Updating attached sources…"
        }

        if (store.responding) {
            return store.runPhaseLabel
        }

        if (!hasChat) {
            return "Select a Chat"
        }

        var sources = store.lastSources || []

        if (sources.length > 0) {
            return "Included: "
                + String(sources[0].libraryName || "Library")
                + " / "
                + String(
                    sources[0].relativePath
                        || sources[0].fileName
                        || "attached source"
                )
                + (sources.length > 1 ? "  +" + String(sources.length - 1) : "")
        }

        return "Enter to send  ·  Shift+Enter for newline"
    }

    function statusColor() {
        if (String(store.errorMessage || "").length > 0) {
            return theme.danger
        }

        if (dock.agentSwitchNotice.length > 0) {
            return theme.accentBright
        }

        return dock.attachmentNotice.length > 0 ? theme.success : theme.mutedText
    }

    Behavior on focusRing {
        enabled: root.motion

        SpringAnimation {
            spring: 4.2
            damping: 0.26
            epsilon: 0.01
        }
    }

    SequentialAnimation {
        id: submitAnimation

        NumberAnimation {
            target: root
            property: "squash"
            to: 1
            duration: 70
            easing.type: Easing.OutQuad
        }

        SpringAnimation {
            target: root
            property: "squash"
            to: 0
            spring: 3.4
            damping: 0.22
            epsilon: 0.005
        }
    }

    Item {
        id: card

        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        anchors.topMargin: 8
        anchors.bottomMargin: 10
        scale: 1 - root.squash * 0.012
        transform: Translate { y: root.squash * 2 }

        // Soft shadow: two stacked rounded rectangles read as a blur at this
        // size and cost nothing to draw.
        Rectangle {
            anchors.fill: parent
            anchors.topMargin: 3
            anchors.bottomMargin: -5
            anchors.leftMargin: -2
            anchors.rightMargin: -2
            radius: face.radius + 3
            color: "#000000"
            opacity: composer.activeFocus ? 0.22 : 0.14

            Behavior on opacity {
                NumberAnimation { duration: 160 }
            }
        }

        Rectangle {
            anchors.fill: parent
            anchors.topMargin: 1
            anchors.bottomMargin: -2
            radius: face.radius + 1
            color: "#000000"
            opacity: 0.3
        }

        Rectangle {
            id: focusRingFrame

            anchors.fill: face
            anchors.margins: -(Math.max(0, root.focusRing) + 1)
            radius: face.radius + Math.max(0, root.focusRing) + 1
            color: "transparent"
            border.width: Math.max(0, root.focusRing)
            border.color: root.theme.withAlpha(root.hue, 0.5)
            visible: root.focusRing > 0.05
        }

        Rectangle {
            id: face

            anchors.fill: parent
            radius: root.theme.composerCardRadius
            color: composer.activeFocus
                ? root.theme.composerCardFocusBg
                : root.theme.composerCardBg
            border.width: 1
            border.color: composer.activeFocus
                ? root.theme.withAlpha(root.hue, 0.42)
                : root.theme.quietBorder

            Behavior on color {
                ColorAnimation { duration: 140 }
            }

            Behavior on border.color {
                ColorAnimation { duration: 140 }
            }
        }

        // Agent switch flash.
        Rectangle {
            anchors.fill: face
            radius: face.radius
            color: "transparent"
            border.width: root.dock.agentSwitchPulse > 0.01 ? 2 : 0
            border.color: root.hue
            opacity: root.dock.agentSwitchPulse * 0.72
            z: 4
        }

        ColumnLayout {
            anchors.fill: face
            anchors.margins: 1
            spacing: 0

            ListView {
                id: attachmentChips

                Layout.fillWidth: true
                Layout.preferredHeight: root.attachments.length > 0 ? 34 : 0
                Layout.leftMargin: 10
                Layout.rightMargin: 10
                visible: root.attachments.length > 0
                orientation: ListView.Horizontal
                spacing: 6
                clip: true
                interactive: contentWidth > width
                boundsBehavior: Flickable.StopAtBounds
                model: root.attachments

                delegate: Item {
                    id: chip

                    required property int index
                    required property var modelData

                    readonly property string sourceName: String(
                        modelData.fileName
                            || modelData.relativePath
                            || "Library file"
                    ).split("/").pop()
                    readonly property bool popping:
                        String(modelData.id || "") === root.attachmentPopId
                    property real enter: 1

                    function pop() {
                        if (!popping || !root.motion) {
                            return
                        }

                        enter = 0
                        chipEnter.restart()
                    }

                    width: chipFace.width
                    height: attachmentChips.height
                    onPoppingChanged: pop()
                    Component.onCompleted: pop()

                    SpringAnimation {
                        id: chipEnter

                        target: chip
                        property: "enter"
                        to: 1
                        spring: 3.6
                        damping: 0.3
                        epsilon: 0.005
                    }

                    Rectangle {
                        id: chipFace

                        anchors.verticalCenter: parent.verticalCenter
                        anchors.verticalCenterOffset: 3
                        width: Math.min(220, chipRow.implicitWidth + 16)
                        height: 24
                        radius: 7
                        scale: 0.86 + 0.14 * chip.enter
                        opacity: Math.min(1, chip.enter * 1.4)
                        color: chipHover.hovered ? root.theme.hoverBg : root.theme.surfaceBg
                        border.width: 1
                        border.color: chipHover.hovered
                            ? root.theme.panelBorder
                            : root.theme.quietBorder

                        HoverHandler {
                            id: chipHover
                        }

                        ToolTip.visible: chipHover.hovered && !chipRemove.hovered
                        ToolTip.delay: 600
                        ToolTip.text: String(chip.modelData.libraryName || "Library")
                            + " / "
                            + String(
                                chip.modelData.relativePath
                                    || chip.modelData.fileName
                                    || chip.sourceName
                            )

                        Row {
                            id: chipRow

                            anchors.left: parent.left
                            anchors.leftMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 6

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: FileIdentity.glyphFor({
                                    fileName: chip.sourceName,
                                    extension: chip.modelData.extension || ""
                                })
                                color: root.hueBright
                                font.pixelSize: root.theme.typeSize(10)
                                font.weight: Font.DemiBold
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                width: Math.min(implicitWidth, 160)
                                text: chip.sourceName
                                color: root.theme.appText
                                font.family: root.theme.chatFontFamily
                                font.pixelSize: root.theme.typeSize(10)
                                elide: Text.ElideMiddle
                            }

                            Button {
                                id: chipRemove

                                anchors.verticalCenter: parent.verticalCenter
                                width: 16
                                height: 16
                                padding: 0
                                hoverEnabled: true
                                enabled: !root.store.responding
                                    && !root.store.mutating
                                    && !root.store.mutatingAttachment
                                text: "×"
                                onClicked: root.store.removeAttachment(
                                    String(chip.modelData.id)
                                )

                                contentItem: Text {
                                    text: parent.text
                                    color: parent.hovered ? root.theme.appText : root.theme.mutedText
                                    opacity: parent.enabled ? 1 : 0.45
                                    font.pixelSize: root.theme.typeSize(11)
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }

                                background: Rectangle {
                                    radius: 8
                                    color: parent.hovered ? root.theme.activeBg : "transparent"
                                }
                            }
                        }
                    }
                }
            }

            TextArea {
                id: composer

                Layout.fillWidth: true
                Layout.fillHeight: true
                enabled: root.hasChat
                    && !root.store.responding
                    && !root.store.mutating
                    && !root.store.mutatingAttachment
                placeholderText: root.store.responding
                    ? root.store.runPhaseLabel
                    : root.hasChat
                        ? "Message " + root.dock.selectedChatTitle + "…"
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
                leftPadding: 14
                rightPadding: 14
                topPadding: root.attachments.length > 0 ? 8 : 13
                bottomPadding: 8
                onTextChanged: root.saveDraft()

                Keys.onPressed: function(event) {
                    var returnPressed = event.key === Qt.Key_Return
                        || event.key === Qt.Key_Enter
                    var shiftPressed = (event.modifiers & Qt.ShiftModifier) !== 0

                    if (returnPressed && !shiftPressed) {
                        root.submitRequested()
                        event.accepted = true
                    }
                }

                background: Item {}
            }

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: root.theme.controlBarHeight + 4

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    height: 1
                    color: root.theme.quietBorder
                    opacity: 0.7
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 6
                    anchors.topMargin: 1
                    spacing: 5

                    Button {
                        id: agentChip

                        Layout.preferredHeight: 24
                        Layout.preferredWidth: Math.min(170, agentChipLabel.implicitWidth + 28)
                        visible: root.hasChat
                        hoverEnabled: true
                        padding: 0
                        scale: (down ? 0.96 : 1) + root.dock.agentSwitchPulse * 0.06
                        ToolTip.visible: hovered
                        ToolTip.delay: 500
                        ToolTip.text: "Agent for this Chat"
                        onClicked: root.agentChipClicked()

                        Behavior on scale {
                            enabled: root.motion && !agentChip.down

                            SpringAnimation {
                                spring: 3.8
                                damping: 0.3
                                epsilon: 0.002
                            }
                        }

                        contentItem: Item {
                            Rectangle {
                                x: 9
                                anchors.verticalCenter: parent.verticalCenter
                                width: 7
                                height: 7
                                radius: 3.5
                                color: root.hue
                            }

                            Text {
                                id: agentChipLabel

                                x: 21
                                width: parent.width - 28
                                anchors.verticalCenter: parent.verticalCenter
                                text: root.dock.selectedAgentName
                                color: root.hueBright
                                font.family: root.theme.chatFontFamily
                                font.pixelSize: root.theme.typeSize(10)
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }
                        }

                        background: Rectangle {
                            radius: 12
                            color: root.theme.withAlpha(root.hue, agentChip.hovered ? 0.2 : 0.12)
                            border.width: 1
                            border.color: root.theme.withAlpha(root.hue, agentChip.hovered ? 0.5 : 0.32)

                            Behavior on color {
                                ColorAnimation { duration: 120 }
                            }
                        }
                    }

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
                            onHoveredChanged: root.dock.updateHoverIndex(
                                "composer",
                                index,
                                hovered
                            )
                            scale: root.dock.magnifierScale(
                                index,
                                root.dock.composerHoverIndex,
                                down
                            )

                            Behavior on scale {
                                enabled: !composerToolButton.down

                                NumberAnimation {
                                    duration: root.dock.composerHoverIndex >= 0
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
                                radius: 6
                                color: parent.hovered ? root.theme.hoverBg : "transparent"
                                border.width: 0
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: root.statusText()
                        color: root.statusColor()
                        font.pixelSize: root.theme.typeSize(9)
                        opacity: 0.78
                        elide: Text.ElideRight
                    }

                    Button {
                        id: sendButton

                        readonly property bool ready: root.dock.canSubmit
                        readonly property string stopLabel: String(root.store.activeRunId || "").length === 0
                            ? "Starting…"
                            : root.store.cancellingRun
                                ? "Stopping…"
                                : "Stop"

                        Layout.preferredWidth: root.stopMode ? 96 : 76
                        Layout.preferredHeight: 30
                        text: root.stopMode ? stopLabel : "Send"
                        enabled: root.stopMode
                            ? String(root.store.activeRunId || "").length > 0
                                && !root.store.cancellingRun
                            : ready
                        hoverEnabled: true
                        padding: 0
                        onClicked: {
                            if (root.stopMode) {
                                root.store.cancelActiveRun()
                                return
                            }

                            root.submitRequested()
                        }
                        onHoveredChanged: root.dock.updateHoverIndex(
                            "composer",
                            3,
                            hovered
                        )
                        scale: root.dock.magnifierScale(
                            3,
                            root.dock.composerHoverIndex,
                            down
                        )

                        Behavior on Layout.preferredWidth {
                            enabled: root.motion

                            SpringAnimation {
                                spring: 4
                                damping: 0.32
                                epsilon: 0.2
                            }
                        }

                        Behavior on scale {
                            enabled: !sendButton.down

                            NumberAnimation {
                                duration: root.dock.composerHoverIndex >= 0
                                    ? root.theme.motionHover
                                    : root.theme.motionHoverExit
                                easing.type: Easing.OutCubic
                            }
                        }

                        contentItem: Item {
                            // Send face.
                            Row {
                                anchors.centerIn: parent
                                spacing: 6
                                opacity: root.stopMode ? 0 : 1
                                scale: root.stopMode ? 0.7 : 1

                                Behavior on opacity {
                                    NumberAnimation { duration: root.motion ? 140 : 0 }
                                }

                                Behavior on scale {
                                    enabled: root.motion

                                    SpringAnimation {
                                        spring: 4
                                        damping: 0.3
                                        epsilon: 0.002
                                    }
                                }

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "↑"
                                    color: sendButton.ready ? root.hueBright : root.theme.mutedText
                                    font.pixelSize: root.theme.typeSize(12)
                                    font.weight: Font.Bold
                                }

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "Send"
                                    color: sendButton.ready ? root.theme.appText : root.theme.mutedText
                                    font.pixelSize: root.theme.typeSize(10)
                                    font.weight: Font.Bold
                                }
                            }

                            // Stop face: a stop square inside a spinning ring.
                            Row {
                                anchors.centerIn: parent
                                spacing: 7
                                opacity: root.stopMode ? 1 : 0
                                scale: root.stopMode ? 1 : 0.7
                                visible: opacity > 0.01

                                Behavior on opacity {
                                    NumberAnimation { duration: root.motion ? 160 : 0 }
                                }

                                Behavior on scale {
                                    enabled: root.motion

                                    SpringAnimation {
                                        spring: 4
                                        damping: 0.3
                                        epsilon: 0.002
                                    }
                                }

                                Item {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 16
                                    height: 16

                                    Rectangle {
                                        anchors.fill: parent
                                        radius: width / 2
                                        color: "transparent"
                                        border.width: 1.5
                                        border.color: root.theme.withAlpha(root.theme.danger, 0.28)
                                    }

                                    Item {
                                        id: stopSpinner

                                        anchors.fill: parent

                                        RotationAnimator on rotation {
                                            from: 0
                                            to: 360
                                            duration: 900
                                            loops: Animation.Infinite
                                            running: root.stopMode && root.motion && sendButton.visible
                                        }

                                        // A quarter of the ring, clipped from a full circle.
                                        Item {
                                            x: parent.width / 2
                                            width: parent.width / 2
                                            height: parent.height / 2
                                            clip: true

                                            Rectangle {
                                                x: -parent.width
                                                width: stopSpinner.width
                                                height: stopSpinner.height
                                                radius: width / 2
                                                color: "transparent"
                                                border.width: 1.5
                                                border.color: root.theme.danger
                                            }
                                        }
                                    }

                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: 6
                                        height: 6
                                        radius: 1.5
                                        color: root.theme.danger
                                    }
                                }

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: sendButton.stopLabel
                                    color: sendButton.enabled ? root.theme.danger : root.theme.mutedText
                                    font.pixelSize: root.theme.typeSize(10)
                                    font.weight: Font.Bold
                                }
                            }
                        }

                        background: Rectangle {
                            radius: 8
                            color: root.stopMode
                                ? sendButton.hovered ? "#3a2424" : "#2d2020"
                                : sendButton.ready
                                    ? root.theme.withAlpha(root.hue, sendButton.hovered ? 0.34 : 0.22)
                                    : "#1f1d19"
                            border.width: 1
                            border.color: root.stopMode
                                ? root.theme.withAlpha(root.theme.danger, 0.34)
                                : sendButton.ready
                                    ? root.theme.withAlpha(root.hue, 0.55)
                                    : root.theme.quietBorder

                            Behavior on color {
                                ColorAnimation { duration: 160 }
                            }

                            Behavior on border.color {
                                ColorAnimation { duration: 160 }
                            }
                        }
                    }
                }
            }
        }
    }

    Timer {
        id: popClearTimer

        interval: 700
        onTriggered: root.attachmentPopId = ""
    }

    onAttachmentPopIdChanged: {
        if (attachmentPopId.length > 0) {
            popClearTimer.restart()
        }
    }
}

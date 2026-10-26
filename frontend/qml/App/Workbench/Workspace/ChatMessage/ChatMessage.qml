import QtQuick
import QtQuick.Controls
import Archivist.Services
import "../ChatViewportPolicy.js" as ChatViewportPolicy
import "ChatLaunchRegistry.js" as ChatLaunchRegistry

Item {
    id: root

    required property var theme
    required property var message
    required property string activeRunAssistantMessageId
    required property string activeRunContent
    required property string activeRunProgressLabel
    required property var activeRunActivity
    required property var activeRunAttachments
    required property bool editProposalBusy
    required property real leftObstruction
    property bool diagnosticTraceEnabled: false
    property bool diagnosticPlainContent: false
    property real diagnosticPreviousHeight: 0
    // The label of the stretch that starts after this row ("Today · 10:31
    // AM"), drawn as a divider below it; empty when no pause follows.
    property string breakAfter: ""
    // The speaking agent's hue: rail, glyph, links, bullets, small headings.
    property color hue: theme.accent
    // The newest row in the transcript; only it can launch or be retried.
    property bool lastInTranscript: false
    property bool retryAvailable: false
    // True while the row sits in the ListView's reuse pool, until onReused.
    // Animations stay off while a recycled row takes its new message. Text
    // layout does not wait for onReused: the ListView places a reused row
    // before it emits reused, so the row must reach its final height while
    // its new properties arrive, or rows above the screen resize after they
    // are placed and push the page.
    property bool recycling: false
    readonly property var view: ListView.view
    readonly property bool transcriptMoving: view ? view.moving : false
    readonly property bool motionEnabled: !theme.reducedMotion
    readonly property real springMass: userMessage
        ? theme.chatSpringMassUser
        : systemMessage
            ? theme.chatSpringMassSystem
            : theme.chatSpringMassAssistant

    signal contextInspectionRequested(string messageId)
    signal retryRequested(string messageId)
    signal editApproveAllRequested(string proposalId)
    signal editApproveSelectedRequested(string proposalId, var operationIds)
    signal editRejectRequested(string proposalId)
    signal editUndoRequested(string proposalId)
    signal revealProgressed()
    signal revealFinished(string messageId)
    signal diagnosticEvent(string eventName, var details)

    readonly property string messageId: String(
        root.message && root.message.id || ""
    )
    readonly property string role: String(
        root.message && root.message.role || "system"
    )
    readonly property string storedContent: String(
        root.message && root.message.content || ""
    )
    readonly property string content: root.messageId
        === root.activeRunAssistantMessageId
        ? root.activeRunContent
        : root.storedContent
    readonly property string timestamp: String(
        root.message && root.message.displayTimestamp || ""
    )
    readonly property string status: String(
        root.message && root.message.status || "complete"
    )
    readonly property string progressLabel: root.messageId
        === root.activeRunAssistantMessageId
        ? root.activeRunProgressLabel
        : root.role === "assistant" && root.status === "streaming"
            ? "Starting Run…"
            : ""
    readonly property var activity: root.messageId
        === root.activeRunAssistantMessageId
        ? root.activeRunActivity
        : ({})
    readonly property var attachedFiles: root.role === "assistant"
        && root.status === "streaming"
        ? root.activeRunAttachments
        : []
    readonly property var editProposal: root.message
        && root.message.editProposal
        ? root.message.editProposal
        : ({})
    readonly property bool animateReveal: Boolean(
        root.message && root.message.animateReveal || false
    )
    readonly property bool userMessage: role === "user"
    readonly property bool systemMessage: role === "system"
    readonly property bool assistantMessage: !userMessage && !systemMessage
    readonly property bool providerWaiting: status === "streaming"
    readonly property bool waitingForFirstOutput: providerWaiting
        && content.trim().length === 0
    readonly property bool streamingMessage: providerWaiting
        || richContent.revealing
    readonly property bool failedMessage: status === "failed"
    readonly property bool cancelledMessage: status === "cancelled"
    readonly property string activityLabel: progressLabel.trim().length > 0
        ? progressLabel
        : "Working…"
    readonly property var contentGeometry: ChatViewportPolicy.contentZone(
        width,
        leftObstruction,
        theme.messageHorizontalInset,
        theme.transcriptContentWidth
    )
    readonly property real contentZoneX: Number(contentGeometry.x || 0)
    readonly property real contentZoneWidth: Number(contentGeometry.width || 0)
    readonly property real desiredFrameWidth: ChatViewportPolicy.messageFrameWidth(
        role,
        content,
        theme.userMessageWidth,
        theme.assistantMessageWidth
    )
    readonly property color hueBright: theme.agentHueBright(hue)

    // Launch: a message that just arrived rises in from below on a light
    // spring, and a reply draws its rail down.
    property real launchRise: 0
    property real launchScale: 1
    property real launchOpacity: 1
    property real railProgress: 1

    // Hover: the row lifts a pixel and its action bar floats in.
    property bool hoverActive: false
    readonly property bool pointerInside: rowHover.hovered || barHover.hovered
    property real hoverLift: hoverActive && motionEnabled ? 1 : 0
    property bool copiedMessage: false

    // Streaming: the glyph breathes and an ember rides the rail's tip.
    property real breath: 0

    Behavior on hoverLift {
        NumberAnimation {
            duration: root.theme.motionHover
            easing.type: Easing.OutCubic
        }
    }

    width: ListView.view ? ListView.view.width : 900
    // Message text is laid out by ChatText as soon as the row exists (usually
    // from the background layout cache), so the row's height is final on the
    // first frame and needs no cached or settling height.
    // A row before a long pause carries the time divider below itself, so
    // the divider is part of the row's height from its first frame.
    height: Math.max(1, Math.ceil(frame.height))
        + (breakAfter.length > 0 ? breakDivider.height + theme.messageVerticalGap : 0)

    ListView.onPooled: {
        root.recycling = true
        launchAnimation.stop()
        root.resetLaunch()
        root.hoverActive = false
    }
    ListView.onReused: {
        root.recycling = false
        root.maybeLaunch()
    }

    Component.onCompleted: root.maybeLaunch()

    onPointerInsideChanged: {
        if (pointerInside) {
            hoverHideDelay.stop()
            hoverActive = true
        } else {
            hoverHideDelay.restart()
        }
    }

    onTranscriptMovingChanged: {
        if (transcriptMoving) {
            hoverActive = false
        }
    }

    onStreamingMessageChanged: {
        if (!streamingMessage && motionEnabled && assistantMessage && visible) {
            finishSweep.restart()
        }
    }

    function resetLaunch() {
        root.launchRise = 0
        root.launchScale = 1
        root.launchOpacity = 1
        root.railProgress = 1
    }

    function maybeLaunch() {
        if (
            !root.motionEnabled
            || !ChatLaunchRegistry.claim(root.message, root.lastInTranscript)
        ) {
            return
        }

        if (root.diagnosticTraceEnabled) {
            root.diagnosticEvent("delegate.launch", {
                messageId: root.messageId
            })
        }

        root.launchRise = root.theme.chatLaunchRise
        root.launchScale = root.theme.chatLaunchScale
        root.launchOpacity = 0
        root.railProgress = 0
        launchAnimation.restart()
    }

    function copyMessage() {
        ChatUi.copyText(root.content)
        root.copiedMessage = true
        copiedReset.restart()
    }

    onMessageIdChanged: {
        if (!root.diagnosticTraceEnabled) {
            return
        }

        root.diagnosticPreviousHeight = root.height
        root.diagnosticEvent("delegate.rebound", {
            messageId: root.messageId,
            height: root.height,
            frameHeight: frame.height,
            layoutWidth: Math.round(frame.width)
        })
    }

    onHeightChanged: {
        if (root.diagnosticTraceEnabled && root.messageId.length > 0) {
            root.diagnosticEvent("delegate.height", {
                messageId: root.messageId,
                previousHeight: root.diagnosticPreviousHeight,
                height: root.height,
                frameHeight: frame.height,
                layoutWidth: Math.round(frame.width)
            })
        }

        root.diagnosticPreviousHeight = root.height
    }

    ParallelAnimation {
        id: launchAnimation

        SpringAnimation {
            target: root
            property: "launchRise"
            to: 0
            spring: root.theme.motionSpring + 0.4
            damping: root.theme.motionDamping
            mass: root.springMass
            epsilon: 0.05
        }

        SpringAnimation {
            target: root
            property: "launchScale"
            to: 1
            spring: root.theme.motionSpring + 0.4
            damping: root.theme.motionDamping
            mass: root.springMass
            epsilon: 0.0005
        }

        NumberAnimation {
            target: root
            property: "launchOpacity"
            to: 1
            duration: 200
            easing.type: Easing.OutCubic
        }

        NumberAnimation {
            target: root
            property: "railProgress"
            to: 1
            duration: 640
            easing.type: Easing.OutCubic
        }
    }

    SequentialAnimation {
        id: breathing

        loops: Animation.Infinite
        running: root.streamingMessage
            && root.assistantMessage
            && root.motionEnabled
            && !root.recycling
        onRunningChanged: {
            if (!running) {
                root.breath = 0
            }
        }

        NumberAnimation {
            target: root
            property: "breath"
            to: 1
            duration: 900
            easing.type: Easing.InOutSine
        }

        NumberAnimation {
            target: root
            property: "breath"
            to: 0
            duration: 900
            easing.type: Easing.InOutSine
        }
    }

    Timer {
        id: hoverHideDelay

        interval: 220
        repeat: false
        onTriggered: root.hoverActive = root.pointerInside
    }

    Timer {
        id: copiedReset

        interval: 1400
        repeat: false
        onTriggered: root.copiedMessage = false
    }

    TimeDivider {
        id: breakDivider

        // Centred between this row and the next: the transcript's own gap
        // sits below, so the same gap goes above.
        y: Math.ceil(frame.height) + root.theme.messageVerticalGap
        width: root.width
        theme: root.theme
        section: root.breakAfter
    }

    Item {
        id: frame

        x: root.userMessage
            ? root.contentZoneX + root.contentZoneWidth - width
            : root.contentZoneX
        width: Math.min(root.contentZoneWidth, root.desiredFrameWidth)
        height: messageColumn.implicitHeight
        scale: root.launchScale
        transformOrigin: Item.Bottom
        opacity: root.launchOpacity
        transform: Translate {
            y: root.launchRise - root.hoverLift
        }

        // When the transcript resizes (say the sidebar moves), messages
        // spring to their new place. Only the position springs; the width,
        // and so the text wrapping, follows at once, keeping text crisp and
        // cheap. A recycled row taking a new message never animates.
        Behavior on x {
            enabled: !root.recycling && root.motionEnabled

            SpringAnimation {
                spring: root.theme.motionSpring
                damping: root.theme.motionDamping
                mass: root.springMass
                epsilon: 0.2
                onRunningChanged: {
                    if (running && root.diagnosticTraceEnabled) {
                        root.diagnosticEvent("delegate.slide", {
                            messageId: root.messageId,
                            x: Math.round(frame.x)
                        })
                    }
                }
            }
        }

        HoverHandler {
            id: rowHover

            enabled: !root.systemMessage
        }

        // The reply rail: a thin line in the agent's hue down the left of a
        // reply. It draws down as a reply arrives and carries a glowing tip
        // while the reply streams.
        Item {
            id: rail

            visible: root.assistantMessage && !root.waitingForFirstOutput
            x: -18
            y: 46
            width: 2
            height: Math.max(0, (frame.height - y - 4) * root.railProgress)

            Rectangle {
                anchors.fill: parent
                radius: 1
                gradient: Gradient {
                    GradientStop {
                        position: 0.0
                        color: root.theme.withAlpha(root.hue, root.streamingMessage ? 0.7 : 0.42)
                    }
                    GradientStop {
                        position: 1.0
                        color: root.theme.withAlpha(root.hue, root.streamingMessage ? 0.3 : 0.06)
                    }
                }
            }

            Rectangle {
                x: -3
                y: parent.height - 4
                width: 8
                height: 8
                radius: 4
                visible: root.streamingMessage && root.motionEnabled
                color: root.hueBright
                opacity: 0.45 + 0.5 * root.breath

                Rectangle {
                    anchors.centerIn: parent
                    width: 18 + 6 * root.breath
                    height: width
                    radius: width / 2
                    color: root.theme.withAlpha(root.hue, 0.14 + 0.12 * root.breath)
                }
            }
        }

        // Stacks the header, activity, message and edit card. Not a Column:
        // a Column lays out at polish time, after the ListView has already
        // placed a recycled row, so the row would resize after placement and
        // push every row below it. Plain bindings keep the height exact the
        // moment a new message arrives.
        Item {
            id: messageColumn

            readonly property real spacing: root.userMessage ? 0 : 14
            readonly property real contentBottom: messageColumn.below(
                editProposalLoader,
                editProposalLoader.y
            )

            // The y for whatever follows `item`, which sits at `y`. Hidden
            // items take no space, as in a Column.
            function below(item, y) {
                return item.visible ? y + item.height + spacing : y
            }

            width: parent.width
            implicitHeight: Math.max(0, contentBottom - spacing)

            Item {
                id: headerRow

                y: 0
                visible: !root.userMessage
                width: parent.width
                height: visible ? 32 : 0
                clip: true

                // A light sweep across the header when a reply finishes.
                Rectangle {
                    id: finishGlow

                    y: 0
                    width: 160
                    height: parent.height
                    x: -width
                    opacity: 0
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: "transparent" }
                        GradientStop { position: 0.5; color: root.theme.withAlpha(root.hue, 0.2) }
                        GradientStop { position: 1.0; color: "transparent" }
                    }
                }

                Row {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 7

                    Item {
                        width: 22
                        height: 22

                        // Ember behind the glyph while the reply streams.
                        Rectangle {
                            anchors.centerIn: parent
                            width: 22 + 10 * root.breath
                            height: width
                            radius: 7 + 5 * root.breath
                            color: root.theme.withAlpha(root.hue, 0.1 + 0.16 * root.breath)
                            visible: root.streamingMessage && root.motionEnabled
                        }

                        Rectangle {
                            id: glyphTile

                            anchors.fill: parent
                            radius: 7
                            color: root.systemMessage
                                ? "#1d1c19"
                                : root.theme.withAlpha(root.hue, 0.18)
                            border.width: root.systemMessage ? 0 : 1
                            border.color: root.theme.withAlpha(root.hue, 0.36)
                            scale: 1 + 0.08 * root.breath

                            Text {
                                anchors.centerIn: parent
                                text: root.systemMessage ? "!" : "✣"
                                color: root.systemMessage
                                    ? root.theme.appText
                                    : root.hueBright
                                font.pixelSize: root.theme.typeSize(11)
                                font.weight: Font.Bold
                            }
                        }
                    }

                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 7

                        Text {
                            text: root.systemMessage ? "SYSTEM" : "ARCHIVIST"
                            color: root.theme.appText
                            font.family: root.theme.chatFontFamily
                            font.pixelSize: root.theme.typeSize(10)
                            font.weight: Font.DemiBold
                            font.letterSpacing: 0.5
                        }

                        Text {
                            text: root.failedMessage
                                ? "FAILED"
                                : root.cancelledMessage
                                    ? "STOPPED"
                                    : root.streamingMessage
                                        ? root.activityLabel
                                        : root.timestamp
                            color: root.failedMessage
                                ? root.theme.danger
                                : root.cancelledMessage
                                    ? root.theme.accentBright
                                    : root.streamingMessage
                                        ? root.hueBright
                                        : root.theme.mutedText
                            font.family: root.theme.chatFontFamily
                            font.pixelSize: root.theme.typeSize(9)
                            opacity: root.failedMessage
                                ? 0.9
                                : root.cancelledMessage
                                    ? 0.72
                                    : root.streamingMessage
                                        ? 0.78
                                        : 0.52
                        }
                    }
                }
            }

            // Only the message waiting for its first output shows activity.
            Loader {
                id: activityCard

                y: messageColumn.below(headerRow, headerRow.y)
                active: root.waitingForFirstOutput
                    && !root.userMessage
                    && !root.systemMessage
                visible: active
                width: parent.width
                height: active && item ? item.implicitHeight : 0
                sourceComponent: Component {
                    RunActivityCard {
                        theme: root.theme
                        activity: root.activity
                        attachedFiles: root.attachedFiles
                        progressLabel: root.activityLabel
                    }
                }
            }

            Item {
                id: surfaceFrame

                y: messageColumn.below(activityCard, activityCard.y)
                visible: !root.waitingForFirstOutput
                width: parent.width
                height: visible
                    ? (root.providerWaiting || root.diagnosticPlainContent
                        ? streamingLoader.height
                        : richContent.implicitHeight)
                        + (root.userMessage ? 30 : root.systemMessage ? 32 : 4)
                    : 0

                Rectangle {
                    x: 0
                    y: 3 + 2 * root.hoverLift
                    width: parent.width
                    height: parent.height
                    radius: root.theme.radiusLarge
                    color: Qt.rgba(0, 0, 0, 0.19 + 0.08 * root.hoverLift)
                    visible: root.userMessage
                }

                Rectangle {
                    id: surface

                    anchors.fill: parent
                    radius: root.userMessage
                        ? root.theme.radiusLarge
                        : root.systemMessage
                            ? root.theme.radiusSmall
                            : 0
                    color: root.userMessage
                        ? root.theme.userBg
                        : root.systemMessage
                            ? root.theme.systemBg
                            : root.theme.assistantBg
                    border.width: root.userMessage || root.systemMessage ? 1 : 0
                    border.color: root.failedMessage
                        ? root.theme.danger
                        : root.cancelledMessage
                            ? root.theme.quietBorder
                            : root.userMessage
                                ? root.theme.quietBorder
                                : root.systemMessage
                                    ? root.theme.quietBorder
                                    : root.theme.panelBorder
                    antialiasing: true
                    clip: root.userMessage || root.systemMessage
                    opacity: root.providerWaiting ? 0.82 : 1

                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        anchors.topMargin: 10
                        anchors.bottomMargin: 10
                        width: 2
                        radius: 1
                        color: root.failedMessage
                            ? root.theme.danger
                            : root.cancelledMessage
                                ? root.theme.accentBright
                                : root.theme.accent
                        opacity: root.failedMessage
                            ? 0.82
                            : root.cancelledMessage
                                ? 0.48
                                : 0
                    }

                    // Plain text while a reply streams (or in the plain-text
                    // diagnostic); finished messages never build it.
                    Loader {
                        id: streamingLoader

                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        active: root.providerWaiting
                            || root.diagnosticPlainContent
                        height: active && item ? item.contentHeight : 0
                        sourceComponent: Component {
                            ChatText {
                                width: Math.min(streamingLoader.width, root.theme.chatReadingMeasure)
                                text: root.content
                                markdown: false
                                cacheable: false
                                color: root.theme.appText
                                font: root.theme.chatBodyFont
                                selectionColor: root.theme.messageSelectionBg
                                selectedTextColor: root.theme.messageSelectionText
                            }
                        }
                    }

                    RichMessageContent {
                        id: richContent

                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.leftMargin: ChatViewportPolicy.messageTextInset(root.role)
                        anchors.rightMargin: ChatViewportPolicy.messageTextInset(root.role)
                        anchors.topMargin: root.userMessage
                            ? 14
                            : root.systemMessage
                                ? 16
                                : 0
                        visible: !root.providerWaiting
                            && !root.diagnosticPlainContent
                        theme: root.theme
                        hue: root.hue
                        cacheKey: root.messageId
                        transcriptMoving: root.transcriptMoving
                        content: root.providerWaiting ? "" : root.content
                        compact: root.userMessage || root.systemMessage
                        animateReveal: root.animateReveal
                            && !root.userMessage
                            && !root.systemMessage
                        onRevealProgressed: root.revealProgressed()
                        onRevealFinished: root.revealFinished(root.messageId)
                    }
                }
            }
            Loader {
                id: editProposalLoader

                y: messageColumn.below(surfaceFrame, surfaceFrame.y)
                active: !root.diagnosticPlainContent
                    && !root.userMessage
                    && !root.systemMessage
                    && String(root.editProposal.id || "").length > 0
                visible: active
                width: parent.width
                height: active && item ? item.implicitHeight : 0
                sourceComponent: Component {
                    AIEditProposalCard {
                        theme: root.theme
                        proposal: root.editProposal
                        busy: root.editProposalBusy
                        reviewReady: !root.providerWaiting
                        onApproveAllRequested: function(proposalId) {
                            root.editApproveAllRequested(proposalId)
                        }
                        onApproveSelectedRequested: function(proposalId, operationIds) {
                            root.editApproveSelectedRequested(proposalId, operationIds)
                        }
                        onRejectRequested: function(proposalId) {
                            root.editRejectRequested(proposalId)
                        }
                        onUndoRequested: function(proposalId) {
                            root.editUndoRequested(proposalId)
                        }
                    }
                }
            }
        }

        // Floating actions, shown while the pointer is on the message. Built
        // only for the hovered row.
        Loader {
            id: actionBarLoader

            active: root.hoverActive
                && !root.systemMessage
                && !root.streamingMessage
                && root.messageId.length > 0
            anchors.right: parent.right
            anchors.rightMargin: root.userMessage ? 6 : 0
            y: root.userMessage ? -30 : 2
            z: 5
            sourceComponent: Component {
                Rectangle {
                    id: actionBar

                    property real arrival: 0

                    width: actionRow.implicitWidth + 8
                    height: 26
                    radius: 13
                    color: root.theme.controlSurfaceBg
                    border.width: 1
                    border.color: root.theme.quietBorder
                    opacity: arrival
                    transform: Translate {
                        y: (1 - actionBar.arrival) * 4
                    }

                    Component.onCompleted: arrival = 1

                    Behavior on arrival {
                        enabled: root.motionEnabled

                        NumberAnimation {
                            duration: root.theme.motionHover
                            easing.type: Easing.OutCubic
                        }
                    }

                    HoverHandler {
                        id: barHoverInner

                        onHoveredChanged: barHover.hovered = hovered
                    }

                    Row {
                        id: actionRow

                        anchors.centerIn: parent
                        spacing: 0

                        MessageAction {
                            label: root.copiedMessage ? "✓  Copied" : "Copy"
                            tint: root.copiedMessage ? root.theme.success : root.hueBright
                            popOnChange: root.copiedMessage
                            onActivated: root.copyMessage()
                        }

                        MessageAction {
                            visible: root.assistantMessage
                            label: "Context"
                            tint: root.hueBright
                            onActivated: root.contextInspectionRequested(root.messageId)
                        }

                        MessageAction {
                            visible: root.assistantMessage && root.retryAvailable
                            label: "Retry"
                            tint: root.hueBright
                            onActivated: root.retryRequested(root.messageId)
                        }
                    }
                }
            }
        }
    }

    // Hover over the action bar keeps it open; it lives in a Loader, so its
    // handler reports here.
    QtObject {
        id: barHover

        property bool hovered: false
    }

    onHoverActiveChanged: {
        if (!hoverActive) {
            barHover.hovered = false
        }
    }

    ParallelAnimation {
        id: finishSweep

        NumberAnimation {
            target: finishGlow
            property: "x"
            from: -finishGlow.width
            to: headerRow.width
            duration: 760
            easing.type: Easing.InOutQuad
        }

        SequentialAnimation {
            NumberAnimation { target: finishGlow; property: "opacity"; to: 1; duration: 160 }
            PauseAnimation { duration: 440 }
            NumberAnimation { target: finishGlow; property: "opacity"; to: 0; duration: 160 }
        }

        SequentialAnimation {
            NumberAnimation {
                target: glyphTile
                property: "scale"
                to: 1.16
                duration: 110
                easing.type: Easing.OutCubic
            }

            SpringAnimation {
                target: glyphTile
                property: "scale"
                to: 1
                spring: 4.5
                damping: 0.25
                epsilon: 0.002
            }
        }
    }

    component MessageAction: Item {
        id: action

        property string label: ""
        property color tint: "white"
        property bool popOnChange: false
        readonly property bool hovered: actionMouse.containsMouse

        signal activated()

        width: visible ? actionText.implicitWidth + 18 : 0
        height: 22
        Accessible.role: Accessible.Button
        Accessible.name: label

        onPopOnChangeChanged: {
            if (popOnChange && root.motionEnabled) {
                pop.restart()
            }
        }

        Rectangle {
            anchors.fill: parent
            radius: 11
            color: action.hovered ? root.theme.hoverBg : "transparent"
        }

        Text {
            id: actionText

            anchors.centerIn: parent
            text: action.label
            color: action.hovered || action.popOnChange
                ? action.tint
                : root.theme.mutedText
            font.family: root.theme.chatFontFamily
            font.pixelSize: root.theme.typeSize(9)
            font.weight: Font.DemiBold
        }

        MouseArea {
            id: actionMouse

            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: action.activated()
        }

        SequentialAnimation {
            id: pop

            NumberAnimation {
                target: actionText
                property: "scale"
                to: 1.2
                duration: 90
                easing.type: Easing.OutCubic
            }

            SpringAnimation {
                target: actionText
                property: "scale"
                to: 1
                spring: 4.5
                damping: 0.25
                epsilon: 0.002
            }
        }
    }
}

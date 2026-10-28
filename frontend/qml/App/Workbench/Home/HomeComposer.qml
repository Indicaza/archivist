import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Archivist.Services 1.0
import "../../Icons"

// Home's composer: the centerpiece. A roomy field over a quiet toolbar
// (add, where the conversation starts, who answers) and one Send button.
// Sending starts a new conversation.
//
// The draft belongs to Home (Shell.homeDraft), not to this field: leaving
// Home, switching projects or restarting keeps it. It's cleared only once
// the conversation exists and the message went out.
FocusScope {
    id: root

    required property var theme
    // [{ id, name }] the conversation can be started in.
    property var libraries: []
    property string libraryId: ""
    property string libraryName: ""
    property string projectName: ""
    // [{ id, name, generation }] who can answer; empty hides the chip.
    property var agents: []
    property string agentId: ""
    // Why sending isn't possible right now; empty when it is.
    property string blockedReason: ""
    property string placeholder: "Ask Archivist anything"
    property bool sending: false
    // A reply is streaming somewhere: Send becomes Stop.
    property bool streaming: false
    // Home's own draft (Shell.homeDraft). Off when another owner keeps the
    // text, such as the workspace's compact chat composer (ChatDrafts).
    property bool syncHomeDraft: true
    // The "+" menu (add a folder, new project).
    property bool showAdd: true
    // An expand button beside Send: grow into the docked workbench.
    property bool showExpand: false
    // The folder chip; off where the folder is already decided.
    property bool showLibrary: true
    // Floating over content (the workspace's compact chat): a raised,
    // slightly lighter bubble with a lit top edge. Its owner draws the
    // shadow underneath.
    property bool floating: false
    property string expandToolTip: "Expand"

    readonly property alias input: field
    readonly property bool hasText: field.text.trim().length > 0
    readonly property bool canSend: blockedReason.length === 0 && !sending && hasText
    readonly property bool focused: field.activeFocus
    readonly property var agent: {
        for (var index = 0; index < agents.length; index += 1) {
            if (String(agents[index].id) === agentId) {
                return agents[index]
            }
        }
        return agents.length > 0 ? agents[0] : null
    }

    signal submitRequested(string text)
    signal stopRequested()
    signal libraryChosen(string libraryId)
    signal agentChosen(string agentId)
    signal addFolderRequested()
    signal newProjectRequested()
    signal expandRequested()

    function submit() {
        if (!canSend) {
            return
        }
        submitRequested(field.text.trim())
    }

    function focusInput() {
        field.forceActiveFocus()
        field.cursorPosition = field.length
    }

    function setText(text) {
        field.text = text
        focusInput()
    }

    function agentLabel(entry) {
        if (!entry) {
            return ""
        }
        var name = String(entry.name || "Agent")
        var model = String((entry.generation || {}).model || "")
        return model.length > 0 ? name + " · " + model : name
    }

    readonly property real fieldHeight: Math.min(260, Math.max(field.implicitHeight, 28 + root.theme.textBodySize * 1.5))

    implicitHeight: card.height
    activeFocusOnTab: true

    // Soft shadow: two offset layers, no blur pass.
    Rectangle {
        visible: !root.floating
        x: 2
        y: 10
        width: card.width - 4
        height: card.height
        radius: card.radius
        color: root.theme.withAlpha(root.theme.homeShadow, root.focused ? 0.22 : 0.14)

        Behavior on color {
            enabled: !root.theme.reducedMotion
            ColorAnimation { duration: root.theme.motionHover }
        }
    }

    // Focus ring: a faint wash of the accent around the card.
    Rectangle {
        anchors.fill: card
        anchors.margins: -3
        radius: card.radius + 3
        color: "transparent"
        border.width: 3
        border.color: root.theme.withAlpha(root.theme.accent, 0.13)
        opacity: root.focused ? 1 : 0

        Behavior on opacity {
            enabled: !root.theme.reducedMotion
            NumberAnimation { duration: root.theme.motionHover }
        }
    }

    Rectangle {
        id: card

        width: parent.width
        height: Math.round(root.fieldHeight + toolbar.height + 18)
        radius: root.floating ? root.theme.compactComposerRadius : root.theme.homeComposerRadius
        color: {
            var base = root.floating ? root.theme.compactComposerBg : root.theme.homeComposerBg
            return root.focused ? Qt.lighter(base, 1.05) : base
        }
        border.width: 1
        border.color: root.focused
            ? root.theme.homeComposerFocusBorder
            : root.floating
                ? (hover.hovered ? root.theme.compactComposerBorderHover : root.theme.compactComposerBorder)
                : hover.hovered ? root.theme.homeCardBorderHover : root.theme.homeCardBorder

        // A lit top edge: the bubble catches light above the page.
        Rectangle {
            visible: root.floating
            anchors.fill: parent
            anchors.margins: 1
            radius: parent.radius - 1
            color: "transparent"
            border.width: 1
            border.color: root.theme.compactComposerHighlight
        }

        Behavior on height {
            enabled: !root.theme.reducedMotion
            NumberAnimation { duration: root.theme.motionFast; easing.type: Easing.OutCubic }
        }

        Behavior on border.color {
            enabled: !root.theme.reducedMotion
            ColorAnimation { duration: root.theme.motionHover }
        }

        Behavior on color {
            enabled: !root.theme.reducedMotion
            ColorAnimation { duration: root.theme.motionHover }
        }

        HoverHandler {
            id: hover
        }

        // A click anywhere on the card that isn't a control puts the caret
        // in the field.
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.IBeamCursor
            onClicked: root.focusInput()
        }

        ScrollView {
            id: fieldScroll

            x: 12
            y: 8
            width: parent.width - 24
            height: root.fieldHeight
            clip: true

            TextArea {
                id: field

                Component.onCompleted: {
                    if (root.syncHomeDraft) {
                        text = Shell.homeDraft
                    }
                }
                onTextChanged: {
                    if (root.syncHomeDraft && text !== Shell.homeDraft) {
                        Shell.homeDraft = text
                    }
                }
                enabled: !root.sending
                placeholderText: root.blockedReason.length > 0 ? root.blockedReason : root.placeholder
                placeholderTextColor: root.theme.composerPlaceholder
                color: root.theme.appText
                selectionColor: root.theme.messageSelectionBg
                selectedTextColor: root.theme.messageSelectionText
                font.family: root.theme.chatFontFamily
                font.pixelSize: root.theme.textBodySize + 1
                wrapMode: TextEdit.Wrap
                leftPadding: 8
                rightPadding: 8
                topPadding: 12
                bottomPadding: 8
                Accessible.name: "Message"
                Accessible.description: root.blockedReason

                Keys.onPressed: function(event) {
                    var returnPressed = event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                    var shiftPressed = (event.modifiers & Qt.ShiftModifier) !== 0
                    if (returnPressed && !shiftPressed) {
                        root.submit()
                        event.accepted = true
                    }
                }

                background: Item {}

                // The draft changed elsewhere (sent, so cleared).
                Connections {
                    target: Shell
                    enabled: root.syncHomeDraft

                    function onHomeDraftChanged() {
                        if (field.text !== Shell.homeDraft) {
                            field.text = Shell.homeDraft
                        }
                    }
                }
            }
        }

        RowLayout {
            id: toolbar

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            anchors.bottomMargin: 10
            height: 36
            spacing: 4

            HomeChip {
                id: plusButton

                visible: root.showAdd
                theme: root.theme
                iconName: "add"
                round: true
                toolTipText: "Add a folder or start a project"
                Accessible.name: "Add"
                onClicked: plusMenu.popup(plusButton, 0, plusButton.height + 6)
            }

            HomeChip {
                id: libraryButton

                visible: root.showLibrary && root.libraries.length > 0
                theme: root.theme
                iconName: "folder"
                label: root.libraryName.length > 0 ? root.libraryName : "Folder"
                menu: root.libraries.length > 1
                enabled: !root.sending
                toolTipText: root.libraries.length > 1
                    ? "Choose the folder this conversation starts in"
                    : "This conversation starts in " + root.libraryName
                Accessible.name: "Start in " + root.libraryName
                onClicked: {
                    if (root.libraries.length > 1) {
                        libraryMenu.popup(libraryButton, 0, libraryButton.height + 6)
                    }
                }
            }

            HomeChip {
                id: agentButton

                visible: root.agent !== null && (root.libraries.length > 0 || !root.showLibrary)
                theme: root.theme
                iconName: "agent"
                label: root.agentLabel(root.agent)
                menu: root.agents.length > 1
                enabled: !root.sending
                Layout.maximumWidth: Math.max(120, toolbar.width * 0.42)
                toolTipText: root.agents.length > 1 ? "Choose who answers" : "Answers with " + root.agentLabel(root.agent)
                Accessible.name: "Answered by " + root.agentLabel(root.agent)
                onClicked: {
                    if (root.agents.length > 1) {
                        agentMenu.popup(agentButton, 0, agentButton.height + 6)
                    }
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
            }

            Text {
                Layout.alignment: Qt.AlignVCenter
                Layout.rightMargin: 6
                text: root.streaming && !root.hasText ? "Archivist is answering" : "⏎ to send  ·  ⇧⏎ new line"
                color: root.theme.withAlpha(root.theme.mutedText, 0.8)
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.textCaptionSize
                opacity: (root.focused && root.hasText) || (root.streaming && !root.hasText) ? 1 : 0
                visible: opacity > 0 && toolbar.width > 460

                Behavior on opacity {
                    enabled: !root.theme.reducedMotion
                    NumberAnimation { duration: root.theme.motionHover }
                }
            }

            HomeChip {
                id: expandButton

                visible: root.showExpand
                theme: root.theme
                iconName: "expand"
                round: true
                toolTipText: root.expandToolTip
                Accessible.name: root.expandToolTip
                onClicked: root.expandRequested()
            }

            HomeSendButton {
                id: sendButton

                theme: root.theme
                mode: root.sending
                    ? "sending"
                    : root.streaming && !root.canSend
                        ? "stop"
                        : root.canSend ? "ready" : "idle"
                onSendRequested: root.submit()
                onStopRequested: root.stopRequested()
            }
        }
    }

    Menu {
        id: plusMenu

        MenuItem {
            text: root.projectName.length > 0 ? "Add a folder to " + root.projectName : "Add a folder"
            enabled: root.projectName.length > 0
            onTriggered: root.addFolderRequested()
        }

        MenuItem {
            text: "New project…"
            onTriggered: root.newProjectRequested()
        }
    }

    Menu {
        id: libraryMenu

        Repeater {
            model: root.libraries

            delegate: MenuItem {
                required property var modelData

                text: String(modelData.name || "Folder")
                checkable: true
                checked: String(modelData.id) === root.libraryId
                onTriggered: root.libraryChosen(String(modelData.id))
            }
        }
    }

    Menu {
        id: agentMenu

        Repeater {
            model: root.agents

            delegate: MenuItem {
                required property var modelData

                text: root.agentLabel(modelData)
                checkable: true
                checked: root.agent && String(modelData.id) === String(root.agent.id)
                onTriggered: root.agentChosen(String(modelData.id))
            }
        }
    }
}

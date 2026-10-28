import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Archivist.Services 1.0
import "../../Icons"

// Home's composer: one rounded field. Sending starts a new conversation in
// the chosen folder.
//
// The draft belongs to Home (Shell.homeDraft), not to this field: leaving
// Home, switching projects or restarting keeps it. It's cleared only once
// the conversation exists and the message went out.
Rectangle {
    id: root

    required property var theme
    // [{ id, name }] the conversation can be started in.
    property var libraries: []
    property string libraryId: ""
    property string libraryName: ""
    property string projectName: ""
    // Why sending isn't possible right now; empty when it is.
    property string blockedReason: ""
    property bool sending: false

    readonly property alias input: field
    readonly property bool canSend: blockedReason.length === 0
        && !sending
        && field.text.trim().length > 0
    readonly property bool multiline: field.lineCount > 1

    signal submitRequested(string text)
    signal libraryChosen(string libraryId)
    signal addFolderRequested()
    signal newProjectRequested()

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

    implicitHeight: Math.max(56, Math.min(240, field.implicitHeight) + 16)
    radius: multiline ? 22 : height / 2
    color: root.theme.composerCardBg
    border.width: 1
    border.color: field.activeFocus ? Qt.lighter(root.theme.panelBorder, 1.25) : root.theme.quietBorder

    Behavior on border.color {
        enabled: !root.theme.reducedMotion
        ColorAnimation { duration: root.theme.motionFast }
    }

    Behavior on radius {
        enabled: !root.theme.reducedMotion
        NumberAnimation { duration: root.theme.motionFast }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        anchors.topMargin: 8
        anchors.bottomMargin: 8
        spacing: 6

        // Add a folder or start a project.
        Button {
            id: plusButton

            Layout.preferredWidth: 36
            Layout.preferredHeight: 36
            Layout.alignment: Qt.AlignBottom
            hoverEnabled: true
            focusPolicy: Qt.StrongFocus
            padding: 0
            Accessible.name: "Add"
            ToolTip.visible: hovered
            ToolTip.delay: 600
            ToolTip.text: "Add a folder or project"
            onClicked: plusMenu.popup(plusButton, 0, plusButton.height + 6)

            contentItem: Item {
                AppIcon {
                    anchors.centerIn: parent
                    name: "add"
                    tone: plusButton.hovered || plusButton.activeFocus ? "normal" : "muted"
                    iconSize: 16
                }
            }

            background: Rectangle {
                radius: height / 2
                color: plusButton.down ? root.theme.activeBg : plusButton.hovered ? root.theme.hoverBg : "transparent"
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

        ScrollView {
            id: fieldScroll

            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(240, field.implicitHeight)
            Layout.alignment: Qt.AlignVCenter
            clip: true

            TextArea {
                id: field

                Component.onCompleted: text = Shell.homeDraft
                onTextChanged: {
                    if (text !== Shell.homeDraft) {
                        Shell.homeDraft = text
                    }
                }
                enabled: !root.sending
                placeholderText: root.blockedReason.length > 0 ? root.blockedReason : "Ask Archivist"
                placeholderTextColor: root.theme.composerPlaceholder
                color: root.theme.appText
                selectionColor: root.theme.messageSelectionBg
                selectedTextColor: root.theme.messageSelectionText
                font.family: root.theme.chatFontFamily
                font.pixelSize: root.theme.textBodySize
                wrapMode: TextEdit.Wrap
                leftPadding: 4
                rightPadding: 4
                topPadding: 9
                bottomPadding: 9
                Accessible.name: "Message"

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

                    function onHomeDraftChanged() {
                        if (field.text !== Shell.homeDraft) {
                            field.text = Shell.homeDraft
                        }
                    }
                }
            }
        }

        // Where the conversation starts, like a model picker.
        Button {
            id: libraryButton

            Layout.alignment: Qt.AlignBottom
            Layout.preferredHeight: 36
            visible: root.libraries.length > 0
            enabled: root.libraries.length > 1 && !root.sending
            hoverEnabled: true
            focusPolicy: Qt.StrongFocus
            leftPadding: 10
            rightPadding: 8
            Accessible.name: "Start the conversation in " + root.libraryName
            ToolTip.visible: hovered
            ToolTip.delay: 600
            ToolTip.text: root.libraries.length > 1
                ? "Choose which folder the conversation starts in"
                : "The conversation starts in " + root.libraryName
            onClicked: libraryMenu.popup(libraryButton, 0, libraryButton.height + 6)

            contentItem: Row {
                spacing: 5

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.libraryName.length > 0 ? root.libraryName : "Folder"
                    color: libraryButton.hovered ? root.theme.appText : root.theme.mutedText
                    font.family: root.theme.bodyFontFamily
                    font.pixelSize: root.theme.textControlSize
                }

                AppIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: root.libraries.length > 1
                    name: "chevron-down"
                    tone: libraryButton.hovered ? "normal" : "muted"
                    iconSize: 10
                }
            }

            background: Rectangle {
                radius: height / 2
                color: libraryButton.hovered && libraryButton.enabled ? root.theme.hoverBg : "transparent"
                border.width: libraryButton.activeFocus ? 1 : 0
                border.color: root.theme.accent
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

        Button {
            id: sendButton

            Layout.preferredWidth: 36
            Layout.preferredHeight: 36
            Layout.alignment: Qt.AlignBottom
            enabled: root.canSend
            hoverEnabled: true
            focusPolicy: Qt.StrongFocus
            padding: 0
            Accessible.name: root.sending ? "Sending" : "Send"
            ToolTip.visible: hovered
            ToolTip.delay: 600
            ToolTip.text: "Send (Enter). Shift+Enter adds a line."
            onClicked: root.submit()

            contentItem: Item {
                AppIcon {
                    anchors.centerIn: parent
                    name: "send"
                    tone: sendButton.enabled ? "normal" : "muted"
                    iconSize: 15
                }
            }

            background: Rectangle {
                radius: height / 2
                color: sendButton.enabled
                    ? (sendButton.down ? Qt.darker(root.theme.accent, 1.15) : sendButton.hovered ? root.theme.accentBright : root.theme.accent)
                    : root.theme.hoverBg

                Behavior on color {
                    enabled: !root.theme.reducedMotion
                    ColorAnimation { duration: root.theme.motionFast }
                }
            }
        }
    }
}

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Archivist.Services 1.0
import "../../Icons"

// Home's composer. Sending starts a new conversation in the chosen library.
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
    // Why sending isn't possible right now; empty when it is.
    property string blockedReason: ""
    property bool sending: false

    readonly property alias input: field
    readonly property bool canSend: blockedReason.length === 0
        && !sending
        && field.text.trim().length > 0

    signal submitRequested(string text)
    signal libraryChosen(string libraryId)

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

    implicitHeight: column.implicitHeight
    radius: root.theme.radiusLarge
    color: field.activeFocus ? root.theme.composerCardFocusBg : root.theme.composerCardBg
    border.width: 1
    border.color: field.activeFocus ? root.theme.accent : root.theme.panelBorder

    Behavior on border.color {
        enabled: !root.theme.reducedMotion
        ColorAnimation { duration: root.theme.motionFast }
    }

    ColumnLayout {
        id: column

        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 0

        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(220, Math.max(76, field.implicitHeight))
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
                placeholderText: root.blockedReason.length > 0
                    ? root.blockedReason
                    : "Ask anything, or describe what you're working on…"
                placeholderTextColor: root.theme.composerPlaceholder
                color: root.theme.appText
                selectionColor: root.theme.messageSelectionBg
                selectedTextColor: root.theme.messageSelectionText
                font.family: root.theme.chatFontFamily
                font.pixelSize: root.theme.textBodySize
                wrapMode: TextEdit.Wrap
                leftPadding: 18
                rightPadding: 18
                topPadding: 16
                bottomPadding: 10
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

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 10
            Layout.bottomMargin: 10
            Layout.preferredHeight: 34
            spacing: 8

            Button {
                id: libraryButton

                visible: root.libraries.length > 0
                enabled: root.libraries.length > 1 && !root.sending
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                leftPadding: 8
                rightPadding: 10
                implicitHeight: 26
                Accessible.name: "Start the conversation in " + root.libraryName
                ToolTip.visible: hovered && root.libraries.length > 1
                ToolTip.delay: 600
                ToolTip.text: "Choose where the conversation starts"
                onClicked: libraryMenu.popup(libraryButton, 0, libraryButton.height + 4)

                contentItem: Row {
                    spacing: 6

                    AppIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "library"
                        tone: libraryButton.hovered ? "accent" : "muted"
                        iconSize: 12
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.libraryName.length > 0 ? root.libraryName : "Library"
                        color: libraryButton.hovered ? root.theme.appText : root.theme.mutedText
                        font.family: root.theme.bodyFontFamily
                        font.pixelSize: root.theme.textCaptionSize
                        font.weight: root.theme.textWeightEmphasis
                    }

                    AppIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: root.libraries.length > 1
                        name: "chevron-down"
                        tone: "muted"
                        iconSize: 9
                    }
                }

                background: Rectangle {
                    radius: height / 2
                    color: libraryButton.hovered ? root.theme.hoverBg : "transparent"
                    border.width: 1
                    border.color: libraryButton.activeFocus ? root.theme.accent : root.theme.quietBorder
                }
            }

            Menu {
                id: libraryMenu

                Repeater {
                    model: root.libraries

                    delegate: MenuItem {
                        required property var modelData

                        text: String(modelData.name || "Library")
                        checkable: true
                        checked: String(modelData.id) === root.libraryId
                        onTriggered: root.libraryChosen(String(modelData.id))
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                text: root.sending ? "Starting the conversation…" : "Enter to send, Shift+Enter for a new line"
                color: root.theme.mutedText
                opacity: field.activeFocus || root.sending ? 0.9 : 0
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.textCaptionSize
                elide: Text.ElideRight

                Behavior on opacity {
                    enabled: !root.theme.reducedMotion
                    NumberAnimation { duration: root.theme.motionFast }
                }
            }

            Button {
                id: sendButton

                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                enabled: root.canSend
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                padding: 0
                Accessible.name: "Send"
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: "Send"
                onClicked: root.submit()

                contentItem: Item {
                    AppIcon {
                        anchors.centerIn: parent
                        name: "send"
                        tone: sendButton.enabled ? "normal" : "muted"
                        iconSize: 14
                    }
                }

                background: Rectangle {
                    radius: height / 2
                    color: sendButton.enabled
                        ? (sendButton.down ? root.theme.accent : root.theme.activeBg)
                        : "transparent"
                    border.width: 1
                    border.color: sendButton.enabled
                        ? (sendButton.hovered || sendButton.activeFocus ? root.theme.accentBright : root.theme.accent)
                        : root.theme.quietBorder
                }
            }
        }
    }
}

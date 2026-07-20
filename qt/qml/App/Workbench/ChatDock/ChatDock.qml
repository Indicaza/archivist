import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property var theme
    property bool attached: true
    property string activePanel: "none"

    signal dockModeToggleRequested()
    signal messageSubmitted(string message)

    function submitDraft() {
        var trimmed = composer.text.trim()

        if (trimmed.length === 0) {
            return
        }

        root.messageSubmitted(trimmed)
        composer.clear()
        composer.forceActiveFocus()
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
                        text: "Context Compiler Test 1"
                        color: root.theme.appText
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }

                    Row {
                        spacing: 8

                        Text {
                            text: "▣  LIBRARY  Archivist"
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
                            text: "♙  AGENT  Grumpy"
                            color: root.theme.mutedText
                            font.pixelSize: 8
                            font.letterSpacing: 0.25
                        }
                    }
                }

                Button {
                    Layout.preferredWidth: 31
                    Layout.preferredHeight: 31
                    text: "✎"
                    hoverEnabled: true
                    padding: 0
                    ToolTip.visible: hovered
                    ToolTip.text: "Manage Chat"

                    contentItem: Text {
                        text: parent.text
                        color: parent.hovered ? root.theme.appText : root.theme.mutedText
                        font.pixelSize: 12
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: 4
                        color: parent.hovered ? root.theme.hoverBg : "transparent"
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
                        onClicked: root.activePanel = root.activePanel === modelData ? "none" : modelData

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
                                height: 2
                                visible: root.activePanel === modelData
                                color: root.theme.accent
                                opacity: 0.72
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
                        placeholderText: "Message Context Compiler Test 1..."
                        placeholderTextColor: root.theme.composerPlaceholder
                        color: root.theme.appText
                        selectionColor: root.theme.accent
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
                                text: "Enter to send  ·  Shift+Enter for newline"
                                color: root.theme.mutedText
                                font.pixelSize: 8
                                opacity: 0.72
                                elide: Text.ElideRight
                            }

                            Button {
                                Layout.preferredWidth: 68
                                Layout.preferredHeight: 28
                                text: "➤  Send"
                                enabled: composer.text.trim().length > 0
                                hoverEnabled: true
                                padding: 0
                                onClicked: root.submitDraft()

                                contentItem: Text {
                                    text: parent.text
                                    color: parent.enabled ? root.theme.appText : root.theme.mutedText
                                    font.pixelSize: 9
                                    font.weight: Font.Bold
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }

                                background: Rectangle {
                                    radius: 4
                                    color: parent.enabled
                                        ? parent.hovered
                                            ? "#332d45"
                                            : "#292438"
                                        : "#201d24"
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
                    : Math.min(220, root.width * 0.28)
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

                Column {
                    anchors.fill: parent
                    anchors.leftMargin: 11
                    anchors.rightMargin: 8
                    anchors.topMargin: 10
                    anchors.bottomMargin: 10
                    spacing: 6

                    Text {
                        text: root.activePanel === "chats" ? "CHATS" : "AGENTS"
                        color: root.theme.mutedText
                        font.pixelSize: 9
                        font.weight: Font.Bold
                        font.letterSpacing: 0.7
                    }

                    Repeater {
                        model: root.activePanel === "chats"
                            ? [
                                "Context Compiler Test 1",
                                "Forever Chat Architecture",
                                "Library Retrieval"
                            ]
                            : [
                                "Grumpy",
                                "Archivist",
                                "Context Curator"
                            ]

                        delegate: Rectangle {
                            id: panelItem

                            required property string modelData
                            required property int index

                            width: parent.width
                            height: 30
                            radius: 0
                            color: panelTap.pressed
                                ? "#292621"
                                : panelHover.hovered
                                    ? root.theme.hoverBg
                                    : index === 0
                                        ? "#211f1c"
                                        : "transparent"

                            Rectangle {
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                width: 2
                                visible: index === 0
                                color: root.theme.accent
                                opacity: 0.65
                            }

                            Text {
                                anchors.fill: parent
                                anchors.leftMargin: 9
                                anchors.rightMargin: 6
                                text: panelItem.modelData
                                color: root.theme.appText
                                font.pixelSize: 9
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }

                            HoverHandler {
                                id: panelHover
                            }

                            TapHandler {
                                id: panelTap
                            }
                        }
                    }
                }
            }
        }
    }
}

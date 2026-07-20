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

    color: theme.surfaceBg
    border.width: 1
    border.color: theme.quietBorder
    clip: true

    function submitDraft() {
        var value = composer.text.trim()

        if (value.length === 0) {
            return
        }

        root.messageSubmitted(value)
        composer.clear()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: root.theme.chatDockHeaderHeight
            color: "#1a1916"

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
                        text: "Qt Migration Workshop"
                        color: root.theme.appText
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }

                    Text {
                        width: parent.width
                        text: "LIBRARY  Archivist    AGENT  Archivist"
                        color: root.theme.mutedText
                        font.pixelSize: 8
                        font.letterSpacing: 0.35
                        elide: Text.ElideRight
                    }
                }

                Button {
                    Layout.preferredWidth: 31
                    Layout.preferredHeight: 31
                    text: root.attached ? "↔" : "↙"
                    hoverEnabled: true
                    padding: 0
                    ToolTip.visible: hovered
                    ToolTip.text: root.attached ? "Center Chat Dock" : "Attach Chat Dock"
                    onClicked: root.dockModeToggleRequested()

                    contentItem: Text {
                        text: parent.text
                        color: parent.hovered ? root.theme.appText : root.theme.mutedText
                        font.pixelSize: 14
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: 5
                        color: parent.hovered ? root.theme.hoverBg : "transparent"
                        border.width: parent.hovered ? 1 : 0
                        border.color: root.theme.panelBorder
                    }
                }

                Repeater {
                    model: ["chats", "agents"]

                    delegate: Button {
                        required property string modelData

                        Layout.preferredWidth: 58
                        Layout.preferredHeight: 31
                        text: modelData === "chats" ? "Chats" : "Agents"
                        hoverEnabled: true
                        padding: 0
                        onClicked: root.activePanel = root.activePanel === modelData ? "none" : modelData

                        contentItem: Text {
                            text: parent.text
                            color: root.activePanel === modelData || parent.hovered ? root.theme.appText : root.theme.mutedText
                            font.pixelSize: 9
                            font.weight: Font.DemiBold
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        background: Rectangle {
                            radius: 5
                            color: root.activePanel === modelData ? root.theme.activeBg : parent.hovered ? root.theme.hoverBg : "transparent"
                            border.width: root.activePanel === modelData || parent.hovered ? 1 : 0
                            border.color: root.activePanel === modelData ? "#554a7b" : root.theme.panelBorder
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
                color: root.theme.composerBg

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    TextArea {
                        id: composer

                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        placeholderText: "Ask Archivist anything..."
                        placeholderTextColor: root.theme.composerPlaceholder
                        color: root.theme.appText
                        selectionColor: root.theme.accent
                        selectedTextColor: "#ffffff"
                        font.pixelSize: 13
                        wrapMode: TextEdit.Wrap
                        leftPadding: 15
                        rightPadding: 15
                        topPadding: 14
                        bottomPadding: 10

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
                            spacing: 7

                            Text {
                                text: "+"
                                color: root.theme.mutedText
                                font.pixelSize: 16
                            }

                            Text {
                                text: "⌘"
                                color: root.theme.mutedText
                                font.pixelSize: 12
                            }

                            Text {
                                Layout.fillWidth: true
                                text: "Native prototype · local UI only"
                                color: root.theme.mutedText
                                font.pixelSize: 8
                                opacity: 0.7
                                elide: Text.ElideRight
                            }

                            Button {
                                Layout.preferredWidth: 68
                                Layout.preferredHeight: 28
                                text: "Send"
                                enabled: composer.text.trim().length > 0
                                hoverEnabled: true
                                onClicked: root.submitDraft()

                                contentItem: Text {
                                    text: parent.text
                                    color: "#ffffff"
                                    font.pixelSize: 9
                                    font.weight: Font.Bold
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }

                                background: Rectangle {
                                    radius: 5
                                    color: parent.enabled ? parent.hovered ? "#7f6bc8" : "#5f4d9b" : "#302b3d"
                                    border.width: 1
                                    border.color: parent.enabled ? "#8f7adf" : "#3a3543"
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: root.activePanel === "none" ? 0 : Math.min(210, root.width * 0.28)
                Layout.fillHeight: true
                visible: root.activePanel !== "none"
                color: root.theme.controlSurfaceBg

                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 1
                    color: root.theme.quietBorder
                }

                Column {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8

                    Text {
                        text: root.activePanel === "chats" ? "CHATS" : "AGENTS"
                        color: root.theme.appText
                        font.pixelSize: 9
                        font.weight: Font.Bold
                        font.letterSpacing: 0.7
                    }

                    Repeater {
                        model: root.activePanel === "chats"
                            ? ["Qt Migration Workshop", "Forever Chat Architecture", "Library Retrieval"]
                            : ["Archivist", "Code Builder", "Context Curator"]

                        delegate: Rectangle {
                            required property string modelData

                            width: parent.width
                            height: 30
                            radius: 5
                            color: index === 0 ? root.theme.activeBg : "#1a1916"
                            border.width: 1
                            border.color: index === 0 ? "#4e446f" : root.theme.quietBorder

                            Text {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                text: modelData
                                color: root.theme.appText
                                font.pixelSize: 9
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }
                        }
                    }
                }

            }
        }
    }
}

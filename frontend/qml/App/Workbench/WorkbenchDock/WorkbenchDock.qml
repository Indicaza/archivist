import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Archivist.Services 1.0
import "../ChatDock"
import "../TerminalDock"

Rectangle {
    id: root

    required property var theme
    property bool attached: true
    property string activeView: "chat"

    signal dockModeToggleRequested()
    signal messageSubmitted(string message)

    color: theme.surfaceBg
    clip: true

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: root.theme.workbenchTabHeight
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
                anchors.leftMargin: 8
                anchors.rightMargin: 5
                spacing: 2

                Repeater {
                    model: [
                        { key: "chat", label: "CHAT" },
                        { key: "terminal", label: "TERMINAL" },
                        { key: "problems", label: "PROBLEMS" }
                    ]

                    delegate: Button {
                        required property var modelData

                        Layout.preferredWidth:
                            tabLabel.implicitWidth + 18
                        Layout.fillHeight: true
                        padding: 0
                        hoverEnabled: true
                        onClicked:
                            root.activeView = modelData.key

                        contentItem: Text {
                            id: tabLabel

                            text: modelData.label
                            color:
                                root.activeView
                                    === modelData.key
                                    ? root.theme.appText
                                    : parent.hovered
                                        ? root.theme.appText
                                        : root.theme.mutedText
                            font.family:
                                root.theme.bodyFontFamily
                            font.pixelSize:
                                root.theme.textMetadataSize
                            font.weight:
                                root.activeView
                                    === modelData.key
                                    ? root.theme
                                        .textWeightStrong
                                    : root.theme
                                        .textWeightEmphasis
                            font.letterSpacing:
                                root.theme.textTrackingCaps
                            horizontalAlignment:
                                Text.AlignHCenter
                            verticalAlignment:
                                Text.AlignVCenter
                        }

                        background: Item {
                            Rectangle {
                                anchors.fill: parent
                                color: parent.hovered
                                    ? root.theme.hoverBg
                                    : "transparent"
                            }

                            Rectangle {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: 1
                                visible:
                                    root.activeView
                                        === modelData.key
                                color: root.theme.accentBright
                            }
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ChatDock {
                anchors.fill: parent
                visible: root.activeView === "chat"
                theme: root.theme
                attached: root.attached
                onDockModeToggleRequested:
                    root.dockModeToggleRequested()
                onMessageSubmitted: function(message) {
                    root.messageSubmitted(message)
                }
            }

            TerminalDock {
                anchors.fill: parent
                visible: root.activeView === "terminal"
                theme: root.theme
                collectionId:
                    CollectionStore.selectedCollectionId
                libraryId:
                    LibraryStore.selectedLibraryId
            }

            Item {
                anchors.fill: parent
                visible: root.activeView === "problems"

                Text {
                    anchors.centerIn: parent
                    text: "No problems yet"
                    color: root.theme.mutedText
                    font.family:
                        root.theme.bodyFontFamily
                    font.pixelSize:
                        root.theme.textControlSize
                }
            }
        }
    }
}

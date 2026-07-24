import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Archivist.Services 1.0
import "CollectionEditor"

Rectangle {
    id: root

    required property var theme

    readonly property var collectionOptions: buildCollectionOptions()

    height: theme.topbarHeight
    color: theme.topbarBg
    border.width: 0
    z: 100

    function buildCollectionOptions() {
        var options = [{
            id: "",
            path: "All Work",
            name: "All Work"
        }]
        var collections = CollectionStore.collections || []

        for (var index = 0; index < collections.length; index += 1) {
            options.push(collections[index])
        }

        return options
    }

    function collectionIndexForId(collectionId) {
        var options = collectionOptions

        for (var index = 0; index < options.length; index += 1) {
            if (String(options[index].id || "") === String(collectionId || "")) {
                return index
            }
        }

        return 0
    }

    Component.onCompleted: CollectionStore.refresh()

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: root.theme.topbarBorder
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 40
        anchors.rightMargin: 39
        spacing: 10

        Text {
            Layout.preferredWidth: 176
            text: "Archivist"
            color: root.theme.appText
            font.family: root.theme.titleFontFamily
            font.pixelSize: root.theme.typeSize(27)
            font.weight: Font.DemiBold
            verticalAlignment: Text.AlignVCenter
        }

        Rectangle {
            Layout.preferredWidth: 1
            Layout.preferredHeight: 24
            color: root.theme.quietBorder
        }

        Text {
            text: "COLLECTION"
            color: root.theme.mutedText
            font.pixelSize: root.theme.typeSize(8)
            font.weight: Font.Bold
            font.letterSpacing: 0.7
        }

        ComboBox {
            id: collectionSelector

            Layout.preferredWidth: Math.min(360, Math.max(220, root.width * 0.28))
            Layout.preferredHeight: 34
            model: root.collectionOptions
            textRole: "path"
            valueRole: "id"
            enabled: !CollectionStore.loading && !CollectionStore.mutating
            hoverEnabled: true
            leftPadding: 10
            rightPadding: 28
            font.pixelSize: root.theme.typeSize(11)

            Binding {
                target: collectionSelector
                property: "currentIndex"
                value: root.collectionIndexForId(
                    CollectionStore.selectedCollectionId
                )
            }

            onActivated: function(index) {
                var option = root.collectionOptions[index]
                CollectionStore.selectCollection(
                    option ? String(option.id || "") : ""
                )
            }

            contentItem: Text {
                text: collectionSelector.displayText.length > 0
                    ? collectionSelector.displayText
                    : CollectionStore.loading
                        ? "Loading Collections…"
                        : "All Work"
                color: root.theme.appText
                font.pixelSize: root.theme.typeSize(11)
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideMiddle
            }

            indicator: Text {
                x: parent.width - width - 9
                y: (parent.height - height) / 2
                text: "⌄"
                color: root.theme.mutedText
                font.pixelSize: root.theme.typeSize(11)
            }

            background: Rectangle {
                radius: 6
                color: parent.hovered
                    ? root.theme.hoverBg
                    : root.theme.controlSurfaceBg
                border.width: parent.popup.visible ? 1 : 0
                border.color: root.theme.panelBorder
            }

            popup: Popup {
                y: collectionSelector.height + 4
                width: collectionSelector.width
                implicitHeight: Math.min(contentItem.implicitHeight + 8, 320)
                padding: 4

                contentItem: ListView {
                    clip: true
                    implicitHeight: contentHeight
                    model: collectionSelector.popup.visible
                        ? collectionSelector.delegateModel
                        : null
                    currentIndex: collectionSelector.highlightedIndex
                    ScrollIndicator.vertical: ScrollIndicator {}
                }

                background: Rectangle {
                    radius: 7
                    color: root.theme.controlSurfaceBg
                    border.width: 1
                    border.color: root.theme.panelBorder
                }
            }

            delegate: ItemDelegate {
                required property int index
                required property var modelData

                width: collectionSelector.width - 8
                height: 38
                highlighted: collectionSelector.highlightedIndex === index

                contentItem: Column {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 1

                    Text {
                        width: parent.width
                        text: String(modelData.path || modelData.name || "All Work")
                        color: root.theme.appText
                        font.pixelSize: root.theme.typeSize(10)
                        font.weight: Font.DemiBold
                        elide: Text.ElideMiddle
                    }

                    Text {
                        width: parent.width
                        visible: String(modelData.id || "").length > 0
                        text: String((modelData.libraryIds || []).length)
                            + " Libraries  ·  "
                            + String((modelData.chatIds || []).length)
                            + " pinned Chats"
                        color: root.theme.mutedText
                        font.pixelSize: root.theme.typeSize(8)
                        elide: Text.ElideRight
                    }
                }

                background: Rectangle {
                    radius: 4
                    color: parent.highlighted
                        ? root.theme.hoverBg
                        : "transparent"
                }
            }
        }

        Button {
            Layout.preferredWidth: 34
            Layout.preferredHeight: 34
            text: "+"
            enabled: !CollectionStore.mutating
            hoverEnabled: true
            padding: 0
            ToolTip.visible: hovered
            ToolTip.text: "Create Collection"
            onClicked: collectionEditor.openForCreate()

            contentItem: Text {
                text: parent.text
                color: parent.hovered
                    ? root.theme.appText
                    : root.theme.accentBright
                font.pixelSize: root.theme.typeSize(16)
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }

            background: Rectangle {
                color: parent.hovered ? root.theme.hoverBg : "transparent"
                border.width: 1
                border.color: root.theme.quietBorder
                radius: 6
            }
        }

        Button {
            Layout.preferredWidth: 34
            Layout.preferredHeight: 34
            text: "•••"
            enabled: CollectionStore.selectedCollectionId.length > 0
                && !CollectionStore.mutating
            hoverEnabled: true
            padding: 0
            ToolTip.visible: hovered
            ToolTip.text: "Manage Collection"
            onClicked: collectionEditor.openForEdit(
                CollectionStore.selectedCollection
            )

            contentItem: Text {
                text: parent.text
                color: parent.enabled && parent.hovered
                    ? root.theme.appText
                    : root.theme.mutedText
                font.pixelSize: root.theme.typeSize(10)
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }

            background: Rectangle {
                color: parent.hovered ? root.theme.hoverBg : "transparent"
                border.width: parent.hovered ? 1 : 0
                border.color: root.theme.quietBorder
                radius: 6
            }
        }

        Item {
            Layout.fillWidth: true
        }

        Text {
            visible: CollectionStore.errorMessage.length > 0
            Layout.maximumWidth: 240
            text: CollectionStore.errorMessage
            color: root.theme.danger
            font.pixelSize: root.theme.typeSize(8)
            elide: Text.ElideRight
        }

        Rectangle {
            Layout.preferredWidth: 35
            Layout.preferredHeight: 35
            radius: width / 2
            border.width: 1
            border.color: "#3c3656"
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#9d8ae8" }
                GradientStop { position: 0.45; color: "#6f70ca" }
                GradientStop { position: 1.0; color: "#2b6f69" }
            }

            Rectangle {
                x: 8
                y: 5
                width: 9
                height: 6
                radius: 4
                color: "#70ffffff"
                rotation: -18
            }

            HoverHandler {
                id: avatarHover
            }

            scale: avatarHover.hovered ? 1.08 : 1.0

            Behavior on scale {
                NumberAnimation {
                    duration: 180
                    easing.type: Easing.OutCubic
                }
            }
        }
    }

    CollectionEditor {
        id: collectionEditor

        theme: root.theme
    }
}

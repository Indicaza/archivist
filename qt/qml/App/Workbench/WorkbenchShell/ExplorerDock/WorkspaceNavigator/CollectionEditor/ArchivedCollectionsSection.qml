import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Archivist.Services 1.0

ColumnLayout {
    id: root

    required property var theme
    required property bool busy
    property bool expanded: false

    Layout.fillWidth: true
    Layout.leftMargin: 20
    Layout.rightMargin: 20
    spacing: 4

    Button {
        Layout.fillWidth: true
        Layout.preferredHeight: 34
        text: (root.expanded ? "▾" : "▸")
            + "  Archived Collections  "
            + String(CollectionStore.archivedCollections.length)
        enabled: !root.busy
        hoverEnabled: true
        onClicked: {
            root.expanded = !root.expanded
            if (root.expanded) {
                CollectionStore.refreshArchived()
            }
        }

        background: Rectangle {
            color: parent.hovered
                ? root.theme.hoverBg
                : root.theme.controlSurfaceBg
            border.width: 1
            border.color: root.theme.quietBorder
            radius: 5
        }
    }

    ListView {
        Layout.fillWidth: true
        Layout.preferredHeight: root.expanded ? Math.min(contentHeight, 150) : 0
        visible: root.expanded
        clip: true
        spacing: 3
        model: CollectionStore.archivedCollections

        delegate: Rectangle {
            required property var modelData

            width: ListView.view.width
            height: 38
            color: root.theme.controlSurfaceBg
            border.width: 1
            border.color: root.theme.quietBorder
            radius: 4

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.right: restoreButton.left
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                text: String(modelData.path || modelData.name || "Collection")
                color: root.theme.mutedText
                font.pixelSize: root.theme.typeSize(10)
                elide: Text.ElideRight
            }

            Button {
                id: restoreButton

                anchors.right: parent.right
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                width: 64
                height: 28
                text: "Restore"
                enabled: !root.busy
                onClicked: CollectionStore.restoreCollection(
                    String(modelData.id)
                )
            }
        }
    }
}

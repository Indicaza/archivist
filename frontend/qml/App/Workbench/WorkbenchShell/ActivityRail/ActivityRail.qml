import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Archivist.Services 1.0
import "RailButton"

// The left-edge rail. Its items and what a click does come from Shell
// (Core/Domains/Shell): Home first, then the side panels, one list.
Rectangle {
    id: root

    required property var theme

    property int hoveredButtonIndex: -1

    width: theme.activityRailWidth
    color: theme.railBg
    border.width: 0

    function itemActive(item) {
        return item.isPanel
            ? Shell.panelShown && Shell.panelId === item.id
            : Shell.homeActive
    }

    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: root.theme.quietBorder
    }

    ColumnLayout {
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: 7
        spacing: 4

        Repeater {
            model: Shell.railItems

            delegate: RailButton {
                id: railButton

                required property int index
                required property var modelData

                Layout.alignment: Qt.AlignHCenter
                theme: root.theme
                iconName: String(railButton.modelData.icon)
                label: String(railButton.modelData.label)
                active: root.itemActive(railButton.modelData)
                neighborHovered: root.hoveredButtonIndex >= 0
                    && Math.abs(root.hoveredButtonIndex - railButton.index) === 1
                Accessible.description: String(railButton.modelData.description || "")
                onHoveredChanged: {
                    if (hovered) {
                        root.hoveredButtonIndex = railButton.index
                    } else if (root.hoveredButtonIndex === railButton.index) {
                        root.hoveredButtonIndex = -1
                    }
                }
                onClicked: Shell.activate(String(railButton.modelData.id))
            }
        }
    }
}

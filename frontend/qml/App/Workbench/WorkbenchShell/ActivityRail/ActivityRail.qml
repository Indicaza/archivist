import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Archivist.Services 1.0
import "RailButton"

// The left-edge rail. Its items and what a click does come from Shell
// (Core/Domains/Shell): Home first, then the side panels.
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

            delegate: ColumnLayout {
                id: railEntry

                required property int index
                required property var modelData

                Layout.alignment: Qt.AlignHCenter
                spacing: 4

                RailButton {
                    Layout.alignment: Qt.AlignHCenter
                    theme: root.theme
                    iconName: String(railEntry.modelData.icon)
                    label: String(railEntry.modelData.label)
                    active: root.itemActive(railEntry.modelData)
                    neighborHovered: root.hoveredButtonIndex >= 0
                        && Math.abs(root.hoveredButtonIndex - railEntry.index) === 1
                    Accessible.description: String(railEntry.modelData.description || "")
                    onHoveredChanged: {
                        if (hovered) {
                            root.hoveredButtonIndex = railEntry.index
                        } else if (root.hoveredButtonIndex === railEntry.index) {
                            root.hoveredButtonIndex = -1
                        }
                    }
                    onClicked: Shell.activate(String(railEntry.modelData.id))
                }

                // Home stands apart from the panels.
                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 18
                    Layout.preferredHeight: 1
                    Layout.topMargin: 2
                    Layout.bottomMargin: 2
                    visible: !railEntry.modelData.isPanel
                    color: root.theme.quietBorder
                }
            }
        }
    }
}

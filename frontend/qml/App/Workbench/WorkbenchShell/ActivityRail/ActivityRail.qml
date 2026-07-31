import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "RailButton"

Rectangle {
    id: root

    required property var theme
    required property int activeViewIndex
    required property bool panelOpen

    signal viewRequested(int index)

    property int hoveredButtonIndex: -1

    width: theme.activityRailWidth
    color: theme.railBg
    border.width: 0

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
            model: ListModel {
                ListElement { iconNameValue: "workspace"; labelValue: "Workspace Navigator" }
                ListElement { iconNameValue: "archive"; labelValue: "Archived Libraries" }
                ListElement { iconNameValue: "search"; labelValue: "Library Search" }
                ListElement { iconNameValue: "plugin"; labelValue: "Plugins" }
                ListElement { iconNameValue: "tools"; labelValue: "Tools" }
            }

            delegate: RailButton {
                required property int index
                required property string iconNameValue
                required property string labelValue

                theme: root.theme
                iconName: iconNameValue
                label: labelValue
                active: root.panelOpen && root.activeViewIndex === index
                neighborHovered: root.hoveredButtonIndex >= 0
                    && Math.abs(root.hoveredButtonIndex - index) === 1
                onHoveredChanged: {
                    if (hovered) {
                        root.hoveredButtonIndex = index
                    } else if (root.hoveredButtonIndex === index) {
                        root.hoveredButtonIndex = -1
                    }
                }
                onClicked: root.viewRequested(index)
            }
        }
    }
}

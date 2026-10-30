import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Archivist.Services 1.0
import "RailButton"
import "../../Dock"

// The left-edge rail. Its items and what a click does come from Shell
// (Core/Domains/Shell): Home first, then the side panels, one list. The
// person's own tabs come last; + adds one, right-click renames or deletes
// it. Tabs and panels dock into a tab by dragging them onto its panel.
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

                objectName: "rail." + String(railButton.modelData.id)
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

                TapHandler {
                    acceptedButtons: Qt.RightButton
                    enabled: railButton.modelData.custom === true
                    onTapped: root.openTabMenu(railButton, String(railButton.modelData.id), String(railButton.modelData.label))
                }
            }
        }

        RailButton {
            id: addButton

            objectName: "rail.addTab"
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 4
            theme: root.theme
            iconName: "add"
            label: "New sidebar tab"
            active: false
            neighborHovered: false
            Accessible.description: "Adds an empty tab to dock tabs and panels into"
            onClicked: Shell.addRailTab("")
        }
    }

    // ---- a person's own tabs -------------------------------------------

    property string menuTabId: ""
    property string menuTabLabel: ""

    function openTabMenu(button, id, label) {
        root.menuTabId = id
        root.menuTabLabel = label
        var point = button.mapToItem(root, button.width, 0)
        tabMenu.x = point.x + 4
        tabMenu.y = point.y
        tabMenu.open()
    }

    function startRename() {
        renameField.text = root.menuTabLabel
        renamePopup.y = tabMenu.y
        renamePopup.open()
        renameField.selectAll()
        renameField.forceActiveFocus()
    }

    // Deleting a tab sends what is docked in it back to the main layout;
    // nothing is closed.
    function deleteTab(id) {
        var docked = Docking.sidebarSurface(id)
        if (docked.length > 0) {
            Docking.closeSurface(docked)
        }
        Shell.removeRailTab(id)
    }

    DockMenu {
        id: tabMenu

        theme: root.theme

        DockMenuItem {
            theme: root.theme
            text: "Rename Tab"
            onTriggered: root.startRename()
        }
        DockMenuItem {
            objectName: "rail.deleteTab"
            theme: root.theme
            text: "Delete Tab"
            onTriggered: root.deleteTab(root.menuTabId)
        }
    }

    Popup {
        id: renamePopup

        x: root.width + 4
        width: 220
        padding: 8
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            radius: root.theme.radiusSmall
            color: root.theme.controlSurfaceBg
            border.width: 1
            border.color: root.theme.panelBorder
        }

        contentItem: TextField {
            id: renameField

            objectName: "rail.renameField"
            placeholderText: "Tab name"
            color: root.theme.appText
            placeholderTextColor: root.theme.mutedText
            selectByMouse: true
            onAccepted: {
                if (text.trim().length > 0) {
                    Shell.renameRailTab(root.menuTabId, text)
                }
                renamePopup.close()
            }

            background: Rectangle {
                radius: 4
                color: root.theme.surfaceBg
                border.width: 1
                border.color: renameField.activeFocus ? root.theme.accent : root.theme.quietBorder
            }
        }
    }
}

import QtQuick
import QtQuick.Controls

// Themed context menu for docking surfaces.
Menu {
    id: root

    required property var theme

    width: 236
    padding: 5
    modal: false
    dim: false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: root.theme.reducedMotion ? 0 : root.theme.motionFast; easing.type: Easing.OutCubic }
            NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: root.theme.reducedMotion ? 0 : root.theme.motionFast; easing.type: Easing.OutCubic }
        }
    }

    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: root.theme.reducedMotion ? 0 : 90 }
    }

    background: Rectangle {
        implicitWidth: 236
        color: root.theme.surfaceBg
        border.width: 1
        border.color: root.theme.panelBorder
        radius: root.theme.radiusSmall + 2

        Rectangle {
            z: -1
            anchors.fill: parent
            anchors.topMargin: 6
            anchors.bottomMargin: -8
            anchors.leftMargin: 2
            anchors.rightMargin: -2
            radius: parent.radius + 2
            color: "#000000"
            opacity: 0.3
        }
    }

    delegate: DockMenuItem {
        theme: root.theme
    }
}

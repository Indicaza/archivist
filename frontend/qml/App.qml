import QtQuick
import QtQuick.Controls
import Archivist.Services 1.0
import "App/Theme"
import "App/Topbar"
import "App/Workbench/WorkbenchShell"

ApplicationWindow {
    id: window

    // Logical sizes shrink as the interface grows (Cmd +), so the window
    // opens, and may be resized, to the same size on screen at any zoom.
    width: Math.round(1280 / InterfaceScale.startupScale)
    height: Math.round(840 / InterfaceScale.startupScale)
    minimumWidth: Math.round(Math.max(600, 960 / InterfaceScale.scale))
    minimumHeight: Math.round(Math.max(420, 640 / InterfaceScale.scale))
    visible: true
    title: "Archivist"
    color: theme.appBg

    Theme {
        id: theme
    }

    Topbar {
        id: topbar

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        theme: theme
    }

    WorkbenchShell {
        anchors.top: topbar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        theme: theme
    }
}

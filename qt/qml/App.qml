import QtQuick
import QtQuick.Controls
import "App/Theme"
import "App/Topbar"
import "App/Workbench/WorkbenchShell"

ApplicationWindow {
    id: window

    width: 1280
    height: 840
    minimumWidth: 960
    minimumHeight: 640
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

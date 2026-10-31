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
    flags: ArchivistDiagnostics.uiLabBackground === true ? Qt.Window | Qt.WindowDoesNotAcceptFocus : Qt.Window
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
        id: shell

        anchors.top: topbar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        theme: theme
    }

    // The Observatory UI Lab's scenario driver; only in a test profile
    // started with ARCHIVIST_UI_LAB_SCENARIO (see src/App/Services/ui_lab.h).
    Loader {
        active: ArchivistDiagnostics.uiLab === true
        source: "App/UiLab/UiLabDriver.qml"
        onLoaded: item.start(window, shell)
    }
}

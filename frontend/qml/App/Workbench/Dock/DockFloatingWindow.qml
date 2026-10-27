import QtQuick
import QtQuick.Window
import Archivist.Services 1.0

// A floating window: one more surface of the docking workspace, with its
// own panels and splits. Closing it sends its tabs back to the main window;
// nothing is closed. Where it sits (and on which monitor) is remembered.
Window {
    id: root

    required property var theme
    required property var workspace // DockWorkspace
    property string surfaceId: ""

    readonly property var info: {
        Docking.revision
        return Docking.surface(root.surfaceId)
    }
    readonly property string activeTitle: {
        Docking.revision
        var focused = root.workspace.anchorItems[Docking.focusedAnchorId]
        var anchorId = focused && focused.surfaceId === root.surfaceId ? Docking.focusedAnchorId : root.lastAnchorId
        var anchor = anchorId.length > 0 ? Docking.anchor(anchorId) : ({})
        var view = anchor.activeViewId ? Docking.view(anchor.activeViewId) : ({})
        return view.viewId ? root.workspace.viewTitle(view) : ""
    }
    // The panel last focused here, focused again when the window is.
    property string lastAnchorId: ""
    property bool placed: false

    title: {
        var count = Number(info.viewCount || 0)
        var extra = count > 1 ? "  +" + (count - 1) : ""
        return (activeTitle.length > 0 ? activeTitle + extra + " — " : "") + "Archivist"
    }
    color: theme.workspaceBgDeep
    minimumWidth: 320
    minimumHeight: 220
    flags: Qt.Window

    function place() {
        var frame = Docking.surface(root.surfaceId)
        if (frame.hasGeometry) {
            root.x = Math.round(frame.x)
            root.y = Math.round(frame.y)
            root.width = Math.max(root.minimumWidth, Math.round(frame.width))
            root.height = Math.max(root.minimumHeight, Math.round(frame.height))
        } else {
            root.width = 720
            root.height = 480
        }
        if (frame.windowMaximized) {
            root.showMaximized()
        } else {
            root.show()
        }
        root.placed = true
    }

    function remember() {
        if (root.placed && root.surfaceId.length > 0 && root.visible) {
            geometrySettle.restart()
        }
    }

    Component.onCompleted: {
        place()
        root.requestActivate()
    }

    onXChanged: remember()
    onYChanged: remember()
    onWidthChanged: remember()
    onHeightChanged: remember()
    onVisibilityChanged: remember()

    Timer {
        id: geometrySettle

        interval: 350
        onTriggered: Docking.setSurfaceGeometry(
            root.surfaceId,
            root.x,
            root.y,
            root.width,
            root.height,
            root.screen ? root.screen.name : "",
            root.visibility === Window.Maximized
        )
    }

    onClosing: function(close) {
        // The surface goes away and the Instantiator destroys this window.
        close.accepted = false
        root.workspace.closeFloatingWindow(root.surfaceId)
    }

    // Clicking into this window focuses one of its panels, so Ctrl+W and
    // the status bar follow the window you are working in.
    onActiveChanged: {
        if (!active || Docking.dragActive) {
            return
        }
        var focused = root.workspace.anchorItems[Docking.focusedAnchorId]
        if (focused && focused.surfaceId === root.surfaceId) {
            return
        }
        var preferred = root.workspace.anchorItems[root.lastAnchorId]
        if (preferred && preferred.surfaceId === root.surfaceId) {
            Docking.focusAnchor(root.lastAnchorId)
            return
        }
        for (var key in root.workspace.anchorItems) {
            if (root.workspace.anchorItems[key].surfaceId === root.surfaceId) {
                Docking.focusAnchor(key)
                return
            }
        }
    }

    Connections {
        target: Docking

        function onSurfaceRecovered(surfaceId) {
            if (surfaceId === root.surfaceId) {
                root.placed = false
                root.place()
            }
        }

        function onRevisionChanged() {
            var focused = root.workspace.anchorItems[Docking.focusedAnchorId]
            if (focused && focused.surfaceId === root.surfaceId) {
                root.lastAnchorId = Docking.focusedAnchorId
                // Keyboard focus moved here from another window.
                if (!root.active && !Docking.dragActive && root.workspace.followFocusAcrossWindows) {
                    root.raise()
                    root.requestActivate()
                }
            }
        }
    }

    DockSurfaceArea {
        id: area

        anchors.fill: parent
        theme: root.theme
        workspace: root.workspace
        surfaceId: root.surfaceId
    }
}

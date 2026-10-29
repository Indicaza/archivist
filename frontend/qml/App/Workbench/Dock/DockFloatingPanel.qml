import QtQuick
import Archivist.Services 1.0
import "../../Icons"

// A floating panel inside the main window: one more surface of the docking
// workspace, with its own tabs and splits, drawn over the main layout.
// Drag it by its bar to move it (hold it at a dock zone to dock it), drag
// an edge or corner to resize it. Closing it sends its tabs back to the
// main layout; nothing is closed.
Item {
    id: root

    required property var theme
    required property var workspace // DockWorkspace
    property string surfaceId: ""

    readonly property var info: {
        Docking.revision
        return Docking.surface(root.surfaceId)
    }
    readonly property real barHeight: 26
    readonly property real minimumWidth: 280
    readonly property real minimumHeight: 180

    // While moving or resizing the panel follows the pointer; otherwise
    // the stored frame, kept reachable when the window is smaller.
    property bool interacting: false
    property rect live: Qt.rect(0, 0, 0, 0)
    readonly property rect stored: {
        var width = Math.max(root.minimumWidth, Number(info.width || 520))
        var height = Math.max(root.minimumHeight, Number(info.height || 360))
        var parentWidth = parent ? parent.width : width
        var parentHeight = parent ? parent.height : height
        width = Math.min(width, Math.max(root.minimumWidth, parentWidth))
        height = Math.min(height, Math.max(root.minimumHeight, parentHeight))
        var x = Math.max(0, Math.min(Number(info.x || 40), parentWidth - Math.min(width, 120)))
        var y = Math.max(0, Math.min(Number(info.y || 40), parentHeight - root.barHeight))
        return Qt.rect(x, y, width, height)
    }
    readonly property rect frame: interacting ? live : stored

    readonly property string activeTitle: {
        Docking.revision
        var focused = root.workspace.anchorItems[Docking.focusedAnchorId]
        var anchorId = focused && focused.surfaceId === root.surfaceId ? Docking.focusedAnchorId : root.firstAnchorId()
        var anchor = anchorId.length > 0 ? Docking.anchor(anchorId) : ({})
        var view = anchor.activeViewId ? Docking.view(anchor.activeViewId) : ({})
        var title = view.viewId ? root.workspace.viewTitle(view) : ""
        var name = String(anchor.name || "")
        return name.length > 0 ? (title.length > 0 ? name + " · " + title : name) : title
    }
    readonly property bool focusedHere: {
        Docking.revision
        var focused = root.workspace.anchorItems[Docking.focusedAnchorId]
        return focused !== undefined && focused !== null && focused.surfaceId === root.surfaceId
    }

    function firstAnchorId() {
        for (var key in root.workspace.anchorItems) {
            if (root.workspace.anchorItems[key].surfaceId === root.surfaceId) {
                return key
            }
        }
        return ""
    }

    function startInteracting() {
        root.live = root.stored
        root.interacting = true
        root.workspace.raisePanel(root.surfaceId)
    }

    function commitFrame() {
        Docking.setSurfaceGeometry(root.surfaceId, root.live.x, root.live.y, root.live.width, root.live.height, "", false)
        root.interacting = false
    }

    x: frame.x
    y: frame.y
    width: frame.width
    height: frame.height
    objectName: "dock.floatingPanel"

    // Lifted off the layout.
    Rectangle {
        anchors.fill: parent
        anchors.topMargin: 8
        anchors.leftMargin: -2
        anchors.rightMargin: -2
        anchors.bottomMargin: -12
        radius: root.theme.radiusPanel + 4
        color: "#000000"
        opacity: root.focusedHere ? 0.36 : 0.24
    }

    Rectangle {
        id: body

        anchors.fill: parent
        radius: root.theme.radiusPanel
        color: root.theme.workspaceBgDeep
        border.width: 1
        border.color: root.focusedHere ? Qt.rgba(root.theme.accentBright.r, root.theme.accentBright.g, root.theme.accentBright.b, 0.6) : root.theme.panelBorder
        clip: true

        // Presses, scrolls and hovers that nothing in the panel takes stop
        // here instead of reaching the layout underneath.
        MouseArea {
            anchors.fill: parent
            z: -1
            hoverEnabled: true
            acceptedButtons: Qt.AllButtons
            onWheel: function(wheel) { wheel.accepted = true }
        }

        // The bar: drag to move, double-click to send its tabs back.
        Rectangle {
            id: bar

            objectName: "dock.floatingPanel.bar"
            width: parent.width
            height: root.barHeight
            color: root.theme.controlSurfaceBg

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.right: closeButton.left
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                text: root.activeTitle.length > 0 ? root.activeTitle : "Floating panel"
                elide: Text.ElideRight
                color: root.focusedHere ? root.theme.appText : root.theme.mutedText
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.textCaptionSize
                font.weight: root.theme.textWeightEmphasis
            }

            DragHandler {
                id: moveHandler

                target: null
                dragThreshold: 2
                property point pressAt: Qt.point(0, 0)
                property rect startFrame: Qt.rect(0, 0, 0, 0)

                onActiveChanged: {
                    if (active) {
                        root.startInteracting()
                        startFrame = root.live
                        root.workspace.beginPanelMove(root.surfaceId)
                    } else if (root.interacting) {
                        root.workspace.endPanelMove(root.surfaceId, root.live)
                        root.interacting = false
                    }
                }
                onTranslationChanged: {
                    if (!active) {
                        return
                    }
                    root.live = Qt.rect(startFrame.x + translation.x, startFrame.y + translation.y, startFrame.width, startFrame.height)
                    root.workspace.movePanel(root.surfaceId, bar.mapToGlobal(centroid.position.x, centroid.position.y), root.live)
                }
            }

            TapHandler {
                onTapped: root.workspace.raisePanel(root.surfaceId)
                onDoubleTapped: root.workspace.closeFloatingWindow(root.surfaceId)
            }

            // Sends the tabs back to the main layout.
            Rectangle {
                id: closeButton

                objectName: "dock.floatingPanel.close"
                anchors.right: parent.right
                anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                width: 20
                height: 20
                radius: 4
                color: closeArea.containsMouse ? root.theme.panelBorder : "transparent"

                AppIcon {
                    anchors.centerIn: parent
                    name: "close"
                    tone: "muted"
                    iconSize: 12
                }

                MouseArea {
                    id: closeArea

                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.workspace.closeFloatingWindow(root.surfaceId)
                }
            }
        }

        DockSurfaceArea {
            id: area

            x: 0
            y: root.barHeight
            width: parent.width
            height: Math.max(0, parent.height - root.barHeight)
            theme: root.theme
            workspace: root.workspace
            surfaceId: root.surfaceId
        }
    }

    // A press anywhere brings it to the front (without taking the press).
    MouseArea {
        anchors.fill: parent
        z: 50
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onPressed: function(mouse) {
            root.workspace.raisePanel(root.surfaceId)
            mouse.accepted = false
        }
    }

    // ---- resizing ----------------------------------------------------------

    component Grip: MouseArea {
        id: grip

        // Inline components don't see this file's ids: the panel is passed in.
        required property var panel

        // Which sides this grip moves: -1 the leading side, 1 the trailing.
        property int horizontal: 0
        property int vertical: 0
        property point pressAt: Qt.point(0, 0)
        property rect startFrame: Qt.rect(0, 0, 0, 0)

        z: 60
        hoverEnabled: true
        preventStealing: true
        cursorShape: horizontal !== 0 && vertical !== 0
            ? (horizontal === vertical ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor)
            : horizontal !== 0 ? Qt.SizeHorCursor : Qt.SizeVerCursor

        onPressed: function(mouse) {
            grip.panel.startInteracting()
            startFrame = grip.panel.live
            pressAt = mapToItem(grip.panel.parent, mouse.x, mouse.y)
        }
        onPositionChanged: function(mouse) {
            if (!pressed) {
                return
            }
            var now = mapToItem(grip.panel.parent, mouse.x, mouse.y)
            var dx = now.x - pressAt.x
            var dy = now.y - pressAt.y
            var x = startFrame.x
            var y = startFrame.y
            var width = startFrame.width
            var height = startFrame.height
            if (horizontal > 0) {
                width = Math.max(grip.panel.minimumWidth, startFrame.width + dx)
            } else if (horizontal < 0) {
                width = Math.max(grip.panel.minimumWidth, startFrame.width - dx)
                x = startFrame.x + startFrame.width - width
            }
            if (vertical > 0) {
                height = Math.max(grip.panel.minimumHeight, startFrame.height + dy)
            } else if (vertical < 0) {
                height = Math.max(grip.panel.minimumHeight, startFrame.height - dy)
                y = startFrame.y + startFrame.height - height
            }
            grip.panel.live = Qt.rect(x, Math.max(0, y), width, height)
        }
        onReleased: grip.panel.commitFrame()
        onCanceled: grip.panel.interacting = false
    }

    readonly property real gripSize: 6

    Grip { panel: root; horizontal: -1; x: -root.gripSize / 2; y: root.gripSize; width: root.gripSize; height: parent.height - 2 * root.gripSize }
    Grip { panel: root; horizontal: 1; x: parent.width - root.gripSize / 2; y: root.gripSize; width: root.gripSize; height: parent.height - 2 * root.gripSize }
    Grip { panel: root; vertical: -1; x: root.gripSize; y: -root.gripSize / 2; width: parent.width - 2 * root.gripSize; height: root.gripSize }
    Grip { panel: root; vertical: 1; x: root.gripSize; y: parent.height - root.gripSize / 2; width: parent.width - 2 * root.gripSize; height: root.gripSize }
    Grip { panel: root; horizontal: -1; vertical: -1; x: -root.gripSize; y: -root.gripSize; width: 2 * root.gripSize; height: 2 * root.gripSize }
    Grip { panel: root; horizontal: 1; vertical: -1; x: parent.width - root.gripSize; y: -root.gripSize; width: 2 * root.gripSize; height: 2 * root.gripSize }
    Grip { panel: root; horizontal: -1; vertical: 1; x: -root.gripSize; y: parent.height - root.gripSize; width: 2 * root.gripSize; height: 2 * root.gripSize }
    Grip { panel: root; horizontal: 1; vertical: 1; x: parent.width - root.gripSize; y: parent.height - root.gripSize; width: 2 * root.gripSize; height: 2 * root.gripSize }
}

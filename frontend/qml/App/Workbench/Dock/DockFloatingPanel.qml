import QtQuick
import Archivist.Services 1.0
import "../../Icons"

// A floating panel inside the main window: one more surface of the docking
// workspace, with its own tabs and splits, drawn over the main layout.
// Holding one panel, it has no bar of its own: the panel's header is its
// one handle and close (drag the grip to move it, hold at a dock zone to
// dock it). Holding a split, a slim bar moves the whole group, and its
// close sends the tabs back to the main layout. Drag an edge or corner to
// resize it. It may hang partly off the window.
Item {
    id: root

    required property var theme
    required property var workspace // DockWorkspace
    property string surfaceId: ""

    readonly property var info: {
        Docking.revision
        return Docking.surface(root.surfaceId)
    }
    // Only a group of panels needs a bar to move them together.
    readonly property bool showsBar: Number(info.anchorCount || 0) > 1
    readonly property real barHeight: showsBar ? 30 : 0
    readonly property real minimumWidth: 280
    readonly property real minimumHeight: 180

    // While moving or resizing the panel follows the pointer; otherwise
    // the stored frame, kept reachable when the window is smaller.
    property bool interacting: false
    property bool resizing: false
    property rect live: Qt.rect(0, 0, 0, 0)
    readonly property rect stored: {
        var width = Math.max(root.minimumWidth, Number(info.width || 520))
        var height = Math.max(root.minimumHeight, Number(info.height || 360))
        var parentWidth = parent ? parent.width : width
        var parentHeight = parent ? parent.height : height
        width = Math.min(width, Math.max(root.minimumWidth, parentWidth))
        height = Math.min(height, Math.max(root.minimumHeight, parentHeight))
        // It may hang off the sides and bottom; its header stays in reach.
        var keep = Math.min(width, root.theme.magnetKeepVisible)
        var x = Math.max(keep - width, Math.min(Number(info.x === undefined ? 40 : info.x), parentWidth - keep))
        var y = Math.max(0, Math.min(Number(info.y === undefined ? 40 : info.y), parentHeight - root.theme.magnetKeepHeader))
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
        root.resizing = false
    }

    // Its only panel is carried away as a picture: the whole floating
    // panel lifts out with it (still alive, it holds the pointer).
    readonly property bool lifted: {
        var anchorId = root.workspace.liftedAnchorId || ""
        if (anchorId.length === 0) {
            return false
        }
        var item = root.workspace.anchorItems[anchorId]
        return item !== undefined && item !== null && item.surfaceId === root.surfaceId
            && Number(root.info.anchorCount || 0) === 1
    }

    x: frame.x
    y: frame.y
    width: frame.width
    height: frame.height
    opacity: lifted ? 0 : 1

    // Let go (or a flick carried it on): it springs to its stored frame.
    Behavior on x { enabled: !root.interacting && !root.lifted && !root.theme.reducedMotion; SpringAnimation { spring: root.theme.magnetLandingSpring; damping: root.theme.magnetLandingDamping; epsilon: 0.25 } }
    Behavior on y { enabled: !root.interacting && !root.lifted && !root.theme.reducedMotion; SpringAnimation { spring: root.theme.magnetLandingSpring; damping: root.theme.magnetLandingDamping; epsilon: 0.25 } }

    // Moved by its bar it shrinks around the spot it is held by, so it is
    // easy to aim, and grows back when let go.
    readonly property real moveScale: moveHandler.active && !root.theme.reducedMotion ? root.theme.magnetPanelMoveScale : 1
    property real shownScale: moveScale
    Behavior on shownScale { enabled: !root.theme.reducedMotion; SpringAnimation { spring: root.theme.magnetPanelMoveSpring; damping: root.theme.magnetPanelMoveDamping; epsilon: 0.005 } }
    transform: Scale {
        origin.x: moveHandler.holdAt.x
        origin.y: moveHandler.holdAt.y
        xScale: root.shownScale
        yScale: root.shownScale
    }
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
        opacity: moveHandler.active && !root.theme.reducedMotion ? 0.52 : root.focusedHere ? 0.36 : 0.24
        Behavior on opacity { NumberAnimation { duration: root.theme.reducedMotion ? 0 : root.theme.magnetPickupDuration } }
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
            visible: root.showsBar
            color: root.theme.controlSurfaceBg

            // Grip dots: grab the bar anywhere to move the panel.
            Grid {
                id: barGrip

                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                columns: 2
                rowSpacing: 2.5
                columnSpacing: 2.5
                opacity: moveHandler.active ? 0.95 : root.focusedHere ? 0.65 : 0.45

                Repeater {
                    model: 6
                    Rectangle {
                        width: 3.5
                        height: 3.5
                        radius: 1.75
                        color: moveHandler.active ? root.theme.accentBright : root.theme.mutedText
                    }
                }
            }

            HoverHandler {
                cursorShape: moveHandler.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor
            }

            Text {
                anchors.left: barGrip.right
                anchors.leftMargin: 9
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
                // Once it moves the panel, nothing underneath (a file in the
                // sidebar, a tab) may take the drag over.
                grabPermissions: PointerHandler.CanTakeOverFromItems
                    | PointerHandler.CanTakeOverFromHandlersOfDifferentType
                    | PointerHandler.CanTakeOverFromHandlersOfSameType
                property point pressAt: Qt.point(0, 0)
                property rect startFrame: Qt.rect(0, 0, 0, 0)
                // Where it is held, in the panel (the shrink pivots on it).
                property point holdAt: Qt.point(0, 0)

                onActiveChanged: {
                    if (active) {
                        holdAt = Qt.point(centroid.pressPosition.x, centroid.pressPosition.y)
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
                // Takes the press itself, so a click or drag on the bar never
                // reaches what lies under the panel (the sidebar's files).
                gesturePolicy: TapHandler.WithinBounds
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
            coalesceResize: root.resizing
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
            grip.panel.resizing = true
            grip.panel.startInteracting()
            startFrame = grip.panel.live
            pressAt = mapToGlobal(mouse.x, mouse.y)
        }
        onPositionChanged: function(mouse) {
            if (!pressed) {
                return
            }
            var now = mapToGlobal(mouse.x, mouse.y)
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
        onCanceled: {
            grip.panel.interacting = false
            grip.panel.resizing = false
        }
    }

    readonly property real gripSize: 10

    Grip { panel: root; horizontal: -1; x: -root.gripSize / 2; y: root.gripSize; width: root.gripSize; height: parent.height - 2 * root.gripSize }
    Grip { panel: root; horizontal: 1; x: parent.width - root.gripSize / 2; y: root.gripSize; width: root.gripSize; height: parent.height - 2 * root.gripSize }
    Grip { panel: root; vertical: -1; x: root.gripSize; y: -root.gripSize / 2; width: parent.width - 2 * root.gripSize; height: root.gripSize }
    Grip { panel: root; vertical: 1; x: root.gripSize; y: parent.height - root.gripSize / 2; width: parent.width - 2 * root.gripSize; height: root.gripSize }
    Grip { panel: root; horizontal: -1; vertical: -1; x: -root.gripSize; y: -root.gripSize; width: 2 * root.gripSize; height: 2 * root.gripSize }
    Grip { panel: root; horizontal: 1; vertical: -1; x: parent.width - root.gripSize; y: -root.gripSize; width: 2 * root.gripSize; height: 2 * root.gripSize }
    Grip { panel: root; horizontal: -1; vertical: 1; x: -root.gripSize; y: parent.height - root.gripSize; width: 2 * root.gripSize; height: 2 * root.gripSize }
    Grip { panel: root; horizontal: 1; vertical: 1; x: parent.width - root.gripSize; y: parent.height - root.gripSize; width: 2 * root.gripSize; height: 2 * root.gripSize }
}

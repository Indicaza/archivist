import QtQuick
import Archivist.Services 1.0

// One window's worth of docking: the panels, splitters, focus ring and drop
// preview of a single surface. The main window and every floating window
// each have one. Panels, tabs and splitters talk to this item as their
// `workspace`; it answers for its own surface and passes the rest to the
// shared DockWorkspace.
Item {
    id: root

    required property var theme
    required property var workspace // DockWorkspace
    // Set explicitly (not bound) so the per-surface models are fetched once.
    property string surfaceId: ""

    readonly property Item area: root
    readonly property alias overlay: overlay
    property bool resizingSplit: false
    property bool resizingSurface: false
    readonly property bool animateLayout: !theme.reducedMotion && !resizingSplit && !resizingSurface
    // New panels ease in once the workspace is up (see DockAnchor).
    readonly property bool animateArrivals: animateLayout && root.workspace.startupSettled === true
    function nextArrivalDelay() {
        return root.workspace.nextArrivalDelay()
    }
    readonly property bool showFocus: Docking.surfaceIds.length > 1 || anchorCount > 1
    readonly property int anchorCount: anchorRepeater.count
    // The pointer of a drag is over this window.
    property bool dragHere: false
    property point dragPointer: Qt.point(0, 0)

    clip: true

    // ---- shared with the workspace ---------------------------------------

    function anchorTitle(anchorId) { return workspace.anchorTitle(anchorId) }
    function viewTitle(info) { return workspace.viewTitle(info) }
    function worktreeLabel(resource) { return workspace.worktreeLabel(resource) }
    function registerAnchor(anchorId, item) { workspace.registerAnchor(anchorId, item) }
    function unregisterAnchor(anchorId, item) { workspace.unregisterAnchor(anchorId, item) }
    function splitAnchor(anchorId, edge) { workspace.splitAnchor(anchorId, edge) }
    function closeAnchor(anchorId) { workspace.closeAnchor(anchorId) }
    function closeView(viewId) { workspace.closeView(viewId) }
    function closeOtherViews(viewId) { workspace.closeOtherViews(viewId) }
    function revealView(viewId) { workspace.revealView(viewId) }
    function openChat(anchorId) { workspace.openChat(anchorId) }
    function openTerminal(anchorId) { workspace.openTerminal(anchorId) }
    function openLocation(filePath, lineNumber, columnNumber) { workspace.openLocation(filePath, lineNumber, columnNumber) }
    function beginDrag(kind, id, title) { return workspace.beginDrag(kind, id, title) }
    // Drags report the pointer in global (screen) coordinates, so a drag can
    // cross from one window into another or out onto the desktop.
    function moveDrag(globalPoint) { workspace.moveDrag(globalPoint) }
    function endDrag() { workspace.endDrag() }
    function cancelDrag() { workspace.cancelDrag() }
    function detachView(viewId) { workspace.detachView(viewId, root) }
    function detachAnchor(anchorId) { workspace.detachAnchor(anchorId, root) }
    function returnToMainWindow(kind, id) { workspace.returnToMainWindow(kind, id) }
    function returnToPrevious(kind, id) { workspace.returnToPrevious(kind, id) }
    function viewIconName(info) { return workspace.viewIconName(info) }
    function libraryLabel(libraryId) { return workspace.libraryLabel(libraryId) }
    function shelfCandidates(anchorId) { return workspace.shelfCandidates(anchorId) }
    function addToShelf(anchorId, viewId) { return workspace.addToShelf(anchorId, viewId) }
    function notePointerFocus() { workspace.notePointerFocus() }
    readonly property bool keyboardFocusVisible: workspace.keyboardFocusVisible
    readonly property bool isFloating: surfaceId.length > 0 && surfaceId !== Docking.primarySurfaceId

    function containsGlobal(point) {
        if (!root.visible || !root.Window.window || !root.Window.window.visible) {
            return false
        }
        var local = root.mapFromGlobal(point.x, point.y)
        return local.x >= 0 && local.y >= 0 && local.x <= width && local.y <= height
    }

    function noteResize() {
        root.resizingSurface = true
        resizeSettle.restart()
        if (root.surfaceId.length > 0) {
            Docking.setSurfaceSize(root.surfaceId, width, height)
        }
    }

    onWidthChanged: noteResize()
    onHeightChanged: noteResize()
    onSurfaceIdChanged: {
        workspace.registerSurfaceArea(surfaceId, root)
        Qt.callLater(function() {
            if (root.surfaceId.length > 0) {
                Docking.setSurfaceSize(root.surfaceId, root.width, root.height)
            }
        })
    }
    Component.onDestruction: workspace.unregisterSurfaceArea(surfaceId, root)

    Timer {
        id: resizeSettle
        interval: 160
        onTriggered: root.resizingSurface = false
    }

    // ---- drawing ---------------------------------------------------------

    Rectangle {
        anchors.fill: parent
        color: root.theme.workspaceBgDeep
    }

    Repeater {
        id: anchorRepeater

        model: root.surfaceId.length > 0 ? Docking.anchors(root.surfaceId) : null

        delegate: DockAnchor {
            theme: root.theme
            workspace: root
        }
    }

    // View content (terminals, previews, the live chat) is parented here by
    // the workspace at z 1, between the panels and the splitters.

    Repeater {
        model: root.surfaceId.length > 0 ? Docking.splitters(root.surfaceId) : null

        delegate: DockSplitter {
            z: 2
            theme: root.theme
            workspace: root
        }
    }

    // Focus moved from the keyboard gets a clear ring, drawn above panel
    // content so it reads over editors and terminals. It does not travel:
    // it fades out on one panel and in on the next. Pointer focus only
    // gets the panel's quiet edge (DockAnchor).
    Rectangle {
        id: focusRing

        readonly property var target: {
            Docking.revision
            return root.workspace.anchorItems[Docking.focusedAnchorId] || null
        }
        readonly property bool shown: root.showFocus && root.keyboardFocusVisible && target !== null && target.visible
            && !Docking.dragActive && target.surfaceId === root.surfaceId

        x: target && shown ? target.x : x
        y: target && shown ? target.y : y
        width: target && shown ? target.width : width
        height: target && shown ? target.height : height
        z: 90
        color: "transparent"
        radius: 3
        border.width: 1.5
        border.color: Qt.rgba(root.theme.accentBright.r, root.theme.accentBright.g, root.theme.accentBright.b, 0.75)
        opacity: shown ? 1 : 0
        enabled: false

        Behavior on opacity { NumberAnimation { duration: root.theme.reducedMotion ? 0 : 120; easing.type: Easing.OutCubic } }
    }

    DockDropOverlay {
        id: overlay

        anchors.fill: parent
        z: 100
        theme: root.theme
        workspace: root
        surfaceId: root.surfaceId
        pointerHere: root.dragHere
        pointer: root.dragPointer
        ghostTitle: root.workspace.dragGhostTitle
        ghostIcon: root.workspace.dragGhostIcon
    }

    // Cmd/Ctrl + = - 0 size the whole interface (InterfaceScale catches
    // the keys application-wide, even inside editors and terminals).
    Connections {
        target: InterfaceScale

        function onScaleChanged() {
            zoomBadge.show(Math.round(InterfaceScale.scale * 100) + "%")
        }
    }

    // A brief readout of the interface size, like a browser's zoom bubble:
    // a percentage and a meter on the size ladder.
    Rectangle {
        id: zoomBadge

        property string label: ""
        readonly property var ladder: [0.67, 0.75, 0.8, 0.9, 1, 1.1, 1.25, 1.5, 1.75, 2]
        readonly property int position: {
            var best = 0
            for (var index = 0; index < ladder.length; index += 1) {
                if (Math.abs(ladder[index] - InterfaceScale.scale) < Math.abs(ladder[best] - InterfaceScale.scale)) {
                    best = index
                }
            }
            return best
        }

        function show(text) {
            label = text
            opacity = 1
            zoomBadgeTimer.restart()
        }

        anchors.horizontalCenter: parent.horizontalCenter
        y: 18
        z: 210
        width: Math.max(150, zoomBadgeRow.implicitWidth + 36)
        height: 58
        radius: 14
        color: Qt.rgba(0.08, 0.075, 0.07, 0.9)
        border.width: 1
        border.color: root.theme.panelBorder
        opacity: 0
        visible: opacity > 0.01
        scale: opacity > 0.5 ? 1 : 0.94

        Behavior on opacity { NumberAnimation { duration: root.theme.reducedMotion ? 0 : 160 } }
        Behavior on scale { enabled: !root.theme.reducedMotion; NumberAnimation { duration: 180; easing.type: Easing.OutBack } }

        HoverHandler {
            onHoveredChanged: hovered ? zoomBadgeTimer.stop() : zoomBadgeTimer.restart()
        }

        Row {
            id: zoomBadgeRow

            anchors.horizontalCenter: parent.horizontalCenter
            y: 9
            spacing: 10

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: zoomBadge.label
                color: root.theme.appText
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.textPanelTitleSize - 4
                font.weight: root.theme.textWeightEmphasis
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: Math.abs(InterfaceScale.scale - 1) > 0.001
                text: Qt.platform.os === "osx" ? "⌘0 resets" : "Ctrl+0 resets"
                color: root.theme.mutedText
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.textMetadataSize
            }
        }

        // Where this size sits on the ladder; the dot glides between steps.
        Item {
            anchors.horizontalCenter: parent.horizontalCenter
            y: parent.height - 15
            width: parent.width - 36
            height: 6

            Repeater {
                model: zoomBadge.ladder.length

                Rectangle {
                    required property int index

                    x: index * (parent.width - 4) / (zoomBadge.ladder.length - 1)
                    anchors.verticalCenter: parent.verticalCenter
                    width: 4
                    height: zoomBadge.ladder[index] === 1 ? 6 : 4
                    radius: 2
                    color: index <= zoomBadge.position ? root.theme.accent : root.theme.panelBorder
                    Behavior on color { ColorAnimation { duration: 120 } }
                }
            }

            Rectangle {
                width: 10
                height: 10
                radius: 5
                anchors.verticalCenter: parent.verticalCenter
                x: zoomBadge.position * (parent.width - 4) / (zoomBadge.ladder.length - 1) - 3
                color: root.theme.accentBright
                Behavior on x { enabled: !root.theme.reducedMotion; SpringAnimation { spring: 4; damping: 0.4; epsilon: 0.25 } }
            }
        }

        Timer {
            id: zoomBadgeTimer
            interval: 1400
            onTriggered: zoomBadge.opacity = 0
        }
    }
}

import QtQuick

// One trip between a panel and its sphere in the Gravity Well: the Genie.
//
// It moves a picture, never the panel: `picture` is a snapshot of the panel
// (a ShaderEffectSource, captured once on the GPU) and the real panel is
// already put away (or not yet shown) while this plays. The picture is
// warped on a grid mesh by shaders/genie.vert, so its sides curve into a
// funnel and it slides through the neck into the sphere, nearest edge
// first. Restoring plays it backwards, a little faster.
//
// Without a picture (nothing was captured, e.g. after a restart) it's a
// plain card that grows out of the sphere; with reduced motion it's a short
// fade and shrink toward the sphere. Both stay where things really are.
Item {
    id: root

    required property var theme
    // "minimize" (into the sphere) or "restore" (out of it).
    property string direction: "minimize"
    // The panel's rect and the sphere (x, y, width, height), in this
    // item's parent's coordinates.
    property rect panel: Qt.rect(0, 0, 0, 0)
    property rect sphere: Qt.rect(0, 0, 0, 0)
    property var picture: null
    // Set when the trip ends, after `finished`.
    property bool done: false

    readonly property bool warped: picture !== null && !theme.reducedMotion && effect.status !== ShaderEffect.Error
    readonly property real sphereRadius: Math.max(4, Math.min(sphere.width, sphere.height) / 2)
    readonly property point sphereCentre: Qt.point(sphere.x + sphere.width / 2, sphere.y + sphere.height / 2)

    // 0 is the panel, 1 is inside the sphere.
    property real progress: direction === "restore" ? 1 : 0
    // Restoring: where the panel is now, asked each frame (it may still be
    // settling: a new floating window sizing itself, neighbours making
    // room), so the trip ends exactly on it.
    property var follow: null
    onProgressChanged: {
        if (follow) {
            var rect = follow()
            if (rect && rect.width > 0 && rect.height > 0) {
                panel = rect
            }
        }
    }

    // How long it stays after the trip, at its end, while what it stood
    // in for fades in under it (no gap, never two).
    property int linger: 0

    signal finished()
    // Lingered and done: the owner can let it go.
    signal gone()

    anchors.fill: parent

    function start() {
        trip.restart()
    }

    function finish() {
        trip.stop()
        if (!root.done) {
            root.done = true
            root.finished()
            lingerTimer.start()
        }
    }

    Timer {
        id: lingerTimer

        interval: root.linger
        onTriggered: root.gone()
    }

    SequentialAnimation {
        id: trip

        NumberAnimation {
            target: root
            property: "progress"
            from: root.direction === "restore" ? 1 : 0
            to: root.direction === "restore" ? 0 : 1
            duration: root.theme.reducedMotion ? root.theme.genieReducedDuration
                : root.direction === "restore" ? root.theme.genieRestoreDuration : root.theme.genieMinimizeDuration
            // In: pulled harder as it goes. Out: quick to open, gentle to settle.
            easing.type: root.direction === "restore" ? Easing.OutCubic : Easing.BezierSpline
            easing.bezierCurve: [0.35, 0.0, 0.25, 1.0, 1.0, 1.0]
        }
        ScriptAction { script: root.finish() }
    }

    // The warp.
    ShaderEffect {
        id: effect

        objectName: "dock.genie.effect"

        anchors.fill: parent
        visible: root.warped
        property var picture: root.picture
        property rect from: root.panel
        property vector4d sphere: Qt.vector4d(root.sphereCentre.x, root.sphereCentre.y, root.sphereRadius, 0)
        property real progress: root.progress
        mesh: GridMesh { resolution: Qt.size(4, 48) }
        vertexShader: "shaders/genie.vert.qsb"
        fragmentShader: "shaders/genie.frag.qsb"
    }

    // Reduced motion, or a picture without the warp: the picture itself
    // shrinks and fades toward the sphere (a short trip, no distortion).
    Item {
        id: plain

        readonly property real travel: root.theme.reducedMotion ? root.progress * 0.25 : root.progress
        readonly property real width0: root.panel.width
        readonly property real height0: root.panel.height
        readonly property real scaleNow: root.theme.reducedMotion
            ? 1 - 0.12 * root.progress
            : 1 - (1 - (root.sphereRadius * 2) / Math.max(1, Math.max(width0, height0))) * root.progress

        visible: !root.warped
        x: root.panel.x + (root.sphereCentre.x - (root.panel.x + width0 / 2)) * travel
        y: root.panel.y + (root.sphereCentre.y - (root.panel.y + height0 / 2)) * travel
        width: width0
        height: height0
        scale: scaleNow
        opacity: root.theme.reducedMotion ? 1 - root.progress : 1 - Math.max(0, (root.progress - 0.7) / 0.3)

        // The picture, when there is one.
        ShaderEffect {
            anchors.fill: parent
            visible: root.picture !== null
            property var source: root.picture
        }

        // No picture: a card in the panel's colours.
        Rectangle {
            anchors.fill: parent
            visible: root.picture === null
            radius: root.theme.radiusMedium + (root.sphereRadius - root.theme.radiusMedium) * root.progress
            color: root.theme.workspaceBg
            border.width: 1
            border.color: root.theme.panelBorder

            Rectangle {
                width: parent.width
                height: Math.min(parent.height, root.theme.editorTabHeight)
                radius: parent.radius
                color: root.theme.surfaceBg
            }
        }
    }
}

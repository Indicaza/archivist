import QtQuick

// A sidebar or dock edge on a spring: `value` chases `target`, keeping its
// speed from frame to frame (so a change of mind mid-move flows on instead
// of starting over), winds back a touch before it goes, carries past its
// mark and settles. The Motion panel's layout knobs set it: duration (how
// long until it's settled), bounce (how far past it carries) and
// anticipation (how far it winds back). Not active (startup, reduced
// motion, a drag), it sits exactly on its target.
Item {
    id: root

    required property var theme
    property real target: 0
    property bool active: true

    property real value: 0
    property real speed: 0
    property bool moving: false
    // The wind-up before a move from rest: for `windup` seconds it heads a
    // little the other way, to `windupTo`, then goes.
    property real windup: 0
    property real windupTo: 0

    visible: false

    // Damping ratio from bounce (0: lands without passing its mark, 1:
    // a lively wobble), and a stiffness that reaches its mark in about one
    // duration.
    readonly property real ratio: Math.max(0.25, Math.min(1, 0.85 - 0.75 * theme.layoutBounce))
    readonly property real omega: 4.8 / Math.max(0.05, theme.layoutMotionDuration / 1000)

    function settle() {
        moving = false
        speed = 0
        windup = 0
        value = target
    }
    onTargetChanged: {
        if (!active) {
            settle()
            return
        }
        if (!moving && theme.anticipation > 0) {
            // Starting from rest: a small wind-up the other way.
            windup = Math.max(0.02, theme.layoutMotionDuration / 1000 * 0.2)
            windupTo = value - (target - value) * 0.08 * theme.anticipation
        }
        moving = true
    }
    onActiveChanged: if (!active) settle()
    Component.onCompleted: settle()

    FrameAnimation {
        running: root.moving
        onTriggered: {
            // A long frame (a stall, the window hidden) doesn't fling it.
            var dt = Math.min(0.05, frameTime)
            var stiffness = root.omega * root.omega
            var damping = 2 * root.ratio * root.omega
            var at = root.value
            var speed = root.speed
            var goal = root.windup > 0 ? root.windupTo : root.target
            root.windup = Math.max(0, root.windup - dt)
            var steps = Math.max(1, Math.ceil(dt / 0.008))
            var h = dt / steps
            for (var i = 0; i < steps; i += 1) {
                speed += (stiffness * (goal - at) - damping * speed) * h
                at += speed * h
            }
            if (root.windup <= 0 && Math.abs(root.target - at) < 0.5 && Math.abs(speed) < 8) {
                root.settle()
            } else {
                root.value = at
                root.speed = speed
            }
        }
    }
}

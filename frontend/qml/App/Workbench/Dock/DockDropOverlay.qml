import QtQuick
import Archivist.Services 1.0
import "../../Icons"

// What a drag will do: the landing area eases to where the content would
// go while the other panels make room, and a ghost follows the pointer.
Item {
    id: root

    required property var theme
    required property var workspace

    property string surfaceId: ""

    readonly property var landing: Docking.dragLandingRect
    readonly property bool hasLanding: Docking.dragActive && Docking.dropAllowed && landing.width !== undefined
        && String(Docking.dropTarget.surfaceId || "") === root.surfaceId
    readonly property var target: Docking.dropTarget
    readonly property string actionLabel: {
        if (!Docking.dragActive || !Docking.dropAllowed) {
            return ""
        }
        var edge = String(target.edge || "")
        if (target.surfaceEdge) {
            return "Dock along the " + edge + " edge"
        }
        if (edge === "center") {
            return Docking.dragKind === "anchor" ? "Merge tabs here" : "Move to this panel"
        }
        return "Split " + (edge === "top" ? "above" : edge === "bottom" ? "below" : edge)
    }

    // Landing area.
    Rectangle {
        id: landingRect

        property real targetX: root.hasLanding ? root.landing.x + 4 : landingRect.x
        property real targetY: root.hasLanding ? root.landing.y + 4 : landingRect.y
        property real targetW: root.hasLanding ? Math.max(0, root.landing.width - 8) : landingRect.width
        property real targetH: root.hasLanding ? Math.max(0, root.landing.height - 8) : landingRect.height

        x: targetX
        y: targetY
        width: targetW
        height: targetH
        radius: root.theme.radiusPanel
        color: Qt.rgba(root.theme.accent.r, root.theme.accent.g, root.theme.accent.b, 0.13)
        border.width: 1.5
        border.color: Qt.rgba(root.theme.accentBright.r, root.theme.accentBright.g, root.theme.accentBright.b, 0.85)
        opacity: root.hasLanding ? 1 : 0
        scale: root.hasLanding ? 1 : 0.97

        Behavior on x { enabled: !root.theme.reducedMotion; SpringAnimation { spring: root.theme.magnetLandingSpring; damping: root.theme.magnetLandingDamping; epsilon: 0.25 } }
        Behavior on y { enabled: !root.theme.reducedMotion; SpringAnimation { spring: root.theme.magnetLandingSpring; damping: root.theme.magnetLandingDamping; epsilon: 0.25 } }
        Behavior on width { enabled: !root.theme.reducedMotion; SpringAnimation { spring: root.theme.magnetLandingSpring; damping: root.theme.magnetLandingDamping; epsilon: 0.25 } }
        Behavior on height { enabled: !root.theme.reducedMotion; SpringAnimation { spring: root.theme.magnetLandingSpring; damping: root.theme.magnetLandingDamping; epsilon: 0.25 } }
        Behavior on opacity { NumberAnimation { duration: root.theme.reducedMotion ? 0 : 140 } }
        Behavior on scale { NumberAnimation { duration: root.theme.reducedMotion ? 0 : 180; easing.type: Easing.OutBack } }

        // Soft inner glow.
        Rectangle {
            anchors.fill: parent
            anchors.margins: 5
            radius: parent.radius - 3
            color: "transparent"
            border.width: 1
            border.color: Qt.rgba(root.theme.accent.r, root.theme.accent.g, root.theme.accent.b, 0.22)
        }

        Rectangle {
            anchors.centerIn: parent
            width: actionText.implicitWidth + 24
            height: 26
            radius: 13
            color: root.theme.controlSurfaceBg
            border.width: 1
            border.color: root.theme.accent
            visible: root.actionLabel.length > 0 && parent.width > width + 16 && parent.height > 40

            Text {
                id: actionText

                anchors.centerIn: parent
                text: root.actionLabel
                color: root.theme.appText
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.textCaptionSize
                font.weight: root.theme.textWeightEmphasis
            }
        }
    }

    // ---- where it came from, and where it is aimed --------------------
    //
    // The carried picture itself is drawn over the whole main window
    // (DockCarry). Here: the spot it was lifted from, and the dock zone a
    // drag is held at, filling until it docks.
    property var carry: null
    readonly property bool carrying: carry !== null && carry.active && !carry.live
    readonly property bool fromHere: carrying && carry.surfaceId === root.surfaceId
    readonly property rect source: carrying ? carry.sourceRect : Qt.rect(0, 0, 0, 0)

    // Where it was lifted from: the panel stays, quietly marked.
    Rectangle {
        x: root.source.x + 2
        y: root.source.y + 2
        width: Math.max(0, root.source.width - 4)
        height: Math.max(0, root.source.height - 4)
        radius: root.theme.radiusPanel
        visible: root.fromHere && Docking.dragActive && root.carry.kind === "anchor"
        color: Qt.rgba(root.theme.workspaceBgDeep.r, root.theme.workspaceBgDeep.g, root.theme.workspaceBgDeep.b, 0.5)
        border.width: 1
        border.color: root.theme.panelBorder
    }

    // Held at a dock zone: a bar along that edge fills over the dwell.
    Item {
        id: aim

        objectName: "magnet.aim"
        readonly property var state: Docking.dragState
        readonly property var target: state.aim || ({})
        readonly property bool here: Docking.dragActive && state.phase === "aiming"
            && String(target.surfaceId || "") === root.surfaceId
        readonly property string edge: String(target.edge || "")
        // The panel aimed at, or the whole window for its border.
        readonly property rect zone: {
            if (!here) {
                return Qt.rect(0, 0, 0, 0)
            }
            var item = target.anchorId ? root.workspace.workspace.anchorItems[target.anchorId] : null
            if (!item) {
                return Qt.rect(0, 0, root.width, root.height)
            }
            var corner = item.mapToItem(root, 0, 0)
            return Qt.rect(corner.x, corner.y, item.width, item.height)
        }
        readonly property real thickness: 4
        property real progress: 0

        visible: here
        x: edge === "right" ? zone.x + zone.width - thickness - 3 : zone.x + 3
        y: edge === "bottom" ? zone.y + zone.height - thickness - 3 : zone.y + 3
        width: edge === "left" || edge === "right" ? thickness : zone.width - 6
        height: edge === "top" || edge === "bottom" ? thickness : zone.height - 6

        // A new zone (or the same one again) starts the fill over.
        property int serial: -1

        function restart() {
            var now = Docking.dragState
            serial = Number(now.aimSerial || 0)
            fill.stop()
            progress = 0
            if (here) {
                fill.duration = Math.max(1, Number(now.dwellMs || 220))
                fill.start()
            }
        }

        onHereChanged: restart()

        Connections {
            target: Docking

            function onDragChanged() {
                if (aim.here && aim.serial !== Number(Docking.dragState.aimSerial || 0)) {
                    aim.restart()
                }
            }
        }

        NumberAnimation {
            id: fill

            target: aim
            property: "progress"
            from: 0
            to: 1
        }

        Rectangle {
            anchors.fill: parent
            radius: 2
            visible: aim.edge !== "center"
            color: Qt.rgba(root.theme.accent.r, root.theme.accent.g, root.theme.accent.b, 0.25)
        }

        Rectangle {
            visible: aim.edge !== "center"
            readonly property bool across: aim.edge === "top" || aim.edge === "bottom"
            x: across ? (parent.width - width) / 2 : 0
            y: across ? 0 : (parent.height - height) / 2
            width: across ? parent.width * aim.progress : parent.width
            height: across ? parent.height : parent.height * aim.progress
            radius: 2
            color: root.theme.accentBright
        }

        Rectangle {
            anchors.fill: parent
            visible: aim.edge === "center"
            radius: root.theme.radiusPanel
            color: "transparent"
            border.width: 1.5
            border.color: Qt.rgba(root.theme.accentBright.r, root.theme.accentBright.g, root.theme.accentBright.b, 0.3 + 0.6 * aim.progress)
        }
    }

    // ---- magnetic pull --------------------------------------------------
    //
    // Near a dock zone, before anything is previewed, the nearest edge of
    // the panel (or window border) under the pointer glows, stronger the
    // closer it gets, with a faint tremor once it is close. It only draws:
    // the store still decides what a drop does.
    Item {
        id: pull

        objectName: "magnet.pull"
        readonly property var dock: root.workspace ? root.workspace.workspace : null
        readonly property string phase: String(Docking.dragState.phase || "")
        readonly property bool armed: Docking.dragActive && !root.hasLanding && dock !== null
            && (phase === "free" || phase === "aiming") && !aim.here && root.workspace.dragHere === true
        readonly property point pointer: {
            if (!armed) {
                return Qt.point(-1, -1)
            }
            var g = dock.dragPointer
            return root.mapFromGlobal(g.x, g.y)
        }
        // The nearest edge: {rect, edge, strength 0..1}.
        readonly property var nearest: {
            var none = { rect: Qt.rect(0, 0, 0, 0), edge: "", strength: 0 }
            if (!armed || pointer.x < 0 || pointer.y < 0 || pointer.x > root.width || pointer.y > root.height) {
                return none
            }
            var best = none
            var reach = root.theme.magnetProximityReach
            function consider(rect, band) {
                var edges = [
                    ["left", pointer.x - rect.x], ["right", rect.x + rect.width - pointer.x],
                    ["top", pointer.y - rect.y], ["bottom", rect.y + rect.height - pointer.y]
                ]
                for (var i = 0; i < edges.length; i += 1) {
                    var d = edges[i][1]
                    if (d < 0) {
                        continue
                    }
                    var strength = Math.max(0, Math.min(1, 1 - d / (band * reach)))
                    if (strength > best.strength) {
                        best = { rect: rect, edge: edges[i][0], strength: strength }
                    }
                }
            }
            consider(Qt.rect(0, 0, root.width, root.height), 18)
            var items = dock.anchorItems
            for (var key in items) {
                var item = items[key]
                if (!item || item.surfaceId !== root.surfaceId || !item.visible || key === dock.liftedAnchorId) {
                    continue
                }
                var corner = item.mapToItem(root, 0, 0)
                if (pointer.x < corner.x || pointer.y < corner.y
                    || pointer.x > corner.x + item.width || pointer.y > corner.y + item.height) {
                    continue
                }
                var span = Math.min(item.width, item.height)
                consider(Qt.rect(corner.x, corner.y, item.width, item.height), Math.max(32, Math.min(72, span * 0.18)))
            }
            return best
        }
        readonly property real strength: Math.pow(nearest.strength, 1.5)
        readonly property string edge: nearest.edge
        readonly property rect zone: nearest.rect
        readonly property bool across: edge === "top" || edge === "bottom"
        readonly property real thickness: 2 + 2 * strength
        // The tremor: a pixel or so, across the edge, only when close.
        property real phaseAngle: 0
        readonly property bool trembling: visible && strength > 0.45 && !root.theme.reducedMotion
        readonly property real tremor: trembling ? root.theme.magnetTremor * strength * Math.sin(phaseAngle) : 0

        visible: armed && edge.length > 0 && strength > 0.02
        opacity: Math.min(0.75, strength)
        x: (edge === "right" ? zone.x + zone.width - thickness - 2 : across ? zone.x + 6 : zone.x + 2) + (across ? 0 : tremor)
        y: (edge === "bottom" ? zone.y + zone.height - thickness - 2 : across ? zone.y + 2 : zone.y + 6) + (across ? tremor : 0)
        width: across ? Math.max(0, zone.width - 12) : thickness
        height: across ? thickness : Math.max(0, zone.height - 12)

        NumberAnimation {
            target: pull
            property: "phaseAngle"
            running: pull.trembling
            from: 0
            to: Math.PI * 2
            duration: root.theme.magnetTremorPeriod
            loops: Animation.Infinite
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            radius: 4
            color: Qt.rgba(root.theme.accent.r, root.theme.accent.g, root.theme.accent.b, 0.18)
        }
        Rectangle {
            anchors.fill: parent
            radius: 2
            color: root.theme.accentBright
        }
    }
}

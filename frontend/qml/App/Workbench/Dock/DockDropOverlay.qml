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

        Behavior on x { enabled: !root.theme.reducedMotion; SpringAnimation { spring: 4.2; damping: 0.36; epsilon: 0.25 } }
        Behavior on y { enabled: !root.theme.reducedMotion; SpringAnimation { spring: 4.2; damping: 0.36; epsilon: 0.25 } }
        Behavior on width { enabled: !root.theme.reducedMotion; SpringAnimation { spring: 4.2; damping: 0.36; epsilon: 0.25 } }
        Behavior on height { enabled: !root.theme.reducedMotion; SpringAnimation { spring: 4.2; damping: 0.36; epsilon: 0.25 } }
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
}

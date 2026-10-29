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
    // The drag's pointer is over this window (the ghost is drawn here).
    property bool pointerHere: true

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

    property point pointer: Qt.point(0, 0)
    property string ghostTitle: ""
    property string ghostIcon: "file"

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

    // ---- the carried panel ----------------------------------------------
    //
    // A picture of the panel, held at the spot it was picked up by,
    // slightly smaller and lifted. The live panel stays where it was (dimmed
    // below) until the drop. Nothing here is committed: it only draws.
    property var carry: null
    readonly property bool carrying: carry !== null && carry.active
    readonly property bool fromHere: carrying && carry.surfaceId === root.surfaceId
    readonly property rect source: carrying ? carry.sourceRect : Qt.rect(0, 0, 0, 0)
    readonly property bool hasPicture: carrying && String(carry.image).length > 0 && source.width > 0
    readonly property bool refused: Docking.dragActive && !Docking.dropAllowed
        && (Docking.dropTarget.surfaceId !== undefined || Docking.dragDetached)
    // Lifted: a little smaller, never so big that it hides where it goes.
    readonly property real carryScale: source.width > 0 && width > 0
        ? Math.min(0.9, 0.55 * width / source.width, 0.6 * height / Math.max(1, source.height))
        : 0.9
    readonly property point grab: carrying && carry.grab.x >= 0 ? carry.grab : Qt.point(60, 14)
    // 0 sits on the original panel, 1 is held by the pointer.
    property real lift: 1

    function startLift() {
        if (!root.fromHere || root.theme.reducedMotion) {
            liftAnimation.stop()
            root.lift = 1
            return
        }
        root.lift = 0
        liftAnimation.to = 1
        liftAnimation.duration = 150
        liftAnimation.restart()
    }

    Connections {
        target: root.carry
        ignoreUnknownSignals: true

        function onTokenChanged() {
            root.startLift()
        }

        function onEndingChanged() {
            if (!root.carry || root.carry.ending !== "returning" || !root.fromHere || root.theme.reducedMotion) {
                return
            }
            // Cancelled or refused: it flies back to where it came from.
            liftAnimation.to = 0
            liftAnimation.duration = 200
            liftAnimation.restart()
        }
    }

    NumberAnimation {
        id: liftAnimation

        target: root
        property: "lift"
        easing.type: Easing.OutCubic
    }

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

    Item {
        id: carried

        objectName: "magnet.carried"
        readonly property real held: root.carryScale
        readonly property real heldX: root.pointer.x - root.grab.x * held
        readonly property real heldY: root.pointer.y - root.grab.y * held
        readonly property real t: root.lift
        readonly property real s: 1 + (held - 1) * t

        visible: root.carrying && !Docking.dragDetached
            && (root.pointerHere || (root.fromHere && root.carry.ending === "returning"))
        x: root.source.x + (heldX - root.source.x) * t
        y: root.source.y + (heldY - root.source.y) * t
        width: Math.max(160, root.source.width) * s
        height: Math.max(90, root.source.height) * s
        opacity: !root.carrying ? 0
            : root.carry.ending === "dropped" ? 0
            : root.carry.ending === "returning" && !root.fromHere ? 0
            : root.refused ? 0.82 : 0.96

        Behavior on opacity {
            NumberAnimation { duration: root.theme.reducedMotion ? 0 : 140 }
        }

        // Lifted: a soft shadow under it.
        Rectangle {
            z: -1
            anchors.fill: parent
            anchors.topMargin: 10
            anchors.leftMargin: 4
            anchors.rightMargin: -4
            anchors.bottomMargin: -14
            radius: root.theme.radiusPanel + 4
            color: "#000000"
            opacity: 0.34 * carried.t
        }

        Rectangle {
            anchors.fill: parent
            radius: root.theme.radiusPanel
            color: root.theme.workspaceBgDeep
            clip: true

            // The panel's picture, cut from its window's grab.
            Image {
                readonly property real k: parent.width / Math.max(1, root.source.width)

                visible: root.hasPicture
                x: -root.source.x * k
                y: -root.source.y * k
                width: root.carry ? root.carry.areaSize.width * k : 0
                height: root.carry ? root.carry.areaSize.height * k : 0
                source: root.carrying ? root.carry.image : ""
                smooth: true
                asynchronous: false
            }

            // Before the picture arrives (a frame), or for a panel in
            // another window: its title and kind.
            Column {
                anchors.centerIn: parent
                visible: !root.hasPicture
                spacing: 8

                AppIcon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    name: Docking.dragKind === "anchor" ? "grid" : root.ghostIcon
                    tone: "muted"
                    iconSize: 22
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: root.ghostTitle
                    color: root.theme.mutedText
                    font.family: root.theme.bodyFontFamily
                    font.pixelSize: root.theme.textCaptionSize
                }
            }
        }

        // Its edge says where it stands: accent over a place that takes
        // it, red over one that doesn't.
        Rectangle {
            anchors.fill: parent
            radius: root.theme.radiusPanel
            color: "transparent"
            border.width: root.refused ? 1.5 : 1
            border.color: root.refused
                ? root.theme.danger
                : Docking.dropAllowed ? root.theme.accentBright : root.theme.panelBorder
        }

        // Why it can't go here.
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.bottom
            anchors.topMargin: 8
            width: Math.min(360, reasonText.implicitWidth + 24)
            height: 28
            radius: 14
            visible: root.refused && Docking.dropRefusal.length > 0
            color: root.theme.controlSurfaceBg
            border.width: 1
            border.color: root.theme.danger

            Text {
                id: reasonText

                anchors.centerIn: parent
                width: Math.min(implicitWidth, 336)
                text: Docking.dropRefusal
                elide: Text.ElideRight
                color: root.theme.appText
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.textCaptionSize
                font.weight: root.theme.textWeightEmphasis
            }
        }
    }
}

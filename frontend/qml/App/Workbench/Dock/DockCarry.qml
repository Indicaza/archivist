import QtQuick
import Archivist.Services 1.0
import "../../Icons"

// What a drag carries: a picture of the panel (one grab at pickup, never
// the live content), drawn over the whole main window.
//
// Over open space it is the panel as it will float, full size, where the
// drop puts it. Aimed at a dock zone it shrinks and lifts, held by the spot
// it was grabbed, while that window shows where it would dock. Picked up it
// rises: a touch smaller (theme.magnetPickupScale), a deeper shadow, an
// accent edge. Let go where it docks, it springs into its new place
// (landAt) and hands over to the real panel there. Nothing here is
// committed: it only draws, and the pointer always decides where it is.
Item {
    id: root

    required property var theme
    property var carry: null
    // Where a drop would float it, in this item's coordinates.
    property rect floatFrame: Qt.rect(0, 0, 0, 0)
    property string title: ""
    property string icon: "file"
    // The pointer, in this item's coordinates, and whether it is over the
    // main window at all.
    property point pointer: Qt.point(0, 0)
    property bool pointerHere: true

    readonly property bool carrying: carry !== null && carry.active && !carry.live
    readonly property rect source: carrying ? carry.sourceRect : Qt.rect(0, 0, 0, 0)
    readonly property rect start: carrying ? carry.startRect : Qt.rect(0, 0, 0, 0)
    readonly property bool hasStart: start.width > 0
    readonly property bool hasPicture: carrying && String(carry.image).length > 0 && source.width > 0
    readonly property bool floats: Docking.dragActive && Docking.dropFloats
    readonly property bool docking: Docking.dragActive && Docking.dropAllowed
    readonly property bool refused: Docking.dragActive && !Docking.dropAllowed && !Docking.dropFloats
        && Docking.dropRefusal.length > 0
    readonly property real heldScale: source.width > 0 && width > 0
        ? Math.min(0.9, 0.55 * width / source.width, 0.6 * height / Math.max(1, source.height))
        : 0.9
    readonly property point grab: carrying && carry.grab.x >= 0 ? carry.grab : Qt.point(60, 14)

    // 0 sits where it was lifted from, 1 is carried.
    property real lift: 1
    // Let go where it docks: from where it was to its landed rect, on a
    // curve that overshoots a little (landT passes 1, then settles on it
    // exactly), then fades over the real panel.
    property bool landing: false
    property rect fromRect: Qt.rect(0, 0, 0, 0)
    property rect toRect: Qt.rect(0, 0, 0, 0)
    property real landT: 0
    property real fade: 1

    function landAt(rect) {
        if (!root.carrying || rect.width <= 0 || rect.height <= 0) {
            return false
        }
        root.fromRect = Qt.rect(carried.x, carried.y, carried.width, carried.height)
        root.toRect = rect
        root.landT = 0
        root.fade = 1
        root.landing = true
        if (root.theme.reducedMotion) {
            root.landT = 1
            root.fade = 0
            root.carry.landed = true
            return true
        }
        landAnimation.restart()
        return true
    }

    ParallelAnimation {
        id: landAnimation

        NumberAnimation {
            target: root
            property: "landT"
            from: 0
            to: 1
            duration: root.theme.magnetReleaseDuration
            easing.type: Easing.OutBack
            easing.overshoot: root.theme.magnetReleaseOvershoot
        }
        SequentialAnimation {
            PauseAnimation { duration: Math.round(root.theme.magnetReleaseDuration * 0.55) }
            ScriptAction { script: if (root.carry) root.carry.landed = true }
            NumberAnimation {
                target: root
                property: "fade"
                to: 0
                duration: Math.round(root.theme.magnetReleaseDuration * 0.45)
                easing.type: Easing.InQuad
            }
        }
    }
    // 0 held small at the pointer (docking), 1 full size where it floats.
    property real spread: 1

    // Full size unless it is aimed at a dock (or refused there).
    readonly property real wantedSpread: root.floats || (!root.docking && !root.refused) ? 1 : 0

    function updateSpread() {
        if (!root.carrying || root.carry.ending.length > 0) {
            return
        }
        spreadAnimation.to = root.wantedSpread
        spreadAnimation.duration = root.theme.reducedMotion ? 0 : root.theme.magnetSpreadDuration
        spreadAnimation.restart()
    }
    onFloatsChanged: updateSpread()
    onDockingChanged: updateSpread()
    onRefusedChanged: updateSpread()

    function startLift() {
        spreadAnimation.stop()
        landAnimation.stop()
        root.landing = false
        root.landT = 0
        root.fade = 1
        root.spread = root.wantedSpread
        if (!root.hasStart || root.theme.reducedMotion) {
            liftAnimation.stop()
            root.lift = 1
            return
        }
        root.lift = 0
        liftAnimation.to = 1
        liftAnimation.duration = root.theme.magnetPickupDuration
        liftAnimation.restart()
    }

    Connections {
        target: root.carry
        ignoreUnknownSignals: true

        function onTokenChanged() {
            root.startLift()
        }

        function onEndingChanged() {
            if (!root.carry || root.carry.ending !== "returning" || !root.hasStart || root.theme.reducedMotion) {
                return
            }
            // Cancelled or refused: it goes back to where it came from.
            liftAnimation.to = 0
            liftAnimation.duration = root.theme.magnetReturnDuration
            liftAnimation.restart()
        }
    }

    NumberAnimation {
        id: liftAnimation

        target: root
        property: "lift"
        easing.type: Easing.OutCubic
    }

    NumberAnimation {
        id: spreadAnimation

        target: root
        property: "spread"
        easing.type: Easing.OutCubic
    }

    Item {
        id: carried

        objectName: "magnet.carried"

        // Held small at the pointer...
        readonly property real heldW: Math.max(160, root.source.width) * root.heldScale
        readonly property real heldH: Math.max(90, root.source.height) * root.heldScale
        readonly property real heldX: root.pointer.x - root.grab.x * root.heldScale
        readonly property real heldY: root.pointer.y - root.grab.y * root.heldScale
        // ...or full size where it floats.
        readonly property bool hasFloat: root.floatFrame.width > 0
        readonly property real f: hasFloat ? root.spread : 0
        readonly property real toX: heldX + ((hasFloat ? root.floatFrame.x : heldX) - heldX) * f
        readonly property real toY: heldY + ((hasFloat ? root.floatFrame.y : heldY) - heldY) * f
        readonly property real toW: heldW + ((hasFloat ? root.floatFrame.width : heldW) - heldW) * f
        readonly property real toH: heldH + ((hasFloat ? root.floatFrame.height : heldH) - heldH) * f
        readonly property real t: root.hasStart ? root.lift : 1
        readonly property real carriedX: root.hasStart ? root.start.x + (toX - root.start.x) * t : toX
        readonly property real carriedY: root.hasStart ? root.start.y + (toY - root.start.y) * t : toY
        readonly property real carriedW: Math.max(1, root.hasStart ? root.start.width + (toW - root.start.width) * t : toW)
        readonly property real carriedH: Math.max(1, root.hasStart ? root.start.height + (toH - root.start.height) * t : toH)
        // Lifted: a touch smaller, around the spot it is held by (so the
        // pointer stays on the same point of it); landing restores it.
        readonly property real lifted: root.landing ? Math.max(0, 1 - root.landT) : t
        readonly property real shrink: root.theme.reducedMotion ? 0 : (1 - root.theme.magnetPickupScale) * lifted

        visible: root.carrying && (root.landing || root.pointerHere || root.floats || (root.hasStart && root.carry.ending === "returning"))
        x: root.landing ? root.fromRect.x + (root.toRect.x - root.fromRect.x) * root.landT : carriedX
        y: root.landing ? root.fromRect.y + (root.toRect.y - root.fromRect.y) * root.landT : carriedY
        width: Math.max(1, root.landing ? root.fromRect.width + (root.toRect.width - root.fromRect.width) * root.landT : carriedW)
        height: Math.max(1, root.landing ? root.fromRect.height + (root.toRect.height - root.fromRect.height) * root.landT : carriedH)
        opacity: !root.carrying ? 0
            : root.landing ? 0.95 * root.fade
            : root.carry.ending === "dropped" ? 0
            : root.carry.ending === "returning" && !root.hasStart ? 0
            : root.refused ? 0.82 : 0.95

        Behavior on opacity {
            enabled: !root.landing
            NumberAnimation { duration: root.theme.reducedMotion ? 0 : 140 }
        }

        transform: Scale {
            origin.x: root.landing ? carried.width / 2 : Math.min(carried.width, root.grab.x * carried.width / Math.max(1, root.source.width))
            origin.y: root.landing ? carried.height / 2 : Math.min(carried.height, root.grab.y * carried.height / Math.max(1, root.source.height))
            xScale: 1 - carried.shrink
            yScale: 1 - carried.shrink
        }

        // Lifted: a deep, soft shadow under it (two layers, no blur pass).
        Rectangle {
            z: -2
            anchors.fill: parent
            anchors.topMargin: root.theme.magnetLiftOffset * carried.lifted
            anchors.leftMargin: -6
            anchors.rightMargin: -6
            anchors.bottomMargin: -(root.theme.magnetLiftOffset + 10) * carried.lifted
            radius: root.theme.radiusPanel + 10
            color: "#000000"
            opacity: root.theme.magnetLiftShadow * 0.45 * carried.lifted
        }
        Rectangle {
            z: -1
            anchors.fill: parent
            anchors.topMargin: root.theme.magnetLiftOffset * 0.6 * carried.lifted
            anchors.leftMargin: 2
            anchors.rightMargin: -2
            anchors.bottomMargin: -root.theme.magnetLiftOffset * carried.lifted
            radius: root.theme.radiusPanel + 4
            color: "#000000"
            opacity: root.theme.magnetLiftShadow * carried.lifted
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

            // Before the picture arrives (a frame), or for a panel from
            // another window: its title and kind.
            Column {
                anchors.centerIn: parent
                visible: !root.hasPicture
                spacing: 8

                AppIcon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    name: Docking.dragKind === "anchor" ? "grid" : root.icon
                    tone: "muted"
                    iconSize: 22
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: root.title
                    color: root.theme.mutedText
                    font.family: root.theme.bodyFontFamily
                    font.pixelSize: root.theme.textCaptionSize
                }
            }
        }

        // Its edge says what a drop does: accent where it docks, red where
        // it can't go, quiet where it floats.
        Rectangle {
            anchors.fill: parent
            radius: root.theme.radiusPanel
            color: "transparent"
            border.width: root.refused || root.docking ? 1.5 : 1
            border.color: root.refused ? root.theme.danger
                : root.docking ? root.theme.accentBright
                : Qt.rgba(root.theme.accentBright.r, root.theme.accentBright.g, root.theme.accentBright.b, 0.2 + 0.3 * carried.lifted)
        }

        // Why it can't go here, or how to dock it.
        Rectangle {
            id: pill

            readonly property string text: root.refused ? Docking.dropRefusal
                : root.floats ? "Release to float · hold at an edge or a tab bar to dock"
                : ""

            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.bottom
            anchors.topMargin: 8
            width: Math.min(400, pillText.implicitWidth + 24)
            height: 28
            radius: 14
            visible: text.length > 0 && Docking.dragActive && !root.landing
            color: root.theme.controlSurfaceBg
            border.width: 1
            border.color: root.refused ? root.theme.danger : root.theme.panelBorder

            Text {
                id: pillText

                anchors.centerIn: parent
                width: Math.min(implicitWidth, 376)
                text: pill.text
                elide: Text.ElideRight
                color: root.refused ? root.theme.appText : root.theme.mutedText
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.textCaptionSize
                font.weight: root.refused ? root.theme.textWeightEmphasis : Font.Normal
            }
        }
    }
}

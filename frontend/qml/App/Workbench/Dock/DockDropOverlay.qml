import QtQuick
import Archivist.Services 1.0
import "../../Icons"

// What a drag will do: the landing area eases to where the content would
// go while the other panels make room, and a ghost follows the pointer.
Item {
    id: root

    required property var theme
    required property var workspace

    readonly property var landing: Docking.dragLandingRect
    readonly property bool hasLanding: Docking.dragActive && Docking.dropAllowed && landing.width !== undefined
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

    // The thing being dragged.
    Rectangle {
        id: ghost

        readonly property bool refused: Docking.dragActive && Docking.dropTarget.surfaceId !== undefined
            && !Docking.dropAllowed

        x: root.pointer.x + 14
        y: root.pointer.y + 10
        width: Math.min(260, ghostRow.implicitWidth + 22)
        height: 30
        radius: 8
        visible: Docking.dragActive
        color: root.theme.controlSurfaceBg
        border.width: 1
        border.color: refused ? root.theme.danger : root.theme.accent
        opacity: refused ? 0.75 : 0.96

        Rectangle {
            // Drop shadow.
            z: -1
            anchors.fill: parent
            anchors.topMargin: 4
            anchors.leftMargin: 2
            anchors.rightMargin: -2
            anchors.bottomMargin: -5
            radius: parent.radius + 2
            color: "#000000"
            opacity: 0.32
        }

        Row {
            id: ghostRow

            anchors.verticalCenter: parent.verticalCenter
            x: 11
            spacing: 7

            AppIcon {
                anchors.verticalCenter: parent.verticalCenter
                name: Docking.dragKind === "anchor" ? "grid" : root.ghostIcon
                tone: ghost.refused ? "danger" : "accent"
                iconSize: 13
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, 210)
                text: ghost.refused ? "Can't drop here" : root.ghostTitle
                elide: Text.ElideRight
                color: root.theme.appText
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.textTabSize
                font.weight: root.theme.textWeightEmphasis
            }
        }
    }
}

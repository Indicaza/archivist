import QtQuick
import QtQuick.Controls

// The surface Home's cards share: a warm panel that brightens and rises a
// hair under the pointer, presses in when clicked and shows keyboard focus
// with the accent. Content goes inside.
Button {
    id: root

    required property var theme
    property bool emphasized: false
    // Lifted for the dive into a project (0..1): the card that was opened
    // grows a touch and brightens while Home lets go.
    property real lift: 0

    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    padding: 0

    readonly property bool lit: hovered || activeFocus

    transform: Translate {
        y: root.lit && !root.down && !root.theme.reducedMotion ? -2 : 0

        Behavior on y {
            enabled: !root.theme.reducedMotion
            NumberAnimation { duration: root.theme.motionHover; easing.type: Easing.OutCubic }
        }
    }
    scale: (root.down ? 0.985 : 1) + root.lift * 0.05

    Behavior on scale {
        enabled: !root.theme.reducedMotion && root.lift === 0
        NumberAnimation { duration: 120; easing.type: Easing.OutCubic }
    }

    background: Item {
        // Shadow under a lifted card.
        Rectangle {
            x: 1
            y: root.lit ? 8 : 4
            width: parent.width - 2
            height: parent.height
            radius: root.theme.homeCardRadius
            color: root.theme.withAlpha(root.theme.homeShadow, root.lit ? 0.22 : 0.1)

            Behavior on y {
                enabled: !root.theme.reducedMotion
                NumberAnimation { duration: root.theme.motionHover; easing.type: Easing.OutCubic }
            }
        }

        Rectangle {
            anchors.fill: parent
            radius: root.theme.homeCardRadius
            color: root.lit || root.lift > 0 ? root.theme.homeCardHoverBg : root.theme.homeCardBg
            border.width: 1
            border.color: root.activeFocus
                ? root.theme.withAlpha(root.theme.accent, 0.8)
                : root.lit || root.lift > 0
                    ? root.theme.homeCardBorderHover
                    : root.emphasized ? root.theme.withAlpha(root.theme.homeCardBorderHover, 0.85) : root.theme.homeCardBorder

            Behavior on color {
                enabled: !root.theme.reducedMotion
                ColorAnimation { duration: root.theme.motionHover }
            }
            Behavior on border.color {
                enabled: !root.theme.reducedMotion
                ColorAnimation { duration: root.theme.motionHover }
            }
        }

        // A hairline of light along the top edge: the card catches the room.
        Rectangle {
            x: root.theme.homeCardRadius
            y: 1
            width: parent.width - root.theme.homeCardRadius * 2
            height: 1
            color: root.theme.withAlpha(root.theme.appText, root.lit ? 0.07 : 0.035)
        }
    }
}

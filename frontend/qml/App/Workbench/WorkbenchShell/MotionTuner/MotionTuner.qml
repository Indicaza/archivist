import QtQuick
import QtQuick.Controls

// A small panel for tuning how the app moves, live: how quick everything
// is, how the sidebar, dock and Home push the layout (how long, how much
// they draw back first and carry past), and the springs the panels follow
// on (stiffness, damping, weight, and how much the far ones lag). Changes
// apply at once and are saved (Theme.setMotion). Ctrl+Alt+Shift+M (⌘⌥⇧M
// on a Mac) shows or hides it. It has buttons to play the moves it tunes.
Rectangle {
    id: root

    required property var theme
    signal toggleSidebarRequested()
    signal toggleDockRequested()
    signal closeRequested()

    width: 300
    height: column.implicitHeight + 24
    radius: theme.radiusMedium
    color: theme.surfaceBg
    border.width: 1
    border.color: theme.panelBorder

    // Clicks stay here.
    MouseArea { anchors.fill: parent }

    Rectangle {
        z: -1
        anchors.fill: parent
        anchors.topMargin: 6
        anchors.bottomMargin: -10
        radius: parent.radius + 2
        color: "#000000"
        opacity: 0.3
    }

    component Knob: Column {
        id: knob

        required property string label
        required property string key
        required property real value
        required property real from
        required property real to
        property real step: 0
        property string hint: ""
        property int decimals: 2
        property string unit: ""

        width: parent.width
        spacing: 2

        Item {
            width: parent.width
            height: labelText.implicitHeight

            Text {
                id: labelText
                text: knob.label
                color: root.theme.appText
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.typeCaption
            }
            Text {
                anchors.right: parent.right
                text: knob.value.toFixed(knob.decimals) + knob.unit
                color: root.theme.mutedText
                font.family: root.theme.monospaceFontFamily
                font.pixelSize: root.theme.typeMicro + 1
            }
        }

        Slider {
            id: slider

            width: parent.width
            height: 20
            from: knob.from
            to: knob.to
            stepSize: knob.step
            // Follows the setting (a reset too), except while it's dragged.
            Binding on value { value: knob.value; when: !slider.pressed }
            onMoved: root.theme.setMotion(knob.key, knob.decimals === 0 ? Math.round(value) : Number(value.toFixed(knob.decimals)))

            background: Rectangle {
                x: slider.leftPadding
                y: slider.topPadding + slider.availableHeight / 2 - height / 2
                width: slider.availableWidth
                height: 3
                radius: 1.5
                color: root.theme.quietBorder

                Rectangle {
                    width: slider.visualPosition * parent.width
                    height: parent.height
                    radius: 1.5
                    color: root.theme.accent
                }
            }
            handle: Rectangle {
                x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
                y: slider.topPadding + slider.availableHeight / 2 - height / 2
                width: 12
                height: 12
                radius: 6
                color: slider.pressed ? root.theme.accentBright : root.theme.appText
            }
        }

        Text {
            visible: knob.hint.length > 0
            width: parent.width
            text: knob.hint
            color: root.theme.mutedText
            opacity: 0.8
            font.family: root.theme.bodyFontFamily
            font.pixelSize: root.theme.typeMicro
            wrapMode: Text.WordWrap
        }
    }

    component Pill: Rectangle {
        id: pill

        required property string label
        signal clicked()

        width: pillText.implicitWidth + 18
        height: 24
        radius: 12
        color: pillArea.containsMouse ? root.theme.hoverBg : "transparent"
        border.width: 1
        border.color: root.theme.panelBorder

        Text {
            id: pillText
            anchors.centerIn: parent
            text: pill.label
            color: root.theme.appText
            font.family: root.theme.bodyFontFamily
            font.pixelSize: root.theme.typeCaption
        }
        MouseArea {
            id: pillArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: pill.clicked()
        }
    }

    Column {
        id: column

        x: 14
        y: 12
        width: parent.width - 28
        spacing: 10

        Item {
            width: parent.width
            height: 20

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "Motion"
                color: root.theme.appText
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.typeLabel
                font.weight: Font.DemiBold
            }
            Text {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: "✕"
                color: root.theme.mutedText
                font.pixelSize: root.theme.typeCaption
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -6
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.closeRequested()
                }
            }
        }

        Knob {
            label: "Speed"
            key: "speed"
            value: root.theme.motionSpeed
            from: 0.4
            to: 2.0
            unit: "×"
            hint: "Scales every animation. Lower is snappier."
        }
        Knob {
            label: "Layout duration"
            key: "layoutDuration"
            value: root.theme.layoutDuration
            from: 100
            to: 600
            step: 10
            decimals: 0
            unit: " ms"
            hint: "Sidebar, bottom dock and Home opening and closing."
        }
        Knob {
            label: "Layout bounce"
            key: "layoutBounce"
            value: root.theme.layoutBounce
            from: 0
            to: 1
            hint: "How far the sidebar and dock carry past their mark."
        }
        Knob {
            label: "Anticipation"
            key: "anticipation"
            value: root.theme.anticipation
            from: 0
            to: 1
            hint: "How far the sidebar and dock draw back before they go."
        }
        Knob {
            label: "Panel spring"
            key: "reflowSpring"
            value: root.theme.reflowSpring
            from: 1
            to: 12
            decimals: 1
            hint: "Panels following a change. Stiffer is quicker."
        }
        Knob {
            label: "Panel damping"
            key: "reflowDamping"
            value: root.theme.reflowDamping
            from: 0.1
            to: 1
            hint: "Less is wobblier, 1 doesn't overshoot."
        }
        Knob {
            label: "Panel weight"
            key: "reflowMass"
            value: root.theme.reflowMass
            from: 0.4
            to: 3
            hint: "Heavier panels lag further behind and swing wider."
        }
        Knob {
            label: "Stagger"
            key: "stagger"
            value: root.theme.stagger
            from: 0
            to: 0.8
            hint: "How much later the panels far from a change follow it."
        }

        Flow {
            width: parent.width
            spacing: 6

            Pill { label: "Toggle sidebar"; onClicked: root.toggleSidebarRequested() }
            Pill { label: "Toggle dock"; onClicked: root.toggleDockRequested() }
            Pill { label: "Reset"; onClicked: root.theme.resetMotion() }
        }
    }
}

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../Icons"

// One line in Home's sidebar: a conversation, a project, an action.
// Styled like the Workspace Navigator's rows (hover nudge, selection).
Button {
    id: root

    required property var theme
    property string iconName: ""
    // A project's hue, shown as a small square instead of an icon.
    property color swatch: "transparent"
    property bool showSwatch: false
    property string label: ""
    property string trailing: ""
    // The trailing text is a state ("OPEN"), shown like the navigator's ACTIVE.
    property bool trailingBadge: false
    property int indent: 0
    property bool current: false
    property bool strong: false
    // A disclosure arrow before the label (projects): -1 none, 0 closed, 1 open.
    property int disclosure: -1
    // Shown on hover at the end of the row.
    property string hoverIconName: ""
    property string hoverToolTip: ""

    signal hoverActionRequested()
    signal disclosureToggled()
    signal contextMenuRequested()

    implicitHeight: 28
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    padding: 0
    leftPadding: (lit ? 9 : 7) + indent
    rightPadding: 5

    Behavior on leftPadding {
        NumberAnimation {
            duration: root.lit ? root.theme.motionHover : root.theme.motionHoverExit
            easing.type: Easing.OutCubic
        }
    }
    Accessible.name: label

    readonly property bool lit: hovered || activeFocus || hoverAction.hovered || disclosureButton.hovered

    Keys.onRightPressed: function(event) {
        if (disclosure === 0) {
            disclosureToggled()
            event.accepted = true
        }
    }
    Keys.onLeftPressed: function(event) {
        if (disclosure === 1) {
            disclosureToggled()
            event.accepted = true
        }
    }
    Keys.onMenuPressed: contextMenuRequested()

    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: root.contextMenuRequested()
    }

    contentItem: RowLayout {
        spacing: 8

        Item {
            Layout.preferredWidth: 16
            Layout.preferredHeight: 16
            Layout.alignment: Qt.AlignVCenter
            visible: root.iconName.length > 0 || root.showSwatch || root.disclosure >= 0

            AppIcon {
                anchors.centerIn: parent
                visible: !root.showSwatch && root.iconName.length > 0 && !(root.disclosure >= 0 && root.lit)
                name: root.iconName
                tone: root.current ? "accent" : root.lit ? "normal" : "muted"
                iconSize: 13
            }

            Rectangle {
                anchors.centerIn: parent
                visible: root.showSwatch && !(root.disclosure >= 0 && root.lit)
                width: 8
                height: 8
                radius: 2.5
                color: root.swatch
            }

            // Projects: the swatch turns into a disclosure arrow on hover.
            Button {
                id: disclosureButton

                anchors.centerIn: parent
                width: 20
                height: 20
                padding: 0
                visible: root.disclosure >= 0 && root.lit
                hoverEnabled: true
                focusPolicy: Qt.NoFocus
                Accessible.name: root.disclosure === 1 ? "Collapse" : "Expand"
                onClicked: root.disclosureToggled()

                contentItem: Item {
                    AppIcon {
                        anchors.centerIn: parent
                        name: "chevron-right"
                        tone: disclosureButton.hovered ? "normal" : "muted"
                        iconSize: 10
                        rotation: root.disclosure === 1 ? 90 : 0

                        Behavior on rotation {
                            enabled: !root.theme.reducedMotion
                            NumberAnimation { duration: root.theme.motionFast; easing.type: Easing.OutCubic }
                        }
                    }
                }

                background: Rectangle {
                    radius: 5
                    color: disclosureButton.hovered ? root.theme.activeBg : "transparent"
                }
            }
        }

        Text {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            text: root.label
            color: root.current || root.lit || root.strong ? root.theme.appText : root.theme.mutedText
            font.pixelSize: root.theme.typeSize(9)
            font.weight: root.current || root.strong ? Font.DemiBold : Font.Normal
            elide: Text.ElideRight
        }

        Text {
            Layout.alignment: Qt.AlignVCenter
            visible: root.trailing.length > 0 && !hoverAction.visible
            text: root.trailing
            color: root.trailingBadge ? root.theme.accentBright : root.theme.mutedText
            opacity: root.trailingBadge ? 1 : 0.62
            font.pixelSize: root.theme.typeSize(root.trailingBadge ? 7 : 8)
            font.weight: root.trailingBadge ? Font.Bold : Font.Normal
            font.letterSpacing: root.trailingBadge ? 0.45 : 0
        }

        Button {
            id: hoverAction

            Layout.preferredWidth: 22
            Layout.preferredHeight: 22
            Layout.alignment: Qt.AlignVCenter
            visible: root.hoverIconName.length > 0 && root.lit
            hoverEnabled: true
            focusPolicy: Qt.NoFocus
            padding: 0
            Accessible.name: root.hoverToolTip
            ToolTip.visible: hovered && root.hoverToolTip.length > 0
            ToolTip.delay: 500
            ToolTip.text: root.hoverToolTip
            onClicked: root.hoverActionRequested()

            contentItem: Item {
                AppIcon {
                    anchors.centerIn: parent
                    name: root.hoverIconName
                    tone: hoverAction.hovered ? "normal" : "muted"
                    iconSize: 12
                }
            }

            background: Rectangle {
                radius: 6
                color: hoverAction.hovered ? root.theme.activeBg : "transparent"
            }
        }
    }

    background: Rectangle {
        radius: root.theme.radiusSmall
        color: root.down
            ? "#292621"
            : root.lit
                ? root.theme.hoverBg
                : root.current
                    ? root.theme.activeBg
                    : "transparent"
        border.width: root.current || root.activeFocus ? 1 : 0
        border.color: root.activeFocus ? root.theme.accent : "#554a7b"

        Behavior on color {
            enabled: !root.theme.reducedMotion
            ColorAnimation { duration: root.theme.motionFast }
        }
    }
}

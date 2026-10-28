import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../Icons"

// One line in Home's sidebar: a conversation, a project, an action.
Button {
    id: root

    required property var theme
    property string iconName: ""
    // A project's hue, shown as a small square instead of an icon.
    property color swatch: "transparent"
    property bool showSwatch: false
    property string label: ""
    property string trailing: ""
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

    implicitHeight: 32
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    padding: 0
    leftPadding: 8 + indent
    rightPadding: 6
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
                tone: root.current || root.lit ? "normal" : "muted"
                iconSize: 14
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
                    color: disclosureButton.hovered ? root.theme.withAlpha(root.theme.appText, 0.08) : "transparent"
                }
            }
        }

        Text {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            text: root.label
            color: root.current || root.lit || root.strong ? root.theme.appText : Qt.darker(root.theme.appText, 1.12)
            font.family: root.theme.bodyFontFamily
            font.pixelSize: root.theme.textControlSize - 1
            font.weight: root.strong ? root.theme.textWeightEmphasis : root.theme.textWeightRegular
            elide: Text.ElideRight
        }

        Text {
            Layout.alignment: Qt.AlignVCenter
            visible: root.trailing.length > 0 && !hoverAction.visible
            text: root.trailing
            color: root.theme.withAlpha(root.theme.mutedText, 0.85)
            font.family: root.theme.bodyFontFamily
            font.pixelSize: root.theme.textCaptionSize - 1
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
                color: hoverAction.hovered ? root.theme.withAlpha(root.theme.appText, 0.08) : "transparent"
            }
        }
    }

    background: Rectangle {
        radius: 8
        color: root.down
            ? root.theme.withAlpha(root.theme.appText, 0.09)
            : root.current
                ? root.theme.withAlpha(root.theme.appText, 0.07)
                : root.lit
                    ? root.theme.withAlpha(root.theme.appText, 0.04)
                    : "transparent"
        border.width: root.activeFocus ? 1 : 0
        border.color: root.theme.withAlpha(root.theme.accent, 0.8)

        Behavior on color {
            enabled: !root.theme.reducedMotion
            ColorAnimation { duration: root.theme.motionFast }
        }
    }
}

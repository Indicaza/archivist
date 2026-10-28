import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../Icons"

// One line in Home's sidebar: a conversation, a project, a file, an action.
Button {
    id: root

    required property var theme
    property string iconName: ""
    property string label: ""
    property string trailing: ""
    property int indent: 0
    property bool current: false
    property bool strong: false
    // Shown on hover at the end of the row (a project's "open" arrow, say).
    property string hoverIconName: ""
    property string hoverToolTip: ""

    signal hoverActionRequested()

    implicitHeight: 32
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    padding: 0
    leftPadding: 10 + indent
    rightPadding: 6
    Accessible.name: label

    contentItem: RowLayout {
        spacing: 9

        AppIcon {
            Layout.alignment: Qt.AlignVCenter
            visible: root.iconName.length > 0
            name: root.iconName
            tone: root.current || root.hovered || root.activeFocus ? "normal" : "muted"
            iconSize: 14
        }

        Text {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            text: root.label
            color: root.current || root.hovered || root.strong ? root.theme.appText : root.theme.mutedText
            font.family: root.theme.bodyFontFamily
            font.pixelSize: root.theme.textControlSize
            font.weight: root.strong ? root.theme.textWeightEmphasis : root.theme.textWeightRegular
            elide: Text.ElideRight
        }

        Text {
            Layout.alignment: Qt.AlignVCenter
            visible: root.trailing.length > 0 && !hoverAction.visible
            text: root.trailing
            color: root.theme.mutedText
            font.family: root.theme.bodyFontFamily
            font.pixelSize: root.theme.textCaptionSize
        }

        Button {
            id: hoverAction

            Layout.preferredWidth: 22
            Layout.preferredHeight: 22
            Layout.alignment: Qt.AlignVCenter
            visible: root.hoverIconName.length > 0 && (root.hovered || hovered || root.activeFocus)
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
                    tone: hoverAction.hovered ? "accent" : "muted"
                    iconSize: 12
                }
            }

            background: Rectangle {
                radius: root.theme.radiusSmall
                color: hoverAction.hovered ? root.theme.activeBg : "transparent"
            }
        }
    }

    background: Rectangle {
        radius: root.theme.radiusSmall + 2
        color: root.down
            ? root.theme.activeBg
            : root.current
                ? root.theme.hoverBg
                : root.hovered
                    ? Qt.rgba(1, 1, 1, 0.04)
                    : "transparent"
        border.width: root.activeFocus ? 1 : 0
        border.color: root.theme.accent
    }
}

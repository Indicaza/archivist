import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../Icons"

// A line in a Home module: an icon or a dot, a title over a quieter line,
// something on the right (a time), and actions that appear on hover or
// keyboard focus.
Button {
    id: root

    required property var theme
    property string iconName: ""
    // A small colored dot instead of an icon (activity, project hue).
    property color dotColor: "transparent"
    property bool showDot: false
    property string title: ""
    property string subtitle: ""
    property string trailing: ""
    property bool emphasized: false
    // Actions revealed on hover; HomeChip items.
    property alias actions: actionRow.data
    // Shown before everything, in Edit Home (a grip).
    property alias leading: leadingRow.data

    implicitHeight: subtitle.length > 0 ? 50 : 38
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    padding: 0
    leftPadding: 10
    rightPadding: 6
    Accessible.name: title
    Accessible.description: subtitle

    readonly property bool lit: hovered || activeFocus || actionsHovered.hovered

    HoverHandler {
        id: actionsHovered
    }

    contentItem: RowLayout {
        spacing: 10

        Row {
            id: leadingRow

            Layout.alignment: Qt.AlignVCenter
            visible: children.length > 0
        }

        Item {
            Layout.preferredWidth: 16
            Layout.preferredHeight: 16
            Layout.alignment: Qt.AlignVCenter

            AppIcon {
                anchors.centerIn: parent
                visible: !root.showDot && root.iconName.length > 0
                name: root.iconName
                tone: root.lit ? "normal" : "muted"
                iconSize: 14
            }

            Rectangle {
                anchors.centerIn: parent
                visible: root.showDot
                width: 7
                height: 7
                radius: 3.5
                color: root.dotColor
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: 2

            Text {
                Layout.fillWidth: true
                text: root.title
                color: root.lit || root.emphasized ? root.theme.appText : Qt.darker(root.theme.appText, 1.06)
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.textControlSize
                font.weight: root.emphasized ? root.theme.textWeightEmphasis : root.theme.textWeightRegular
                elide: Text.ElideRight
            }

            Text {
                Layout.fillWidth: true
                visible: root.subtitle.length > 0
                text: root.subtitle
                color: root.theme.mutedText
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.textCaptionSize
                elide: Text.ElideRight
            }
        }

        Text {
            Layout.alignment: Qt.AlignVCenter
            visible: root.trailing.length > 0 && !(root.lit && actionRow.children.length > 0)
            text: root.trailing
            color: root.theme.mutedText
            font.family: root.theme.bodyFontFamily
            font.pixelSize: root.theme.textCaptionSize
        }

        Row {
            id: actionRow

            Layout.alignment: Qt.AlignVCenter
            spacing: 0
            visible: root.lit && children.length > 0
        }
    }

    background: Rectangle {
        radius: 10
        color: root.down
            ? root.theme.withAlpha(root.theme.appText, 0.08)
            : root.lit ? root.theme.withAlpha(root.theme.appText, 0.045) : "transparent"
        border.width: root.activeFocus ? 1 : 0
        border.color: root.theme.withAlpha(root.theme.accent, 0.8)

        Behavior on color {
            enabled: !root.theme.reducedMotion
            ColorAnimation { duration: root.theme.motionFast }
        }
    }
}

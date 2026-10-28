import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../Icons"

// One thing to reopen: a conversation, a project, a file.
Button {
    id: root

    required property var theme
    property string iconName: "chat"
    property string iconTone: ""
    property string title: ""
    property string subtitle: ""
    property string meta: ""
    property bool current: false

    implicitHeight: 46
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    padding: 0
    leftPadding: 10
    rightPadding: 10

    Accessible.name: subtitle.length > 0 ? title + ", " + subtitle : title

    contentItem: RowLayout {
        spacing: 10

        Rectangle {
            Layout.preferredWidth: 28
            Layout.preferredHeight: 28
            Layout.alignment: Qt.AlignVCenter
            radius: root.theme.radiusSmall
            color: root.current ? root.theme.activeBg : root.theme.controlSurfaceBg
            border.width: 1
            border.color: root.current ? root.theme.accent : root.theme.quietBorder

            AppIcon {
                anchors.centerIn: parent
                name: root.iconName
                tone: root.iconTone.length > 0
                    ? root.iconTone
                    : root.hovered || root.activeFocus || root.current ? "accent" : "muted"
                iconSize: 14
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: 1

            Text {
                Layout.fillWidth: true
                text: root.title
                color: root.theme.appText
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.textControlSize
                font.weight: root.theme.textWeightEmphasis
                elide: Text.ElideRight
            }

            Text {
                Layout.fillWidth: true
                visible: root.subtitle.length > 0
                text: root.subtitle
                color: root.theme.mutedText
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.textCaptionSize
                elide: Text.ElideMiddle
            }
        }

        Text {
            Layout.alignment: Qt.AlignVCenter
            visible: root.meta.length > 0
            text: root.meta
            color: root.theme.mutedText
            font.family: root.theme.bodyFontFamily
            font.pixelSize: root.theme.textCaptionSize
        }
    }

    background: Rectangle {
        radius: root.theme.radiusMedium
        color: root.down
            ? root.theme.activeBg
            : root.hovered
                ? root.theme.hoverBg
                : "transparent"
        border.width: root.activeFocus ? 1 : 0
        border.color: root.theme.accent

        Behavior on color {
            enabled: !root.theme.reducedMotion
            ColorAnimation { duration: root.theme.motionFast }
        }
    }
}

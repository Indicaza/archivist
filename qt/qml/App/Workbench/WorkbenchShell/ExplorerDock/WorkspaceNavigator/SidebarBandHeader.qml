import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property var theme
    required property string title
    required property string glyph
    required property int count
    required property bool expanded

    property bool primaryVisible: false
    property bool primaryEnabled: true
    property string primaryText: "+"
    property string primaryToolTip: ""
    property bool secondaryVisible: false
    property bool secondaryEnabled: true
    property string secondaryText: "•••"
    property string secondaryToolTip: ""

    signal toggleRequested()
    signal primaryRequested()
    signal secondaryRequested()

    color: theme.controlSurfaceBg

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: root.theme.quietBorder
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        spacing: 4

        Button {
            id: toggleButton

            Layout.fillWidth: true
            Layout.fillHeight: true
            hoverEnabled: true
            padding: 0
            onClicked: root.toggleRequested()

            contentItem: RowLayout {
                spacing: 7

                Text {
                    text: root.expanded ? "▾" : "▸"
                    color: toggleButton.hovered
                        ? root.theme.appText
                        : root.theme.mutedText
                    font.pixelSize: root.theme.typeSize(9)
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                Text {
                    text: root.glyph
                    color: root.expanded
                        ? root.theme.accentBright
                        : root.theme.mutedText
                    font.pixelSize: root.theme.typeSize(10)
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                Text {
                    Layout.fillWidth: true
                    text: root.title
                    color: toggleButton.hovered || root.expanded
                        ? root.theme.appText
                        : root.theme.mutedText
                    font.pixelSize: root.theme.typeSize(9)
                    font.weight: Font.Bold
                    font.letterSpacing: 0.8
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }

                Rectangle {
                    Layout.preferredWidth: countLabel.implicitWidth + 12
                    Layout.preferredHeight: 20
                    radius: 10
                    color: root.expanded
                        ? root.theme.activeBg
                        : root.theme.surfaceBg
                    border.width: 1
                    border.color: root.theme.quietBorder

                    Text {
                        id: countLabel

                        anchors.centerIn: parent
                        text: String(root.count)
                        color: root.theme.mutedText
                        font.pixelSize: root.theme.typeSize(8)
                        font.weight: Font.DemiBold
                    }
                }
            }

            background: Rectangle {
                radius: root.theme.radiusSmall
                color: parent.hovered ? root.theme.hoverBg : "transparent"
            }
        }

        Button {
            id: primaryButton

            visible: root.primaryVisible
            Layout.preferredWidth: visible ? 24 : 0
            Layout.preferredHeight: 24
            text: root.primaryText
            enabled: root.primaryEnabled
            hoverEnabled: true
            padding: 0
            ToolTip.visible: hovered && root.primaryToolTip.length > 0
            ToolTip.text: root.primaryToolTip
            onClicked: root.primaryRequested()

            contentItem: Text {
                text: parent.text
                color: parent.enabled && parent.hovered
                    ? root.theme.appText
                    : root.theme.mutedText
                font.pixelSize: root.theme.typeSize(12)
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                opacity: parent.enabled ? 1 : 0.42
            }

            background: Rectangle {
                radius: 4
                color: parent.hovered ? root.theme.hoverBg : "transparent"
                border.width: parent.hovered ? 1 : 0
                border.color: root.theme.panelBorder
            }
        }

        Button {
            id: secondaryButton

            visible: root.secondaryVisible
            Layout.preferredWidth: visible ? 24 : 0
            Layout.preferredHeight: 24
            text: root.secondaryText
            enabled: root.secondaryEnabled
            hoverEnabled: true
            padding: 0
            ToolTip.visible: hovered && root.secondaryToolTip.length > 0
            ToolTip.text: root.secondaryToolTip
            onClicked: root.secondaryRequested()

            contentItem: Text {
                text: parent.text
                color: parent.enabled && parent.hovered
                    ? root.theme.appText
                    : root.theme.mutedText
                font.pixelSize: root.theme.typeSize(8)
                font.weight: Font.Bold
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                opacity: parent.enabled ? 1 : 0.42
            }

            background: Rectangle {
                radius: 4
                color: parent.hovered ? root.theme.hoverBg : "transparent"
                border.width: parent.hovered ? 1 : 0
                border.color: root.theme.panelBorder
            }
        }
    }
}

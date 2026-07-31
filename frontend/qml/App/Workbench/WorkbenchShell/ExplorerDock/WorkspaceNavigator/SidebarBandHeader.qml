import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property var theme
    required property string title
    required property string iconName
    required property int count
    required property bool expanded

    property bool primaryVisible: false
    property bool primaryEnabled: true
    property string primaryIconName: "add"
    property string primaryToolTip: ""
    property bool secondaryVisible: false
    property bool secondaryEnabled: true
    property string secondaryIconName: "more"
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

                AppIcon {
                    iconSize: 11
                    name: root.expanded ? "chevron-down" : "chevron-right"
                    tone: toggleButton.hovered ? "normal" : "muted"
                    accessibleLabel: root.expanded ? "Collapse" : "Expand"
                }

                AppIcon {
                    iconSize: 15
                    name: root.iconName
                    tone: root.expanded ? "accent" : "muted"
                    accessibleLabel: root.title
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

        IconButton {
            id: primaryButton

            visible: root.primaryVisible
            Layout.preferredWidth: visible ? 24 : 0
            Layout.preferredHeight: 24
            width: 24
            height: 24
            theme: root.theme
            iconName: root.primaryIconName
            iconSize: 14
            enabled: root.primaryEnabled
            toolTipText: root.primaryToolTip
            onClicked: root.primaryRequested()
        }

        IconButton {
            id: secondaryButton

            visible: root.secondaryVisible
            Layout.preferredWidth: visible ? 24 : 0
            Layout.preferredHeight: 24
            width: 24
            height: 24
            theme: root.theme
            iconName: root.secondaryIconName
            iconSize: 14
            enabled: root.secondaryEnabled
            toolTipText: root.secondaryToolTip
            onClicked: root.secondaryRequested()
        }
    }
}

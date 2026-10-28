import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../Icons"

// One module on Home: a quiet heading (small caps) over its content. In
// Edit Home the heading grows controls (move, width, hide) and the module
// gets an outline, so what can be arranged is obvious.
Item {
    id: root

    required property var theme
    // The module's entry from HomeStore.modules.
    property var module: ({})
    property bool editing: false
    property bool canMoveEarlier: false
    property bool canMoveLater: false
    property string title: String(module.title || "")
    property string iconName: String(module.icon || "")
    // Small text after the title ("3", "2 new").
    property string note: ""
    // Header actions shown outside Edit Home (HomeChip, say).
    property alias actions: actionRow.data
    default property alias content: body.data

    readonly property bool shown: module.visible !== false

    signal moveRequested(int step)
    signal visibilityToggled()
    signal spanToggled()

    implicitHeight: shown || editing ? column.implicitHeight : 0
    visible: shown || editing
    opacity: shown ? 1 : 0.55

    Behavior on opacity {
        enabled: !root.theme.reducedMotion
        NumberAnimation { duration: root.theme.motionPanel }
    }

    // Edit Home outline.
    Rectangle {
        anchors.fill: parent
        anchors.margins: -9
        radius: root.theme.homeCardRadius + 4
        color: root.editing ? root.theme.withAlpha(root.theme.appText, 0.02) : "transparent"
        border.width: root.editing ? 1 : 0
        border.color: root.shown ? root.theme.homeCardBorderHover : root.theme.homeCardBorder
        opacity: root.editing ? 1 : 0

        Behavior on opacity {
            enabled: !root.theme.reducedMotion
            NumberAnimation { duration: root.theme.motionPanel }
        }
    }

    ColumnLayout {
        id: column

        width: parent.width
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            spacing: 8

            AppIcon {
                Layout.alignment: Qt.AlignVCenter
                visible: root.iconName.length > 0
                name: root.iconName
                tone: "muted"
                iconSize: 12
            }

            Text {
                Layout.alignment: Qt.AlignVCenter
                text: root.title.toUpperCase()
                color: root.theme.homeEyebrow
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.typeEyebrow
                font.weight: root.theme.textWeightEmphasis
                font.letterSpacing: root.theme.textTrackingEyebrow
                Accessible.role: Accessible.Heading
                Accessible.name: root.title
            }

            Text {
                Layout.alignment: Qt.AlignVCenter
                visible: root.note.length > 0 && !root.editing
                text: root.note
                color: root.theme.withAlpha(root.theme.homeEyebrow, 0.75)
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.typeEyebrow
            }

            Text {
                Layout.alignment: Qt.AlignVCenter
                visible: root.editing && !root.shown
                text: "Hidden"
                color: root.theme.homeEyebrow
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.typeEyebrow
                font.italic: true
            }

            Item {
                Layout.fillWidth: true
            }

            Row {
                id: actionRow

                Layout.alignment: Qt.AlignVCenter
                spacing: 2
                visible: !root.editing && root.shown
            }

            Row {
                Layout.alignment: Qt.AlignVCenter
                spacing: 2
                visible: root.editing

                HomeChip {
                    theme: root.theme
                    round: true
                    implicitHeight: 28
                    iconName: "chevron-up"
                    iconSize: 11
                    enabled: root.canMoveEarlier
                    toolTipText: "Move earlier"
                    onClicked: root.moveRequested(-1)
                }

                HomeChip {
                    theme: root.theme
                    round: true
                    implicitHeight: 28
                    iconName: "chevron-down"
                    iconSize: 11
                    enabled: root.canMoveLater
                    toolTipText: "Move later"
                    onClicked: root.moveRequested(1)
                }

                HomeChip {
                    theme: root.theme
                    round: true
                    implicitHeight: 28
                    iconName: root.module.span === "half" ? "span-full" : "span-half"
                    iconSize: 13
                    visible: root.shown
                    toolTipText: root.module.span === "half" ? "Full width" : "Half width"
                    onClicked: root.spanToggled()
                }

                HomeChip {
                    theme: root.theme
                    implicitHeight: 28
                    iconName: root.shown ? "eye-off" : "eye"
                    iconSize: 13
                    label: root.shown ? "Hide" : "Show"
                    onClicked: root.visibilityToggled()
                }
            }
        }

        Item {
            id: body

            Layout.fillWidth: true
            visible: root.shown
            implicitHeight: childrenRect.height
        }
    }
}

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Archivist.Services 1.0
import "../../Icons"

// Layouts: the person's saved workspace layouts, and the built-in ones,
// one click from Home. A layout is how a project is presented; choosing
// one goes back to the current project and applies it there.
Item {
    id: root

    required property var theme
    required property var home

    signal applyRequested(var layout)

    readonly property var saved: Docking.savedLayouts || []
    readonly property var builtIn: Docking.presets || []
    readonly property var all: saved.concat(builtIn)

    width: parent ? parent.width : 0
    implicitHeight: flow.implicitHeight + (note.visible ? note.implicitHeight + 10 : 0)

    Flow {
        id: flow

        width: parent.width
        spacing: 8

        Repeater {
            model: root.all

            delegate: HomeCard {
                id: chip

                required property var modelData
                required property int index

                readonly property bool current: String(modelData.id) === String(Docking.currentLayoutId || "")

                width: Math.min(implicitWidth, flow.width)
                implicitWidth: chipRow.implicitWidth + 28
                height: 40
                theme: root.theme
                emphasized: current
                enabled: root.home.hasProject
                Accessible.name: "Apply " + String(modelData.name || "layout")
                onClicked: root.applyRequested(modelData)

                contentItem: Item {
                    Row {
                        id: chipRow

                        anchors.centerIn: parent
                        spacing: 8

                        AppIcon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: "grid"
                            tone: chip.current ? "accent" : chip.lit ? "normal" : "muted"
                            iconSize: 12
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: String(chip.modelData.name || "Layout")
                            color: chip.lit || chip.current ? root.theme.appText : root.theme.mutedText
                            font.family: root.theme.bodyFontFamily
                            font.pixelSize: root.theme.textControlSize - 1
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: chip.index < root.saved.length
                            text: "saved"
                            color: root.theme.withAlpha(root.theme.mutedText, 0.75)
                            font.family: root.theme.bodyFontFamily
                            font.pixelSize: root.theme.textCaptionSize - 1
                        }
                    }
                }
            }
        }
    }

    Text {
        id: note

        anchors.top: flow.bottom
        anchors.topMargin: 10
        width: parent.width
        visible: !root.home.hasProject || root.all.length === 0
        text: root.all.length === 0
            ? "Save a layout from the workspace's layout picker and it appears here."
            : "Open a project to apply a layout."
        color: root.theme.mutedText
        font.family: root.theme.bodyFontFamily
        font.pixelSize: root.theme.textCaptionSize
        wrapMode: Text.Wrap
    }
}

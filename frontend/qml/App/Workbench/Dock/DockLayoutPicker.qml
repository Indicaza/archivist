import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Archivist.Services 1.0
import "../../Icons"

// Layout switcher: built-in presets and the person's own layouts, each with
// a miniature. Switching re-homes the open views; nothing opens or closes.
Popup {
    id: root

    required property var theme
    property int previewRevision: 0

    width: Math.min(600, (parent ? parent.width : 600) - 32)
    padding: 18
    topPadding: 16
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    onAboutToShow: {
        previewRevision += 1
        saveField.text = ""
    }

    enter: Transition {
        enabled: !root.theme.reducedMotion
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 140; easing.type: Easing.OutCubic }
        NumberAnimation { property: "scale"; from: 0.97; to: 1; duration: 180; easing.type: Easing.OutBack }
    }
    exit: Transition {
        enabled: !root.theme.reducedMotion
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 100 }
    }

    background: Item {
        Rectangle {
            anchors.fill: parent
            anchors.topMargin: 6
            anchors.leftMargin: 3
            anchors.rightMargin: -3
            anchors.bottomMargin: -8
            radius: 14
            color: "#60000000"
        }

        Rectangle {
            anchors.fill: parent
            radius: 12
            color: root.theme.controlSurfaceBg
            border.width: 1
            border.color: root.theme.panelBorder
        }
    }

    function choose(layoutId) {
        root.close()
        Docking.applyLayout(layoutId)
    }

    contentItem: ColumnLayout {
        spacing: 14

        RowLayout {
            Layout.fillWidth: true

            Column {
                Layout.fillWidth: true
                spacing: 2

                Text {
                    text: "Layouts"
                    color: root.theme.appText
                    font.family: root.theme.titleFontFamily
                    font.pixelSize: root.theme.textPanelTitleSize
                    font.weight: root.theme.textWeightEmphasis
                }

                Text {
                    text: "Rearrange your panels. Nothing you have open is closed."
                    color: root.theme.mutedText
                    font.family: root.theme.bodyFontFamily
                    font.pixelSize: root.theme.textCaptionSize
                }
            }

            Text {
                Layout.alignment: Qt.AlignTop
                text: "Ctrl+Alt+L"
                color: root.theme.mutedText
                opacity: 0.8
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.textMetadataSize
            }
        }

        GridLayout {
            id: presetGrid

            Layout.fillWidth: true
            columns: 3
            columnSpacing: 18
            rowSpacing: 16

            Repeater {
                model: Docking.presets

                delegate: DockLayoutCard {
                    required property var modelData

                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    theme: root.theme
                    layoutId: String(modelData.id)
                    name: String(modelData.name)
                    description: String(modelData.description)
                    active: Docking.currentLayoutId === layoutId
                    previewRevision: root.previewRevision
                    onChosen: root.choose(layoutId)
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: root.theme.quietBorder
        }

        Text {
            text: "YOUR LAYOUTS"
            color: root.theme.mutedText
            font.family: root.theme.bodyFontFamily
            font.pixelSize: root.theme.textMetadataSize
            font.weight: root.theme.textWeightStrong
            font.letterSpacing: root.theme.textTrackingLabel
        }

        GridLayout {
            Layout.fillWidth: true
            visible: Docking.savedLayouts.length > 0
            columns: 3
            columnSpacing: 18
            rowSpacing: 16

            Repeater {
                model: Docking.savedLayouts

                delegate: DockLayoutCard {
                    required property var modelData

                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.alignment: Qt.AlignTop
                    theme: root.theme
                    layoutId: String(modelData.id)
                    name: String(modelData.name)
                    editable: true
                    active: Docking.currentLayoutId === layoutId
                    previewRevision: root.previewRevision
                    onChosen: root.choose(layoutId)
                    onRenamed: function(name) { Docking.renameLayout(layoutId, name) }
                    onDeleted: Docking.deleteLayout(layoutId)
                }
            }

            // Keep cards their normal width when there are only one or two.
            Repeater {
                model: Math.max(0, 3 - Docking.savedLayouts.length)
                delegate: Item { Layout.fillWidth: true; Layout.preferredWidth: 1 }
            }
        }

        Text {
            Layout.fillWidth: true
            visible: Docking.savedLayouts.length === 0
            text: "Arrange panels the way you like, then save it here to come back to it in any Collection."
            wrapMode: Text.Wrap
            color: root.theme.mutedText
            font.family: root.theme.bodyFontFamily
            font.pixelSize: root.theme.textCaptionSize
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            TextField {
                id: saveField

                Layout.fillWidth: true
                Layout.preferredHeight: 30
                placeholderText: "Save the current arrangement as…"
                placeholderTextColor: root.theme.mutedText
                color: root.theme.appText
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.textCaptionSize
                leftPadding: 10
                selectByMouse: true
                background: Rectangle {
                    radius: root.theme.radiusSmall + 1
                    color: root.theme.workspaceBgDeep
                    border.width: 1
                    border.color: saveField.activeFocus ? root.theme.accent : root.theme.panelBorder

                    Behavior on border.color { ColorAnimation { duration: root.theme.motionFast } }
                }
                onAccepted: saveButton.clicked()
            }

            DockDialogButton {
                id: saveButton

                theme: root.theme
                primary: true
                text: "Save Layout"
                enabled: saveField.text.trim().length > 0
                onClicked: {
                    if (Docking.saveLayout(saveField.text).length > 0) {
                        saveField.text = ""
                        root.previewRevision += 1
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: root.theme.quietBorder
        }

        // How the person likes the workspace, as the assistant remembered it.
        // Each can be forgotten here.
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            visible: Docking.workspacePreferences.length > 0

            Text {
                text: "Workspace preferences"
                color: root.theme.appText
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.textCaptionSize
            }

            Repeater {
                model: Docking.workspacePreferences

                delegate: RowLayout {
                    required property var modelData

                    Layout.fillWidth: true
                    spacing: 6

                    Text {
                        Layout.fillWidth: true
                        text: String(modelData.sentence || "")
                        elide: Text.ElideRight
                        color: root.theme.appText
                        font.family: root.theme.bodyFontFamily
                        font.pixelSize: root.theme.textMetadataSize
                    }

                    Text {
                        text: modelData.lasting ? "always" : "this session"
                        color: root.theme.mutedText
                        font.family: root.theme.bodyFontFamily
                        font.pixelSize: root.theme.textMetadataSize
                    }

                    IconButton {
                        theme: root.theme
                        width: 22
                        height: 22
                        iconName: "close"
                        iconSize: 10
                        toolTipText: "Forget"
                        onClicked: Docking.forgetPreference(String(modelData.id))
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: root.theme.quietBorder
            visible: Docking.workspacePreferences.length > 0
        }

        // The size of the whole interface lives here too, for discoverability.
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Text {
                text: "Interface size"
                color: root.theme.appText
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.textCaptionSize
            }

            Text {
                Layout.fillWidth: true
                text: Qt.platform.os === "osx" ? "⌘+  ⌘−  ⌘0" : "Ctrl +  −  0"
                color: root.theme.mutedText
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.textMetadataSize
            }

            IconButton {
                theme: root.theme
                width: 26
                height: 26
                iconName: "zoom-out"
                iconSize: 12
                toolTipText: "Smaller"
                enabled: InterfaceScale.canShrink
                onClicked: InterfaceScale.step(-1)
            }

            Text {
                Layout.preferredWidth: 42
                horizontalAlignment: Text.AlignHCenter
                text: Math.round(InterfaceScale.scale * 100) + "%"
                color: root.theme.appText
                font.family: root.theme.bodyFontFamily
                font.pixelSize: root.theme.textCaptionSize
                font.weight: root.theme.textWeightEmphasis
            }

            IconButton {
                theme: root.theme
                width: 26
                height: 26
                iconName: "zoom-in"
                iconSize: 12
                toolTipText: "Larger"
                enabled: InterfaceScale.canGrow
                onClicked: InterfaceScale.step(1)
            }

            DockDialogButton {
                visible: Math.abs(InterfaceScale.scale - 1) > 0.001
                theme: root.theme
                implicitHeight: 26
                text: "Reset"
                onClicked: InterfaceScale.reset()
            }
        }
    }
}

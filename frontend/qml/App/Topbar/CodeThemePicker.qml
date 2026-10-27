import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Archivist.Services 1.0

// Picks the colors for code in chat and the editor, from Archivist's own
// theme or themes imported from VS Code, Sublime Text and TextMate.
Popup {
    id: root

    required property var theme
    property string statusText: ""
    property bool statusIsError: false

    width: 340
    height: Math.min(contentColumn.implicitHeight + topPadding + bottomPadding, 560)
    padding: 14
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    onOpened: {
        statusText = ""
        statusIsError = false
    }

    enter: Transition {
        NumberAnimation {
            property: "opacity"
            from: 0.0
            to: 1.0
            duration: root.theme.motionFast
            easing.type: Easing.OutCubic
        }
    }

    exit: Transition {
        NumberAnimation {
            property: "opacity"
            from: 1.0
            to: 0.0
            duration: root.theme.motionFast
            easing.type: Easing.InCubic
        }
    }

    background: Rectangle {
        color: root.theme.surfaceBg
        border.width: 1
        border.color: root.theme.panelBorder
        radius: root.theme.radiusPanel
    }

    function formatLabel(format) {
        if (format === "vscode") {
            return "VS Code"
        }
        if (format === "tmtheme") {
            return "TextMate"
        }
        if (format === "sublime") {
            return "Sublime Text"
        }
        return "Built in"
    }

    function importSelected(fileUrl) {
        var result = CodeThemes.importFile(fileUrl)

        statusIsError = !result.ok
        statusText = result.ok
            ? "Imported " + result.name + "."
            : String(result.error || "The theme could not be imported.")
    }

    function exportTo(folderUrl) {
        var result = CodeThemes.exportArchivistExtension(folderUrl)

        statusIsError = !result.ok
        statusText = result.ok
            ? "Saved to " + result.path + ". In VS Code, Cursor or VSCodium, run \"Developer: Install Extension from Location…\" and pick that folder."
            : String(result.error || "The theme could not be saved.")
    }

    ColumnLayout {
        id: contentColumn

        anchors.fill: parent
        spacing: 10

        Text {
            Layout.fillWidth: true
            text: "Code Theme"
            color: root.theme.appText
            font.family: root.theme.bodyFontFamily
            font.pixelSize: root.theme.textControlSize
            font.weight: root.theme.textWeightStrong
        }

        Text {
            Layout.fillWidth: true
            text: "Colors code in chat and the editor. Import color themes from VS Code (.json), Sublime Text (.sublime-color-scheme) or TextMate (.tmTheme)."
            color: root.theme.mutedText
            font.family: root.theme.bodyFontFamily
            font.pixelSize: root.theme.textCaptionSize
            wrapMode: Text.WordWrap
            lineHeight: root.theme.textLineHeightCompact
        }

        ListView {
            id: themeList

            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 300)
            Layout.fillHeight: true
            clip: true
            spacing: 2
            model: CodeThemes.themes
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: ScrollBar {
                policy: themeList.contentHeight > themeList.height
                    ? ScrollBar.AsNeeded
                    : ScrollBar.AlwaysOff
            }

            delegate: Rectangle {
                id: row

                required property var modelData

                readonly property bool current: modelData.id === CodeThemes.currentThemeId

                width: ListView.view.width
                height: 44
                radius: root.theme.radiusSmall
                color: current
                    ? root.theme.activeBg
                    : rowHover.hovered
                        ? root.theme.hoverBg
                        : "transparent"
                border.width: current ? 1 : 0
                border.color: root.theme.accent

                Accessible.role: Accessible.RadioButton
                Accessible.name: modelData.name
                Accessible.checked: current

                HoverHandler {
                    id: rowHover
                }

                TapHandler {
                    onTapped: CodeThemes.currentThemeId = row.modelData.id
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 6
                    spacing: 10

                    // A small preview: the theme's background with its main
                    // token colors.
                    Rectangle {
                        Layout.preferredWidth: 58
                        Layout.preferredHeight: 28
                        radius: 4
                        color: row.modelData.background
                        border.width: 1
                        border.color: root.theme.withAlpha(root.theme.appText, 0.14)

                        Row {
                            anchors.centerIn: parent
                            spacing: 3

                            Repeater {
                                model: row.modelData.swatches

                                Rectangle {
                                    required property var modelData

                                    width: 7
                                    height: 7
                                    radius: 3.5
                                    color: modelData
                                }
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1

                        Text {
                            Layout.fillWidth: true
                            text: row.modelData.name
                            color: root.theme.appText
                            font.family: root.theme.bodyFontFamily
                            font.pixelSize: root.theme.typeLabel
                            elide: Text.ElideRight
                        }

                        Text {
                            Layout.fillWidth: true
                            text: root.formatLabel(row.modelData.sourceFormat)
                                + (String(row.modelData.type).indexOf("light") >= 0 ? " · Light" : "")
                            color: root.theme.mutedText
                            font.family: root.theme.bodyFontFamily
                            font.pixelSize: root.theme.textCaptionSize
                            elide: Text.ElideRight
                        }
                    }

                    IconButton {
                        visible: !row.modelData.builtIn
                            && (rowHover.hovered || activeFocus)
                        Layout.preferredWidth: 26
                        Layout.preferredHeight: 26
                        theme: root.theme
                        iconName: "close"
                        iconSize: 11
                        toolTipText: "Remove " + row.modelData.name
                        onClicked: CodeThemes.removeTheme(row.modelData.id)
                    }

                    AppIcon {
                        visible: row.current
                        name: "check"
                        tone: "accent"
                        iconSize: 13
                        accessibleLabel: "Current theme"
                    }
                }
            }
        }

        Text {
            Layout.fillWidth: true
            visible: root.statusText.length > 0
            text: root.statusText
            color: root.statusIsError ? root.theme.danger : root.theme.mutedText
            font.family: root.theme.bodyFontFamily
            font.pixelSize: root.theme.textCaptionSize
            wrapMode: Text.WordWrap
            lineHeight: root.theme.textLineHeightCompact
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Button {
                id: importButton

                Layout.fillWidth: true
                Layout.preferredHeight: 32
                text: "Import Theme…"
                hoverEnabled: true
                onClicked: importDialog.open()

                contentItem: Text {
                    text: importButton.text
                    color: root.theme.appText
                    font.family: root.theme.bodyFontFamily
                    font.pixelSize: root.theme.textCaptionSize
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                background: Rectangle {
                    radius: 5
                    color: importButton.hovered ? "#625783" : "#51486e"
                    border.width: 1
                    border.color: root.theme.accent
                }
            }

            Button {
                id: exportButton

                Layout.fillWidth: true
                Layout.preferredHeight: 32
                text: "Export for VS Code…"
                hoverEnabled: true
                onClicked: exportDialog.open()

                ToolTip.visible: hovered
                ToolTip.delay: 350
                ToolTip.text: "Save Archivist's colors as a VS Code theme extension"

                contentItem: Text {
                    text: exportButton.text
                    color: root.theme.appText
                    font.family: root.theme.bodyFontFamily
                    font.pixelSize: root.theme.textCaptionSize
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                background: Rectangle {
                    radius: 5
                    color: exportButton.hovered ? root.theme.hoverBg : "transparent"
                    border.width: 1
                    border.color: root.theme.panelBorder
                }
            }
        }
    }

    FileDialog {
        id: importDialog

        title: "Import Color Theme"
        fileMode: FileDialog.OpenFile
        nameFilters: [
            "Color themes (*.json *.tmTheme *.sublime-color-scheme)",
            "All files (*)"
        ]
        onAccepted: root.importSelected(selectedFile)
    }

    FolderDialog {
        id: exportDialog

        title: "Export Archivist Theme for VS Code"
        acceptLabel: "Export Here"
        onAccepted: root.exportTo(selectedFolder)
    }
}

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../../Icons"

// The first launch: no projects yet. Three steps that say how Archivist
// works; only the first is an action, because it's the only one that can
// be taken now.
Item {
    id: root

    required property var theme

    signal setupProjectRequested()

    readonly property bool row: width >= 720

    implicitHeight: grid.implicitHeight

    GridLayout {
        id: grid

        width: parent.width
        columns: root.row ? 3 : 1
        columnSpacing: 14
        rowSpacing: 14

        Repeater {
            model: [
                {
                    step: "1",
                    title: "Choose your folders",
                    body: "A project is a set of folders Archivist reads and works in: code, notes, a manuscript.",
                    action: "Set up a project"
                },
                {
                    step: "2",
                    title: "Ask about them",
                    body: "Questions, summaries, drafts and edits, with the sources it used one click away.",
                    action: ""
                },
                {
                    step: "3",
                    title: "Shape your workspace",
                    body: "Editors, terminals and conversations arranged your way, remembered for each project.",
                    action: ""
                }
            ]

            delegate: HomeCard {
                id: card

                required property var modelData

                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.preferredHeight: 176
                theme: root.theme
                emphasized: modelData.action.length > 0
                enabled: true
                focusPolicy: modelData.action.length > 0 ? Qt.StrongFocus : Qt.NoFocus
                hoverEnabled: modelData.action.length > 0
                Accessible.name: modelData.title
                Accessible.description: modelData.body
                onClicked: {
                    if (modelData.action.length > 0) {
                        root.setupProjectRequested()
                    }
                }

                contentItem: Item {
                    Text {
                        x: 20
                        y: 18
                        text: card.modelData.step
                        color: card.modelData.action.length > 0 ? root.theme.accentBright : root.theme.homeEyebrow
                        font.family: root.theme.titleFontFamily
                        font.pixelSize: root.theme.px(22)
                    }

                    Text {
                        x: 20
                        y: 58
                        width: parent.width - 40
                        text: card.modelData.title
                        color: root.theme.chatHeadingText
                        font.family: root.theme.titleFontFamily
                        font.pixelSize: root.theme.px(17)
                        elide: Text.ElideRight
                    }

                    Text {
                        x: 20
                        y: 86
                        width: parent.width - 40
                        text: card.modelData.body
                        color: root.theme.mutedText
                        font.family: root.theme.bodyFontFamily
                        font.pixelSize: root.theme.textCaptionSize + 1
                        lineHeight: 1.25
                        wrapMode: Text.Wrap
                        maximumLineCount: 3
                        elide: Text.ElideRight
                    }

                    Row {
                        x: 20
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 16
                        spacing: 6
                        visible: card.modelData.action.length > 0

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: card.modelData.action
                            color: root.theme.appText
                            font.family: root.theme.bodyFontFamily
                            font.pixelSize: root.theme.textCaptionSize + 1
                            font.weight: root.theme.textWeightEmphasis
                        }

                        AppIcon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: "arrow-right"
                            tone: card.lit ? "accent" : "normal"
                            iconSize: 12
                        }
                    }
                }
            }
        }
    }
}

import QtQuick

Item {
    id: root

    required property var theme
    property string title: "Opening document"
    property string detail: "Preparing a local preview."
    property string fileLabel: ""

    implicitWidth: 420
    implicitHeight: 224

    Column {
        anchors.centerIn: parent
        width: Math.min(420, parent.width)
        spacing: 11

        Item {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 126
            height: 104

            Rectangle {
                x: 27
                y: 13
                width: 62
                height: 78
                rotation: -7
                radius: 5
                color: root.theme.controlSurfaceBg
                border.width: 1
                border.color: root.theme.quietBorder
                opacity: 0.42
            }

            Rectangle {
                x: 37
                y: 9
                width: 62
                height: 78
                rotation: 4
                radius: 5
                color: root.theme.controlSurfaceBg
                border.width: 1
                border.color: root.theme.panelBorder
                opacity: 0.68
            }

            Rectangle {
                id: frontPage

                x: 31
                y: 6
                width: 66
                height: 84
                radius: 5
                color: root.theme.controlSurfaceBg
                border.width: 1
                border.color: root.theme.accent
                clip: true

                Rectangle {
                    x: 11
                    y: 14
                    width: 27
                    height: 5
                    radius: 2
                    color: root.theme.accentBright
                    opacity: 0.9
                }

                Repeater {
                    model: 4

                    Rectangle {
                        required property int index

                        x: 11
                        y: 29 + index * 10
                        width: index === 3 ? 29 : 43
                        height: 3
                        radius: 2
                        color: root.theme.mutedText
                        opacity: 0.34
                    }
                }

                Rectangle {
                    id: scanLine

                    x: 5
                    width: parent.width - 10
                    height: 2
                    radius: 1
                    color: root.theme.accentBright
                    opacity: 0

                    SequentialAnimation on y {
                        loops: Animation.Infinite

                        PropertyAction {
                            value: 12
                        }

                        ParallelAnimation {
                            NumberAnimation {
                                target: scanLine
                                property: "y"
                                to: 72
                                duration: 1050
                                easing.type: Easing.InOutCubic
                            }

                            SequentialAnimation {
                                NumberAnimation {
                                    target: scanLine
                                    property: "opacity"
                                    to: 0.92
                                    duration: 180
                                }

                                PauseAnimation {
                                    duration: 650
                                }

                                NumberAnimation {
                                    target: scanLine
                                    property: "opacity"
                                    to: 0
                                    duration: 220
                                }
                            }
                        }

                        PauseAnimation {
                            duration: 220
                        }
                    }
                }
            }

            Rectangle {
                anchors.horizontalCenter: frontPage.horizontalCenter
                y: 95
                width: 54
                height: 3
                radius: 2
                color: root.theme.accent
                opacity: 0.16

                SequentialAnimation on width {
                    loops: Animation.Infinite

                    NumberAnimation {
                        to: 76
                        duration: 650
                        easing.type: Easing.InOutSine
                    }

                    NumberAnimation {
                        to: 54
                        duration: 650
                        easing.type: Easing.InOutSine
                    }
                }

                SequentialAnimation on opacity {
                    loops: Animation.Infinite

                    NumberAnimation {
                        to: 0.46
                        duration: 650
                    }

                    NumberAnimation {
                        to: 0.16
                        duration: 650
                    }
                }
            }
        }

        Text {
            width: parent.width
            text: root.title
            color: root.theme.appText
            font.pixelSize: root.theme.typeSize(16)
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
        }

        Text {
            width: parent.width
            text: root.detail
            color: root.theme.mutedText
            font.pixelSize: root.theme.typeSize(10)
            lineHeight: root.theme.typeLineHeightBody
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
        }

        Text {
            width: parent.width
            visible: root.fileLabel.length > 0
            text: root.fileLabel
            color: root.theme.accentBright
            opacity: 0.76
            elide: Text.ElideMiddle
            font.family: root.theme.monospaceFontFamily
            font.pixelSize: root.theme.typeSize(9)
            horizontalAlignment: Text.AlignHCenter
        }
    }
}

import QtQuick

Text {
    id: root

    required property string glyph
    required property string glyphFontFamily
    required property real glyphFontSize
    required property color hotColor
    required property color warmColor
    required property color settledColor
    required property real settledX
    required property real settledY
    required property real rise
    required property real startScale
    required property int settleDuration
    required property int coolingDuration
    property int birthDelay: 0

    x: settledX
    y: settledY - rise
    z: 40
    text: glyph
    color: hotColor
    opacity: 0
    scale: startScale
    transformOrigin: Item.Center
    font.family: glyphFontFamily
    font.pixelSize: glyphFontSize
    font.weight: Font.Medium
    font.kerning: true
    font.contextFontMerging: true
    font.preferTypoLineMetrics: true
    textFormat: Text.PlainText
    renderType: Text.QtRendering

    Component.onCompleted: arrival.start()

    SequentialAnimation {
        id: arrival

        PauseAnimation {
            duration: root.birthDelay
        }

        PropertyAction {
            target: root
            property: "opacity"
            value: 1
        }

        ParallelAnimation {
            NumberAnimation {
                target: root
                property: "y"
                to: root.settledY
                duration: root.settleDuration
                easing.type: Easing.OutCubic
            }

            NumberAnimation {
                target: root
                property: "scale"
                to: 1
                duration: root.settleDuration
                easing.type: Easing.OutCubic
            }

            SequentialAnimation {
                ColorAnimation {
                    target: root
                    property: "color"
                    to: root.warmColor
                    duration: Math.round(root.coolingDuration * 0.46)
                    easing.type: Easing.OutQuad
                }

                ColorAnimation {
                    target: root
                    property: "color"
                    to: root.settledColor
                    duration: Math.round(root.coolingDuration * 0.54)
                    easing.type: Easing.InOutQuad
                }
            }
        }

        NumberAnimation {
            target: root
            property: "opacity"
            to: 0
            duration: 60
            easing.type: Easing.InQuad
        }

        ScriptAction {
            script: root.destroy()
        }
    }
}

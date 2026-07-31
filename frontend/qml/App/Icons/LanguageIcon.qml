import QtQuick
import "LanguageRegistry.js" as LanguageRegistry
import "GeneratedSetiRegistry.js" as SetiRegistry

Item {
    id: root

    property string iconId: ""
    property string languageId: ""
    property string fileName: ""
    property string extension: ""
    property string tone: ""
    property bool active: false
    property bool hovered: false
    property real iconSize: 16
    property string accessibleLabel: ""

    readonly property string resolvedLanguageIcon: LanguageRegistry.languageIconName({
        iconId: iconId,
        languageId: languageId,
        fileName: fileName,
        extension: extension
    })
    readonly property string resolvedTone: tone.length > 0
        ? tone
        : "brand"
    readonly property string resolvedGlyph: SetiRegistry.glyph(
        resolvedLanguageIcon
    )
    readonly property bool hasLanguageIcon: resolvedGlyph.length > 0
        && setiFont.status === FontLoader.Ready
    readonly property string fallbackIconName:
        LanguageRegistry.fallbackAppIconName(root.iconId)
    readonly property string defaultFileGlyph: SetiRegistry.glyph("default")
    readonly property bool hasDefaultFileIcon:
        !root.hasLanguageIcon
        && root.fallbackIconName === "file"
        && root.defaultFileGlyph.length > 0
        && setiFont.status === FontLoader.Ready
    readonly property bool usesSetiGlyph:
        root.hasLanguageIcon || root.hasDefaultFileIcon

    implicitWidth: iconSize
    implicitHeight: iconSize
    width: iconSize
    height: iconSize

    Accessible.role: Accessible.Graphic
    Accessible.name: accessibleLabel

    FontLoader {
        id: setiFont

        source: Qt.resolvedUrl("Assets/fonts/seti.ttf")
    }

    Text {
        anchors.centerIn: parent
        width: root.iconSize * 1.45
        height: root.iconSize * 1.45
        visible: root.usesSetiGlyph
        text: root.hasLanguageIcon
            ? root.resolvedGlyph
            : root.defaultFileGlyph
        color: SetiRegistry.color(
            root.hasLanguageIcon
                ? root.resolvedLanguageIcon
                : "default",
            root.resolvedTone
        )
        font.family: setiFont.name
        font.pixelSize: root.iconSize * 1.28
        font.hintingPreference: Font.PreferFullHinting
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        renderType: Text.NativeRendering
    }

    AppIcon {
        anchors.centerIn: parent
        visible: !root.usesSetiGlyph
        name: root.fallbackIconName
        tone: root.resolvedTone === "brand" ? "accent" : root.resolvedTone
        iconSize: root.iconSize
        accessibleLabel: root.accessibleLabel
    }
}

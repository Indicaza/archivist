import QtQuick
import "IconRegistry.js" as IconRegistry
import "LanguageRegistry.js" as LanguageRegistry

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
    readonly property bool hasLanguageIcon: resolvedLanguageIcon.length > 0

    implicitWidth: iconSize
    implicitHeight: iconSize
    width: iconSize
    height: iconSize

    Accessible.role: Accessible.Graphic
    Accessible.name: accessibleLabel

    Image {
        id: languageImage

        anchors.centerIn: parent
        width: root.iconSize
        height: root.iconSize
        visible: root.hasLanguageIcon && status !== Image.Error
        source: root.hasLanguageIcon
            ? IconRegistry.languageIconSource(
                root.resolvedLanguageIcon,
                root.resolvedTone
            )
            : ""
        sourceSize.width: Math.ceil(root.iconSize * 2)
        sourceSize.height: Math.ceil(root.iconSize * 2)
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
        asynchronous: false
        cache: true
    }

    AppIcon {
        anchors.centerIn: parent
        visible: !root.hasLanguageIcon
            || languageImage.status === Image.Error
        name: LanguageRegistry.fallbackAppIconName(root.iconId)
        tone: root.resolvedTone === "brand" ? "accent" : root.resolvedTone
        iconSize: root.iconSize
        accessibleLabel: root.accessibleLabel
    }
}

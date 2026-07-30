import QtQuick
import "IconRegistry.js" as IconRegistry

Item {
    id: root

    property string name: "file"
    property string tone: "muted"
    property real iconSize: 16
    property string accessibleLabel: ""
    property bool mirrored: false
    property real iconRotation: 0

    implicitWidth: iconSize
    implicitHeight: iconSize
    width: iconSize
    height: iconSize

    Accessible.role: Accessible.Graphic
    Accessible.name: accessibleLabel

    Image {
        anchors.centerIn: parent
        width: root.iconSize
        height: root.iconSize
        source: IconRegistry.appIconSource(root.name, root.tone)
        sourceSize.width: Math.ceil(root.iconSize * 2)
        sourceSize.height: Math.ceil(root.iconSize * 2)
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
        mirror: root.mirrored
        rotation: root.iconRotation
        asynchronous: false
        cache: true
    }
}

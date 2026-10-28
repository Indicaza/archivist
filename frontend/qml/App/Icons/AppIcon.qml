import QtQuick
import Archivist.Services 1.0
import "IconRegistry.js" as IconRegistry

Item {
    id: root

    property string name: "file"
    property string tone: "muted"
    property real iconSize: 16
    property string accessibleLabel: ""
    property bool mirrored: false
    property real iconRotation: 0
    // Icons grow with the interface size (Cmd/Ctrl + -), like the text.
    readonly property real drawnSize: Math.round(iconSize * InterfaceScale.scale)

    implicitWidth: drawnSize
    implicitHeight: drawnSize
    width: drawnSize
    height: drawnSize

    Accessible.role: Accessible.Graphic
    Accessible.name: accessibleLabel

    Image {
        anchors.centerIn: parent
        width: root.drawnSize
        height: root.drawnSize
        source: IconRegistry.appIconSource(root.name, root.tone)
        sourceSize.width: Math.ceil(root.drawnSize * 2)
        sourceSize.height: Math.ceil(root.drawnSize * 2)
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
        mirror: root.mirrored
        rotation: root.iconRotation
        asynchronous: false
        cache: true
    }
}

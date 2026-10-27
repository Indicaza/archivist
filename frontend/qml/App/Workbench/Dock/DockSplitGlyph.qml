import QtQuick

// A small "split" icon: a frame with a divider.
Item {
    id: root

    property color color: "#9a9387"
    property bool vertical: false

    implicitWidth: 13
    implicitHeight: 13

    Rectangle {
        anchors.fill: parent
        anchors.margins: 0.5
        color: "transparent"
        radius: 2
        border.width: 1.2
        border.color: root.color
    }

    Rectangle {
        x: root.vertical ? 1 : parent.width / 2 - 0.6
        y: root.vertical ? parent.height / 2 - 0.6 : 1
        width: root.vertical ? parent.width - 2 : 1.2
        height: root.vertical ? 1.2 : parent.height - 2
        color: root.color
    }
}

import QtQuick
import QtQuick.Controls

Rectangle {
    id: root

    required property string title

    width: 320
    height: 160
    radius: 12
    color: "#24221d"

    Text {
        anchors.centerIn: parent
        text: root.title
        color: "#d8d2c7"
    }
}

pragma Singleton

import QtQuick

Item {
    id: root

    width: 0
    height: 0
    visible: false

    readonly property string family: loader.name
    readonly property bool ready:
        loader.status === FontLoader.Ready

    FontLoader {
        id: loader

        source: Qt.resolvedUrl("Assets/fonts/seti.ttf")
    }
}

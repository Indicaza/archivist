import QtQuick

// The quiet container lists sit in on Home: one panel, not a card per row.
Rectangle {
    id: root

    required property var theme
    default property alias content: inner.data
    property real padding: 6

    implicitHeight: inner.childrenRect.height + padding * 2
    radius: theme.homeCardRadius
    color: theme.withAlpha(theme.homeCardBg, 0.7)
    border.width: 1
    border.color: theme.homeCardBorder

    Item {
        id: inner

        x: root.padding
        y: root.padding
        width: root.width - root.padding * 2
        height: childrenRect.height
    }
}

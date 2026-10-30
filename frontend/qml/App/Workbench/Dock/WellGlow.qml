import QtQuick
import QtQuick.Shapes

// A soft round light or shadow: `color` at `strength` in the middle, fading
// to nothing at the edge. It has no hard edge, so it needs no antialiasing,
// and it's drawn once: scale it with a transform, never by resizing it.
Shape {
    id: root

    property color color: "#000000"
    property real strength: 1
    // Where the fade starts, 0..1 of the radius.
    property real core: 0

    width: 32
    height: 32

    ShapePath {
        strokeWidth: -1
        strokeColor: "transparent"
        fillGradient: RadialGradient {
            centerX: root.width / 2
            centerY: root.height / 2
            centerRadius: Math.min(root.width, root.height) / 2
            focalX: root.width / 2
            focalY: root.height / 2
            focalRadius: 0

            GradientStop { position: 0; color: Qt.rgba(root.color.r, root.color.g, root.color.b, root.strength) }
            GradientStop { position: root.core; color: Qt.rgba(root.color.r, root.color.g, root.color.b, root.strength) }
            GradientStop { position: 0.55; color: Qt.rgba(root.color.r, root.color.g, root.color.b, root.strength * 0.38) }
            GradientStop { position: 1; color: Qt.rgba(root.color.r, root.color.g, root.color.b, 0) }
        }

        startX: 0
        startY: 0
        PathLine { x: root.width; y: 0 }
        PathLine { x: root.width; y: root.height }
        PathLine { x: 0; y: root.height }
        PathLine { x: 0; y: 0 }
    }
}

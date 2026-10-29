import QtQuick
import Archivist.Services 1.0

// Keeps hold of the pointer for a drag whose source let go of it.
//
// The tab or panel being dragged holds the pointer. If it loses it before
// the button is released (its row rebuilt, a parent stealing the grab), the
// drag must not end, and nothing else under the pointer (a file in the
// sidebar, another panel) may pick it up. The workspace marks the drag
// orphaned; this shield, above everything, then takes the pointer over and
// carries the same drag on to the release.
Item {
    id: root

    // DockWorkspace: sourceOrphaned, moveDrag(globalPoint), endDrag().
    required property var workspace

    readonly property bool holding: shieldDrag.active

    function watch() {
        orphanWatch.restart()
    }

    objectName: "dock.dragShield"
    x: 0
    y: 0
    width: parent ? parent.width : 0
    height: parent ? parent.height : 0
    z: 1000

    DragHandler {
        id: shieldDrag

        target: null
        enabled: root.workspace.sourceOrphaned === true
        dragThreshold: 0
        grabPermissions: PointerHandler.CanTakeOverFromAnything

        onCentroidChanged: {
            if (active && Docking.dragActive) {
                root.workspace.moveDrag(root.mapToGlobal(centroid.position.x, centroid.position.y))
            }
        }
        onActiveChanged: {
            if (!active && root.workspace.sourceOrphaned) {
                orphanWatch.stop()
                if (Docking.dragActive) {
                    root.workspace.endDrag()
                }
                root.workspace.sourceOrphaned = false
            }
        }
    }

    // Released before the shield saw the pointer move (or in another
    // window): the drop happens where it was.
    Timer {
        id: orphanWatch

        interval: 120
        repeat: true
        onTriggered: {
            if (!root.workspace.sourceOrphaned || !Docking.dragActive) {
                root.workspace.sourceOrphaned = false
                stop()
            } else if (!shieldDrag.active && !Docking.pointerButtonDown()) {
                stop()
                root.workspace.endDrag()
            }
        }
    }
}

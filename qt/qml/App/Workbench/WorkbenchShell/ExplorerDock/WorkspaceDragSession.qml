import QtQuick

QtObject {
    id: root

    property bool active: false
    property string payloadType: ""
    property var payload: ({})
    property string sourceLabel: ""
    property string targetType: ""
    property string targetId: ""
    property string targetLabel: ""
    property bool dropAllowed: false

    readonly property string dragKey: payloadType.length > 0
        ? "archivist-" + payloadType
        : ""
    readonly property string statusText: dropAllowed && targetLabel.length > 0
        ? "Move " + sourceLabel + " to " + targetLabel
        : active
            ? "Drop " + sourceLabel + " onto a folder"
            : ""

    signal started(string payloadType, var payload)
    signal targetChanged(string targetType, string targetId, bool allowed)
    signal completed(string payloadType, var payload)
    signal cancelled(string payloadType, var payload)

    function begin(type, nextPayload, label) {
        active = true
        payloadType = String(type || "")
        payload = nextPayload || ({})
        sourceLabel = String(label || "Item")
        clearTarget()
        started(payloadType, payload)
    }

    function setTarget(type, id, label, allowed) {
        targetType = String(type || "")
        targetId = String(id || "")
        targetLabel = String(label || "")
        dropAllowed = Boolean(allowed)
        targetChanged(targetType, targetId, dropAllowed)
    }

    function clearTarget(id) {
        if (
            id !== undefined
            && String(id || "").length > 0
            && String(id) !== targetId
        ) {
            return
        }

        targetType = ""
        targetId = ""
        targetLabel = ""
        dropAllowed = false
    }

    function finish(success) {
        var completedType = payloadType
        var completedPayload = payload

        active = false
        payloadType = ""
        payload = ({})
        sourceLabel = ""
        clearTarget()

        if (success) {
            completed(completedType, completedPayload)
        } else {
            cancelled(completedType, completedPayload)
        }
    }
}

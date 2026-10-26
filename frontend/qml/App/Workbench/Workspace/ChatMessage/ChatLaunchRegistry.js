.pragma library

// Which new messages already played their launch, so a message that is
// swapped for its saved copy (a new row with a new id) does not launch twice.
var launched = ({})

function messageTime(message) {
    var stamp = Date.parse(String(message && message.createdAt || ""))
    return isNaN(stamp) ? 0 : stamp
}

function keyFor(message) {
    var role = String(message && message.role || "")
    var chat = String(message && message.chatId || "")

    // A reply is one launch per turn; your message is keyed by its text.
    return role === "user"
        ? chat + "|user|" + String(message.content || "")
        : chat + "|" + role
}

// True once for a message that just arrived at the end of the transcript.
function claim(message, atEnd) {
    if (!atEnd || !message) {
        return false
    }

    var now = Date.now()
    var stamp = messageTime(message)

    if (stamp === 0 || Math.abs(now - stamp) > 8000) {
        return false
    }

    var key = keyFor(message)
    var previous = launched[key] || 0

    if (now - previous < 60000) {
        return false
    }

    launched[key] = now

    // Your next message opens a new turn: the reply to it may launch again.
    if (String(message.role || "") === "user") {
        delete launched[String(message.chatId || "") + "|assistant"]
    }

    return true
}

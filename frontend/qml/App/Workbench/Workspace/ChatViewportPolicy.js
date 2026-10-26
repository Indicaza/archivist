.pragma library

function finiteNumber(value, fallback) {
    var parsed = Number(value)
    return Number.isFinite(parsed) ? parsed : Number(fallback || 0)
}

function clamp(value, minimum, maximum) {
    return Math.min(maximum, Math.max(minimum, value))
}

function contentZone(viewportWidth, leftObstruction, horizontalInset, maximumWidth) {
    var width = Math.max(0, finiteNumber(viewportWidth, 0))
    var obstruction = clamp(
        finiteNumber(leftObstruction, 0),
        0,
        width
    )
    var inset = Math.max(0, finiteNumber(horizontalInset, 0))
    var innerWidth = Math.max(0, width - inset * 2)
    var desiredWidth = Math.min(
        innerWidth,
        Math.max(0, finiteNumber(maximumWidth, innerWidth))
    )
    var centeredX = Math.max(inset, (width - desiredWidth) / 2)
    var collisionX = obstruction > 0
        ? obstruction + inset
        : inset
    var zoneX = clamp(Math.max(centeredX, collisionX), 0, width)
    var zoneWidth = Math.max(
        0,
        Math.min(desiredWidth, width - inset - zoneX)
    )

    return {
        x: zoneX,
        width: zoneWidth,
        availableLeft: obstruction,
        availableWidth: Math.max(0, width - obstruction)
    }
}

// Width of a message bubble before it is clamped to the content zone.
function messageFrameWidth(role, content, userMessageWidth, assistantMessageWidth) {
    if (role !== "user") {
        return assistantMessageWidth
    }

    const text = String(content || "")

    return Math.min(
        userMessageWidth,
        text.indexOf("\n") >= 0 || text.length > 76
            ? userMessageWidth
            : Math.max(104, text.length * 8.4 + 48)
    )
}

// Horizontal padding between a bubble's edge and its text.
function messageTextInset(role) {
    return role === "user" ? 20 : role === "system" ? 18 : 0
}

function transcriptEndY(originY, contentHeight, viewportHeight, topMargin, bottomMargin) {
    var origin = finiteNumber(originY, 0)
    var content = Math.max(0, finiteNumber(contentHeight, 0))
    var viewport = Math.max(0, finiteNumber(viewportHeight, 0))
    var top = Math.max(0, finiteNumber(topMargin, 0))
    var bottom = Math.max(0, finiteNumber(bottomMargin, 0))

    return Math.max(
        origin - top,
        origin + content - viewport + bottom
    )
}

function clampContentY(originY, contentHeight, viewportHeight, topMargin, bottomMargin, contentY) {
    var minimum = finiteNumber(originY, 0)
        - Math.max(0, finiteNumber(topMargin, 0))
    var maximum = transcriptEndY(
        originY,
        contentHeight,
        viewportHeight,
        topMargin,
        bottomMargin
    )

    return clamp(finiteNumber(contentY, minimum), minimum, maximum)
}

function distanceFromEnd(originY, contentHeight, viewportHeight, topMargin, bottomMargin, contentY) {
    return Math.max(
        0,
        transcriptEndY(
            originY,
            contentHeight,
            viewportHeight,
            topMargin,
            bottomMargin
        ) - finiteNumber(contentY, 0)
    )
}

function isNearEnd(originY, contentHeight, viewportHeight, topMargin, bottomMargin, contentY, threshold) {
    return distanceFromEnd(
        originY,
        contentHeight,
        viewportHeight,
        topMargin,
        bottomMargin,
        contentY
    ) <= Math.max(0, finiteNumber(threshold, 0))
}


function scrollDuration(distance, explicitJump) {
    var pixels = Math.max(0, finiteNumber(distance, 0))

    if (pixels <= 1) {
        return 0
    }

    if (Boolean(explicitJump)) {
        return Math.round(clamp(180 + Math.sqrt(pixels) * 8, 220, 520))
    }

    return Math.round(clamp(70 + Math.sqrt(pixels) * 3, 90, 180))
}

// How the jump-to-latest button travels. Short trips glide straight to the
// end. Long trips launch toward it, cut ahead while moving too fast to read,
// and glide the last screens home, so the button feels like a fast scroll
// without laying out every message in between.
function jumpPlan(distance, viewportHeight) {
    var pixels = Math.max(0, finiteNumber(distance, 0))
    var viewport = Math.max(200, finiteNumber(viewportHeight, 0))
    var travel = viewport * 2.2
    var launch = viewport * 0.6

    if (pixels <= travel + launch) {
        return {
            launch: 0,
            travel: pixels,
            duration: Math.round(clamp(220 + Math.sqrt(pixels) * 5, 240, 440))
        }
    }

    return {
        launch: launch,
        travel: travel,
        duration: 480
    }
}

function shouldPrefetchHistory(options) {
    var state = options || ({})

    return Boolean(state.visible)
        && Boolean(state.hasOlderMessages)
        && Boolean(state.nearBeginning)
        && !Boolean(state.loadingMessages)
        && !Boolean(state.loadingOlderMessages)
        && !Boolean(state.historyLoadPending)
        && !Boolean(state.restoringViewport)
        && !Boolean(state.responding)
        && !Boolean(state.scrollToEndPending)
        && !Boolean(state.interacting)
}

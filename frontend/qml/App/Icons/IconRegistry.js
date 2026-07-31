.pragma library

var tones = {
    "muted": true,
    "normal": true,
    "accent": true,
    "success": true,
    "warning": true,
    "danger": true,
    "info": true,
    "purple": true
}

function normalizedTone(tone) {
    var value = String(tone || "muted")
    return tones[value] === true ? value : "muted"
}

function safeName(name, fallback) {
    var value = String(name || "").trim()
    return value.length > 0 ? value : String(fallback || "file")
}

function appIconSource(name, tone) {
    return Qt.resolvedUrl(
        "Assets/ui/"
            + normalizedTone(tone)
            + "/"
            + safeName(name, "file")
            + ".svg"
    )
}

function languageIconSource(name, tone) {
    var iconName = safeName(name, "")
    if (iconName.length === 0) {
        return ""
    }

    var iconTone = String(tone || "muted")
    if (iconTone !== "brand") {
        iconTone = normalizedTone(iconTone)
    }

    return Qt.resolvedUrl(
        "Assets/languages/"
            + iconTone
            + "/"
            + iconName
            + ".svg"
    )
}

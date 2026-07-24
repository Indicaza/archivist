.pragma library

var definitionsByExtension = {
    ".md": identity("document", "Markdown", "markdown", "file-markdown", "▤", "markdown"),
    ".markdown": identity("document", "Markdown", "markdown", "file-markdown", "▤", "markdown"),
    ".txt": identity("text", "Plain Text", "plaintext", "file-text", "▤", "plain-text"),
    ".log": identity("text", "Log", "log", "file-log", "▤", "plain-text"),
    ".json": identity("data", "JSON", "json", "language-json", "{}", "structured-json"),
    ".yaml": identity("data", "YAML", "yaml", "language-yaml", "{}", "structured-yaml"),
    ".yml": identity("data", "YAML", "yaml", "language-yaml", "{}", "structured-yaml"),
    ".toml": identity("data", "TOML", "toml", "language-toml", "{}", "plain-text"),
    ".xml": identity("data", "XML", "xml", "language-xml", "{}", "plain-text"),
    ".html": identity("code", "HTML", "html", "language-html", "{}", "plain-text"),
    ".htm": identity("code", "HTML", "html", "language-html", "{}", "plain-text"),
    ".css": identity("code", "CSS", "css", "language-css", "{}", "plain-text"),
    ".scss": identity("code", "SCSS", "scss", "language-scss", "{}", "plain-text"),
    ".sass": identity("code", "Sass", "sass", "language-sass", "{}", "plain-text"),
    ".js": identity("code", "JavaScript", "javascript", "language-javascript", "{}", "plain-text"),
    ".jsx": identity("code", "JavaScript JSX", "javascriptreact", "language-javascript", "{}", "plain-text"),
    ".ts": identity("code", "TypeScript", "typescript", "language-typescript", "{}", "plain-text"),
    ".tsx": identity("code", "TypeScript JSX", "typescriptreact", "language-typescript", "{}", "plain-text"),
    ".py": identity("code", "Python", "python", "language-python", "{}", "plain-text"),
    ".rs": identity("code", "Rust", "rust", "language-rust", "{}", "plain-text"),
    ".go": identity("code", "Go", "go", "language-go", "{}", "plain-text"),
    ".c": identity("code", "C", "c", "language-c", "{}", "plain-text"),
    ".h": identity("code", "C/C++ Header", "cpp", "language-cpp", "{}", "plain-text"),
    ".cc": identity("code", "C++", "cpp", "language-cpp", "{}", "plain-text"),
    ".cpp": identity("code", "C++", "cpp", "language-cpp", "{}", "plain-text"),
    ".cxx": identity("code", "C++", "cpp", "language-cpp", "{}", "plain-text"),
    ".hpp": identity("code", "C++ Header", "cpp", "language-cpp", "{}", "plain-text"),
    ".qml": identity("code", "Qt QML", "qml", "language-qml", "◇", "plain-text"),
    ".sh": identity("code", "Shell Script", "shell", "language-shell", "{}", "plain-text"),
    ".bash": identity("code", "Bash", "shell", "language-shell", "{}", "plain-text"),
    ".zsh": identity("code", "Zsh", "shell", "language-shell", "{}", "plain-text"),
    ".sql": identity("code", "SQL", "sql", "language-sql", "{}", "plain-text"),
    ".svg": identity("vector", "SVG", "svg", "file-svg", "◇", "svg"),
    ".png": identity("image", "PNG Image", "", "file-image", "▧", "image"),
    ".jpg": identity("image", "JPEG Image", "", "file-image", "▧", "image"),
    ".jpeg": identity("image", "JPEG Image", "", "file-image", "▧", "image"),
    ".gif": identity("image", "GIF Image", "", "file-image", "▧", "image"),
    ".webp": identity("image", "WebP Image", "", "file-image", "▧", "image"),
    ".bmp": identity("image", "Bitmap Image", "", "file-image", "▧", "image"),
    ".ico": identity("image", "Icon Image", "", "file-image", "▧", "image"),
    ".pdf": identity("document", "PDF Document", "", "file-pdf", "▤", "pdf"),
    ".fbx": identity("model3d", "FBX Model", "", "file-model-3d", "◇", "model-3d"),
    ".obj": identity("model3d", "OBJ Model", "", "file-model-3d", "◇", "model-3d"),
    ".gltf": identity("model3d", "glTF Model", "", "file-model-3d", "◇", "model-3d"),
    ".glb": identity("model3d", "glTF Binary Model", "", "file-model-3d", "◇", "model-3d"),
    ".usd": identity("model3d", "USD Scene", "", "file-model-3d", "◇", "model-3d"),
    ".usdz": identity("model3d", "USDZ Scene", "", "file-model-3d", "◇", "model-3d"),
    ".uproject": identity("project", "Unreal Project", "", "brand-unreal", "◇", "plain-text"),
    ".zip": identity("archive", "ZIP Archive", "", "file-archive", "▣", "binary"),
    ".tar": identity("archive", "TAR Archive", "", "file-archive", "▣", "binary"),
    ".gz": identity("archive", "Gzip Archive", "", "file-archive", "▣", "binary"),
    ".mp3": identity("audio", "MP3 Audio", "", "file-audio", "♪", "audio"),
    ".wav": identity("audio", "WAV Audio", "", "file-audio", "♪", "audio"),
    ".ogg": identity("audio", "Ogg Audio", "", "file-audio", "♪", "audio"),
    ".mp4": identity("video", "MP4 Video", "", "file-video", "▶", "video"),
    ".mov": identity("video", "QuickTime Video", "", "file-video", "▶", "video"),
    ".webm": identity("video", "WebM Video", "", "file-video", "▶", "video")
}

var definitionsByFileName = {
    "dockerfile": identity("code", "Dockerfile", "dockerfile", "brand-docker", "{}", "plain-text"),
    "makefile": identity("code", "Makefile", "makefile", "file-build", "{}", "plain-text"),
    "cmakelists.txt": identity("code", "CMake", "cmake", "language-cmake", "{}", "plain-text"),
    "package.json": identity("data", "Node Package", "json", "brand-nodejs", "{}", "structured-json"),
    "package-lock.json": identity("data", "Node Package Lock", "json", "brand-nodejs", "{}", "structured-json"),
    "tsconfig.json": identity("data", "TypeScript Config", "json", "language-typescript", "{}", "structured-json"),
    "readme": identity("document", "README", "plaintext", "file-readme", "▤", "plain-text"),
    "license": identity("document", "License", "plaintext", "file-license", "▤", "plain-text")
}

var languageAliases = {
    "c++": "cpp",
    "cxx": "cpp",
    "typescriptreact": "typescriptreact",
    "typescript jsx": "typescriptreact",
    "javascriptreact": "javascriptreact",
    "javascript jsx": "javascriptreact",
    "qt/qml": "qml",
    "shellscript": "shell",
    "bash": "shell"
}

var definitionsByLanguage = {
    "markdown": identity("document", "Markdown", "markdown", "file-markdown", "▤", "markdown"),
    "plaintext": identity("text", "Plain Text", "plaintext", "file-text", "▤", "plain-text"),
    "json": identity("data", "JSON", "json", "language-json", "{}", "structured-json"),
    "yaml": identity("data", "YAML", "yaml", "language-yaml", "{}", "structured-yaml"),
    "toml": identity("data", "TOML", "toml", "language-toml", "{}", "plain-text"),
    "xml": identity("data", "XML", "xml", "language-xml", "{}", "plain-text"),
    "html": identity("code", "HTML", "html", "language-html", "{}", "plain-text"),
    "css": identity("code", "CSS", "css", "language-css", "{}", "plain-text"),
    "javascript": identity("code", "JavaScript", "javascript", "language-javascript", "{}", "plain-text"),
    "javascriptreact": identity("code", "JavaScript JSX", "javascriptreact", "language-javascript", "{}", "plain-text"),
    "typescript": identity("code", "TypeScript", "typescript", "language-typescript", "{}", "plain-text"),
    "typescriptreact": identity("code", "TypeScript JSX", "typescriptreact", "language-typescript", "{}", "plain-text"),
    "python": identity("code", "Python", "python", "language-python", "{}", "plain-text"),
    "rust": identity("code", "Rust", "rust", "language-rust", "{}", "plain-text"),
    "go": identity("code", "Go", "go", "language-go", "{}", "plain-text"),
    "c": identity("code", "C", "c", "language-c", "{}", "plain-text"),
    "cpp": identity("code", "C++", "cpp", "language-cpp", "{}", "plain-text"),
    "qml": identity("code", "Qt QML", "qml", "language-qml", "◇", "plain-text"),
    "shell": identity("code", "Shell Script", "shell", "language-shell", "{}", "plain-text"),
    "sql": identity("code", "SQL", "sql", "language-sql", "{}", "plain-text")
}

function identity(category, displayLabel, languageId, iconId, glyph, preferredRendererId) {
    return {
        category: category,
        displayLabel: displayLabel,
        languageId: languageId,
        iconId: iconId,
        glyph: glyph,
        preferredRendererId: preferredRendererId
    }
}

function normalizedFileName(value) {
    var normalized = String(value || "").split("\\").join("/")
    var separator = normalized.lastIndexOf("/")
    return (separator >= 0 ? normalized.slice(separator + 1) : normalized).toLowerCase()
}

function normalizedExtension(fileName, extension) {
    var suffix = String(extension || "").trim().toLowerCase()

    if (suffix.length > 0 && suffix.charAt(0) !== ".") {
        suffix = "." + suffix
    }

    if (suffix.length > 0) {
        return suffix
    }

    var name = normalizedFileName(fileName)
    var dot = name.lastIndexOf(".")

    return dot > 0 ? name.slice(dot) : ""
}

function normalizedLanguage(value) {
    var language = String(value || "").trim().toLowerCase()
    return languageAliases[language] || language
}

function definitionForMimeType(mimeType) {
    var mime = String(mimeType || "").trim().toLowerCase()

    if (mime === "text/markdown") {
        return definitionsByLanguage.markdown
    }

    if (mime === "application/json" || mime.endsWith("+json")) {
        return definitionsByLanguage.json
    }

    if (mime === "application/yaml" || mime === "text/yaml" || mime.endsWith("+yaml")) {
        return definitionsByLanguage.yaml
    }

    if (mime === "image/svg+xml") {
        return definitionsByExtension[".svg"]
    }

    if (mime.indexOf("image/") === 0) {
        return identity("image", "Image", "", "file-image", "▧", "image")
    }

    if (mime.indexOf("audio/") === 0) {
        return identity("audio", "Audio", "", "file-audio", "♪", "audio")
    }

    if (mime.indexOf("video/") === 0) {
        return identity("video", "Video", "", "file-video", "▶", "video")
    }

    if (mime.indexOf("text/") === 0) {
        return definitionsByLanguage.plaintext
    }

    return null
}

function cloneDefinition(definition, fileName, extension, mimeType) {
    return {
        fileName: fileName,
        extension: extension,
        mimeType: mimeType,
        category: definition.category,
        displayLabel: definition.displayLabel,
        languageId: definition.languageId,
        iconId: definition.iconId,
        glyph: definition.glyph,
        preferredRendererId: definition.preferredRendererId
    }
}

function resolve(options) {
    var input = options || ({})
    var fileName = normalizedFileName(input.fileName || input.relativePath)
    var extension = normalizedExtension(fileName, input.extension)
    var mimeType = String(input.mimeType || "").trim().toLowerCase()
    var languageId = normalizedLanguage(input.languageId || input.language)

    var definition = definitionsByFileName[fileName]

    if (!definition && languageId.length > 0) {
        definition = definitionsByLanguage[languageId]
    }

    if (!definition && extension.length > 0) {
        definition = definitionsByExtension[extension]
    }

    if (!definition && mimeType.length > 0) {
        definition = definitionForMimeType(mimeType)
    }

    if (!definition) {
        definition = identity("unknown", "File", languageId, "file-generic", "·", "plain-text")
    }

    return cloneDefinition(definition, fileName, extension, mimeType)
}

function glyphFor(options) {
    return resolve(options).glyph
}

function iconIdFor(options) {
    return resolve(options).iconId
}

function displayLabelFor(options) {
    return resolve(options).displayLabel
}

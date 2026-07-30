.pragma library

var iconIds = {
    "language-json": "json",
    "language-yaml": "yaml",
    "language-toml": "toml",
    "language-xml": "xml",
    "language-html": "html",
    "language-css": "css",
    "language-scss": "scss",
    "language-sass": "sass",
    "language-less": "less",
    "language-javascript": "javascript",
    "language-typescript": "typescript",
    "language-python": "python",
    "language-rust": "rust",
    "language-go": "go",
    "language-java": "java",
    "language-kotlin": "kotlin",
    "language-swift": "swift",
    "language-php": "php",
    "language-c": "c",
    "language-cpp": "cpp",
    "language-csharp": "csharp",
    "language-qml": "qml",
    "language-shell": "shell",
    "language-sql": "sql",
    "language-cmake": "cmake",
    "brand-docker": "docker",
    "brand-nodejs": "nodejs",
    "brand-unreal": "unreal",
    "brand-git": "git",
    "brand-github": "github",
    "brand-gitlab": "gitlab"
}

var languageAliases = {
    "c++": "cpp",
    "cxx": "cpp",
    "cpp": "cpp",
    "c#": "csharp",
    "cs": "csharp",
    "typescriptreact": "typescriptreact",
    "typescript jsx": "typescriptreact",
    "javascriptreact": "javascriptreact",
    "javascript jsx": "javascriptreact",
    "jsx": "javascriptreact",
    "tsx": "typescriptreact",
    "qt/qml": "qml",
    "shellscript": "shell",
    "bash": "shell",
    "zsh": "shell",
    "dockerfile": "docker",
    "node": "nodejs"
}

var extensions = {
    "md": "markdown",
    "markdown": "markdown",
    "json": "json",
    "yaml": "yaml",
    "yml": "yaml",
    "toml": "toml",
    "xml": "xml",
    "html": "html",
    "htm": "html",
    "css": "css",
    "scss": "scss",
    "sass": "sass",
    "less": "less",
    "js": "javascript",
    "mjs": "javascript",
    "cjs": "javascript",
    "jsx": "javascriptreact",
    "ts": "typescript",
    "mts": "typescript",
    "cts": "typescript",
    "tsx": "typescriptreact",
    "py": "python",
    "rs": "rust",
    "go": "go",
    "java": "java",
    "kt": "kotlin",
    "kts": "kotlin",
    "swift": "swift",
    "php": "php",
    "c": "c",
    "h": "cpp",
    "cc": "cpp",
    "cpp": "cpp",
    "cxx": "cpp",
    "hpp": "cpp",
    "cs": "csharp",
    "qml": "qml",
    "sh": "shell",
    "bash": "shell",
    "zsh": "shell",
    "ps1": "powershell",
    "sql": "sql",
    "cmake": "cmake",
    "dockerfile": "docker",
    "uproject": "unreal"
}

var supportedLanguageIcons = {
    "markdown": true,
    "json": true,
    "yaml": true,
    "toml": true,
    "xml": true,
    "html": true,
    "css": true,
    "scss": true,
    "sass": true,
    "less": true,
    "javascript": true,
    "react": true,
    "typescript": true,
    "python": true,
    "rust": true,
    "go": true,
    "java": true,
    "kotlin": true,
    "swift": true,
    "php": true,
    "c": true,
    "cpp": true,
    "csharp": true,
    "qml": true,
    "shell": true,
    "powershell": true,
    "sql": true,
    "cmake": true,
    "docker": true,
    "nodejs": true,
    "npm": true,
    "pnpm": true,
    "yarn": true,
    "bun": true,
    "vue": true,
    "nextjs": true,
    "nuxt": true,
    "svelte": true,
    "angular": true,
    "vite": true,
    "webpack": true,
    "tailwind": true,
    "graphql": true,
    "ruby": true,
    "rails": true,
    "laravel": true,
    "django": true,
    "flask": true,
    "fastapi": true,
    "dotnet": true,
    "dart": true,
    "flutter": true,
    "objectivec": true,
    "lua": true,
    "perl": true,
    "r": true,
    "matlab": true,
    "julia": true,
    "elixir": true,
    "erlang": true,
    "haskell": true,
    "ocaml": true,
    "clojure": true,
    "scala": true,
    "groovy": true,
    "solidity": true,
    "zig": true,
    "nim": true,
    "crystal": true,
    "fortran": true,
    "git": true,
    "github": true,
    "gitlab": true,
    "postgresql": true,
    "mysql": true,
    "mongodb": true,
    "sqlite": true,
    "redis": true,
    "firebase": true,
    "supabase": true,
    "aws": true,
    "azure": true,
    "googlecloud": true,
    "kubernetes": true,
    "unreal": true,
    "godot": true,
    "unity": true,
    "blender": true,
    "maya": true,
    "linux": true,
    "apple": true,
    "windows": true
}

var fallbackAppIcons = {
    "file-markdown": "file-markdown",
    "file-text": "file-text",
    "file-log": "file-text",
    "file-svg": "file-svg",
    "file-image": "file-image",
    "file-pdf": "file-pdf",
    "file-word": "file-word",
    "file-document": "file-document",
    "file-excel": "file-excel",
    "file-spreadsheet": "file-spreadsheet",
    "file-powerpoint": "file-powerpoint",
    "file-presentation": "file-presentation",
    "file-model-3d": "file-model-3d",
    "file-archive": "file-archive",
    "file-audio": "file-audio",
    "file-video": "file-video",
    "file-build": "file-build",
    "file-readme": "file-readme",
    "file-license": "file-license"
}

function normalize(value) {
    return String(value || "").trim().toLowerCase()
}

function extensionFor(fileName, extension) {
    var explicit = normalize(extension).replace(/^\./, "")
    if (explicit.length > 0) {
        return explicit
    }

    var name = normalize(fileName)
    var dot = name.lastIndexOf(".")
    return dot >= 0 ? name.slice(dot + 1) : name
}

function languageIconName(options) {
    var input = options || ({})
    var iconId = normalize(input.iconId)
    if (iconIds[iconId]) {
        return iconIds[iconId]
    }

    var languageId = normalize(input.languageId)
    languageId = languageAliases[languageId] || languageId
    if (languageId.length > 0) {
        if (languageId === "javascriptreact") {
            return "react"
        }
        if (languageId === "typescriptreact") {
            return "react"
        }
        return supportedLanguageIcons[languageId] === true
            ? languageId
            : ""
    }

    var extension = extensionFor(input.fileName, input.extension)
    var resolved = extensions[extension] || ""
    if (resolved === "javascriptreact" || resolved === "typescriptreact") {
        return "react"
    }
    return supportedLanguageIcons[resolved] === true
        ? resolved
        : ""
}

function fallbackAppIconName(iconId) {
    var value = normalize(iconId)
    return fallbackAppIcons[value] || "file"
}

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

var fileNames = {
    "package.json": "json",
    "package-lock.json": "json",
    "npm-shrinkwrap.json": "json",
    "tsconfig.json": "json",
    "jsconfig.json": "json",
    ".eslintrc": "json",
    ".eslintrc.json": "json",
    ".prettierrc": "json",
    ".prettierrc.json": "json",
    ".babelrc": "json",
    ".babelrc.json": "json",
    ".stylelintrc": "json",
    ".stylelintrc.json": "json",
    "composer.lock": "json",
    "pipfile.lock": "json",
    "cargo.toml": "toml",
    "cargo.lock": "toml",
    "pipfile": "toml",
    "pyproject.toml": "toml",
    "poetry.lock": "toml",
    "go.mod": "go",
    "go.sum": "go",
    "go.work": "go",
    "go.work.sum": "go",
    "dockerfile": "docker",
    "cmakelists.txt": "cmake",
    ".gitignore": "git",
    ".gitattributes": "git",
    ".gitmodules": "git",
    ".gitkeep": "git",
    ".gitlab-ci.yml": "gitlab",
    ".gitlab-ci.yaml": "gitlab",
    "gemfile": "ruby",
    "rakefile": "ruby",
    "vagrantfile": "ruby",
    "podfile": "ruby",
    "jenkinsfile": "groovy",
    "project.godot": "godot"
}

var extensions = {
    "md": "markdown",
    "markdown": "markdown",
    "json": "json",
    "jsonc": "json",
    "json5": "json",
    "code-workspace": "json",
    "yaml": "yaml",
    "yml": "yaml",
    "toml": "toml",
    "xml": "xml",
    "qrc": "xml",
    "ui": "xml",
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
    "vue": "vue",
    "svelte": "svelte",
    "graphql": "graphql",
    "gql": "graphql",
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
    "hh": "cpp",
    "cc": "cpp",
    "cpp": "cpp",
    "cxx": "cpp",
    "hpp": "cpp",
    "hxx": "cpp",
    "cs": "csharp",
    "csproj": "dotnet",
    "fsproj": "dotnet",
    "vbproj": "dotnet",
    "sln": "dotnet",
    "qml": "qml",
    "qmltypes": "qml",
    "sh": "shell",
    "bash": "shell",
    "zsh": "shell",
    "ps1": "powershell",
    "sql": "sql",
    "cmake": "cmake",
    "dockerfile": "docker",
    "rb": "ruby",
    "rake": "ruby",
    "gemspec": "ruby",
    "dart": "dart",
    "mm": "objectivec",
    "lua": "lua",
    "pl": "perl",
    "pm": "perl",
    "r": "r",
    "jl": "julia",
    "ex": "elixir",
    "exs": "elixir",
    "erl": "erlang",
    "hrl": "erlang",
    "hs": "haskell",
    "lhs": "haskell",
    "ml": "ocaml",
    "mli": "ocaml",
    "clj": "clojure",
    "cljs": "clojure",
    "cljc": "clojure",
    "edn": "clojure",
    "scala": "scala",
    "sc": "scala",
    "groovy": "groovy",
    "gradle": "groovy",
    "sol": "solidity",
    "zig": "zig",
    "nim": "nim",
    "nims": "nim",
    "nimble": "nim",
    "cr": "crystal",
    "f": "fortran",
    "for": "fortran",
    "f77": "fortran",
    "f90": "fortran",
    "f95": "fortran",
    "f03": "fortran",
    "f08": "fortran",
    "gd": "godot",
    "gdshader": "godot",
    "tscn": "godot",
    "tres": "godot",
    "uproject": "unreal",
    "uasset": "unreal",
    "umap": "unreal",
    "blend": "blender",
    "ma": "maya",
    "mb": "maya",
    "sqlite": "sqlite",
    "sqlite3": "sqlite"
}

var extensionKeys = Object.keys(extensions).sort(function(left, right) {
    return right.length - left.length
})

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

function baseNameFor(fileName) {
    var normalized = normalize(fileName).replace(/\\/g, "/")
    var slash = normalized.lastIndexOf("/")
    return slash >= 0 ? normalized.slice(slash + 1) : normalized
}

function extensionFor(fileName, extension) {
    var explicit = normalize(extension).replace(/^\./, "")
    if (explicit.length > 0) {
        return explicit
    }

    var name = baseNameFor(fileName)
    for (var index = 0; index < extensionKeys.length; index += 1) {
        var candidate = extensionKeys[index]
        if (name === candidate || name.endsWith("." + candidate)) {
            return candidate
        }
    }

    var dot = name.lastIndexOf(".")
    return dot >= 0 ? name.slice(dot + 1) : name
}

function supportedIconName(value) {
    var normalized = normalize(value)
    var resolved = languageAliases[normalized] || normalized
    if (resolved === "javascriptreact" || resolved === "typescriptreact") {
        return "react"
    }
    return supportedLanguageIcons[resolved] === true
        ? resolved
        : ""
}

function fileNameIconName(fileName) {
    var name = baseNameFor(fileName)
    var exact = fileNames[name] || ""
    if (exact.length > 0) {
        return supportedIconName(exact)
    }

    if (/\.(?:js|mjs|cjs|ts|mts|cts|css)\.map$/.test(name)) {
        return "json"
    }

    return ""
}

function languageIconName(options) {
    var input = options || ({})

    var resolved = fileNameIconName(input.fileName)
    if (resolved.length > 0) {
        return resolved
    }

    var extension = extensionFor(input.fileName, input.extension)
    resolved = supportedIconName(extensions[extension] || "")
    if (resolved.length > 0) {
        return resolved
    }

    var languageId = supportedIconName(input.languageId)
    if (languageId.length > 0) {
        return languageId
    }

    var iconId = iconIds[normalize(input.iconId)] || ""
    return supportedIconName(iconId)
}

function fallbackAppIconName(iconId) {
    var value = normalize(iconId)
    return fallbackAppIcons[value] || "file"
}

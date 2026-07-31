import fs from "node:fs";
import path from "node:path";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";
import zlib from "node:zlib";

const require = createRequire(import.meta.url);
const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const rootDirectory = path.resolve(scriptDirectory, "..");
const outputDirectory = path.join(
  rootDirectory,
  "frontend/qml/App/Icons/Assets",
);

const tones = {
  muted: "#9a9387",
  normal: "#d8d2c7",
  accent: "#b7aad2",
  success: "#3fb950",
  warning: "#d29922",
  danger: "#f85149",
  info: "#79c0ff",
  purple: "#a371f7",
};

const uiIcons = {
  workspace: ["layout-dashboard", "dashboard", "grid-layout", "module-four"],
  archive: ["archive-box", "archive", "folder-lock", "box"],
  search: ["search", "search-circle", "magnifying-glass"],
  plugin: ["plugin", "puzzle-piece", "module-three"],
  tools: ["tools-wench", "tool-box", "wrench", "hammer-wrench"],
  library: ["library", "books", "book-open", "archive-books"],
  collection: ["folder-library", "folder-bookmark", "collection", "layers"],
  chat: ["chat-bubble", "chat", "messages-bubble", "conversation"],
  agent: ["user-robot", "robot", "artificial-intelligence", "user-circle"],
  terminal: ["terminal", "command-line", "code-monitor", "programming-browser"],
  file: ["file", "common-file-text", "document", "page"],
  "file-text": ["common-file-text", "file-text", "document-text"],
  "file-markdown": ["common-file-text", "file-code", "document-text"],
  "file-image": ["image-file", "file-image", "image-picture"],
  "file-pdf": ["file-pdf", "common-file-pdf", "document"],
  "file-word": ["file-word", "common-file-text", "document-text"],
  "file-document": ["common-file-text", "file-text", "document-text"],
  "file-excel": ["file-table", "common-file-double", "spreadsheet"],
  "file-spreadsheet": ["file-table", "spreadsheet", "table"],
  "file-powerpoint": ["presentation-projector", "presentation", "file"],
  "file-presentation": ["presentation-projector", "presentation", "file"],
  "file-model-3d": ["design-tool-pen-station", "cube", "shape-cube"],
  "file-archive": ["archive-box", "zip-file", "box"],
  "file-audio": ["audio-file", "music-note", "headphones"],
  "file-video": ["video-file", "video", "button-play"],
  "file-svg": ["vector-path", "design-tool-pen-station", "image-file"],
  "file-build": ["hammer-wrench", "tools-wench", "settings-cog"],
  "file-readme": ["book-open", "common-file-text", "document-text"],
  "file-license": ["legal-document", "common-file-text", "document-text"],
  folder: ["folder", "folder-empty", "folder-1"],
  "folder-open": ["folder-open", "folder-open-1", "folder"],
  "chevron-left": ["arrow-left", "chevron-left", "navigation-left"],
  "chevron-right": ["arrow-right", "chevron-right", "navigation-right"],
  "chevron-up": ["arrow-up", "chevron-up", "navigation-up"],
  "chevron-down": ["arrow-down", "chevron-down", "navigation-down"],
  add: ["add", "plus", "add-circle"],
  close: ["close", "remove", "delete", "multiply"],
  more: ["navigation-menu-horizontal", "more-horizontal", "menu-dots"],
  edit: ["pencil", "edit", "pen-write"],
  copy: ["copy-paste", "copy", "common-file-double"],
  "external-link": ["open-in-new-window", "external-link", "share-up"],
  reveal: ["target-center", "focus-frame", "locate-target"],
  refresh: ["synchronize-arrows", "refresh", "rotate-back"],
  settings: ["settings-cog", "cog", "settings-slider"],
  send: ["send-email", "send", "navigation-next"],
  attach: ["attachment", "paperclip", "link"],
  expand: ["expand", "maximize", "arrow-expand"],
  collapse: ["collapse", "minimize", "arrow-shrink"],
  "zoom-in": ["zoom-in", "search-add", "magnifying-glass-add"],
  "zoom-out": ["zoom-out", "search-remove", "magnifying-glass-remove"],
  fit: ["expand-window", "fit-to-screen", "frame"],
  grid: ["layout-grid", "grid", "module-four"],
  list: ["list", "navigation-menu", "menu"],
  save: ["floppy-disk", "save", "download-box"],
  play: ["button-play", "play", "media-play"],
  stop: ["button-stop", "stop", "media-stop"],
  pause: ["button-pause", "pause", "media-pause"],
  check: ["check", "check-circle", "validate"],
  warning: ["warning-triangle", "alert-triangle", "warning"],
  error: ["remove-circle", "alert-circle", "error"],
  info: ["information-circle", "info-circle", "information"],
  "git-branch": ["branch-line", "git-branch", "hierarchy"],
  "arrow-left-right": ["arrow-left-right", "synchronize-arrows", "move-horizontal"],
  "actual-size": ["expand-6", "focus-frame", "frame"],
  "context-memory": ["brain", "memory-chip", "database"],
};


const codiconUiIcons = {
  search: "search",
  library: "folder-library",
  collection: "files",
  terminal: "terminal",
  "chevron-left": "chevron-left",
  "chevron-right": "chevron-right",
  "chevron-up": "chevron-up",
  "chevron-down": "chevron-down",
  add: "add",
  close: "close",
  more: "ellipsis",
  edit: "edit",
  copy: "copy",
  refresh: "refresh",
};

const compactFileIconCandidates = {
  markdown: ["markdown", "markdownlint"],
  yaml: ["yaml", "yml"],
  toml: ["toml"],
  xml: ["xml"],
  html: ["html", "html5"],
  css: ["css", "css3"],
  scss: ["scss", "sass"],
  sass: ["sass"],
  less: ["less"],
  javascript: ["js", "javascript"],
  javascriptreact: ["jsx", "reactjs", "react"],
  typescript: ["typescript", "ts"],
  typescriptreact: ["tsx", "reactjs", "react"],
  python: ["python"],
  rust: ["rust"],
  go: ["go"],
  java: ["java"],
  kotlin: ["kotlin"],
  swift: ["swift"],
  php: ["php"],
  c: ["c"],
  cpp: ["cplusplus", "cpp"],
  csharp: ["csharp"],
  qml: ["qml", "qt"],
  shell: ["shell", "bash"],
  powershell: ["powershell"],
  sql: ["sql", "database"],
  cmake: ["cmake"],
  docker: ["docker"],
  nodejs: ["nodejs", "node"],
  npm: ["npm"],
  pnpm: ["pnpm"],
  yarn: ["yarn"],
  bun: ["bun"],
  react: ["reactjs", "react"],
  vue: ["vue", "vuejs"],
  nextjs: ["nextjs", "next"],
  nuxt: ["nuxt", "nuxtjs"],
  svelte: ["svelte"],
  angular: ["angular"],
  vite: ["vite"],
  webpack: ["webpack"],
  tailwind: ["tailwind", "tailwindcss"],
  graphql: ["graphql"],
  ruby: ["ruby"],
  rails: ["rails"],
  laravel: ["laravel"],
  django: ["django"],
  flask: ["flask"],
  fastapi: ["fastapi"],
  dotnet: ["dotnet"],
  dart: ["dart"],
  flutter: ["flutter"],
  objectivec: ["objectivec", "objective-c"],
  lua: ["lua"],
  r: ["r"],
  matlab: ["matlab"],
  julia: ["julia"],
  elixir: ["elixir"],
  erlang: ["erlang"],
  haskell: ["haskell"],
  clojure: ["clojure"],
  scala: ["scala"],
  solidity: ["solidity"],
  zig: ["zig"],
  git: ["git"],
  github: ["github"],
  gitlab: ["gitlab"],
  postgresql: ["postgresql", "postgres"],
  mysql: ["mysql"],
  mongodb: ["mongodb", "mongo"],
  sqlite: ["sqlite"],
  redis: ["redis"],
  firebase: ["firebase"],
  supabase: ["supabase"],
  aws: ["aws"],
  azure: ["azure"],
  googlecloud: ["googlecloud", "gcp"],
  kubernetes: ["kubernetes", "k8s"],
  unreal: ["unreal", "unrealengine"],
  godot: ["godot"],
  unity: ["unity"],
  blender: ["blender"],
  maya: ["maya"],
  linux: ["linux"],
  apple: ["apple"],
  windows: ["windows"],

};

const setiRepresentativeFiles = {
  default: "file",
  markdown: "README.md",
  json: "settings.json",
  yaml: "config.yaml",
  toml: "config.toml",
  xml: "document.xml",
  html: "index.html",
  css: "styles.css",
  scss: "styles.scss",
  sass: "styles.sass",
  less: "styles.less",
  javascript: "app.js",
  javascriptreact: "Component.jsx",
  typescript: "app.ts",
  typescriptreact: "Component.tsx",
  python: "app.py",
  rust: "app.rs",
  go: "app.go",
  java: "App.java",
  kotlin: "App.kt",
  swift: "App.swift",
  php: "index.php",
  c: "main.c",
  cpp: "main.cpp",
  csharp: "Program.cs",
  qml: "Main.qml",
  shell: "script.sh",
  powershell: "script.ps1",
  sql: "schema.sql",
  cmake: "CMakeLists.txt",
  docker: "Dockerfile",
  nodejs: "package.json",
  npm: "package.json",
  pnpm: "pnpm-lock.yaml",
  yarn: "yarn.lock",
  bun: "bun.lockb",
  react: "Component.jsx",
  vue: "App.vue",
  nextjs: "next.config.js",
  nuxt: "nuxt.config.ts",
  svelte: "App.svelte",
  angular: "angular.json",
  vite: "vite.config.ts",
  webpack: "webpack.config.js",
  tailwind: "tailwind.config.js",
  graphql: "schema.graphql",
  ruby: "app.rb",
  rails: "Gemfile",
  laravel: "artisan",
  django: "manage.py",
  flask: "app.py",
  fastapi: "app.py",
  dotnet: "Program.cs",
  dart: "app.dart",
  flutter: "pubspec.yaml",
  objectivec: "app.m",
  lua: "app.lua",
  r: "analysis.r",
  matlab: "script.m",
  julia: "app.jl",
  elixir: "app.ex",
  erlang: "app.erl",
  haskell: "app.hs",
  clojure: "app.clj",
  scala: "app.scala",
  solidity: "Contract.sol",
  zig: "app.zig",
  git: ".gitignore",
  github: ".github/workflows/ci.yml",
  gitlab: ".gitlab-ci.yml",
  postgresql: "schema.sql",
  mysql: "schema.sql",
  mongodb: "database.js",
  sqlite: "database.sqlite",
  redis: "redis.conf",
  firebase: "firebase.json",
  kubernetes: "deployment.yaml",
  godot: "project.godot",
  linux: "script.sh",
  apple: "App.swift",
  windows: "Program.cs",
};


const setiLanguageIds = {
  javascriptreact: "javascriptreact",
  typescriptreact: "typescriptreact",
  react: "javascriptreact",
  shell: "shellscript",
};

const setiPalette = {
  blue: "#519aba",
  grey: "#4d5a5e",
  "grey-light": "#6d8086",
  green: "#8dc149",
  orange: "#e37933",
  pink: "#f55385",
  purple: "#a074c4",
  red: "#cc3e44",
  white: "#d4d7d6",
  yellow: "#cbcb41",
  ignore: "#41535b",
};

const languageBrandColors = {

  markdown: "#f2f2f2",
  json: "#f2c94c",
  yaml: "#cb171e",
  toml: "#9c4221",
  xml: "#f16529",
  html: "#e34f26",
  css: "#1572b6",
  scss: "#cd6799",
  sass: "#cc6699",
  less: "#1d365d",
  javascript: "#f7df1e",
  javascriptreact: "#61dafb",
  typescript: "#3178c6",
  typescriptreact: "#61dafb",
  python: "#3776ab",
  rust: "#dea584",
  go: "#00add8",
  java: "#e76f00",
  kotlin: "#a97bff",
  swift: "#f05138",
  php: "#777bb4",
  c: "#a8b9cc",
  cpp: "#00599c",
  csharp: "#9b4f96",
  qml: "#41cd52",
  shell: "#89e051",
  powershell: "#5391fe",
  sql: "#336791",
  cmake: "#064f8c",
  docker: "#2496ed",
  nodejs: "#5fa04e",
  npm: "#cb3837",
  pnpm: "#f69220",
  yarn: "#2c8ebb",
  bun: "#fbf0df",
  react: "#61dafb",
  vue: "#42b883",
  nextjs: "#f2f2f2",
  nuxt: "#00dc82",
  svelte: "#ff3e00",
  angular: "#dd0031",
  vite: "#bd34fe",
  webpack: "#8dd6f9",
  tailwind: "#38bdf8",
  graphql: "#e10098",
  ruby: "#cc342d",
  rails: "#d30001",
  laravel: "#ff2d20",
  django: "#44b78b",
  flask: "#f2f2f2",
  fastapi: "#009688",
  dotnet: "#512bd4",
  dart: "#0175c2",
  flutter: "#54c5f8",
  objectivec: "#438eff",
  lua: "#7f7fff",
  r: "#276dc3",
  matlab: "#e16737",
  julia: "#9558b2",
  elixir: "#6e4a7e",
  erlang: "#a90533",
  haskell: "#5d4f85",
  clojure: "#63b132",
  scala: "#dc322f",
  solidity: "#9b9b9b",
  zig: "#f7a41d",
  git: "#f05032",
  github: "#f2f2f2",
  gitlab: "#fc6d26",
  postgresql: "#4169e1",
  mysql: "#4479a1",
  mongodb: "#47a248",
  sqlite: "#0f80cc",
  redis: "#dc382d",
  firebase: "#ffca28",
  supabase: "#3ecf8e",
  aws: "#ff9900",
  azure: "#0078d4",
  googlecloud: "#4285f4",
  kubernetes: "#326ce5",
  unreal: "#f2f2f2",
  godot: "#478cbf",
  unity: "#f2f2f2",
  blender: "#f5792a",
  maya: "#37a5cc",
  linux: "#fcc624",
  apple: "#f2f2f2",
  windows: "#0078d4",
};

const languageIcons = {
  default: ["file"],
  markdown: ["markdown"],
  json: ["json"],
  yaml: ["yaml"],
  toml: ["toml"],
  xml: ["xml"],
  html: ["html5"],
  css: ["css3"],
  scss: ["sass"],
  sass: ["sass"],
  less: ["less"],
  javascript: ["javascript"],
  javascriptreact: ["react", "javascript"],
  typescript: ["typescript"],
  typescriptreact: ["react", "typescript"],
  python: ["python"],
  rust: ["rust"],
  go: ["go"],
  java: ["java"],
  kotlin: ["kotlin"],
  swift: ["swift"],
  php: ["php"],
  c: ["c"],
  cpp: ["cplusplus"],
  csharp: ["csharp"],
  qml: ["qt"],
  shell: ["bash", "linux"],
  powershell: ["powershell"],
  sql: ["azuresqldatabase", "postgresql", "mysql"],
  cmake: ["cmake"],
  docker: ["docker"],
  nodejs: ["nodejs"],
  npm: ["npm"],
  pnpm: ["pnpm"],
  yarn: ["yarn"],
  bun: ["bun"],
  react: ["react"],
  vue: ["vuejs"],
  nextjs: ["nextjs"],
  nuxt: ["nuxtjs"],
  svelte: ["svelte"],
  angular: ["angularjs"],
  vite: ["vitejs"],
  webpack: ["webpack"],
  tailwind: ["tailwindcss"],
  graphql: ["graphql"],
  ruby: ["ruby"],
  rails: ["rails"],
  laravel: ["laravel"],
  django: ["django"],
  flask: ["flask"],
  fastapi: ["fastapi"],
  dotnet: ["dotnetcore"],
  dart: ["dart"],
  flutter: ["flutter"],
  objectivec: ["objectivec"],
  lua: ["lua"],
  perl: ["perl"],
  r: ["r"],
  matlab: ["matlab"],
  julia: ["julia"],
  elixir: ["elixir"],
  erlang: ["erlang"],
  haskell: ["haskell"],
  ocaml: ["ocaml"],
  clojure: ["clojure"],
  scala: ["scala"],
  groovy: ["groovy"],
  solidity: ["solidity"],
  zig: ["zig"],
  nim: ["nim"],
  crystal: ["crystal"],
  fortran: ["fortran"],
  git: ["git"],
  github: ["github"],
  gitlab: ["gitlab"],
  postgresql: ["postgresql"],
  mysql: ["mysql"],
  mongodb: ["mongodb"],
  sqlite: ["sqlite"],
  redis: ["redis"],
  firebase: ["firebase"],
  supabase: ["supabase"],
  aws: ["amazonwebservices"],
  azure: ["azure"],
  googlecloud: ["googlecloud"],
  kubernetes: ["kubernetes"],
  unreal: ["unrealengine"],
  godot: ["godot"],
  unity: ["unity"],
  blender: ["blender"],
  maya: ["maya"],
  linux: ["linux"],
  apple: ["apple"],
  windows: ["windows11", "windows8"],
};

function loadIconSet(packageName) {
  const filename = require.resolve(`${packageName}/icons.json`);
  return JSON.parse(fs.readFileSync(filename, "utf8"));
}

function resolveIcon(set, name) {
  const direct = set.icons?.[name];
  if (direct) {
    return {
      ...direct,
      left: direct.left ?? set.left ?? 0,
      top: direct.top ?? set.top ?? 0,
      width: direct.width ?? set.width ?? 16,
      height: direct.height ?? set.height ?? 16,
    };
  }

  const alias = set.aliases?.[name];
  if (!alias) {
    return null;
  }

  const parent = resolveIcon(set, alias.parent);
  return parent
    ? {
        ...parent,
        left: alias.left ?? parent.left,
        top: alias.top ?? parent.top,
        width: alias.width ?? parent.width,
        height: alias.height ?? parent.height,
      }
    : null;
}

function chooseIconName(set, candidates, logicalName) {
  const available = new Set([
    ...Object.keys(set.icons ?? {}),
    ...Object.keys(set.aliases ?? {}),
  ]);

  for (const candidate of candidates) {
    if (available.has(candidate)) {
      return candidate;
    }
  }

  const tokens = logicalName
    .split(/[-_\s]+/)
    .filter((token) => token.length > 1);
  const ranked = [...available]
    .map((name) => ({
      name,
      score: tokens.reduce(
        (score, token) => score + (name.includes(token) ? 1 : 0),
        0,
      ),
    }))
    .filter((entry) => entry.score > 0)
    .sort((left, right) => {
      if (right.score !== left.score) {
        return right.score - left.score;
      }
      return left.name.length - right.name.length;
    });

  return ranked[0]?.name ?? "";
}

function monochromeBody(body, color) {
  return body
    .replace(/currentColor/gi, color)
    .replace(
      /(fill|stroke)="(?!none|transparent|url\()[^"]+"/gi,
      `$1="${color}"`,
    )
    .replace(
      /(fill|stroke)='(?!none|transparent|url\()[^']+'/gi,
      `$1='${color}'`,
    )
    .replace(
      /(fill|stroke):\s*(?!none|transparent|url\()[^;"']+/gi,
      `$1:${color}`,
    );
}

function svgFor(icon, body) {
  const left = icon.left ?? 0;
  const top = icon.top ?? 0;
  const width = icon.width ?? 16;
  const height = icon.height ?? 16;
  return [
    '<svg xmlns="http://www.w3.org/2000/svg"',
    ` viewBox="${left} ${top} ${width} ${height}"`,
    ' preserveAspectRatio="xMidYMid meet">',
    body,
    "</svg>",
    "",
  ].join("");
}


function alignToFour(value) {
  return (value + 3) & ~3;
}

function sfntChecksum(buffer) {
  let sum = 0;
  const paddedLength = alignToFour(buffer.length);

  for (let offset = 0; offset < paddedLength; offset += 4) {
    const value = offset + 4 <= buffer.length
      ? buffer.readUInt32BE(offset)
      : Buffer.concat([buffer.subarray(offset), Buffer.alloc(4)]).readUInt32BE(0);
    sum = (sum + value) >>> 0;
  }

  return sum >>> 0;
}

function woffToTtf(woff) {
  if (woff.length < 44 || woff.toString("ascii", 0, 4) !== "wOFF") {
    throw new Error("The Seti font is not a valid WOFF 1 font.");
  }

  const flavor = woff.readUInt32BE(4);
  const numTables = woff.readUInt16BE(12);
  const totalSfntSize = woff.readUInt32BE(16);
  const directoryOffset = 44;
  const tableDataOffset = 12 + numTables * 16;
  const ttf = Buffer.alloc(totalSfntSize);
  const maximumPowerOfTwo = 2 ** Math.floor(Math.log2(numTables));
  const entrySelector = Math.floor(Math.log2(maximumPowerOfTwo));

  ttf.writeUInt32BE(flavor, 0);
  ttf.writeUInt16BE(numTables, 4);
  ttf.writeUInt16BE(maximumPowerOfTwo * 16, 6);
  ttf.writeUInt16BE(entrySelector, 8);
  ttf.writeUInt16BE(numTables * 16 - maximumPowerOfTwo * 16, 10);

  let outputOffset = tableDataOffset;
  let headOffset = -1;

  for (let index = 0; index < numTables; index += 1) {
    const sourceRecordOffset = directoryOffset + index * 20;
    const targetRecordOffset = 12 + index * 16;
    const tag = woff.toString("ascii", sourceRecordOffset, sourceRecordOffset + 4);
    const sourceOffset = woff.readUInt32BE(sourceRecordOffset + 4);
    const compressedLength = woff.readUInt32BE(sourceRecordOffset + 8);
    const originalLength = woff.readUInt32BE(sourceRecordOffset + 12);
    const originalChecksum = woff.readUInt32BE(sourceRecordOffset + 16);
    const compressed = woff.subarray(sourceOffset, sourceOffset + compressedLength);
    const table = compressedLength < originalLength
      ? zlib.inflateSync(compressed)
      : Buffer.from(compressed);

    if (table.length !== originalLength) {
      throw new Error(`Seti font table ${tag} has an invalid length.`);
    }

    ttf.write(tag, targetRecordOffset, 4, "ascii");
    ttf.writeUInt32BE(originalChecksum, targetRecordOffset + 4);
    ttf.writeUInt32BE(outputOffset, targetRecordOffset + 8);
    ttf.writeUInt32BE(originalLength, targetRecordOffset + 12);
    table.copy(ttf, outputOffset);

    if (tag === "head") {
      headOffset = outputOffset;
    }

    outputOffset = alignToFour(outputOffset + originalLength);
  }

  if (headOffset >= 0) {
    ttf.writeUInt32BE(0, headOffset + 8);
    const adjustment = (0xB1B0AFBA - sfntChecksum(ttf)) >>> 0;
    ttf.writeUInt32BE(adjustment, headOffset + 8);
  }

  return ttf;
}

function setiThemeCandidates() {
  const home = process.env.HOME ?? "";
  const localAppData = process.env.LOCALAPPDATA ?? "";
  const programFiles = process.env.ProgramFiles ?? "";
  const configured = process.env.ARCHIVIST_SETI_THEME_DIR ?? "";

  return [
    configured,
    "/Applications/Visual Studio Code.app/Contents/Resources/app/extensions/theme-seti/icons",
    "/Applications/Visual Studio Code - Insiders.app/Contents/Resources/app/extensions/theme-seti/icons",
    "/Applications/Cursor.app/Contents/Resources/app/extensions/theme-seti/icons",
    "/Applications/VSCodium.app/Contents/Resources/app/extensions/theme-seti/icons",
    path.join(home, "Applications/Visual Studio Code.app/Contents/Resources/app/extensions/theme-seti/icons"),
    "/usr/share/code/resources/app/extensions/theme-seti/icons",
    "/usr/share/codium/resources/app/extensions/theme-seti/icons",
    "/opt/visual-studio-code/resources/app/extensions/theme-seti/icons",
    path.join(localAppData, "Programs/Microsoft VS Code/resources/app/extensions/theme-seti/icons"),
    path.join(programFiles, "Microsoft VS Code/resources/app/extensions/theme-seti/icons"),
  ].filter((candidate) => candidate.length > 0);
}

function findSetiTheme() {
  for (const directory of setiThemeCandidates()) {
    const font = path.join(directory, "seti.woff");
    const theme = path.join(directory, "vs-seti-icon-theme.json");

    if (fs.existsSync(font) && fs.existsSync(theme)) {
      return { directory, font, theme };
    }
  }

  throw new Error(
    "Archivist could not find VS Code's built-in Seti icon theme. "
      + "Install VS Code/Cursor or set ARCHIVIST_SETI_THEME_DIR to the folder "
      + "containing seti.woff and vs-seti-icon-theme.json.",
  );
}

function setiDefinitionForFile(theme, fileName, languageId) {
  const normalizedPath = String(fileName || "").replaceAll("\\", "/").toLowerCase();
  const baseName = path.posix.basename(normalizedPath);
  const fileNames = theme.fileNames ?? {};

  if (fileNames[normalizedPath]) {
    return fileNames[normalizedPath];
  }
  if (fileNames[baseName]) {
    return fileNames[baseName];
  }

  const extensionKeys = Object.keys(theme.fileExtensions ?? {})
    .sort((left, right) => right.length - left.length);

  for (const extension of extensionKeys) {
    if (baseName === extension || baseName.endsWith(`.${extension}`)) {
      return theme.fileExtensions[extension];
    }
  }

  const languageDefinition = (theme.languageIds ?? {})[languageId];
  return languageDefinition ?? theme.file ?? "_default";
}

function setiCharacter(value) {
  const match = /^\\([0-9a-f]{4,6})$/i.exec(String(value || ""));
  return match ? String.fromCodePoint(Number.parseInt(match[1], 16)) : "";
}

function escapedGlyph(value) {
  if (!value) {
    return "";
  }

  const codePoint = value.codePointAt(0);
  return codePoint <= 0xFFFF
    ? `\\u${codePoint.toString(16).toUpperCase().padStart(4, "0")}`
    : `\\u{${codePoint.toString(16).toUpperCase()}}`;
}

function generatedSetiRegistry(glyphs, colors) {
  const glyphLines = Object.entries(glyphs)
    .map(([name, glyph]) => `    ${JSON.stringify(name)}: "${escapedGlyph(glyph)}"`)
    .join(",\n");
  const colorLines = Object.entries(colors)
    .map(([name, color]) => `    ${JSON.stringify(name)}: ${JSON.stringify(color)}`)
    .join(",\n");
  const toneLines = Object.entries(tones)
    .map(([name, color]) => `    ${JSON.stringify(name)}: ${JSON.stringify(color)}`)
    .join(",\n");

  return `.pragma library\n\nvar glyphs = {\n${glyphLines}\n}\n\n`
    + `var brandColors = {\n${colorLines}\n}\n\n`
    + `var toneColors = {\n${toneLines}\n}\n\n`
    + `function glyph(name) {\n`
    + `    return glyphs[String(name || "")] || ""\n`
    + `}\n\n`
    + `function color(name, tone) {\n`
    + `    var requestedTone = String(tone || "brand")\n`
    + `    if (requestedTone !== "brand") {\n`
    + `        return toneColors[requestedTone] || toneColors.muted\n`
    + `    }\n`
    + `    return brandColors[String(name || "")] || toneColors.normal\n`
    + `}\n`;
}

function writeFile(relativePath, content) {
  const filename = path.join(outputDirectory, relativePath);
  fs.mkdirSync(path.dirname(filename), { recursive: true });
  fs.writeFileSync(filename, content, "utf8");
}

function writeBinaryFile(relativePath, content) {
  const filename = path.join(outputDirectory, relativePath);
  fs.mkdirSync(path.dirname(filename), { recursive: true });
  fs.writeFileSync(filename, content);
}

function cleanOutput() {
  fs.rmSync(outputDirectory, { recursive: true, force: true });
  fs.mkdirSync(outputDirectory, { recursive: true });
}

function vendorUiIcons(streamlineSet, codiconSet) {
  const manifest = {};

  for (const [logicalName, candidates] of Object.entries(uiIcons)) {
    const codiconName = codiconUiIcons[logicalName] ?? "";
    const sourceSet = codiconName.length > 0 ? codiconSet : streamlineSet;
    const sourceName = codiconName.length > 0
      ? codiconName
      : chooseIconName(sourceSet, candidates, logicalName);
    const icon = sourceName ? resolveIcon(sourceSet, sourceName) : null;

    let resolvedName = sourceName;
    let resolvedIcon = icon;
    let resolvedSet = codiconName.length > 0 ? "codicon" : "streamline-flex";

    if (!resolvedIcon) {
      resolvedName = chooseIconName(
        streamlineSet,
        ["file", "common-file-text", "document"],
        "file",
      );
      resolvedIcon = resolveIcon(streamlineSet, resolvedName);
      resolvedSet = "streamline-flex";
      console.warn(
        `Using generic file icon for unavailable UI icon: ${logicalName}`,
      );
    }

    if (!resolvedIcon) {
      throw new Error("No usable fallback icon was found.");
    }

    manifest[logicalName] = `${resolvedSet}:${resolvedName}`;

    for (const [toneName, color] of Object.entries(tones)) {
      writeFile(
        path.join("ui", toneName, `${logicalName}.svg`),
        svgFor(resolvedIcon, monochromeBody(resolvedIcon.body, color)),
      );
    }
  }

  return manifest;
}

function vendorLanguageIcons() {
  const source = findSetiTheme();
  const theme = JSON.parse(fs.readFileSync(source.theme, "utf8"));
  const ttf = woffToTtf(fs.readFileSync(source.font));
  const glyphs = {};
  const colors = {};
  const manifest = {};

  writeBinaryFile(path.join("fonts", "seti.ttf"), ttf);

  for (const logicalName of Object.keys(languageIcons)) {
    const representativeFile = setiRepresentativeFiles[logicalName] ?? `file.${logicalName}`;
    const setiLanguageId = setiLanguageIds[logicalName] ?? logicalName;
    const definitionName = (theme.languageIds ?? {})[setiLanguageId]
      ?? setiDefinitionForFile(
        theme,
        representativeFile,
        setiLanguageId,
      );
    const definition = (theme.iconDefinitions ?? {})[definitionName]
      ?? (theme.iconDefinitions ?? {})[theme.file]
      ?? (theme.iconDefinitions ?? {})._default;
    const glyph = setiCharacter(definition?.fontCharacter);

    if (!glyph) {
      console.warn(`No Seti glyph found for ${logicalName}; using generic file icon.`);
      manifest[logicalName] = "fallback:file";
      continue;
    }

    glyphs[logicalName] = glyph;
    colors[logicalName] = definition.fontColor ?? tones.normal;
    manifest[logicalName] = `seti-font:${definitionName}`;
  }

  const registryFilename = path.join(
    rootDirectory,
    "frontend/qml/App/Icons/GeneratedSetiRegistry.js",
  );
  fs.writeFileSync(
    registryFilename,
    generatedSetiRegistry(glyphs, colors),
    "utf8",
  );

  console.log(`Using Seti theme from ${source.directory}`);
  return manifest;
}

cleanOutput();
const streamlineFlex = loadIconSet("@iconify-json/streamline-flex");
const codicon = loadIconSet("@iconify-json/codicon");
const manifest = {
  generatedAt: new Date().toISOString(),
  ui: vendorUiIcons(streamlineFlex, codicon),
  languages: vendorLanguageIcons(),
};
writeFile("manifest.json", `${JSON.stringify(manifest, null, 2)}\n`);

console.log(
  `Vendored ${Object.keys(manifest.ui).length} UI icons and `
    + `${Object.keys(manifest.languages).length} language/brand icons.`,
);

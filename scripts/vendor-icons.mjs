import fs from "node:fs";
import path from "node:path";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";

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
      width: direct.width ?? set.width ?? 24,
      height: direct.height ?? set.height ?? 24,
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
  const width = icon.width ?? 24;
  const height = icon.height ?? 24;
  return [
    '<svg xmlns="http://www.w3.org/2000/svg"',
    ` viewBox="0 0 ${width} ${height}"`,
    ' preserveAspectRatio="xMidYMid meet">',
    body,
    "</svg>",
    "",
  ].join("");
}

function writeFile(relativePath, content) {
  const filename = path.join(outputDirectory, relativePath);
  fs.mkdirSync(path.dirname(filename), { recursive: true });
  fs.writeFileSync(filename, content, "utf8");
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

function vendorLanguageIcons(deviconSet, codiconSet) {
  const manifest = {};

  for (const [logicalName, candidates] of Object.entries(languageIcons)) {
    const useJsonCodicon = logicalName === "json";
    const sourceSet = useJsonCodicon ? codiconSet : deviconSet;
    const sourceName = useJsonCodicon
      ? "json"
      : chooseIconName(sourceSet, candidates, logicalName);
    const icon = sourceName ? resolveIcon(sourceSet, sourceName) : null;

    if (!icon) {
      console.warn(`Using generic file icon for unavailable language icon: ${logicalName}`);
      manifest[logicalName] = "fallback:file";
      const accentFallback = fs.readFileSync(
        path.join(outputDirectory, "ui", "accent", "file.svg"),
        "utf8",
      );
      writeFile(
        path.join("languages", "brand", `${logicalName}.svg`),
        accentFallback,
      );
      for (const toneName of Object.keys(tones)) {
        const fallback = fs.readFileSync(
          path.join(outputDirectory, "ui", toneName, "file.svg"),
          "utf8",
        );
        writeFile(
          path.join("languages", toneName, `${logicalName}.svg`),
          fallback,
        );
      }
      continue;
    }

    const sourceLabel = useJsonCodicon ? "codicon" : "devicon-plain";
    const brandColor = languageBrandColors[logicalName] ?? tones.normal;
    manifest[logicalName] = `${sourceLabel}:${sourceName}`;
    writeFile(
      path.join("languages", "brand", `${logicalName}.svg`),
      svgFor(icon, monochromeBody(icon.body, brandColor)),
    );

    for (const [toneName, color] of Object.entries(tones)) {
      writeFile(
        path.join("languages", toneName, `${logicalName}.svg`),
        svgFor(icon, monochromeBody(icon.body, color)),
      );
    }
  }

  return manifest;
}

cleanOutput();
const streamlineFlex = loadIconSet("@iconify-json/streamline-flex");
const codicon = loadIconSet("@iconify-json/codicon");
const devicon = loadIconSet("@iconify-json/devicon-plain");
const manifest = {
  generatedAt: new Date().toISOString(),
  ui: vendorUiIcons(streamlineFlex, codicon),
  languages: vendorLanguageIcons(devicon, codicon),
};
writeFile("manifest.json", `${JSON.stringify(manifest, null, 2)}\n`);

console.log(
  `Vendored ${Object.keys(manifest.ui).length} UI icons and `
    + `${Object.keys(manifest.languages).length} language/brand icons.`,
);

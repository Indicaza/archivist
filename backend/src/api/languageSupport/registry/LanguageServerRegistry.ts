import type {
  LanguageServerDefinition,
} from "../types/LanguageSupportTypes.js";

const definitions: readonly LanguageServerDefinition[] = [
  {
    id: "typescript",
    displayName: "TypeScript / JavaScript",
    languageIds: [
      "typescript",
      "javascript",
      "typescriptreact",
      "javascriptreact",
    ],
    executableCandidates: [
      "typescript-language-server",
    ],
    args: ["--stdio"],
    rootMarkers: [
      "tsconfig.json",
      "jsconfig.json",
      "package.json",
      ".git",
    ],
    enabledByDefault: true,
  },
  {
    id: "qml",
    displayName: "QML",
    languageIds: ["qml"],
    executableCandidates: ["qmlls", "qmlls6"],
    args: [],
    rootMarkers: [
      ".qmlls.ini",
      "qmldir",
      "CMakeLists.txt",
      ".git",
    ],
    enabledByDefault: true,
  },
  {
    id: "clangd",
    displayName: "C / C++",
    languageIds: [
      "c",
      "cpp",
      "objective-c",
      "objective-cpp",
    ],
    executableCandidates: ["clangd"],
    args: ["--background-index"],
    rootMarkers: [
      "compile_commands.json",
      "compile_flags.txt",
      "CMakeLists.txt",
      ".git",
    ],
    enabledByDefault: true,
  },
  {
    id: "rust",
    displayName: "Rust",
    languageIds: ["rust"],
    executableCandidates: ["rust-analyzer"],
    args: [],
    rootMarkers: ["Cargo.toml", ".git"],
    enabledByDefault: true,
  },
  {
    id: "python",
    displayName: "Python",
    languageIds: ["python"],
    executableCandidates: ["pyright-langserver"],
    args: ["--stdio"],
    rootMarkers: [
      "pyrightconfig.json",
      "pyproject.toml",
      "setup.cfg",
      "requirements.txt",
      ".git",
    ],
    enabledByDefault: true,
  },
  {
    id: "go",
    displayName: "Go",
    languageIds: ["go"],
    executableCandidates: ["gopls"],
    args: [],
    rootMarkers: ["go.work", "go.mod", ".git"],
    enabledByDefault: true,
  },
  {
    id: "html",
    displayName: "HTML",
    languageIds: ["html"],
    executableCandidates: [
      "vscode-html-language-server",
    ],
    args: ["--stdio"],
    rootMarkers: ["package.json", ".git"],
    enabledByDefault: false,
  },
  {
    id: "css",
    displayName: "CSS / SCSS / Less",
    languageIds: ["css", "scss", "less"],
    executableCandidates: [
      "vscode-css-language-server",
    ],
    args: ["--stdio"],
    rootMarkers: ["package.json", ".git"],
    enabledByDefault: false,
  },
  {
    id: "json",
    displayName: "JSON",
    languageIds: ["json"],
    executableCandidates: [
      "vscode-json-language-server",
    ],
    args: ["--stdio"],
    rootMarkers: ["package.json", ".git"],
    enabledByDefault: false,
  },
  {
    id: "yaml",
    displayName: "YAML",
    languageIds: ["yaml"],
    executableCandidates: ["yaml-language-server"],
    args: ["--stdio"],
    rootMarkers: [".yamllint", "package.json", ".git"],
    enabledByDefault: false,
  },
  {
    id: "bash",
    displayName: "Bash / Shell",
    languageIds: ["shell", "bash", "sh"],
    executableCandidates: ["bash-language-server"],
    args: ["start"],
    rootMarkers: [".git"],
    enabledByDefault: false,
  },
  {
    id: "markdown",
    displayName: "Markdown",
    languageIds: ["markdown"],
    executableCandidates: ["marksman"],
    args: ["server"],
    rootMarkers: [".marksman.toml", ".git"],
    enabledByDefault: false,
  },
];

const definitionsById = new Map(
  definitions.map((definition) => [
    definition.id,
    definition,
  ]),
);

export function getLanguageServerDefinitions():
  readonly LanguageServerDefinition[] {
  return definitions;
}

export function getLanguageServerDefinition(
  serverId: string,
): LanguageServerDefinition | null {
  return definitionsById.get(serverId) ?? null;
}

export function findLanguageServerForLanguage(
  languageId: string,
): LanguageServerDefinition | null {
  const normalized = languageId.trim().toLowerCase();

  return definitions.find((definition) =>
    definition.languageIds.includes(normalized)
  ) ?? null;
}

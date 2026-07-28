export interface LanguageSupportDefinition {
  serverId: string;
  displayName: string;
  monacoLanguageIds: readonly string[];
  completionTriggers: readonly string[];
  signatureHelp: boolean;
}

export const languageSupportDefinitions:
  readonly LanguageSupportDefinition[] = [
    {
      serverId: "typescript",
      displayName: "TypeScript / JavaScript",
      monacoLanguageIds: ["typescript", "javascript"],
      completionTriggers: [
        ".",
        '"',
        "'",
        "`",
        "/",
        "@",
        "<",
        "#",
      ],
      signatureHelp: true,
    },
    {
      serverId: "qml",
      displayName: "QML",
      monacoLanguageIds: ["qml"],
      completionTriggers: [".", ":", '"', "'"],
      signatureHelp: false,
    },
    {
      serverId: "clangd",
      displayName: "C / C++",
      monacoLanguageIds: ["c", "cpp"],
      completionTriggers: [".", ">", ":", '"', "<"],
      signatureHelp: true,
    },
    {
      serverId: "rust",
      displayName: "Rust",
      monacoLanguageIds: ["rust"],
      completionTriggers: [".", ":", "'"],
      signatureHelp: true,
    },
    {
      serverId: "python",
      displayName: "Python",
      monacoLanguageIds: ["python"],
      completionTriggers: [".", '"', "'"],
      signatureHelp: true,
    },
    {
      serverId: "go",
      displayName: "Go",
      monacoLanguageIds: ["go"],
      completionTriggers: [".", '"', "'"],
      signatureHelp: true,
    },
    {
      serverId: "yaml",
      displayName: "YAML",
      monacoLanguageIds: ["yaml"],
      completionTriggers: [" ", ":", "-", '"', "'"],
      signatureHelp: false,
    },
    {
      serverId: "bash",
      displayName: "Bash / Shell",
      monacoLanguageIds: ["shell"],
      completionTriggers: ["$", "-", "/", '"', "'", "{"],
      signatureHelp: false,
    },
    {
      serverId: "markdown",
      displayName: "Markdown",
      monacoLanguageIds: ["markdown"],
      completionTriggers: ["[", "(", "#", "`", "/"],
      signatureHelp: false,
    },
  ];

export function languageSupportDefinition(
  monacoLanguageId: string,
): LanguageSupportDefinition | null {
  return languageSupportDefinitions.find((definition) =>
    definition.monacoLanguageIds.includes(monacoLanguageId)
  ) ?? null;
}

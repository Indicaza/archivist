import type * as Monaco from "monaco-editor/editor/editor.api";
import {
  registerBuildLanguages,
} from "./Languages/BuildLanguages.js";
import {
  registerQmlLanguage,
} from "./Languages/QmlLanguage.js";

type MonacoApi = typeof Monaco;

const languageAliases: Record<string, string> = {
  "c++": "cpp",
  bash: "shell",
  cxx: "cpp",
  javascriptreact: "javascript",
  "javascript jsx": "javascript",
  jsx: "javascript",
  sass: "scss",
  shellscript: "shell",
  toml: "ini",
  tsx: "typescript",
  typescriptreact: "typescript",
  "typescript jsx": "typescript",
  "qt/qml": "qml",
};

const standardLanguageIds = new Set([
  "c",
  "cpp",
  "css",
  "dockerfile",
  "go",
  "html",
  "ini",
  "java",
  "javascript",
  "json",
  "kotlin",
  "less",
  "markdown",
  "php",
  "python",
  "rust",
  "scss",
  "shell",
  "sql",
  "swift",
  "typescript",
  "xml",
  "yaml",
]);

let standardLanguagesPromise: Promise<void> | null = null;

export function normalizeLanguageId(
  languageId: string,
): string {
  const normalized = String(
    languageId || "plaintext",
  )
    .trim()
    .toLowerCase();

  return (
    languageAliases[normalized]
    || normalized
    || "plaintext"
  );
}

function languageRegistered(
  monaco: MonacoApi,
  languageId: string,
): boolean {
  return monaco.languages
    .getLanguages()
    .some((language) => language.id === languageId);
}

function registerCustomLanguage(
  monaco: MonacoApi,
  languageId: string,
): void {
  if (languageId === "qml") {
    registerQmlLanguage(monaco);
    return;
  }

  if (
    languageId === "cmake"
    || languageId === "makefile"
  ) {
    registerBuildLanguages(monaco);
  }
}

async function loadStandardLanguages(): Promise<void> {
  if (!standardLanguagesPromise) {
    standardLanguagesPromise = import(
      "monaco-editor"
    ).then(() => undefined);
  }

  try {
    await standardLanguagesPromise;
  } catch (error) {
    standardLanguagesPromise = null;
    throw error;
  }
}

export async function ensureLanguage(
  monaco: MonacoApi,
  requestedLanguageId: string,
): Promise<string> {
  const languageId = normalizeLanguageId(
    requestedLanguageId,
  );

  registerCustomLanguage(monaco, languageId);

  if (
    languageId === "plaintext"
    || languageId === "log"
    || languageRegistered(monaco, languageId)
  ) {
    return languageRegistered(monaco, languageId)
      ? languageId
      : "plaintext";
  }

  if (!standardLanguageIds.has(languageId)) {
    return "plaintext";
  }

  try {
    await loadStandardLanguages();
  } catch {
    return "plaintext";
  }

  return languageRegistered(monaco, languageId)
    ? languageId
    : "plaintext";
}

import type * as Monaco from "monaco-editor/editor/editor.api";
import type {
  ArchivistTheme,
} from "../IdeHost.types.js";

type MonacoApi = typeof Monaco;

function tokenColor(
  value: string,
  fallback: string,
): string {
  const normalized = String(value || "")
    .replace("#", "")
    .trim();

  return /^[0-9A-Fa-f]{6}$/.test(normalized)
    ? normalized
    : fallback;
}

export function defineArchivistEditorTheme(
  monaco: MonacoApi,
  theme: ArchivistTheme,
): void {
  const foreground = tokenColor(
    theme.appText,
    "D8D2C7",
  );
  const muted = tokenColor(
    theme.mutedText,
    "8F887D",
  );
  const accent = tokenColor(
    theme.accent,
    "9280BC",
  );
  const accentBright = tokenColor(
    theme.accentBright,
    "B7AAD2",
  );

  monaco.editor.defineTheme("archivist", {
    base: "vs-dark",
    inherit: true,
    rules: [
      {
        token: "",
        foreground,
      },
      {
        token: "comment",
        foreground: muted,
        fontStyle: "italic",
      },
      {
        token: "keyword",
        foreground: accentBright,
      },
      {
        token: "keyword.directive",
        foreground: accent,
      },
      {
        token: "type",
        foreground: "C6A66F",
      },
      {
        token: "type.identifier",
        foreground: "C6A66F",
      },
      {
        token: "function",
        foreground: "D9C7A3",
      },
      {
        token: "string",
        foreground: "AAB38C",
      },
      {
        token: "string.escape",
        foreground: "C4BA82",
      },
      {
        token: "number",
        foreground: "80A4B8",
      },
      {
        token: "constant",
        foreground: "80A4B8",
      },
      {
        token: "variable",
        foreground: "C8C1B5",
      },
      {
        token: "variable.other.property",
        foreground: "C8C1B5",
      },
      {
        token: "tag",
        foreground: "B5A0C6",
      },
      {
        token: "attribute.name",
        foreground: "C6A66F",
      },
      {
        token: "operator",
        foreground: "AAA39A",
      },
      {
        token: "delimiter",
        foreground: "8F887D",
      },
      {
        token: "regexp",
        foreground: "B9A685",
      },
    ],
    colors: {
      "editor.background": theme.workspaceBg,
      "editor.foreground": theme.appText,
      "editorLineNumber.foreground": theme.mutedText,
      "editorLineNumber.activeForeground": theme.appText,
      "editorCursor.foreground": theme.accentBright,
      "editor.selectionBackground": theme.activeBg,
      "editor.inactiveSelectionBackground":
        theme.controlSurfaceBg,
      "editorIndentGuide.background1": theme.quietBorder,
      "editorIndentGuide.activeBackground1":
        theme.panelBorder,
      "editorBracketHighlight.foreground1":
        theme.accentBright,
      "editorBracketHighlight.foreground2": "#C6A66F",
      "editorBracketHighlight.foreground3": "#80A4B8",
      "editorBracketHighlight.foreground4": "#AAB38C",
      "editorBracketHighlight.foreground5": "#C8C1B5",
      "editorBracketHighlight.foreground6":
        theme.mutedText,
      "editorWidget.background": theme.surfaceBg,
      "editorWidget.border": theme.panelBorder,
      "editorSuggestWidget.background": theme.surfaceBg,
      "editorSuggestWidget.border": theme.panelBorder,
      "editorSuggestWidget.selectedBackground":
        theme.activeBg,
      "editorHoverWidget.background": theme.surfaceBg,
      "editorHoverWidget.border": theme.panelBorder,
      "minimap.background": theme.workspaceBg,
      "minimap.selectionHighlight": theme.activeBg,
      "minimap.findMatchHighlight": theme.accent,
      "scrollbarSlider.background": theme.controlSurfaceBg,
      "scrollbarSlider.hoverBackground": theme.hoverBg,
      "scrollbarSlider.activeBackground": theme.activeBg,
    },
  });

  monaco.editor.setTheme("archivist");
}

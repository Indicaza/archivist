import type * as Monaco from "monaco-editor/editor/editor.api";
import type {
  ArchivistDocument,
} from "../../IdeHost.types.js";
import {
  isRecord,
  type LanguageServerSessionDescriptor,
  type LspCompletionItem,
  type LspCompletionList,
  type LspDiagnostic,
  type LspDocumentHighlight,
  type LspDocumentSymbol,
  type LspSymbolInformation,
  type LspTextEdit,
  type LspWorkspaceEdit,
  type LspHover,
  type LspLocation,
  type LspLocationLink,
  type LspMarkupContent,
  type LspPosition,
  type LspRange,
} from "./LspTypes.js";

type MonacoApi = typeof Monaco;

function normalizedPath(value: string): string {
  const normalized = String(value || "")
    .trim()
    .replace(/\\/g, "/")
    .replace(/\/+$/, "");

  return /^[A-Za-z]:\//.test(normalized)
    ? normalized.toLowerCase()
    : normalized;
}

export function pathWithin(
  parentPath: string,
  childPath: string,
): boolean {
  const parent = normalizedPath(parentPath);
  const child = normalizedPath(childPath);

  return child === parent || child.startsWith(`${parent}/`);
}

export function fileName(pathValue: string): string {
  return normalizedPath(pathValue)
    .split("/")
    .filter(Boolean)
    .at(-1) || "workspace";
}

export function lspLanguageId(
  document: ArchivistDocument,
  monacoLanguageId: string,
): string {
  const lowerPath = normalizedPath(document.filePath).toLowerCase();

  if (lowerPath.endsWith(".tsx")) {
    return "typescriptreact";
  }

  if (lowerPath.endsWith(".jsx")) {
    return "javascriptreact";
  }

  return monacoLanguageId;
}

export function toLspPosition(
  position: Monaco.Position,
): LspPosition {
  return {
    line: position.lineNumber - 1,
    character: position.column - 1,
  };
}

export function toMonacoRange(
  monaco: MonacoApi,
  range: LspRange,
): Monaco.Range {
  return new monaco.Range(
    range.start.line + 1,
    range.start.character + 1,
    range.end.line + 1,
    range.end.character + 1,
  );
}

export function markdownValue(
  content: string | LspMarkupContent | undefined,
): string | Monaco.IMarkdownString | undefined {
  if (content === undefined) {
    return undefined;
  }

  return typeof content === "string"
    ? content
    : { value: content.value };
}

export function hoverContents(
  contents: LspHover["contents"],
): Monaco.IMarkdownString[] {
  const values = Array.isArray(contents)
    ? contents
    : [contents];

  return values.map((value) => {
    if (typeof value === "string") {
      return { value };
    }

    if ("language" in value) {
      return {
        value: `\`\`\`${value.language}\n${value.value}\n\`\`\``,
      };
    }

    return { value: value.value };
  });
}

function completionItemKind(
  monaco: MonacoApi,
  kind: number | undefined,
): Monaco.languages.CompletionItemKind {
  const kinds = monaco.languages.CompletionItemKind;
  const mapping: Record<number, Monaco.languages.CompletionItemKind> = {
    1: kinds.Text,
    2: kinds.Method,
    3: kinds.Function,
    4: kinds.Constructor,
    5: kinds.Field,
    6: kinds.Variable,
    7: kinds.Class,
    8: kinds.Interface,
    9: kinds.Module,
    10: kinds.Property,
    11: kinds.Unit,
    12: kinds.Value,
    13: kinds.Enum,
    14: kinds.Keyword,
    15: kinds.Snippet,
    16: kinds.Color,
    17: kinds.File,
    18: kinds.Reference,
    19: kinds.Folder,
    20: kinds.EnumMember,
    21: kinds.Constant,
    22: kinds.Struct,
    23: kinds.Event,
    24: kinds.Operator,
    25: kinds.TypeParameter,
  };

  return kind ? mapping[kind] ?? kinds.Text : kinds.Text;
}

function markerSeverity(
  monaco: MonacoApi,
  severity: number | undefined,
): Monaco.MarkerSeverity {
  switch (severity) {
    case 2:
      return monaco.MarkerSeverity.Warning;
    case 3:
      return monaco.MarkerSeverity.Info;
    case 4:
      return monaco.MarkerSeverity.Hint;
    default:
      return monaco.MarkerSeverity.Error;
  }
}

function diagnosticTags(
  monaco: MonacoApi,
  tags: number[] | undefined,
): Monaco.MarkerTag[] | undefined {
  if (!tags || tags.length === 0) {
    return undefined;
  }

  const result = tags.flatMap((tag) => {
    switch (tag) {
      case 1:
        return [monaco.MarkerTag.Unnecessary];
      case 2:
        return [monaco.MarkerTag.Deprecated];
      default:
        return [];
    }
  });

  return result.length > 0 ? result : undefined;
}

export function diagnosticMarker(
  monaco: MonacoApi,
  diagnostic: LspDiagnostic,
): Monaco.editor.IMarkerData {
  const range = toMonacoRange(monaco, diagnostic.range);

  return {
    severity: markerSeverity(monaco, diagnostic.severity),
    message: diagnostic.message,
    source: diagnostic.source,
    code: diagnostic.code === undefined
      ? undefined
      : String(diagnostic.code),
    tags: diagnosticTags(monaco, diagnostic.tags),
    startLineNumber: range.startLineNumber,
    startColumn: range.startColumn,
    endLineNumber: range.endLineNumber,
    endColumn: range.endColumn,
    relatedInformation: diagnostic.relatedInformation?.map(
      (information) => {
        const relatedRange = toMonacoRange(
          monaco,
          information.location.range,
        );

        return {
          resource: monaco.Uri.parse(
            information.location.uri,
          ),
          message: information.message,
          startLineNumber: relatedRange.startLineNumber,
          startColumn: relatedRange.startColumn,
          endLineNumber: relatedRange.endLineNumber,
          endColumn: relatedRange.endColumn,
        };
      },
    ),
  };
}

export function locationResult(
  monaco: MonacoApi,
  value: LspLocation | LspLocationLink,
): Monaco.languages.Location | Monaco.languages.LocationLink {
  if ("targetUri" in value) {
    return {
      uri: monaco.Uri.parse(value.targetUri),
      range: toMonacoRange(monaco, value.targetRange),
      targetSelectionRange: toMonacoRange(
        monaco,
        value.targetSelectionRange,
      ),
      originSelectionRange: value.originSelectionRange
        ? toMonacoRange(monaco, value.originSelectionRange)
        : undefined,
    };
  }

  return {
    uri: monaco.Uri.parse(value.uri),
    range: toMonacoRange(monaco, value.range),
  };
}

export function completionResult(
  monaco: MonacoApi,
  model: Monaco.editor.ITextModel,
  position: Monaco.Position,
  result: LspCompletionItem[] | LspCompletionList | null,
): Monaco.languages.CompletionList {
  if (!result) {
    return { suggestions: [] };
  }

  const items = Array.isArray(result)
    ? result
    : result.items;
  const word = model.getWordUntilPosition(position);
  const fallbackRange = new monaco.Range(
    position.lineNumber,
    word.startColumn,
    position.lineNumber,
    word.endColumn,
  );

  return {
    incomplete: Array.isArray(result)
      ? false
      : result.isIncomplete === true,
    suggestions: items.map((item) => {
      let insertText = item.insertText || item.label;
      let range:
        | Monaco.IRange
        | Monaco.languages.CompletionItemRanges
        = fallbackRange;

      if (item.textEdit) {
        insertText = item.textEdit.newText;

        if ("range" in item.textEdit) {
          range = toMonacoRange(monaco, item.textEdit.range);
        } else {
          range = {
            insert: toMonacoRange(monaco, item.textEdit.insert),
            replace: toMonacoRange(monaco, item.textEdit.replace),
          };
        }
      }

      const suggestion: Monaco.languages.CompletionItem = {
        label: item.labelDetails
          ? {
              label: item.label,
              detail: item.labelDetails.detail,
              description: item.labelDetails.description,
            }
          : item.label,
        kind: completionItemKind(monaco, item.kind),
        insertText,
        range,
        detail: item.detail,
        documentation: markdownValue(item.documentation),
        sortText: item.sortText,
        filterText: item.filterText,
        preselect: item.preselect,
        commitCharacters: item.commitCharacters,
        insertTextRules: item.insertTextFormat === 2
          ? monaco.languages.CompletionItemInsertTextRule.InsertAsSnippet
          : undefined,
        tags: item.deprecated || item.tags?.includes(1)
          ? [monaco.languages.CompletionItemTag.Deprecated]
          : undefined,
        additionalTextEdits: item.additionalTextEdits?.map(
          (edit) => ({
            range: toMonacoRange(monaco, edit.range),
            text: edit.newText,
          }),
        ),
      };

      return suggestion;
    }),
  };
}

export function parseSessionResponse(
  payload: unknown,
): LanguageServerSessionDescriptor {
  if (!isRecord(payload) || payload.ok !== true) {
    const message = isRecord(payload)
      && isRecord(payload.error)
      && typeof payload.error.message === "string"
        ? payload.error.message
        : "Language-support session could not be created.";

    throw new Error(message);
  }

  const session = payload.session;

  if (
    !isRecord(session)
    || typeof session.sessionId !== "string"
    || typeof session.serverId !== "string"
    || typeof session.displayName !== "string"
    || !Array.isArray(session.languageIds)
    || typeof session.workspaceRoot !== "string"
    || typeof session.socketUrl !== "string"
    || typeof session.expiresAt !== "string"
  ) {
    throw new Error(
      "Language-support session response was malformed.",
    );
  }

  return {
    sessionId: session.sessionId,
    serverId: session.serverId,
    displayName: session.displayName,
    languageIds: session.languageIds.map(String),
    state: "pending",
    workspaceRoot: session.workspaceRoot,
    filePath: typeof session.filePath === "string"
      ? session.filePath
      : null,
    socketUrl: session.socketUrl,
    expiresAt: session.expiresAt,
  };
}

export function rangesIntersect(
  left: LspRange,
  right: LspRange,
): boolean {
  const before = (a: LspPosition, b: LspPosition): boolean =>
    a.line < b.line
    || (a.line === b.line && a.character < b.character);

  return !before(left.end, right.start)
    && !before(right.end, left.start);
}

export function toLspRange(
  range: Monaco.IRange,
): LspRange {
  return {
    start: {
      line: range.startLineNumber - 1,
      character: range.startColumn - 1,
    },
    end: {
      line: range.endLineNumber - 1,
      character: range.endColumn - 1,
    },
  };
}

export function textEditsResult(
  monaco: MonacoApi,
  edits: readonly LspTextEdit[] | null | undefined,
): Monaco.languages.TextEdit[] {
  return (edits ?? []).map((edit) => ({
    range: toMonacoRange(monaco, edit.range),
    text: edit.newText,
  }));
}

export interface SingleDocumentEdit {
  /** Edits that target the requested document. */
  edits: LspTextEdit[];
  /** Other files the workspace edit would change. */
  otherUris: string[];
  /** Create, rename, or delete operations, which Archivist does not apply. */
  resourceOperations: number;
}

/**
 * Archivist only applies language-server edits to the document that is open
 * in the editor, because other Library files are not backed by an editable,
 * saveable model. Split a workspace edit into the part that targets that
 * document and a list of everything else so callers can refuse partial edits.
 */
export function singleDocumentEdit(
  monaco: MonacoApi,
  workspaceEdit: LspWorkspaceEdit | null | undefined,
  documentUri: string,
): SingleDocumentEdit {
  const target = monaco.Uri.parse(documentUri).toString();
  const result: SingleDocumentEdit = {
    edits: [],
    otherUris: [],
    resourceOperations: 0,
  };
  const addEdits = (
    uri: string,
    edits: readonly LspTextEdit[],
  ): void => {
    if (edits.length === 0) {
      return;
    }

    const normalized = monaco.Uri.parse(uri).toString();

    if (normalized === target) {
      result.edits.push(...edits);
    } else if (!result.otherUris.includes(normalized)) {
      result.otherUris.push(normalized);
    }
  };

  if (!workspaceEdit) {
    return result;
  }

  if (Array.isArray(workspaceEdit.documentChanges)) {
    for (const change of workspaceEdit.documentChanges) {
      if ("textDocument" in change && Array.isArray(change.edits)) {
        addEdits(change.textDocument.uri, change.edits);
      } else {
        result.resourceOperations += 1;
      }
    }

    return result;
  }

  for (const [uri, edits] of Object.entries(workspaceEdit.changes ?? {})) {
    if (Array.isArray(edits)) {
      addEdits(uri, edits);
    }
  }

  return result;
}

export function describeOtherFiles(
  monaco: MonacoApi,
  uris: readonly string[],
): string {
  const names = uris
    .slice(0, 3)
    .map((uri) => fileName(monaco.Uri.parse(uri).fsPath || uri));
  const remainder = uris.length - names.length;

  return names.join(", ")
    + (remainder > 0 ? ` and ${remainder} more` : "");
}

function symbolKind(
  monaco: MonacoApi,
  kind: number,
): Monaco.languages.SymbolKind {
  // LSP SymbolKind starts at 1 (File); Monaco's enum starts at 0 (File).
  const value = Math.trunc(Number(kind)) - 1;

  return value >= 0 && value <= 25
    ? value as Monaco.languages.SymbolKind
    : monaco.languages.SymbolKind.Variable;
}

function symbolTags(
  monaco: MonacoApi,
  tags: number[] | undefined,
  deprecated: boolean | undefined,
): Monaco.languages.SymbolTag[] {
  return deprecated || tags?.includes(1)
    ? [monaco.languages.SymbolTag.Deprecated]
    : [];
}

function documentSymbol(
  monaco: MonacoApi,
  value: LspDocumentSymbol,
): Monaco.languages.DocumentSymbol {
  const range = toMonacoRange(monaco, value.range);
  const selectionRange = toMonacoRange(monaco, value.selectionRange);

  return {
    name: value.name || " ",
    detail: value.detail ?? "",
    kind: symbolKind(monaco, value.kind),
    tags: symbolTags(monaco, value.tags, value.deprecated),
    range,
    // Monaco requires the selection range to sit inside the full range.
    selectionRange: range.containsRange(selectionRange)
      ? selectionRange
      : range,
    children: (value.children ?? []).map((child) =>
      documentSymbol(monaco, child)
    ),
  };
}

export function documentSymbolsResult(
  monaco: MonacoApi,
  values:
    | readonly (LspDocumentSymbol | LspSymbolInformation)[]
    | null
    | undefined,
  documentUri: string,
): Monaco.languages.DocumentSymbol[] {
  const target = monaco.Uri.parse(documentUri).toString();

  return (values ?? []).flatMap((value) => {
    if ("location" in value) {
      if (
        monaco.Uri.parse(value.location.uri).toString() !== target
      ) {
        return [];
      }

      const range = toMonacoRange(monaco, value.location.range);

      return [{
        name: value.name || " ",
        detail: value.containerName ?? "",
        kind: symbolKind(monaco, value.kind),
        tags: symbolTags(monaco, value.tags, value.deprecated),
        range,
        selectionRange: range,
        containerName: value.containerName,
      }];
    }

    return [documentSymbol(monaco, value)];
  });
}

export function documentHighlightsResult(
  monaco: MonacoApi,
  values: readonly LspDocumentHighlight[] | null | undefined,
): Monaco.languages.DocumentHighlight[] {
  return (values ?? []).map((value) => {
    const kind = value.kind === 2
      ? monaco.languages.DocumentHighlightKind.Read
      : value.kind === 3
        ? monaco.languages.DocumentHighlightKind.Write
        : monaco.languages.DocumentHighlightKind.Text;

    return {
      range: toMonacoRange(monaco, value.range),
      kind,
    };
  });
}

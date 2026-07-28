import type * as Monaco from "monaco-editor/editor/editor.api";
import type {
  ArchivistDocument,
} from "../../IdeHost.types.js";
import type {
  LspLocation,
  LspLocationLink,
} from "./LspTypes.js";

type MonacoApi = typeof Monaco;

type JsonRecord = Record<string, unknown>;

interface LibraryFileEntry {
  id: string;
  relativePath: string;
  languageId: string;
  status: string;
}

function isRecord(value: unknown): value is JsonRecord {
  return Boolean(value)
    && typeof value === "object"
    && !Array.isArray(value);
}

function stringValue(
  value: unknown,
  fallback = "",
): string {
  return typeof value === "string" ? value : fallback;
}

function normalizedPath(value: string): string {
  return String(value || "")
    .trim()
    .replace(/\\/g, "/")
    .replace(/\/{2,}/g, "/");
}

function normalizedRelativePath(value: string): string {
  return normalizedPath(value).replace(/^\/+/, "");
}

function targetUri(
  value: LspLocation | LspLocationLink,
): string {
  return "targetUri" in value
    ? value.targetUri
    : value.uri;
}

function parseLibraryFiles(payload: unknown): LibraryFileEntry[] {
  if (!isRecord(payload) || !Array.isArray(payload.files)) {
    return [];
  }

  return payload.files.flatMap((value) => {
    if (!isRecord(value)) {
      return [];
    }

    const id = stringValue(value.id);
    const relativePath = stringValue(value.relativePath);

    if (!id || !relativePath) {
      return [];
    }

    return [{
      id,
      relativePath,
      languageId: stringValue(value.languageId, "plaintext"),
      status: stringValue(value.status, "available"),
    }];
  });
}

function parsePreviewContent(payload: unknown): string | null {
  if (!isRecord(payload) || !isRecord(payload.preview)) {
    return null;
  }

  return typeof payload.preview.content === "string"
    ? payload.preview.content
    : null;
}

/**
 * Monaco's standalone text-model service only resolves URIs that already have
 * an ITextModel. LSP definition/reference responses can point at Library files
 * that have not been opened in a tab yet, so preload those models lazily using
 * Archivist's existing Library APIs before returning the locations to Monaco.
 */
export class LibraryModelResolver {
  private readonly catalogByLibraryId = new Map<
    string,
    Promise<LibraryFileEntry[]>
  >();

  private readonly pendingByModelUri = new Map<
    string,
    Promise<Monaco.editor.ITextModel | null>
  >();

  constructor(
    private readonly monaco: MonacoApi,
  ) {}

  async ensureLocationModels(
    document: ArchivistDocument,
    values: readonly (LspLocation | LspLocationLink)[],
  ): Promise<void> {
    await Promise.all(
      values.map((value) =>
        this.ensureModel(document, targetUri(value))
      ),
    );
  }

  private async ensureModel(
    document: ArchivistDocument,
    uriValue: string,
  ): Promise<Monaco.editor.ITextModel | null> {
    const uri = this.monaco.Uri.parse(uriValue);

    if (uri.scheme !== "file" || !uri.fsPath) {
      return null;
    }

    const existing = this.monaco.editor.getModel(uri);

    if (existing) {
      return existing;
    }

    const modelKey = uri.toString();
    const pending = this.pendingByModelUri.get(modelKey);

    if (pending) {
      return pending;
    }

    const request = this.loadModel(document, uri)
      .catch((error) => {
        const message = error instanceof Error
          ? error.message
          : String(error || "Unknown model-loading error.");

        console.warn(
          `[Language Support] Could not preload ${modelKey}: ${message}`,
        );
        return null;
      })
      .finally(() => {
        this.pendingByModelUri.delete(modelKey);
      });

    this.pendingByModelUri.set(modelKey, request);
    return request;
  }

  private async loadModel(
    document: ArchivistDocument,
    uri: Monaco.Uri,
  ): Promise<Monaco.editor.ITextModel | null> {
    const relativePath = this.relativeLibraryPath(
      document,
      uri.fsPath,
    );

    if (!relativePath) {
      return null;
    }

    let files = await this.catalogFor(document.libraryId);
    let file = this.fileForPath(files, relativePath);

    if (!file) {
      this.catalogByLibraryId.delete(document.libraryId);
      files = await this.catalogFor(document.libraryId);
      file = this.fileForPath(files, relativePath);
    }

    if (!file || file.status !== "available") {
      return null;
    }

    const response = await fetch(
      `/api/libraries/${encodeURIComponent(document.libraryId)}`
      + `/files/${encodeURIComponent(file.id)}/content`,
    );
    const payload: unknown = await response.json();

    if (!response.ok) {
      throw new Error(
        `Library file request failed (${response.status}).`,
      );
    }

    const content = parsePreviewContent(payload);

    if (content === null) {
      throw new Error("Library file response did not include text content.");
    }

    return this.monaco.editor.getModel(uri)
      ?? this.monaco.editor.createModel(
        content,
        file.languageId || "plaintext",
        uri,
      );
  }

  private async catalogFor(
    libraryId: string,
  ): Promise<LibraryFileEntry[]> {
    const existing = this.catalogByLibraryId.get(libraryId);

    if (existing) {
      return existing;
    }

    const request = fetch(
      `/api/libraries/${encodeURIComponent(libraryId)}/files`,
    ).then(async (response) => {
      const payload: unknown = await response.json();

      if (!response.ok) {
        throw new Error(
          `Library catalog request failed (${response.status}).`,
        );
      }

      return parseLibraryFiles(payload);
    }).catch((error) => {
      this.catalogByLibraryId.delete(libraryId);
      throw error;
    });

    this.catalogByLibraryId.set(libraryId, request);
    return request;
  }

  private fileForPath(
    files: readonly LibraryFileEntry[],
    relativePath: string,
  ): LibraryFileEntry | null {
    const target = normalizedRelativePath(relativePath);

    return files.find((file) =>
      normalizedRelativePath(file.relativePath) === target
    ) ?? null;
  }

  private relativeLibraryPath(
    document: ArchivistDocument,
    targetFilePath: string,
  ): string | null {
    const root = normalizedPath(document.workspaceRoot)
      .replace(/\/$/, "");
    const target = normalizedPath(targetFilePath);

    if (!root || !target.startsWith(`${root}/`)) {
      return null;
    }

    return normalizedRelativePath(target.slice(root.length + 1));
  }
}

import * as monaco from "monaco-editor/editor/editor.api";
import editorWorker from "monaco-editor/editor/editor.worker?worker";
import "monaco-editor/language/typescript/monaco.contribution";
import typescriptWorker from "monaco-editor/language/typescript/ts.worker?worker";
import type {
  ArchivistDocument,
  ArchivistSaveResult,
  ArchivistTheme,
} from "../IdeHost.types.js";

interface MonacoWorkerEnvironment {
  getWorker(moduleId: string, label: string): Worker;
}

interface MonacoEditorCallbacks {
  reportDirty(documentId: string, dirty: boolean): void;
  reportStatus(message: string): void;
  requestSave(
    documentId: string,
    content: string,
    expectedModifiedAt: string,
  ): void;
}

interface DocumentState {
  documentId: string;
  modifiedAt: string;
  readOnly: boolean;
  savedContent: string;
}

const monacoGlobal = globalThis as typeof globalThis & {
  MonacoEnvironment: MonacoWorkerEnvironment;
};

monacoGlobal.MonacoEnvironment = {
  getWorker(_moduleId: string, label: string): Worker {
    if (
      label === "typescript"
      || label === "javascript"
    ) {
      return new typescriptWorker();
    }

    return new editorWorker();
  },
};

const languageAliases: Record<string, string> = {
  javascriptreact: "javascript",
  typescriptreact: "typescript",
  qml: "plaintext",
  shellscript: "shell",
};

function normalizedLanguage(language: string): string {
  const normalized = String(language || "plaintext")
    .trim()
    .toLowerCase();

  return languageAliases[normalized] || normalized || "plaintext";
}

function editorViewStateStorageKey(
  documentId: string,
): string {
  return (
    "archivist.monaco.view-state.v1."
    + encodeURIComponent(documentId)
  );
}

function loadPersistedViewState(
  documentId: string,
): monaco.editor.ICodeEditorViewState | null {
  try {
    const value = window.localStorage.getItem(
      editorViewStateStorageKey(documentId),
    );

    return value
      ? (JSON.parse(value) as monaco.editor.ICodeEditorViewState)
      : null;
  } catch {
    return null;
  }
}

function persistViewState(
  documentId: string,
  viewState:
    monaco.editor.ICodeEditorViewState | null,
): void {
  if (!documentId || !viewState) {
    return;
  }

  try {
    window.localStorage.setItem(
      editorViewStateStorageKey(documentId),
      JSON.stringify(viewState),
    );
  } catch {
    return;
  }
}

function documentUri(document: ArchivistDocument): monaco.Uri {
  const identity = encodeURIComponent(
    document.id || document.path || "untitled",
  );

  return monaco.Uri.parse(
    `archivist://file/${identity}`,
  );
}

export class MonacoEditor {
  readonly element: HTMLElement;

  private readonly editor:
    monaco.editor.IStandaloneCodeEditor;

  private readonly viewStates = new Map<
    string,
    monaco.editor.ICodeEditorViewState | null
  >();

  private readonly statesByModelKey = new Map<
    string,
    DocumentState
  >();

  private readonly modelKeyByDocumentId = new Map<
    string,
    string
  >();

  private readonly savesInFlight = new Set<string>();
  private viewStateSaveTimer: number | null = null;
  private activeModelKey = "";
  private suppressContentEvents = false;

  constructor(
    element: HTMLElement,
    private readonly callbacks: MonacoEditorCallbacks,
  ) {
    this.element = element;
    this.editor = monaco.editor.create(element, {
      model: null,
      automaticLayout: true,
      minimap: {
        enabled: false,
      },
      padding: {
        top: 14,
        bottom: 14,
      },
      fontLigatures: true,
      readOnly: true,
      domReadOnly: false,
      scrollBeyondLastLine: false,
    });

    this.element.addEventListener(
      "pointerdown",
      () => {
        this.editor.focus();
      },
      {
        capture: true,
      },
    );

    const scheduleViewStateSave = (): void => {
      this.scheduleViewStateSave();
    };

    this.editor.onDidScrollChange(scheduleViewStateSave);
    this.editor.onDidChangeCursorPosition(
      scheduleViewStateSave,
    );
    this.editor.onDidChangeCursorSelection(
      scheduleViewStateSave,
    );

    window.addEventListener("beforeunload", () => {
      this.rememberViewState();
    });

    this.editor.onDidChangeModelContent(() => {
      if (!this.suppressContentEvents) {
        this.reportActiveDirtyState();
      }
    });

    this.editor.addCommand(
      monaco.KeyMod.CtrlCmd | monaco.KeyCode.KeyS,
      () => {
        this.saveActiveDocument();
      },
    );
  }

  applyTheme(theme: ArchivistTheme): void {
    monaco.editor.defineTheme("archivist", {
      base: "vs-dark",
      inherit: true,
      rules: [],
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
        "editorWidget.background": theme.surfaceBg,
        "editorWidget.border": theme.panelBorder,
      },
    });

    monaco.editor.setTheme("archivist");
    this.editor.updateOptions({
      fontFamily: theme.monospaceFontFamily,
      fontSize: theme.textControlSize,
    });
  }

  openDocument(document: ArchivistDocument): void {
    const documentKey = document.id || document.path;

    if (!documentKey) {
      this.rememberViewState();
      this.activeModelKey = "";
      this.editor.setModel(null);
      return;
    }

    this.rememberViewState();

    const uri = documentUri(document);
    const modelKey = uri.toString();
    const language = normalizedLanguage(document.language);

    let model = monaco.editor.getModel(uri);
    let state = this.statesByModelKey.get(modelKey);

    if (!model || !state) {
      model = monaco.editor.createModel(
        document.content,
        language,
        uri,
      );
      state = {
        documentId: document.id,
        modifiedAt: document.modifiedAt,
        readOnly: document.readOnly,
        savedContent: document.content,
      };
      this.statesByModelKey.set(modelKey, state);
      this.modelKeyByDocumentId.set(
        document.id,
        modelKey,
      );
    } else {
      if (model.getLanguageId() !== language) {
        monaco.editor.setModelLanguage(model, language);
      }

      const dirty = model.getValue() !== state.savedContent;

      state.readOnly = document.readOnly;

      if (!dirty) {
        if (model.getValue() !== document.content) {
          this.suppressContentEvents = true;
          model.setValue(document.content);
          this.suppressContentEvents = false;
        }

        state.savedContent = document.content;
        state.modifiedAt = document.modifiedAt;
      }
    }

    this.activeModelKey = modelKey;
    this.editor.setModel(model);
    this.editor.updateOptions({
      readOnly: state.readOnly,
      domReadOnly: false,
    });

    const viewState =
      this.viewStates.get(modelKey)
      ?? loadPersistedViewState(state.documentId);

    if (viewState) {
      this.viewStates.set(modelKey, viewState);
      this.editor.restoreViewState(viewState);
    }

    this.editor.layout();
    this.reportActiveDirtyState();
  }

  applySaveResult(result: ArchivistSaveResult): void {
    if (!result.documentId) {
      return;
    }

    this.savesInFlight.delete(result.documentId);

    const modelKey = this.modelKeyByDocumentId.get(
      result.documentId,
    );
    const model = modelKey
      ? monaco.editor.getModel(monaco.Uri.parse(modelKey))
      : null;
    const state = modelKey
      ? this.statesByModelKey.get(modelKey)
      : null;

    if (!model || !state) {
      return;
    }

    if (!result.ok) {
      this.callbacks.reportStatus(
        result.message || "The file could not be saved.",
      );
      this.callbacks.reportDirty(
        state.documentId,
        model.getValue() !== state.savedContent,
      );
      return;
    }

    state.savedContent =
      result.preview?.content ?? model.getValue();
    state.modifiedAt =
      result.preview?.file?.modifiedAt
      ?? state.modifiedAt;

    const dirty = model.getValue() !== state.savedContent;
    this.callbacks.reportDirty(state.documentId, dirty);
    this.callbacks.reportStatus(
      dirty
        ? "Saved; newer edits remain unsaved"
        : "Saved",
    );
  }

  setVisible(visible: boolean): void {
    this.element.hidden = !visible;
    this.element.style.pointerEvents =
      visible ? "auto" : "none";

    if (visible) {
      requestAnimationFrame(() => {
        this.editor.layout();
        this.editor.focus();
      });
    }
  }

  layout(): void {
    this.editor.layout();
  }

  private activeState(): {
    model: monaco.editor.ITextModel;
    state: DocumentState;
  } | null {
    if (!this.activeModelKey) {
      return null;
    }

    const model = monaco.editor.getModel(
      monaco.Uri.parse(this.activeModelKey),
    );
    const state = this.statesByModelKey.get(
      this.activeModelKey,
    );

    return model && state
      ? { model, state }
      : null;
  }

  private saveActiveDocument(): void {
    const active = this.activeState();

    if (!active) {
      return;
    }

    if (active.state.readOnly) {
      this.callbacks.reportStatus(
        "This file is read-only.",
      );
      return;
    }

    const content = active.model.getValue();

    if (content === active.state.savedContent) {
      this.callbacks.reportStatus(
        "No changes to save.",
      );
      return;
    }

    if (
      this.savesInFlight.has(
        active.state.documentId,
      )
    ) {
      this.callbacks.reportStatus(
        "Save already in progress.",
      );
      return;
    }

    this.savesInFlight.add(
      active.state.documentId,
    );
    this.callbacks.reportStatus("Saving…");
    this.callbacks.requestSave(
      active.state.documentId,
      content,
      active.state.modifiedAt,
    );
  }

  private reportActiveDirtyState(): void {
    const active = this.activeState();

    if (!active) {
      return;
    }

    this.callbacks.reportDirty(
      active.state.documentId,
      active.model.getValue()
        !== active.state.savedContent,
    );
  }

  private scheduleViewStateSave(): void {
    if (this.viewStateSaveTimer !== null) {
      window.clearTimeout(this.viewStateSaveTimer);
    }

    this.viewStateSaveTimer = window.setTimeout(() => {
      this.viewStateSaveTimer = null;
      this.rememberViewState();
    }, 180);
  }

  private rememberViewState(): void {
    if (!this.activeModelKey) {
      return;
    }

    const viewState = this.editor.saveViewState();
    const state = this.statesByModelKey.get(
      this.activeModelKey,
    );

    this.viewStates.set(
      this.activeModelKey,
      viewState,
    );

    if (state) {
      persistViewState(
        state.documentId,
        viewState,
      );
    }
  }
}

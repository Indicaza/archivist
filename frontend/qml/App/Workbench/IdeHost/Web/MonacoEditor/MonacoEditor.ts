import * as monaco from "monaco-editor/editor/editor.api";
import editorWorker from "monaco-editor/editor/editor.worker?worker";
import cssWorker from "monaco-editor/language/css/css.worker?worker";
import htmlWorker from "monaco-editor/language/html/html.worker?worker";
import jsonWorker from "monaco-editor/language/json/json.worker?worker";
import {
  JsxEmit,
  javascriptDefaults,
  typescriptDefaults,
  type CompilerOptions,
  type DiagnosticsOptions,
} from "monaco-editor/languages/features/typescript/register";
import typescriptWorker from "monaco-editor/language/typescript/ts.worker?worker";
import {
  defineArchivistEditorTheme,
} from "./ArchivistEditorTheme.js";
import {
  ensureLanguage,
  normalizeLanguageId,
} from "./LanguageRegistry.js";
import {
  registerSqlLanguageSupport,
} from "./Languages/SqlLanguageSupport.js";
import {
  EditorCommandRegistry,
  type EditorCommandContext,
} from "./Commands/EditorCommandRegistry.js";
import {
  EditorContextMenu,
} from "./Commands/EditorContextMenu.js";
import {
  LanguageSupportClient,
} from "./LanguageSupport/LanguageSupportClient.js";
import type {
  ArchivistDocument,
  ArchivistEditorCommand,
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
  requestOpenLocation(
    filePath: string,
    lineNumber: number,
    columnNumber: number,
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

    if (label === "json") {
      return new jsonWorker();
    }

    if (
      label === "css"
      || label === "scss"
      || label === "less"
    ) {
      return new cssWorker();
    }

    if (
      label === "html"
      || label === "handlebars"
      || label === "razor"
    ) {
      return new htmlWorker();
    }

    return new editorWorker();
  },
};

function configureMonacoTypeScriptDefaults(): void {
  const diagnostics: DiagnosticsOptions = {
    noSemanticValidation: true,
    noSyntaxValidation: true,
    noSuggestionDiagnostics: true,
  };
  const compilerOptions: CompilerOptions = {
    allowJs: true,
    allowNonTsExtensions: true,
    checkJs: true,
    jsx: JsxEmit.Preserve,
  };

  typescriptDefaults.setDiagnosticsOptions(diagnostics);
  javascriptDefaults.setDiagnosticsOptions(diagnostics);
  typescriptDefaults.setCompilerOptions({
    ...typescriptDefaults.getCompilerOptions(),
    ...compilerOptions,
  });
  javascriptDefaults.setCompilerOptions({
    ...javascriptDefaults.getCompilerOptions(),
    ...compilerOptions,
  });
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

function normalizedLocalPath(value: string): string {
  return String(value || "")
    .trim()
    .replace(/\\/g, "/");
}

function documentUri(document: ArchivistDocument): monaco.Uri {
  const filePath = normalizedLocalPath(
    document.filePath,
  );

  if (filePath) {
    return monaco.Uri.file(filePath);
  }

  const identity = encodeURIComponent(
    document.id || document.path || "untitled",
  );
  const fileName = String(
    document.path || "untitled",
  )
    .split(/[\\/]/)
    .filter(Boolean)
    .at(-1) || "untitled";

  return monaco.Uri.from({
    scheme: "archivist",
    authority: "file",
    path: `/${identity}/${fileName}`,
  });
}

export class MonacoEditor {
  readonly element: HTMLElement;

  private readonly editor:
    monaco.editor.IStandaloneCodeEditor;

  private readonly languageSupport: LanguageSupportClient;

  private readonly sqlLanguageSupport: monaco.IDisposable;

  private readonly editorOpener: monaco.IDisposable;

  private readonly commandRegistry = new EditorCommandRegistry();

  private readonly contextMenu: EditorContextMenu;

  private readonly commandDisposables: {
    dispose(): void;
  }[] = [];

  private readonly nativeContextMenuHandler: (
    event: MouseEvent,
  ) => void;

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

  private readonly pendingRevealByDocumentId = new Map<
    string,
    {
      lineNumber: number;
      columnNumber: number;
    }
  >();

  private readonly languageRevisionByModelKey = new Map<
    string,
    number
  >();

  private viewStateSaveTimer: number | null = null;
  private activeModelKey = "";
  private suppressContentEvents = false;

  constructor(
    element: HTMLElement,
    private readonly callbacks: MonacoEditorCallbacks,
  ) {
    this.element = element;
    configureMonacoTypeScriptDefaults();
    this.sqlLanguageSupport = registerSqlLanguageSupport(monaco);
    this.languageSupport = new LanguageSupportClient(
      monaco,
      callbacks,
    );
    this.editor = monaco.editor.create(element, {
      model: null,
      automaticLayout: true,
      minimap: {
        enabled: true,
        renderCharacters: true,
        maxColumn: 110,
        scale: 1,
        showSlider: "always",
      },
      padding: {
        top: 14,
        bottom: 14,
      },
      bracketPairColorization: {
        enabled: true,
        independentColorPoolPerBracketType: true,
      },
      guides: {
        indentation: true,
        bracketPairs: true,
        bracketPairsHorizontal: true,
        highlightActiveBracketPair: true,
      },
      detectIndentation: true,
      folding: true,
      fontLigatures: true,
      formatOnType: true,
      glyphMargin: true,
      matchBrackets: "always",
      readOnly: true,
      domReadOnly: false,
      contextmenu: false,
      renderLineHighlight: "all",
      renderWhitespace: "selection",
      scrollBeyondLastLine: false,
      showFoldingControls: "mouseover",
      smoothScrolling: true,
      stickyScroll: {
        enabled: true,
      },
      wordWrap: "off",
    });

    this.editorOpener = monaco.editor.registerEditorOpener({
      openCodeEditor: (source, resource, target) => {
        if (source !== this.editor) {
          return false;
        }

        const currentResource = source.getModel()?.uri;

        if (
          currentResource?.toString()
            === resource.toString()
        ) {
          return false;
        }

        if (resource.scheme !== "file" || !resource.fsPath) {
          return false;
        }

        const position = target && "lineNumber" in target
          ? target
          : target
            ? {
                lineNumber: target.startLineNumber,
                column: target.startColumn,
              }
            : {
                lineNumber: 1,
                column: 1,
              };

        this.callbacks.requestOpenLocation(
          resource.fsPath,
          position.lineNumber,
          position.column,
        );
        return true;
      },
    });

    this.contextMenu = new EditorContextMenu(
      this.commandRegistry,
      {
        reportStatus: (message) => {
          this.callbacks.reportStatus(message);
        },
        restoreEditorFocus: () => {
          this.editor.focus();
        },
      },
    );
    this.registerEditorCommands();
    this.nativeContextMenuHandler = (event) => {
      this.openContextMenu(event);
    };
    this.element.addEventListener(
      "contextmenu",
      this.nativeContextMenuHandler,
      true,
    );

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
      this.element.removeEventListener(
        "contextmenu",
        this.nativeContextMenuHandler,
        true,
      );
      this.sqlLanguageSupport.dispose();
      this.contextMenu.dispose();

      for (const disposable of this.commandDisposables) {
        disposable.dispose();
      }

      this.commandDisposables.length = 0;
      this.editorOpener.dispose();
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
    defineArchivistEditorTheme(monaco, theme);
    this.contextMenu.applyTheme(theme);
    this.editor.updateOptions({
      fontFamily: theme.monospaceFontFamily,
      fontSize: theme.textControlSize,
    });
  }

  openDocument(document: ArchivistDocument): void {
    this.contextMenu.hide(false);
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
    const requestedLanguage = normalizeLanguageId(
      document.language,
    );

    let model = monaco.editor.getModel(uri);
    let state = this.statesByModelKey.get(modelKey);

    if (!model) {
      model = monaco.editor.createModel(
        document.content,
        "plaintext",
        uri,
      );
    }

    if (!state) {
      if (model.getValue() !== document.content) {
        model.setValue(document.content);
      }

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

    this.applyLanguage(
      model,
      modelKey,
      requestedLanguage,
    );
    this.languageSupport.openDocument(
      document,
      model,
      requestedLanguage,
    );
    const pendingReveal =
      this.pendingRevealByDocumentId.get(document.id);

    if (pendingReveal) {
      this.pendingRevealByDocumentId.delete(document.id);
      this.revealLocation(
        document.id,
        pendingReveal.lineNumber,
        pendingReveal.columnNumber,
      );
    }

    this.editor.layout();
    this.reportActiveDirtyState();
  }

  private applyLanguage(
    model: monaco.editor.ITextModel,
    modelKey: string,
    requestedLanguage: string,
  ): void {
    const revision =
      (this.languageRevisionByModelKey.get(modelKey) ?? 0)
      + 1;

    this.languageRevisionByModelKey.set(
      modelKey,
      revision,
    );

    void ensureLanguage(
      monaco,
      requestedLanguage,
    ).then((languageId) => {
      if (
        model.isDisposed()
        || this.languageRevisionByModelKey.get(modelKey)
          !== revision
      ) {
        return;
      }

      if (model.getLanguageId() !== languageId) {
        monaco.editor.setModelLanguage(
          model,
          languageId,
        );
      }
    });
  }

  applyCommand(command: ArchivistEditorCommand): void {
    switch (command.type) {
      case "save":
        this.saveDocument(command.documentId);
        break;
      case "discard":
        this.discardDocument(command.documentId);
        break;
      case "revealLocation":
        this.revealLocation(
          command.documentId,
          command.lineNumber,
          command.columnNumber,
        );
        break;
    }
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
    if (!visible) {
      this.contextMenu.hide(false);
    }

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

  private documentState(documentId: string): {
    modelKey: string;
    model: monaco.editor.ITextModel;
    state: DocumentState;
  } | null {
    const modelKey = this.modelKeyByDocumentId.get(
      documentId,
    );
    const model = modelKey
      ? monaco.editor.getModel(
          monaco.Uri.parse(modelKey),
        )
      : null;
    const state = modelKey
      ? this.statesByModelKey.get(modelKey)
      : null;

    return modelKey && model && state
      ? {
          modelKey,
          model,
          state,
        }
      : null;
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

  private registerEditorCommands(): void {
    const isMac = /Mac|iPhone|iPad|iPod/.test(
      navigator.userAgent,
    );
    const primary = isMac ? "⌘" : "Ctrl+";

    this.commandDisposables.push(
      this.commandRegistry.register({
        id: "editor.goToDefinition",
        label: "Go to Definition",
        group: "navigation",
        groupOrder: 10,
        order: 10,
        shortcut: "F12",
        run: (context) =>
          this.goToDefinition(
            context.model,
            context.position,
          ),
      }),
      this.commandRegistry.register({
        id: "editor.cut",
        label: "Cut",
        group: "clipboard",
        groupOrder: 20,
        order: 10,
        shortcut: `${primary}X`,
        isEnabled: (context) =>
          !context.readOnly && !context.selection.isEmpty(),
        run: () =>
          this.runEditorAction(
            "editor.action.clipboardCutAction",
            "Cut is unavailable.",
          ),
      }),
      this.commandRegistry.register({
        id: "editor.copy",
        label: "Copy",
        group: "clipboard",
        groupOrder: 20,
        order: 20,
        shortcut: `${primary}C`,
        isEnabled: (context) =>
          !context.selection.isEmpty(),
        run: () =>
          this.runEditorAction(
            "editor.action.clipboardCopyAction",
            "Copy is unavailable.",
          ),
      }),
      this.commandRegistry.register({
        id: "editor.paste",
        label: "Paste",
        group: "clipboard",
        groupOrder: 20,
        order: 30,
        shortcut: `${primary}V`,
        isEnabled: (context) => !context.readOnly,
        run: () =>
          this.runEditorAction(
            "editor.action.clipboardPasteAction",
            "Paste is unavailable.",
          ),
      }),
      this.commandRegistry.register({
        id: "editor.selectAll",
        label: "Select All",
        group: "selection",
        groupOrder: 30,
        order: 10,
        shortcut: `${primary}A`,
        run: () =>
          this.runEditorAction(
            "editor.action.selectAll",
            "Select All is unavailable.",
          ),
      }),
    );
  }

  private openContextMenu(event: MouseEvent): void {
    event.preventDefault();
    event.stopImmediatePropagation();

    const model = this.editor.getModel();
    const target = this.editor.getTargetAtClientPoint(
      event.clientX,
      event.clientY,
    );
    const targetPosition = target?.position
      ?? this.editor.getPosition();

    if (!model || !targetPosition) {
      this.contextMenu.hide(false);
      return;
    }

    const position = model.validatePosition(targetPosition);
    const currentSelection = this.editor.getSelection();

    if (
      !currentSelection
      || !monaco.Range.containsPosition(
        currentSelection,
        position,
      )
    ) {
      this.editor.setPosition(position);
    }

    const context = this.editorCommandContext(
      model,
      position,
    );

    if (!context) {
      this.contextMenu.hide(false);
      return;
    }

    this.contextMenu.show(
      event.clientX,
      event.clientY,
      context,
    );
  }

  private editorCommandContext(
    model: monaco.editor.ITextModel,
    position: monaco.Position,
  ): EditorCommandContext | null {
    const state = this.statesByModelKey.get(
      model.uri.toString(),
    );

    if (!state || model.isDisposed()) {
      return null;
    }

    const validPosition = model.validatePosition(position);
    const selection = this.editor.getSelection()
      ?? new monaco.Selection(
        validPosition.lineNumber,
        validPosition.column,
        validPosition.lineNumber,
        validPosition.column,
      );

    return {
      editor: this.editor,
      model,
      position: validPosition,
      selection,
      readOnly: state.readOnly,
    };
  }

  private async runEditorAction(
    actionId: string,
    unavailableMessage: string,
  ): Promise<void> {
    const action = this.editor.getAction(actionId);

    if (!action) {
      this.callbacks.reportStatus(unavailableMessage);
      return;
    }

    await action.run();
  }

  private async goToDefinition(
    model: monaco.editor.ITextModel,
    position: monaco.Position,
  ): Promise<void> {
    if (model.isDisposed()) {
      this.callbacks.reportStatus(
        "The editor model is no longer available.",
      );
      return;
    }

    this.callbacks.reportStatus("Finding definition…");

    const definitions = await this.languageSupport.definitionsAt(
      model,
      model.validatePosition(position),
    );
    const definition = definitions[0];

    if (!definition) {
      this.callbacks.reportStatus("No definition found");
      return;
    }

    const target = {
      lineNumber: definition.range.startLineNumber,
      column: definition.range.startColumn,
    };

    if (
      definition.uri.toString() === model.uri.toString()
      && this.editor.getModel() === model
    ) {
      const validPosition = model.validatePosition(target);

      this.editor.setPosition(validPosition);
      this.editor.revealPositionInCenter(validPosition);
      this.editor.focus();
      this.callbacks.reportStatus("Definition opened");
      return;
    }

    if (
      definition.uri.scheme !== "file"
      || !definition.uri.fsPath
    ) {
      this.callbacks.reportStatus(
        "The definition target cannot be opened.",
      );
      return;
    }

    this.callbacks.reportStatus(
      definitions.length > 1
        ? `Opening first of ${definitions.length} definitions…`
        : "Opening definition…",
    );
    this.callbacks.requestOpenLocation(
      definition.uri.fsPath,
      target.lineNumber,
      target.column,
    );
  }

  private revealLocation(
    documentId: string,
    lineNumber: number,
    columnNumber: number,
  ): void {
    const document = this.documentState(documentId);

    if (!document) {
      this.pendingRevealByDocumentId.set(documentId, {
        lineNumber,
        columnNumber,
      });
      this.callbacks.reportStatus("Opening definition…");
      return;
    }

    if (this.activeModelKey !== document.modelKey) {
      this.rememberViewState();
      this.activeModelKey = document.modelKey;
      this.editor.setModel(document.model);
    }

    const position = document.model.validatePosition({
      lineNumber: Math.max(1, lineNumber),
      column: Math.max(1, columnNumber),
    });

    this.editor.setPosition(position);
    this.editor.revealPositionInCenter(position);
    this.editor.focus();
    this.callbacks.reportStatus("Definition opened");
  }

  private saveActiveDocument(): void {
    const active = this.activeState();

    if (active) {
      this.saveDocument(active.state.documentId);
    }
  }

  private saveDocument(documentId: string): void {
    const document = this.documentState(documentId);

    if (!document) {
      this.callbacks.reportStatus(
        "The editor model is no longer available.",
      );
      return;
    }

    if (document.state.readOnly) {
      this.callbacks.reportStatus(
        "This file is read-only.",
      );
      return;
    }

    const content = document.model.getValue();

    if (content === document.state.savedContent) {
      this.callbacks.reportStatus(
        "No changes to save.",
      );
      this.callbacks.reportDirty(documentId, false);
      return;
    }

    if (this.savesInFlight.has(documentId)) {
      this.callbacks.reportStatus(
        "Save already in progress.",
      );
      return;
    }

    this.savesInFlight.add(documentId);
    this.callbacks.reportStatus("Saving…");
    this.callbacks.requestSave(
      documentId,
      content,
      document.state.modifiedAt,
    );
  }

  private reportActiveDirtyState(): void {
    const active = this.activeState();

    if (!active) {
      return;
    }

    this.callbacks.reportDirty(
      active.state.documentId,
      active.model.getValue() !== active.state.savedContent,
    );
  }

  private discardDocument(documentId: string): void {
    const document = this.documentState(documentId);

    if (!document) {
      return;
    }

    if (this.activeModelKey === document.modelKey) {
      this.rememberViewState();
      this.activeModelKey = "";
      this.editor.setModel(null);
    }

    this.suppressContentEvents = true;
    document.model.setValue(
      document.state.savedContent,
    );
    this.suppressContentEvents = false;
    this.callbacks.reportDirty(documentId, false);

    document.model.dispose();
    this.statesByModelKey.delete(document.modelKey);
    this.modelKeyByDocumentId.delete(documentId);
    this.viewStates.delete(document.modelKey);
    this.languageRevisionByModelKey.delete(
      document.modelKey,
    );
    this.callbacks.reportStatus(
      "Discarded unsaved changes",
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

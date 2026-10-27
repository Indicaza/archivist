export interface ArchivistTheme {
  appBg: string;
  appText: string;
  mutedText: string;
  accent: string;
  accentBright: string;
  surfaceBg: string;
  controlSurfaceBg: string;
  workspaceBg: string;
  panelBorder: string;
  quietBorder: string;
  hoverBg: string;
  activeBg: string;
  codeBlockBg: string;
  codeBlockText: string;
  monospaceFontFamily: string;
  textControlSize: number;
  // A theme imported from VS Code, TextMate or Sublime Text, or null while
  // Archivist's own colors are in use.
  codeTheme?: ArchivistCodeTheme | null;
}

export interface ArchivistCodeTokenStyle {
  foreground?: string;
  fontStyle?: string;
}

export interface ArchivistCodeTheme {
  name: string;
  type: "dark" | "light" | "hc-dark" | "hc-light";
  // Monaco token name ("" is the default text) to its style.
  tokens: Record<string, ArchivistCodeTokenStyle>;
  // VS Code color keys ("editor.background" ...) to #rrggbb[aa].
  editorColors: Record<string, string>;
}

export interface ArchivistDocument {
  id: string;
  libraryId: string;
  path: string;
  filePath: string;
  workspaceRoot: string;
  language: string;
  content: string;
  modifiedAt: string;
  readOnly: boolean;
  // Synced documents live in Archivist's native buffer store. The page
  // reports every edit against `version` and applies edits made in other
  // views; the store, not the page, decides what is dirty and saves.
  synced?: boolean;
  version?: number;
  // The workspace view showing the document. Cursor and scroll are kept
  // per view, so two views of one file keep their own place.
  viewId?: string;
  viewState?: string;
  nonce?: string;
}

export interface ArchivistTextChange {
  offset: number;
  length: number;
  text: string;
}

// Commands for synced documents, delivered one by one as signal arguments
// (a property would coalesce rapid edits).
export type ArchivistSyncCommand =
  | {
      type: "applyEdits";
      documentId: string;
      baseVersion: number;
      version: number;
      changes: ArchivistTextChange[];
    }
  | {
      type: "reset";
      documentId: string;
      content: string;
      version: number;
      readOnly?: boolean;
    }
  | {
      type: "forget";
      documentId: string;
    };

export interface ArchivistTerminalContext {
  collectionId: string;
  libraryId: string;
  sessionId: string;
}

export interface ArchivistTerminalCommand {
  type: "kill";
  collectionId: string;
  libraryId: string;
  sessionId: string;
  nonce: string;
}

export type ArchivistEditorCommand =
  | {
      type: "save";
      documentId: string;
      nonce: string;
    }
  | {
      type: "discard";
      documentId: string;
      nonce: string;
    }
  | {
      type: "revealLocation";
      documentId: string;
      lineNumber: number;
      columnNumber: number;
      nonce: string;
    };

export interface ArchivistSaveResult {
  documentId: string;
  ok: boolean;
  message: string;
  preview?: {
    content: string;
    file?: {
      modifiedAt?: string;
    };
  };
}

interface WebChannelSignal {
  connect(callback: () => void): void;
}

export interface ArchivistBridge {
  themeJson: string;
  surface: string;
  documentJson: string;
  editorCommandJson: string;
  terminalJson: string;
  terminalCommandJson: string;
  saveResultJson: string;
  themeJsonChanged: WebChannelSignal;
  surfaceChanged: WebChannelSignal;
  documentJsonChanged: WebChannelSignal;
  editorCommandJsonChanged: WebChannelSignal;
  terminalJsonChanged: WebChannelSignal;
  terminalCommandJsonChanged: WebChannelSignal;
  saveResultJsonChanged: WebChannelSignal;
  syncCommand?: {
    connect(callback: (commandJson: string) => void): void;
  };
  reportReady(version: string): void;
  reportStatus(message: string): void;
  reportTerminalState(
    sessionId: string,
    state: string,
    title: string,
    cwd: string,
  ): void;
  reportDirty(documentId: string, dirty: boolean): void;
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
  reportEdits?(
    documentId: string,
    baseVersion: number,
    changesJson: string,
    length: number,
  ): void;
  requestResync?(documentId: string): void;
  reportViewState?(
    viewId: string,
    documentId: string,
    stateJson: string,
  ): void;
}

export interface WebChannelInstance {
  objects: {
    archivistBridge: ArchivistBridge;
  };
}

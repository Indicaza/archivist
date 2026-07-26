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
}

export interface ArchivistDocument {
  id: string;
  path: string;
  language: string;
  content: string;
  modifiedAt: string;
  readOnly: boolean;
}

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
  terminalJson: string;
  terminalCommandJson: string;
  saveResultJson: string;
  themeJsonChanged: WebChannelSignal;
  surfaceChanged: WebChannelSignal;
  documentJsonChanged: WebChannelSignal;
  terminalJsonChanged: WebChannelSignal;
  terminalCommandJsonChanged: WebChannelSignal;
  saveResultJsonChanged: WebChannelSignal;
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
}

export interface WebChannelInstance {
  objects: {
    archivistBridge: ArchivistBridge;
  };
}

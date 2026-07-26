import "./IdeHost.css";
import type {
  ArchivistBridge,
  ArchivistDocument,
  ArchivistSaveResult,
  ArchivistTerminalCommand,
  ArchivistTerminalContext,
  ArchivistTheme,
  WebChannelInstance,
} from "./IdeHost.types.js";

interface EditorSurface {
  applySaveResult(result: ArchivistSaveResult): void;
  applyTheme(theme: ArchivistTheme): void;
  layout(): void;
  openDocument(document: ArchivistDocument): void;
  setVisible(visible: boolean): void;
}

interface TerminalSurface {
  applyCommand(command: ArchivistTerminalCommand): void;
  applyContext(context: ArchivistTerminalContext): void;
  applyTheme(theme: ArchivistTheme): void;
  fit(): void;
  setVisible(visible: boolean): void;
}

type IdeSurface = "editor" | "terminal";

const fallbackTheme: ArchivistTheme = {
  appBg: "#171613",
  appText: "#d8d2c7",
  mutedText: "#9a9387",
  accent: "#9280bc",
  accentBright: "#b7aad2",
  surfaceBg: "#1e1c18",
  controlSurfaceBg: "#24221d",
  workspaceBg: "#181713",
  panelBorder: "#3b3730",
  quietBorder: "#2d2a25",
  hoverBg: "#2b2924",
  activeBg: "#302b38",
  codeBlockBg: "#201f1b",
  codeBlockText: "#cec8bd",
  monospaceFontFamily: "monospace",
  textControlSize: 14,
};

const emptyDocument: ArchivistDocument = {
  id: "",
  path: "",
  language: "plaintext",
  content: "",
  modifiedAt: "",
  readOnly: true,
};

function requireElement(
  selector: string,
): HTMLElement {
  const element =
    document.querySelector<HTMLElement>(selector);

  if (!element) {
    throw new Error(
      `Archivist IDE element is missing: ${selector}`,
    );
  }

  return element;
}

function normalizedSurface(surface: string): IdeSurface {
  return surface === "editor"
    ? "editor"
    : "terminal";
}

function surfaceFromLocation(): IdeSurface {
  const value = new URLSearchParams(
    window.location.search,
  ).get("surface");

  return normalizedSurface(String(value || ""));
}

function parseTheme(
  themeJson: string,
): ArchivistTheme {
  try {
    return {
      ...fallbackTheme,
      ...JSON.parse(themeJson) as Partial<ArchivistTheme>,
    };
  } catch {
    return fallbackTheme;
  }
}

function parseDocument(
  documentJson: string,
): ArchivistDocument {
  try {
    const document = JSON.parse(
      documentJson,
    ) as Partial<ArchivistDocument>;

    return {
      id: String(document.id || ""),
      path: String(document.path || ""),
      language: String(
        document.language || "plaintext",
      ),
      content: String(document.content || ""),
      modifiedAt: String(document.modifiedAt || ""),
      readOnly: document.readOnly !== false,
    };
  } catch {
    return emptyDocument;
  }
}

function parseTerminalContext(
  terminalJson: string,
): ArchivistTerminalContext {
  try {
    const context = JSON.parse(
      terminalJson,
    ) as Partial<ArchivistTerminalContext>;

    return {
      collectionId: String(context.collectionId || ""),
      libraryId: String(context.libraryId || ""),
      sessionId: String(context.sessionId || "primary"),
    };
  } catch {
    return {
      collectionId: "",
      libraryId: "",
      sessionId: "primary",
    };
  }
}

function parseTerminalCommand(
  commandJson: string,
): ArchivistTerminalCommand | null {
  if (!commandJson) {
    return null;
  }

  try {
    const command = JSON.parse(
      commandJson,
    ) as Partial<ArchivistTerminalCommand>;

    if (
      command.type !== "kill"
      || !command.collectionId
      || !command.libraryId
      || !command.sessionId
    ) {
      return null;
    }

    return {
      type: "kill",
      collectionId: String(command.collectionId),
      libraryId: String(command.libraryId),
      sessionId: String(command.sessionId),
      nonce: String(command.nonce || ""),
    };
  } catch {
    return null;
  }
}

function parseSaveResult(
  saveResultJson: string,
): ArchivistSaveResult | null {
  if (!saveResultJson) {
    return null;
  }

  try {
    const result = JSON.parse(
      saveResultJson,
    ) as Partial<ArchivistSaveResult>;

    return {
      documentId: String(result.documentId || ""),
      ok: result.ok === true,
      message: String(result.message || ""),
      preview: result.preview,
    };
  } catch {
    return null;
  }
}

class IdeHost {
  private readonly editorElement = requireElement(
    "#monaco-editor",
  );

  private readonly terminalElement = requireElement(
    "#terminal",
  );

  private editor: EditorSurface | null = null;
  private terminal: TerminalSurface | null = null;
  private bridge: ArchivistBridge | null = null;
  private currentTheme = fallbackTheme;
  private activeSurface: IdeSurface = "terminal";

  async start(): Promise<void> {
    this.editorElement.hidden = true;
    this.terminalElement.hidden = true;
    this.applyTheme(fallbackTheme);

    const initialSurface = surfaceFromLocation();
    await this.activateSurface(initialSurface);

    if (!window.qt?.webChannelTransport) {
      return;
    }

    new QWebChannel(
      window.qt.webChannelTransport,
      (channel: WebChannelInstance) => {
        void this.connectBridge(channel);
      },
    );

    window.addEventListener("resize", () => {
      this.editor?.layout();
      this.terminal?.fit();
    });
  }

  private async connectBridge(
    channel: WebChannelInstance,
  ): Promise<void> {
    this.bridge = channel.objects.archivistBridge;
    this.applyTheme(
      parseTheme(this.bridge.themeJson),
    );

    await this.activateSurface(
      normalizedSurface(this.bridge.surface),
    );
    this.syncEditorDocument();

    this.bridge.themeJsonChanged.connect(() => {
      if (this.bridge) {
        this.applyTheme(
          parseTheme(this.bridge.themeJson),
        );
      }
    });

    this.bridge.surfaceChanged.connect(() => {
      if (this.bridge) {
        void this.activateSurface(
          normalizedSurface(this.bridge.surface),
        );
      }
    });

    this.bridge.documentJsonChanged.connect(() => {
      this.syncEditorDocument();
    });

    this.bridge.terminalJsonChanged.connect(() => {
      this.syncTerminalContext();
    });

    this.bridge.terminalCommandJsonChanged.connect(() => {
      if (!this.bridge || !this.terminal) {
        return;
      }

      const command = parseTerminalCommand(
        this.bridge.terminalCommandJson,
      );

      if (command) {
        this.terminal.applyCommand(command);
      }
    });

    this.bridge.saveResultJsonChanged.connect(() => {
      if (!this.bridge || !this.editor) {
        return;
      }

      const result = parseSaveResult(
        this.bridge.saveResultJson,
      );

      if (result) {
        this.editor.applySaveResult(result);
      }
    });

    this.bridge.reportReady("v0.3");
    this.bridge.reportStatus(
      this.activeSurface === "editor"
        ? "Monaco connected"
        : "xterm.js connected",
    );

    this.syncTerminalContext();
  }

  private async activateSurface(
    surface: IdeSurface,
  ): Promise<void> {
    this.activeSurface = surface;

    if (surface === "editor") {
      if (!this.editor) {
        const module = await import(
          "./MonacoEditor/MonacoEditor.js"
        );
        this.editor = new module.MonacoEditor(
          this.editorElement,
          {
            reportDirty: (documentId, dirty) => {
              this.bridge?.reportDirty(documentId, dirty);
            },
            reportStatus: (message) => {
              this.bridge?.reportStatus(message);
            },
            requestSave: (
              documentId,
              content,
              expectedModifiedAt,
            ) => {
              this.bridge?.requestSave(
                documentId,
                content,
                expectedModifiedAt,
              );
            },
          },
        );
        this.editor.applyTheme(this.currentTheme);
      }

      this.terminal?.setVisible(false);
      this.editor.setVisible(true);
      this.syncEditorDocument();
      return;
    }

    if (!this.terminal) {
      const module = await import(
        "./Terminal/Terminal.js"
      );
      this.terminal = new module.Terminal(
        this.terminalElement,
        {
          reportStatus: (message) => {
            this.bridge?.reportStatus(message);
          },
          reportState: (
            sessionId,
            state,
            title,
            cwd,
          ) => {
            this.bridge?.reportTerminalState(
              sessionId,
              state,
              title,
              cwd,
            );
          },
        },
      );
      this.terminal.applyTheme(this.currentTheme);
      this.syncTerminalContext();
    }

    this.editor?.setVisible(false);
    this.terminal.setVisible(true);
  }

  private applyTheme(theme: ArchivistTheme): void {
    this.currentTheme = theme;
    document.documentElement.style.background =
      theme.workspaceBg;
    document.body.style.background =
      theme.workspaceBg;

    this.editor?.applyTheme(theme);
    this.terminal?.applyTheme(theme);
  }

  private syncTerminalContext(): void {
    if (!this.terminal || !this.bridge) {
      return;
    }

    this.terminal.applyContext(
      parseTerminalContext(this.bridge.terminalJson),
    );
  }

  private syncEditorDocument(): void {
    if (!this.editor || !this.bridge) {
      return;
    }

    this.editor.openDocument(
      parseDocument(this.bridge.documentJson),
    );
  }
}

void new IdeHost().start();

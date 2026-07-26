import { FitAddon } from "@xterm/addon-fit";
import { Terminal as XtermTerminal } from "@xterm/xterm";
import "@xterm/xterm/css/xterm.css";
import type {
  ArchivistTerminalCommand,
  ArchivistTerminalContext,
  ArchivistTheme,
} from "../IdeHost.types.js";

interface TerminalCallbacks {
  reportStatus(message: string): void;
  reportState(
    sessionId: string,
    state: string,
    title: string,
    cwd: string,
  ): void;
}

type TerminalServerMessage =
  | {
      type: "ready";
      sessionId: string;
      cwd: string;
      shell: string;
    }
  | {
      type: "output";
      data: string;
    }
  | {
      type: "exit";
      exitCode: number;
      signal: number;
    }
  | {
      type: "error";
      message: string;
    }
  | {
      type: "pong";
      sentAt: number;
    };

type PersistedTerminalViewport = {
  viewportY: number;
  wasAtBottom: boolean;
};

function contextKey(
  context: ArchivistTerminalContext,
): string {
  return [
    context.collectionId,
    context.libraryId,
    context.sessionId,
  ].join(":");
}

function validContext(
  context: ArchivistTerminalContext,
): boolean {
  return (
    context.collectionId.length > 0
    && context.libraryId.length > 0
    && context.sessionId.length > 0
  );
}

function shellTitle(shell: string): string {
  const segments = String(shell || "")
    .split(/[\\/]/)
    .filter(Boolean);

  return segments.at(-1) || "Terminal";
}

function terminalSocketUrl(
  context: ArchivistTerminalContext,
  cols: number,
  rows: number,
): URL {
  const protocol = window.location.protocol === "https:"
    ? "wss:"
    : "ws:";
  const url = new URL(
    `${protocol}//${window.location.host}`
      + "/api/terminals/socket",
  );

  url.searchParams.set(
    "sessionId",
    context.sessionId,
  );
  url.searchParams.set(
    "collectionId",
    context.collectionId,
  );
  url.searchParams.set(
    "libraryId",
    context.libraryId,
  );
  url.searchParams.set(
    "cols",
    String(Math.max(2, cols)),
  );
  url.searchParams.set(
    "rows",
    String(Math.max(2, rows)),
  );

  return url;
}

function terminalViewportStorageKey(
  context: ArchivistTerminalContext,
): string {
  return (
    "archivist.terminal.viewport.v1."
    + encodeURIComponent(contextKey(context))
  );
}

function loadTerminalViewport(
  context: ArchivistTerminalContext,
): PersistedTerminalViewport | null {
  if (!validContext(context)) {
    return null;
  }

  try {
    const value = window.localStorage.getItem(
      terminalViewportStorageKey(context),
    );

    return value
      ? JSON.parse(value) as PersistedTerminalViewport
      : null;
  } catch {
    return null;
  }
}

class TerminalSessionView {
  readonly key: string;
  readonly context: ArchivistTerminalContext;
  readonly element: HTMLDivElement;

  private terminal!: XtermTerminal;
  private fitAddon!: FitAddon;
  private readonly resizeObserver: ResizeObserver;
  private socket: WebSocket | null = null;
  private theme: ArchivistTheme | null = null;
  private connectionGeneration = 0;
  private reconnectTimer: number | null = null;
  private pingTimer: number | null = null;
  private viewportSaveTimer: number | null = null;
  private viewportRestoreTimer: number | null = null;
  private pendingViewportRestore = true;
  private suppressViewportPersistence = false;
  private visible = false;
  private disposed = false;

  constructor(
    root: HTMLElement,
    context: ArchivistTerminalContext,
    private readonly callbacks: TerminalCallbacks,
  ) {
    this.key = contextKey(context);
    this.context = context;
    this.element = document.createElement("div");
    this.element.className = "terminal-session";
    this.element.hidden = true;
    this.element.tabIndex = 0;
    root.appendChild(this.element);

    this.createTerminal();

    this.element.addEventListener(
      "pointerdown",
      () => {
        requestAnimationFrame(() => {
          this.terminal.focus();
        });
      },
      {
        capture: true,
      },
    );

    this.element.addEventListener(
      "focus",
      () => {
        requestAnimationFrame(() => {
          this.terminal.focus();
        });
      },
    );

    this.resizeObserver = new ResizeObserver(() => {
      this.fitAndResize();
    });
    this.resizeObserver.observe(this.element);

    this.callbacks.reportState(
      context.sessionId,
      "connecting",
      "Terminal",
      "",
    );
    this.connect(++this.connectionGeneration);
  }

  applyTheme(theme: ArchivistTheme): void {
    this.theme = theme;
    this.terminal.options.theme = {
      background: theme.workspaceBg,
      foreground: theme.codeBlockText,
      cursor: theme.accentBright,
      selectionBackground: theme.activeBg,
      black: theme.appBg,
      brightBlack: theme.mutedText,
      magenta: theme.accent,
      brightMagenta: theme.accentBright,
      white: theme.appText,
      brightWhite: theme.appText,
    };
    this.terminal.options.fontFamily =
      theme.monospaceFontFamily;
    this.terminal.options.fontSize =
      theme.textControlSize;
    this.fitAndResize();
  }

  setVisible(visible: boolean): void {
    this.visible = visible;

    if (!visible) {
      this.persistViewport();
    }

    this.element.hidden = !visible;
    this.element.style.pointerEvents =
      visible ? "auto" : "none";

    if (visible) {
      requestAnimationFrame(() => {
        this.fitAndResize();
        this.terminal.focus();
      });
    }
  }

  fit(): void {
    this.fitAndResize();
  }

  kill(): void {
    if (
      this.socket
      && this.socket.readyState === WebSocket.OPEN
    ) {
      this.send({
        type: "kill",
      });
      return;
    }

    this.killDetached();
  }

  dispose(): void {
    if (this.disposed) {
      return;
    }

    this.disposed = true;
    this.persistViewport();
    this.clearReconnectTimer();
    this.stopPingTimer();
    this.clearViewportSaveTimer();
    this.clearViewportRestoreTimer();
    this.resizeObserver.disconnect();

    if (this.socket) {
      this.socket.close(
        1000,
        "Terminal view disposed.",
      );
      this.socket = null;
    }

    this.terminal.dispose();
    this.element.remove();
  }

  private createTerminal(): void {
    this.element.replaceChildren();
    this.fitAddon = new FitAddon();
    this.terminal = new XtermTerminal({
      cursorBlink: true,
      convertEol: true,
      scrollback: 10000,
      allowProposedApi: false,
    });
    this.terminal.loadAddon(this.fitAddon);
    this.terminal.open(this.element);

    this.terminal.onData((data) => {
      this.send({
        type: "input",
        data,
      });
    });

    this.terminal.onScroll(() => {
      if (!this.suppressViewportPersistence) {
        this.scheduleViewportSave();
      }
    });

    if (this.theme) {
      this.applyTheme(this.theme);
    }
  }

  private recreateForReplay(): void {
    this.persistViewport();
    this.terminal.dispose();
    this.pendingViewportRestore = true;
    this.suppressViewportPersistence = true;
    this.createTerminal();
    this.suppressViewportPersistence = false;

    if (this.visible) {
      requestAnimationFrame(() => {
        this.fitAndResize();
        this.terminal.focus();
      });
    }
  }

  private connect(generation: number): void {
    if (
      this.disposed
      || generation !== this.connectionGeneration
    ) {
      return;
    }

    this.callbacks.reportStatus("Connecting terminal…");
    this.fitAddon.fit();

    const socket = new WebSocket(
      terminalSocketUrl(
        this.context,
        this.terminal.cols,
        this.terminal.rows,
      ),
    );
    this.socket = socket;

    socket.addEventListener("open", () => {
      if (
        this.disposed
        || generation !== this.connectionGeneration
      ) {
        socket.close(
          1000,
          "Superseded terminal connection.",
        );
        return;
      }

      this.callbacks.reportStatus("Terminal connected");
      this.fitAndResize();
      this.startPingTimer();

      if (this.visible) {
        this.terminal.focus();
      }
    });

    socket.addEventListener("message", (event) => {
      if (
        this.disposed
        || generation !== this.connectionGeneration
      ) {
        return;
      }

      let message: TerminalServerMessage;

      try {
        message = JSON.parse(
          String(event.data),
        ) as TerminalServerMessage;
      } catch {
        return;
      }

      switch (message.type) {
        case "ready": {
          const title = shellTitle(message.shell);

          this.scheduleViewportRestore();
          this.callbacks.reportStatus(
            `Terminal · ${message.cwd}`,
          );
          this.callbacks.reportState(
            message.sessionId,
            "running",
            title,
            message.cwd,
          );
          break;
        }
        case "output":
          this.terminal.write(message.data, () => {
            this.scheduleViewportRestore();
          });
          break;
        case "exit":
          this.callbacks.reportStatus(
            `Terminal exited (${message.exitCode})`,
          );
          this.callbacks.reportState(
            this.context.sessionId,
            "exited",
            "",
            "",
          );
          this.terminal.write(
            `\r\n[process exited ${message.exitCode}]\r\n`,
          );
          break;
        case "error":
          this.callbacks.reportStatus(message.message);
          this.callbacks.reportState(
            this.context.sessionId,
            "error",
            "",
            "",
          );
          this.terminal.write(
            `\r\n[${message.message}]\r\n`,
          );
          break;
        case "pong":
          break;
      }
    });

    socket.addEventListener("close", (event) => {
      if (this.socket === socket) {
        this.socket = null;
      }

      this.stopPingTimer();

      if (
        this.disposed
        || generation !== this.connectionGeneration
        || event.code === 1000
      ) {
        return;
      }

      this.callbacks.reportStatus(
        "Terminal disconnected; reconnecting…",
      );
      this.callbacks.reportState(
        this.context.sessionId,
        "disconnected",
        "",
        "",
      );
      this.scheduleReconnect(generation);
    });

    socket.addEventListener("error", () => {
      this.callbacks.reportStatus(
        "Terminal connection failed",
      );
    });
  }

  private fitAndResize(): void {
    if (
      this.disposed
      || this.element.hidden
    ) {
      return;
    }

    try {
      this.fitAddon.fit();
    } catch {
      return;
    }

    this.send({
      type: "resize",
      cols: Math.max(2, this.terminal.cols),
      rows: Math.max(2, this.terminal.rows),
    });
  }

  private send(message: object): void {
    if (
      !this.socket
      || this.socket.readyState !== WebSocket.OPEN
    ) {
      return;
    }

    this.socket.send(JSON.stringify(message));
  }

  private scheduleReconnect(generation: number): void {
    this.clearReconnectTimer();
    this.reconnectTimer = window.setTimeout(() => {
      this.reconnectTimer = null;

      if (
        this.disposed
        || generation !== this.connectionGeneration
      ) {
        return;
      }

      this.recreateForReplay();
      this.connect(generation);
    }, 650);
  }

  private clearReconnectTimer(): void {
    if (this.reconnectTimer !== null) {
      window.clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
  }

  private startPingTimer(): void {
    this.stopPingTimer();
    this.pingTimer = window.setInterval(() => {
      this.send({
        type: "ping",
        sentAt: Date.now(),
      });
    }, 20000);
  }

  private stopPingTimer(): void {
    if (this.pingTimer !== null) {
      window.clearInterval(this.pingTimer);
      this.pingTimer = null;
    }
  }

  private scheduleViewportSave(): void {
    this.clearViewportSaveTimer();
    this.viewportSaveTimer = window.setTimeout(() => {
      this.viewportSaveTimer = null;
      this.persistViewport();
    }, 160);
  }

  private clearViewportSaveTimer(): void {
    if (this.viewportSaveTimer !== null) {
      window.clearTimeout(this.viewportSaveTimer);
      this.viewportSaveTimer = null;
    }
  }

  private persistViewport(): void {
    const buffer = this.terminal.buffer.active;
    const viewport: PersistedTerminalViewport = {
      viewportY: buffer.viewportY,
      wasAtBottom:
        buffer.viewportY >= buffer.baseY,
    };

    try {
      window.localStorage.setItem(
        terminalViewportStorageKey(this.context),
        JSON.stringify(viewport),
      );
    } catch {
      return;
    }
  }

  private scheduleViewportRestore(): void {
    if (!this.pendingViewportRestore) {
      return;
    }

    this.clearViewportRestoreTimer();
    this.viewportRestoreTimer = window.setTimeout(() => {
      this.viewportRestoreTimer = null;
      this.restoreViewport();
    }, 80);
  }

  private restoreViewport(): void {
    if (!this.pendingViewportRestore) {
      return;
    }

    this.pendingViewportRestore = false;
    const viewport = loadTerminalViewport(
      this.context,
    );

    if (!viewport) {
      this.terminal.scrollToBottom();
      return;
    }

    this.suppressViewportPersistence = true;

    if (viewport.wasAtBottom) {
      this.terminal.scrollToBottom();
    } else {
      this.terminal.scrollToLine(
        Math.min(
          Math.max(0, viewport.viewportY),
          this.terminal.buffer.active.baseY,
        ),
      );
    }

    this.suppressViewportPersistence = false;
  }

  private clearViewportRestoreTimer(): void {
    if (this.viewportRestoreTimer !== null) {
      window.clearTimeout(this.viewportRestoreTimer);
      this.viewportRestoreTimer = null;
    }
  }

  private killDetached(): void {
    const socket = new WebSocket(
      terminalSocketUrl(this.context, 2, 2),
    );
    const timeout = window.setTimeout(() => {
      socket.close(
        1000,
        "Terminal kill request timed out.",
      );
    }, 3000);

    socket.addEventListener("open", () => {
      socket.send(
        JSON.stringify({
          type: "kill",
        }),
      );
    });

    socket.addEventListener("close", () => {
      window.clearTimeout(timeout);
    });

    socket.addEventListener("error", () => {
      window.clearTimeout(timeout);
      socket.close();
    });
  }
}

export class Terminal {
  readonly element: HTMLElement;

  private readonly sessions = new Map<
    string,
    TerminalSessionView
  >();
  private activeKey = "";
  private theme: ArchivistTheme | null = null;
  private visible = false;

  constructor(
    element: HTMLElement,
    private readonly callbacks: TerminalCallbacks,
  ) {
    this.element = element;
    this.element.tabIndex = 0;

    window.addEventListener("beforeunload", () => {
      for (const session of this.sessions.values()) {
        session.setVisible(false);
      }
    });
  }

  applyCommand(command: ArchivistTerminalCommand): void {
    const context: ArchivistTerminalContext = {
      collectionId: command.collectionId,
      libraryId: command.libraryId,
      sessionId: command.sessionId,
    };

    if (!validContext(context)) {
      return;
    }

    const key = contextKey(context);
    const session = this.sessions.get(key);

    if (session) {
      session.kill();
      session.dispose();
      this.sessions.delete(key);

      if (this.activeKey === key) {
        this.activeKey = "";
      }

      return;
    }

    this.killDetached(context);
  }

  applyContext(context: ArchivistTerminalContext): void {
    const key = validContext(context)
      ? contextKey(context)
      : "";

    if (this.activeKey === key) {
      this.activeSession()?.setVisible(this.visible);
      return;
    }

    this.activeSession()?.setVisible(false);
    this.activeKey = key;

    if (!key) {
      this.callbacks.reportStatus(
        "Select a Library to open its terminal.",
      );
      return;
    }

    let session = this.sessions.get(key);

    if (!session) {
      session = new TerminalSessionView(
        this.element,
        context,
        this.callbacks,
      );
      this.sessions.set(key, session);

      if (this.theme) {
        session.applyTheme(this.theme);
      }
    }

    session.setVisible(this.visible);
  }

  applyTheme(theme: ArchivistTheme): void {
    this.theme = theme;

    for (const session of this.sessions.values()) {
      session.applyTheme(theme);
    }
  }

  setVisible(visible: boolean): void {
    this.visible = visible;
    this.element.hidden = !visible;
    this.element.style.pointerEvents =
      visible ? "auto" : "none";
    this.activeSession()?.setVisible(visible);
  }

  fit(): void {
    this.activeSession()?.fit();
  }

  private activeSession(): TerminalSessionView | null {
    return this.sessions.get(this.activeKey) ?? null;
  }

  private killDetached(
    context: ArchivistTerminalContext,
  ): void {
    const socket = new WebSocket(
      terminalSocketUrl(context, 2, 2),
    );
    const timeout = window.setTimeout(() => {
      socket.close(
        1000,
        "Terminal kill request timed out.",
      );
    }, 3000);

    socket.addEventListener("open", () => {
      socket.send(
        JSON.stringify({
          type: "kill",
        }),
      );
    });

    socket.addEventListener("close", () => {
      window.clearTimeout(timeout);
    });

    socket.addEventListener("error", () => {
      window.clearTimeout(timeout);
      socket.close();
    });
  }
}

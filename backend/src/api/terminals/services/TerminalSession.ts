import * as pty from "@lydell/node-pty";
import { WebSocket } from "ws";
import type {
  TerminalServerMessage,
  TerminalSocketContext,
} from "../types/TerminalTypes.js";
import {
  deleteTerminalSessionSnapshot,
  loadTerminalSessionSnapshot,
  saveTerminalSessionSnapshot,
} from "./TerminalSessionSnapshotStore.js";
import {
  deleteTerminalShellState,
  prepareTerminalShell,
} from "./TerminalShellProfile.js";

const maximumScrollbackCharacters = 1_500_000;

function sendMessage(
  socket: WebSocket,
  message: TerminalServerMessage,
): void {
  if (socket.readyState !== WebSocket.OPEN) {
    return;
  }

  socket.send(JSON.stringify(message));
}

export class TerminalSession {
  readonly sessionId: string;
  readonly collectionId: string;
  readonly libraryId: string;
  readonly cwd: string;
  readonly shell: string;

  private readonly sockets = new Set<WebSocket>();
  private readonly process: pty.IPty;
  private scrollback = "";
  private snapshotTimer: NodeJS.Timeout | null = null;
  private removeSnapshotOnExit = false;
  private exited = false;

  constructor(
    context: TerminalSocketContext,
    cwd: string,
    shell: string,
    private readonly onExit: (
      session: TerminalSession,
    ) => void,
  ) {
    this.sessionId = context.sessionId;
    this.collectionId = context.collectionId;
    this.libraryId = context.libraryId;
    this.cwd = cwd;
    this.shell = shell;

    const snapshot = loadTerminalSessionSnapshot(context);

    if (snapshot) {
      this.scrollback = snapshot.scrollback;
    }

    const preparedShell = prepareTerminalShell(
      context,
      shell,
    );

    this.process = pty.spawn(shell, preparedShell.args, {
      name: "xterm-256color",
      cols: context.cols,
      rows: context.rows,
      cwd,
      env: preparedShell.env,
    });

    this.process.onData((data) => {
      this.appendScrollback(data);
      this.scheduleSnapshotSave();
      this.broadcast({
        type: "output",
        sessionId: this.sessionId,
        data,
      });
    });

    this.process.onExit(({ exitCode, signal }) => {
      this.exited = true;
      this.clearSnapshotTimer();

      if (this.removeSnapshotOnExit) {
        deleteTerminalSessionSnapshot(this);
      } else {
        this.persistSnapshot();
      }

      this.broadcast({
        type: "exit",
        sessionId: this.sessionId,
        exitCode,
        signal: signal ?? 0,
      });

      for (const socket of this.sockets) {
        socket.close(1000, "Terminal process exited.");
      }

      this.sockets.clear();
      this.onExit(this);
    });
  }

  get hasExited(): boolean {
    return this.exited;
  }

  attach(socket: WebSocket): void {
    if (this.exited) {
      sendMessage(socket, {
        type: "error",
        sessionId: this.sessionId,
        message: "This terminal process has exited.",
      });
      socket.close(1011, "Terminal process exited.");
      return;
    }

    this.sockets.add(socket);
    sendMessage(socket, {
      type: "ready",
      sessionId: this.sessionId,
      cwd: this.cwd,
      shell: this.shell,
    });

    if (this.scrollback.length > 0) {
      sendMessage(socket, {
        type: "output",
        sessionId: this.sessionId,
        data: this.scrollback,
      });
    }
  }

  detach(socket: WebSocket): void {
    this.sockets.delete(socket);
  }

  write(data: string): void {
    if (!this.exited) {
      this.process.write(data);
    }
  }

  resize(cols: number, rows: number): void {
    if (!this.exited) {
      this.process.resize(cols, rows);
    }
  }

  kill(): void {
    if (!this.exited) {
      this.removeSnapshotOnExit = true;
      this.clearSnapshotTimer();
      deleteTerminalSessionSnapshot(this);
      deleteTerminalShellState(this);
      this.process.kill();
    }
  }

  dispose(): void {
    for (const socket of this.sockets) {
      socket.close(1001, "Archivist backend is stopping.");
    }

    this.sockets.clear();
    this.clearSnapshotTimer();
    this.persistSnapshot();

    if (!this.exited) {
      this.process.kill();
    }
  }

  private persistSnapshot(): void {
    saveTerminalSessionSnapshot({
      version: 2,
      sessionId: this.sessionId,
      collectionId: this.collectionId,
      libraryId: this.libraryId,
      cwd: this.cwd,
      shell: this.shell,
      scrollback: this.scrollback,
      savedAt: new Date().toISOString(),
    });
  }

  private scheduleSnapshotSave(): void {
    this.clearSnapshotTimer();
    this.snapshotTimer = setTimeout(() => {
      this.snapshotTimer = null;
      this.persistSnapshot();
    }, 750);
  }

  private clearSnapshotTimer(): void {
    if (this.snapshotTimer) {
      clearTimeout(this.snapshotTimer);
      this.snapshotTimer = null;
    }
  }

  private appendScrollback(data: string): void {
    this.scrollback += data;

    if (this.scrollback.length > maximumScrollbackCharacters) {
      this.scrollback = this.scrollback.slice(
        -maximumScrollbackCharacters,
      );
    }
  }

  private broadcast(message: TerminalServerMessage): void {
    for (const socket of this.sockets) {
      sendMessage(socket, message);
    }
  }
}

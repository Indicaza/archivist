import fs from "node:fs";
import path from "node:path";
import { AppError } from "../../../errors/app-error.js";
import { getLibraryById } from "../../libraries/models/Library.js";
import type { TerminalSocketContext } from "../types/TerminalTypes.js";
import { TerminalSession } from "./TerminalSession.js";

const maximumTerminalSessions = 16;

function sessionKey(context: TerminalSocketContext): string {
  return [
    context.collectionId,
    context.libraryId,
    context.sessionId,
  ].join(":");
}

function resolveShell(): string {
  const configuredShell = process.env.SHELL;

  if (
    configuredShell
    && path.isAbsolute(configuredShell)
    && fs.existsSync(configuredShell)
  ) {
    return configuredShell;
  }

  return process.platform === "win32"
    ? "powershell.exe"
    : fs.existsSync("/bin/zsh")
      ? "/bin/zsh"
      : "/bin/bash";
}

function resolveLibraryRoot(libraryId: string): string {
  const library = getLibraryById(libraryId);

  if (!library) {
    throw new AppError(404, "Library not found.");
  }

  if (library.archivedAt) {
    throw new AppError(
      409,
      "Archived Libraries cannot open terminals.",
    );
  }

  let rootPath: string;

  try {
    rootPath = fs.realpathSync.native(library.rootPath);
  } catch {
    throw new AppError(
      404,
      "The Library folder could not be resolved.",
    );
  }

  const stats = fs.statSync(rootPath);

  if (!stats.isDirectory()) {
    throw new AppError(
      409,
      "The Library root is no longer a folder.",
    );
  }

  try {
    fs.accessSync(
      rootPath,
      fs.constants.R_OK | fs.constants.X_OK,
    );
  } catch {
    throw new AppError(
      403,
      "Archivist cannot enter the Library folder.",
    );
  }

  return rootPath;
}

export class TerminalSessionManager {
  private readonly sessions = new Map<
    string,
    TerminalSession
  >();

  getOrCreate(
    context: TerminalSocketContext,
  ): TerminalSession {
    const key = sessionKey(context);
    const existing = this.sessions.get(key);

    if (existing && !existing.hasExited) {
      existing.resize(context.cols, context.rows);
      return existing;
    }

    if (existing) {
      this.sessions.delete(key);
    }

    if (this.sessions.size >= maximumTerminalSessions) {
      throw new AppError(
        429,
        "Too many terminal sessions are open.",
      );
    }

    const session = new TerminalSession(
      context,
      resolveLibraryRoot(context.libraryId),
      resolveShell(),
      (exitedSession) => {
        if (this.sessions.get(key) === exitedSession) {
          this.sessions.delete(key);
        }
      },
    );

    this.sessions.set(key, session);
    return session;
  }

  shutdown(): void {
    for (const session of this.sessions.values()) {
      session.dispose();
    }

    this.sessions.clear();
  }
}

export const terminalSessionManager =
  new TerminalSessionManager();

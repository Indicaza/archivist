import { createHash } from "node:crypto";
import {
  appendFileSync,
  chmodSync,
  mkdirSync,
  rmSync,
  writeFileSync,
} from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import type { TerminalSocketContext } from "../types/TerminalTypes.js";

export type PreparedTerminalShell = {
  args: string[];
  env: Record<string, string>;
};

const serviceDirectory = path.dirname(
  fileURLToPath(import.meta.url),
);
const terminalShellStateDirectory = path.resolve(
  serviceDirectory,
  "../../../..",
  "data",
  "terminals",
  "shells",
);

function terminalIdentity(
  context: Pick<
    TerminalSocketContext,
    "sessionId" | "collectionId" | "libraryId"
  >,
): string {
  return [
    context.collectionId,
    context.libraryId,
    context.sessionId,
  ].join(":");
}

function terminalIdentityHash(
  context: Pick<
    TerminalSocketContext,
    "sessionId" | "collectionId" | "libraryId"
  >,
): string {
  return createHash("sha256")
    .update(terminalIdentity(context))
    .digest("hex");
}

function terminalStateDirectory(
  context: Pick<
    TerminalSocketContext,
    "sessionId" | "collectionId" | "libraryId"
  >,
): string {
  return path.join(
    terminalShellStateDirectory,
    terminalIdentityHash(context),
  );
}

function quoteForShell(value: string): string {
  return `'${value.replaceAll("'", `'\\''`)}'`;
}

function baseEnvironment(): Record<string, string> {
  const environment: Record<string, string> = {};

  for (const [key, value] of Object.entries(process.env)) {
    if (typeof value === "string") {
      environment[key] = value;
    }
  }

  environment.TERM = "xterm-256color";
  environment.COLORTERM = "truecolor";
  environment.ARCHIVIST_TERMINAL = "1";

  return environment;
}

function sourceHomeZshFile(fileName: string): string {
  return [
    'typeset __archivist_zdotdir="$ZDOTDIR"',
    'export ZDOTDIR="$HOME"',
    `[[ -r "$HOME/${fileName}" ]] && source "$HOME/${fileName}"`,
    'export ZDOTDIR="$__archivist_zdotdir"',
    "unset __archivist_zdotdir",
    "",
  ].join("\n");
}

function prepareZsh(
  stateDirectory: string,
  historyPath: string,
  environment: Record<string, string>,
): PreparedTerminalShell {
  const zdotDirectory = path.join(
    stateDirectory,
    "zdotdir",
  );

  mkdirSync(zdotDirectory, {
    recursive: true,
  });

  for (const fileName of [
    ".zshenv",
    ".zprofile",
    ".zlogin",
    ".zlogout",
  ]) {
    writeFileSync(
      path.join(zdotDirectory, fileName),
      sourceHomeZshFile(fileName),
      {
        encoding: "utf8",
        mode: 0o600,
      },
    );
  }

  const historyLiteral = quoteForShell(historyPath);
  const zshrc = [
    `export HISTFILE=${historyLiteral}`,
    sourceHomeZshFile(".zshrc").trimEnd(),
    `export HISTFILE=${historyLiteral}`,
    "unsetopt SHARE_HISTORY",
    "unsetopt PROMPT_SP",
    "PROMPT_EOL_MARK=''",
    "setopt APPEND_HISTORY",
    "setopt INC_APPEND_HISTORY",
    'fc -p "$HISTFILE"',
    "",
  ].join("\n");

  writeFileSync(
    path.join(zdotDirectory, ".zshrc"),
    zshrc,
    {
      encoding: "utf8",
      mode: 0o600,
    },
  );

  environment.ZDOTDIR = zdotDirectory;
  environment.HISTFILE = historyPath;
  environment.ARCHIVIST_TERMINAL_HISTORY =
    historyPath;

  return {
    args: ["-l"],
    env: environment,
  };
}

function prepareBash(
  stateDirectory: string,
  historyPath: string,
  environment: Record<string, string>,
): PreparedTerminalShell {
  const rcPath = path.join(
    stateDirectory,
    "bashrc",
  );
  const historyLiteral = quoteForShell(historyPath);
  const bashrc = [
    '[[ -r "$HOME/.bashrc" ]] && source "$HOME/.bashrc"',
    `export HISTFILE=${historyLiteral}`,
    "history -c",
    'history -r "$HISTFILE" 2>/dev/null || true',
    "shopt -s histappend",
    'PROMPT_COMMAND="history -a${PROMPT_COMMAND:+;$PROMPT_COMMAND}"',
    "",
  ].join("\n");

  writeFileSync(
    rcPath,
    bashrc,
    {
      encoding: "utf8",
      mode: 0o600,
    },
  );

  environment.HISTFILE = historyPath;
  environment.ARCHIVIST_TERMINAL_HISTORY =
    historyPath;

  return {
    args: [
      "--noprofile",
      "--rcfile",
      rcPath,
      "-i",
    ],
    env: environment,
  };
}

export function prepareTerminalShell(
  context: TerminalSocketContext,
  shell: string,
): PreparedTerminalShell {
  const stateDirectory =
    terminalStateDirectory(context);
  const historyPath = path.join(
    stateDirectory,
    "history",
  );

  mkdirSync(stateDirectory, {
    recursive: true,
  });
  appendFileSync(historyPath, "", {
    encoding: "utf8",
    mode: 0o600,
  });
  chmodSync(historyPath, 0o600);

  const environment = baseEnvironment();
  const shellName = path
    .basename(shell)
    .toLowerCase();

  if (shellName.includes("zsh")) {
    return prepareZsh(
      stateDirectory,
      historyPath,
      environment,
    );
  }

  if (shellName.includes("bash")) {
    return prepareBash(
      stateDirectory,
      historyPath,
      environment,
    );
  }

  environment.HISTFILE = historyPath;
  environment.ARCHIVIST_TERMINAL_HISTORY =
    historyPath;

  return {
    args:
      process.platform === "win32"
        ? []
        : ["-l"],
    env: environment,
  };
}

export function deleteTerminalShellState(
  context: Pick<
    TerminalSocketContext,
    "sessionId" | "collectionId" | "libraryId"
  >,
): void {
  rmSync(
    terminalStateDirectory(context),
    {
      recursive: true,
      force: true,
    },
  );
}

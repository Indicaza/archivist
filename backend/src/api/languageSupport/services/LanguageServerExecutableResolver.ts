import fs from "node:fs";
import path from "node:path";
import type {
  LanguageServerDefinition,
} from "../types/LanguageSupportTypes.js";

function executableExtensions(): readonly string[] {
  if (process.platform !== "win32") {
    return [""];
  }

  const configured = process.env.PATHEXT
    ?.split(";")
    .map((extension) => extension.trim())
    .filter(Boolean);

  return configured && configured.length > 0
    ? ["", ...configured]
    : ["", ".EXE", ".CMD", ".BAT", ".COM"];
}

function isExecutable(filePath: string): boolean {
  try {
    const stats = fs.statSync(filePath);

    if (!stats.isFile()) {
      return false;
    }

    fs.accessSync(
      filePath,
      process.platform === "win32"
        ? fs.constants.F_OK
        : fs.constants.X_OK,
    );

    return true;
  } catch {
    return false;
  }
}

function candidateDirectories(
  workspaceRoot?: string,
): readonly string[] {
  const directories = new Set<string>();

  if (workspaceRoot) {
    directories.add(
      path.join(workspaceRoot, "node_modules", ".bin"),
    );
  }

  directories.add(
    path.resolve(process.cwd(), "node_modules", ".bin"),
  );
  directories.add(
    path.resolve(
      process.cwd(),
      "..",
      "node_modules",
      ".bin",
    ),
  );

  for (
    const entry of (process.env.PATH ?? "").split(
      path.delimiter,
    )
  ) {
    const normalized = entry.trim();

    if (normalized) {
      directories.add(normalized);
    }
  }

  return [...directories];
}

function resolveCandidate(
  candidate: string,
  directories: readonly string[],
): string | null {
  const containsSeparator =
    candidate.includes("/")
    || candidate.includes("\\");
  const directCandidates =
    path.isAbsolute(candidate) || containsSeparator
      ? [candidate]
      : [];

  for (const directCandidate of directCandidates) {
    const resolved = path.resolve(directCandidate);

    if (isExecutable(resolved)) {
      return resolved;
    }
  }

  if (directCandidates.length > 0) {
    return null;
  }

  for (const directory of directories) {
    for (const extension of executableExtensions()) {
      const resolved = path.join(
        directory,
        `${candidate}${extension}`,
      );

      if (isExecutable(resolved)) {
        return resolved;
      }
    }
  }

  return null;
}

export function resolveLanguageServerExecutable(
  definition: LanguageServerDefinition,
  workspaceRoot?: string,
): string | null {
  const directories = candidateDirectories(workspaceRoot);

  for (
    const candidate of definition.executableCandidates
  ) {
    const executablePath = resolveCandidate(
      candidate,
      directories,
    );

    if (executablePath) {
      return executablePath;
    }
  }

  return null;
}

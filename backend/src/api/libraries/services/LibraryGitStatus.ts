import { execFile } from "node:child_process";
import path from "node:path";
import { promisify } from "node:util";
import { AppError } from "../../../errors/app-error.js";
import { getLibraryById } from "../models/Library.js";

const execFileAsync = promisify(execFile);

export type LibraryGitFileStatus =
  | "modified"
  | "added"
  | "deleted"
  | "renamed"
  | "conflicted"
  | "untracked"
  | "ignored";

export type LibraryGitStatusEntry = {
  path: string;
  originalPath: string | null;
  status: LibraryGitFileStatus;
  indexStatus: string;
  worktreeStatus: string;
  directory: boolean;
};

export type LibraryGitStatus = {
  repository: boolean;
  repositoryRoot: string | null;
  branch: string | null;
  detached: boolean;
  upstream: string | null;
  ahead: number;
  behind: number;
  dirty: boolean;
  entries: LibraryGitStatusEntry[];
  counts: Record<LibraryGitFileStatus, number>;
};

const emptyCounts = (): Record<LibraryGitFileStatus, number> => ({
  modified: 0,
  added: 0,
  deleted: 0,
  renamed: 0,
  conflicted: 0,
  untracked: 0,
  ignored: 0,
});

function pathIsInsideRoot(rootPath: string, candidatePath: string): boolean {
  const relativePath = path.relative(rootPath, candidatePath);

  return (
    relativePath === "" ||
    (
      relativePath !== ".." &&
      !relativePath.startsWith(`..${path.sep}`) &&
      !path.isAbsolute(relativePath)
    )
  );
}

function normalizedLibraryPath(
  repositoryRoot: string,
  libraryRoot: string,
  repositoryRelativePath: string,
): string | null {
  const absolutePath = path.resolve(
    repositoryRoot,
    ...repositoryRelativePath.split("/"),
  );

  if (!pathIsInsideRoot(libraryRoot, absolutePath)) {
    return null;
  }

  const relativePath = path.relative(libraryRoot, absolutePath);
  return relativePath.split(path.sep).join("/");
}

function classifyStatus(
  recordType: string,
  indexStatus: string,
  worktreeStatus: string,
): LibraryGitFileStatus {
  if (
    recordType === "u" ||
    indexStatus === "U" ||
    worktreeStatus === "U" ||
    `${indexStatus}${worktreeStatus}` === "AA" ||
    `${indexStatus}${worktreeStatus}` === "DD"
  ) {
    return "conflicted";
  }

  if (recordType === "!") {
    return "ignored";
  }

  if (recordType === "?") {
    return "untracked";
  }

  if (recordType === "2" || indexStatus === "R" || worktreeStatus === "R") {
    return "renamed";
  }

  if (indexStatus === "D" || worktreeStatus === "D") {
    return "deleted";
  }

  // Any index-side change has been staged through `git add` (or an
  // equivalent Git operation). Keep that visually distinct from working-tree
  // modifications and untracked files.
  if (indexStatus !== ".") {
    return "added";
  }

  return "modified";
}

function parseTrackedRecord(record: string): {
  indexStatus: string;
  worktreeStatus: string;
  path: string;
} | null {
  const match = record.match(
    /^1 ([^ ]{2}) (?:[^ ]+ ){6}(.+)$/s,
  );

  if (!match) {
    return null;
  }

  return {
    indexStatus: match[1]?.charAt(0) ?? ".",
    worktreeStatus: match[1]?.charAt(1) ?? ".",
    path: match[2] ?? "",
  };
}

function parseUnmergedRecord(record: string): {
  indexStatus: string;
  worktreeStatus: string;
  path: string;
} | null {
  const match = record.match(
    /^u ([^ ]{2}) (?:[^ ]+ ){8}(.+)$/s,
  );

  if (!match) {
    return null;
  }

  return {
    indexStatus: match[1]?.charAt(0) ?? "U",
    worktreeStatus: match[1]?.charAt(1) ?? "U",
    path: match[2] ?? "",
  };
}

function parseRenamedRecord(record: string): {
  indexStatus: string;
  worktreeStatus: string;
  path: string;
} | null {
  const match = record.match(
    /^2 ([^ ]{2}) (?:[^ ]+ ){7}(.+)$/s,
  );

  if (!match) {
    return null;
  }

  return {
    indexStatus: match[1]?.charAt(0) ?? ".",
    worktreeStatus: match[1]?.charAt(1) ?? ".",
    path: match[2] ?? "",
  };
}

function parseBranchAheadBehind(value: string): {
  ahead: number;
  behind: number;
} {
  const match = value.match(/^\+(\d+)\s+-(\d+)$/);

  return {
    ahead: Number(match?.[1] ?? 0),
    behind: Number(match?.[2] ?? 0),
  };
}

export async function readLibraryGitStatus(
  libraryId: string,
): Promise<LibraryGitStatus> {
  const library = getLibraryById(libraryId);

  if (!library) {
    throw new AppError(404, "Library not found.");
  }

  let repositoryRoot: string;

  try {
    const result = await execFileAsync(
      "git",
      ["-C", library.rootPath, "rev-parse", "--show-toplevel"],
      {
        encoding: "utf8",
        maxBuffer: 1024 * 1024,
      },
    );
    repositoryRoot = String(result.stdout).trim();
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);

    if (
      message.includes("not a git repository") ||
      message.includes("not a git work tree")
    ) {
      return {
        repository: false,
        repositoryRoot: null,
        branch: null,
        detached: false,
        upstream: null,
        ahead: 0,
        behind: 0,
        dirty: false,
        entries: [],
        counts: emptyCounts(),
      };
    }

    throw new AppError(503, "Archivist could not inspect Git status.", {
      cause: message,
    });
  }

  let output: string;

  try {
    const result = await execFileAsync(
      "git",
      [
        "-C",
        library.rootPath,
        "status",
        "--porcelain=v2",
        "-z",
        "--branch",
        "--untracked-files=all",
        "--ignored=matching",
      ],
      {
        encoding: "utf8",
        maxBuffer: 8 * 1024 * 1024,
      },
    );
    output = String(result.stdout);
  } catch (error) {
    throw new AppError(503, "Archivist could not read Git status.", {
      cause: error instanceof Error ? error.message : String(error),
    });
  }

  const tokens = output.split("\0").filter((token) => token.length > 0);
  const entries: LibraryGitStatusEntry[] = [];
  const counts = emptyCounts();
  let branch: string | null = null;
  let detached = false;
  let upstream: string | null = null;
  let ahead = 0;
  let behind = 0;

  for (let index = 0; index < tokens.length; index += 1) {
    const token = tokens[index] ?? "";

    if (token.startsWith("# branch.head ")) {
      const value = token.slice("# branch.head ".length).trim();
      detached = value === "(detached)";
      branch = detached ? null : value;
      continue;
    }

    if (token.startsWith("# branch.upstream ")) {
      upstream = token.slice("# branch.upstream ".length).trim() || null;
      continue;
    }

    if (token.startsWith("# branch.ab ")) {
      ({ ahead, behind } = parseBranchAheadBehind(
        token.slice("# branch.ab ".length).trim(),
      ));
      continue;
    }

    if (token.startsWith("# ")) {
      continue;
    }

    const recordType = token.charAt(0);
    let parsed: {
      indexStatus: string;
      worktreeStatus: string;
      path: string;
    } | null = null;
    let originalPath: string | null = null;

    if (recordType === "1") {
      parsed = parseTrackedRecord(token);
    } else if (recordType === "u") {
      parsed = parseUnmergedRecord(token);
    } else if (recordType === "2") {
      parsed = parseRenamedRecord(token);
      originalPath = tokens[index + 1] ?? null;
      index += 1;
    } else if (recordType === "?" || recordType === "!") {
      parsed = {
        indexStatus: recordType,
        worktreeStatus: recordType,
        path: token.slice(2),
      };
    } else {
      continue;
    }

    if (!parsed || parsed.path.length === 0) {
      continue;
    }

    const libraryPath = normalizedLibraryPath(
      repositoryRoot,
      library.rootPath,
      parsed.path,
    );

    if (libraryPath === null || libraryPath.length === 0) {
      continue;
    }

    const normalizedOriginalPath = originalPath
      ? normalizedLibraryPath(repositoryRoot, library.rootPath, originalPath)
      : null;
    const status = classifyStatus(
      recordType,
      parsed.indexStatus,
      parsed.worktreeStatus,
    );

    entries.push({
      path: libraryPath,
      originalPath: normalizedOriginalPath,
      status,
      indexStatus: parsed.indexStatus,
      worktreeStatus: parsed.worktreeStatus,
      directory: parsed.path.endsWith("/"),
    });
    counts[status] += 1;
  }

  entries.sort((left, right) => left.path.localeCompare(right.path));

  return {
    repository: true,
    repositoryRoot,
    branch,
    detached,
    upstream,
    ahead,
    behind,
    dirty: entries.some((entry) => entry.status !== "ignored"),
    entries,
    counts,
  };
}

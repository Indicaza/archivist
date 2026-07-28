import fs from "node:fs";
import path from "node:path";
import type {
  LanguageServerDefinition,
  ResolvedLanguageWorkspace,
} from "../types/LanguageSupportTypes.js";

export interface LanguageServerLaunchConfiguration {
  args: readonly string[];
  buildDirectory: string | null;
  importDirectories: readonly string[];
}

function realDirectory(candidatePath: string): string | null {
  try {
    const resolved = fs.realpathSync.native(candidatePath);

    return fs.statSync(resolved).isDirectory()
      ? resolved
      : null;
  } catch {
    return null;
  }
}

function cmakeSourceDirectory(
  buildDirectory: string,
): string | null {
  try {
    const cache = fs.readFileSync(
      path.join(buildDirectory, "CMakeCache.txt"),
      "utf8",
    );
    const match = cache.match(
      /^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)$/m,
    );

    return match?.[1]
      ? fs.realpathSync.native(match[1].trim())
      : null;
  } catch {
    return null;
  }
}

function childDirectories(parentPath: string): string[] {
  try {
    return fs.readdirSync(parentPath, {
      withFileTypes: true,
    })
      .filter((entry) => entry.isDirectory())
      .map((entry) => path.join(parentPath, entry.name));
  } catch {
    return [];
  }
}

function qmlBuildDirectoryCandidates(
  workspace: ResolvedLanguageWorkspace,
): string[] {
  const libraryBuildRoot = path.join(
    workspace.libraryRoot,
    "build",
  );
  const workspaceBuildRoot = path.join(
    workspace.workspaceRoot,
    "build",
  );
  const candidates = [
    path.join(libraryBuildRoot, "frontend-debug"),
    path.join(libraryBuildRoot, "qt-debug"),
    workspaceBuildRoot,
    ...childDirectories(libraryBuildRoot),
    ...childDirectories(workspaceBuildRoot),
  ];

  return [...new Set(candidates)];
}

function resolveQmlBuildDirectory(
  workspace: ResolvedLanguageWorkspace,
): string | null {
  const workspaceRoot = fs.realpathSync.native(
    workspace.workspaceRoot,
  );
  const candidates = qmlBuildDirectoryCandidates(workspace)
    .map(realDirectory)
    .filter((candidate): candidate is string => Boolean(candidate));

  for (const candidate of candidates) {
    if (cmakeSourceDirectory(candidate) === workspaceRoot) {
      return candidate;
    }
  }

  return candidates.find((candidate) =>
    fs.existsSync(path.join(candidate, "CMakeCache.txt"))
  ) ?? null;
}

export function resolveLanguageServerLaunch(
  definition: LanguageServerDefinition,
  workspace: ResolvedLanguageWorkspace,
): LanguageServerLaunchConfiguration {
  if (definition.id !== "qml") {
    return {
      args: definition.args,
      buildDirectory: null,
      importDirectories: [],
    };
  }

  const buildDirectory = resolveQmlBuildDirectory(workspace);
  const importDirectories = [
    path.join(workspace.workspaceRoot, "qml-tooling"),
    path.join(workspace.workspaceRoot, "qml"),
  ]
    .map(realDirectory)
    .filter((candidate): candidate is string => Boolean(candidate));
  const args = [
    ...definition.args,
    "--no-cmake-calls",
  ];

  if (buildDirectory) {
    args.push("--build-dir", buildDirectory);
  }

  for (const importDirectory of importDirectories) {
    args.push("-I", importDirectory);
  }

  return {
    args,
    buildDirectory,
    importDirectories,
  };
}

import { lstat, opendir, realpath } from "node:fs/promises";
import path from "node:path";
import { AppError } from "../../../errors/app-error.js";
import { getLibraryById } from "../models/Library.js";
import {
  isCatalogableLibraryDirectory,
  isManagedPhotoLibraryDirectory,
} from "./LibraryFilePolicy.js";

const ignoredDirectoryNames = new Set([
  ".git",
  ".obsidian",
  ".archivist-trash",
  "node_modules",
  "dist",
  "build",
  "coverage",
]);

function catalogPath(value: string): string {
  return value.split(path.sep).join("/");
}

function pathIsInsideRoot(
  rootPath: string,
  candidatePath: string,
): boolean {
  const relativePath = path.relative(rootPath, candidatePath);

  return (
    relativePath !== ".."
    && !relativePath.startsWith(`..${path.sep}`)
    && !path.isAbsolute(relativePath)
  );
}

async function listDirectory(
  rootPath: string,
  relativeDirectory: string,
  results: string[],
): Promise<void> {
  const absoluteDirectory = path.resolve(
    rootPath,
    relativeDirectory,
  );

  if (!pathIsInsideRoot(rootPath, absoluteDirectory)) {
    return;
  }

  let directory;

  try {
    directory = await opendir(absoluteDirectory);
  } catch {
    return;
  }

  for await (const entry of directory) {
    if (!entry.isDirectory() || entry.isSymbolicLink()) {
      continue;
    }

    if (
      ignoredDirectoryNames.has(entry.name.toLowerCase())
      || isManagedPhotoLibraryDirectory(entry.name)
      || !isCatalogableLibraryDirectory(entry.name)
    ) {
      continue;
    }

    const relativePath = relativeDirectory
      ? path.join(relativeDirectory, entry.name)
      : entry.name;
    results.push(catalogPath(relativePath));
    await listDirectory(rootPath, relativePath, results);
  }
}

export async function listLibraryDirectories(
  libraryId: string,
): Promise<string[]> {
  const library = getLibraryById(libraryId);

  if (!library) {
    throw new AppError(404, "Library not found.");
  }

  let rootPath: string;

  try {
    rootPath = await realpath(library.rootPath);
    const stats = await lstat(rootPath);

    if (!stats.isDirectory() || stats.isSymbolicLink()) {
      throw new Error("The Library root is not a directory.");
    }
  } catch {
    throw new AppError(
      404,
      "The Library folder could not be resolved.",
    );
  }

  const directories: string[] = [];
  await listDirectory(rootPath, "", directories);
  directories.sort((left, right) =>
    left.localeCompare(right, undefined, {
      sensitivity: "base",
    }),
  );
  return directories;
}

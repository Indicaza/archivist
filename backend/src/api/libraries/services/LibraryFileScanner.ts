import { constants as fsConstants } from "node:fs";
import { access, lstat, opendir } from "node:fs/promises";
import path from "node:path";
import { AppError } from "../../../errors/app-error.js";
import { getLibraryById } from "../models/Library.js";
import { supportedLibraryTextExtensions } from "./LibraryFilePolicy.js";
import {
  completeLibraryScan,
  createLibraryScan,
  failLibraryScan,
  getLibraryFileCatalog,
} from "../models/LibraryFile.js";
import type {
  LibraryScanIssue,
  ScanLibraryResult,
  ScannedLibraryFile,
} from "../types/LibraryFileTypes.js";

const ignoredDirectoryNames = new Set([
  ".git",
  "node_modules",
  "dist",
  "build",
  "coverage",
]);

const ignoredFileNames = new Set([".DS_Store"]);
const maxReportedIssues = 100;

type ScanAccumulator = {
  files: ScannedLibraryFile[];
  issues: LibraryScanIssue[];
  discoveredFileCount: number;
  ignoredEntryCount: number;
  errorCount: number;
};

function toCatalogPath(relativePath: string): string {
  return relativePath.split(path.sep).join("/");
}

function resolveInsideRoot(rootPath: string, relativePath: string): string {
  const absolutePath = path.resolve(rootPath, relativePath);
  const pathFromRoot = path.relative(rootPath, absolutePath);

  if (
    pathFromRoot === ".." ||
    pathFromRoot.startsWith(`..${path.sep}`) ||
    path.isAbsolute(pathFromRoot)
  ) {
    throw new AppError(400, "A scanned path escaped the Library root.");
  }

  return absolutePath;
}

function addIssue(
  accumulator: ScanAccumulator,
  relativePath: string | null,
  message: string,
): void {
  accumulator.errorCount += 1;

  if (accumulator.issues.length < maxReportedIssues) {
    accumulator.issues.push({
      relativePath,
      message,
    });
  }
}

function getErrorMessage(error: unknown): string {
  if (error instanceof Error && error.message.trim()) {
    return error.message;
  }

  return "An unknown filesystem error occurred.";
}

async function validateLibraryRoot(rootPath: string): Promise<void> {
  let stats;

  try {
    stats = await lstat(rootPath);
  } catch {
    throw new AppError(404, "The Library folder no longer exists.");
  }

  if (!stats.isDirectory()) {
    throw new AppError(409, "The Library root is no longer a folder.");
  }

  try {
    await access(rootPath, fsConstants.R_OK);
  } catch {
    throw new AppError(403, "Archivist cannot read the Library folder.");
  }
}

async function scanDirectory(
  rootPath: string,
  relativeDirectory: string,
  accumulator: ScanAccumulator,
): Promise<void> {
  const absoluteDirectory = resolveInsideRoot(rootPath, relativeDirectory);

  let directory;

  try {
    directory = await opendir(absoluteDirectory);
  } catch (error) {
    addIssue(
      accumulator,
      relativeDirectory ? toCatalogPath(relativeDirectory) : null,
      getErrorMessage(error),
    );
    return;
  }

  try {
    for await (const entry of directory) {
      const relativeEntryPath = relativeDirectory
        ? path.join(relativeDirectory, entry.name)
        : entry.name;

      if (entry.isSymbolicLink()) {
        accumulator.ignoredEntryCount += 1;
        continue;
      }

      if (entry.isDirectory()) {
        if (ignoredDirectoryNames.has(entry.name.toLowerCase())) {
          accumulator.ignoredEntryCount += 1;
          continue;
        }

        await scanDirectory(rootPath, relativeEntryPath, accumulator);
        continue;
      }

      if (!entry.isFile()) {
        accumulator.ignoredEntryCount += 1;
        continue;
      }

      if (ignoredFileNames.has(entry.name)) {
        accumulator.ignoredEntryCount += 1;
        continue;
      }

      const extension = path.extname(entry.name).toLowerCase();

      if (!supportedLibraryTextExtensions.has(extension)) {
        accumulator.ignoredEntryCount += 1;
        continue;
      }

      accumulator.discoveredFileCount += 1;

      const absoluteFilePath = resolveInsideRoot(rootPath, relativeEntryPath);
      const catalogPath = toCatalogPath(relativeEntryPath);

      let stats;

      try {
        stats = await lstat(absoluteFilePath);
      } catch (error) {
        addIssue(accumulator, catalogPath, getErrorMessage(error));
        continue;
      }

      if (stats.isSymbolicLink() || !stats.isFile()) {
        accumulator.ignoredEntryCount += 1;
        continue;
      }

      let status: ScannedLibraryFile["status"] = "available";

      try {
        await access(absoluteFilePath, fsConstants.R_OK);
      } catch (error) {
        status = "unreadable";
        addIssue(accumulator, catalogPath, getErrorMessage(error));
      }

      accumulator.files.push({
        relativePath: catalogPath,
        name: entry.name,
        extension,
        sizeBytes: stats.size,
        modifiedAt: stats.mtime.toISOString(),
        status,
      });
    }
  } catch (error) {
    addIssue(
      accumulator,
      relativeDirectory ? toCatalogPath(relativeDirectory) : null,
      getErrorMessage(error),
    );
  }
}

export async function scanLibraryFiles(
  libraryId: string,
): Promise<ScanLibraryResult> {
  const library = getLibraryById(libraryId);

  if (!library) {
    throw new AppError(404, "Library not found.");
  }

  if (library.archivedAt) {
    throw new AppError(409, "Archived Libraries cannot be scanned.");
  }

  const scan = createLibraryScan(libraryId);

  try {
    await validateLibraryRoot(library.rootPath);

    const accumulator: ScanAccumulator = {
      files: [],
      issues: [],
      discoveredFileCount: 0,
      ignoredEntryCount: 0,
      errorCount: 0,
    };

    await scanDirectory(library.rootPath, "", accumulator);

    const completedScan = completeLibraryScan({
      scanId: scan.id,
      libraryId,
      files: accumulator.files,
      discoveredFileCount: accumulator.discoveredFileCount,
      ignoredEntryCount: accumulator.ignoredEntryCount,
      errorCount: accumulator.errorCount,
    });

    const catalog = getLibraryFileCatalog(libraryId);

    return {
      ...catalog,
      scan: completedScan,
      issues: accumulator.issues,
    };
  } catch (error) {
    const message = getErrorMessage(error);

    failLibraryScan(scan.id, libraryId, message);

    if (error instanceof AppError) {
      throw error;
    }

    throw new AppError(500, "The Library scan failed.", {
      cause: message,
    });
  }
}

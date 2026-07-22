import { constants as fsConstants } from "node:fs";
import { access, lstat, readFile, realpath } from "node:fs/promises";
import path from "node:path";
import { TextDecoder } from "node:util";
import { AppError } from "../../../errors/app-error.js";
import { getLibraryById } from "../models/Library.js";
import { getLibraryFileById } from "../models/LibraryFile.js";
import type { LibraryFilePreview } from "../types/LibraryFileTypes.js";
import {
  maxLibraryTextPreviewBytes,
  supportedLibraryTextExtensions,
} from "./LibraryFilePolicy.js";

function pathIsInsideRoot(rootPath: string, candidatePath: string): boolean {
  const relativePath = path.relative(rootPath, candidatePath);

  return (
    relativePath !== ".." &&
    !relativePath.startsWith(`..${path.sep}`) &&
    !path.isAbsolute(relativePath)
  );
}

function filesystemMessage(error: unknown, fallback: string): string {
  if (error instanceof Error && error.message.trim()) {
    return error.message;
  }

  return fallback;
}

export async function readLibraryFileText(
  libraryId: string,
  fileId: string,
): Promise<LibraryFilePreview> {
  const library = getLibraryById(libraryId);

  if (!library) {
    throw new AppError(404, "Library not found.");
  }

  if (library.archivedAt) {
    throw new AppError(409, "Archived Libraries cannot preview files.");
  }

  const file = getLibraryFileById(libraryId, fileId);

  if (!file) {
    throw new AppError(404, "Library file not found.");
  }

  if (file.status !== "available") {
    throw new AppError(
      409,
      `This file is marked ${file.status}. Rescan the Library before opening it.`,
    );
  }

  if (!supportedLibraryTextExtensions.has(file.extension.toLowerCase())) {
    throw new AppError(415, "This file type is not supported for text preview.");
  }

  let canonicalRootPath: string;

  try {
    canonicalRootPath = await realpath(library.rootPath);
  } catch (error) {
    throw new AppError(404, "The Library folder could not be resolved.", {
      cause: filesystemMessage(error, "Unknown filesystem error."),
    });
  }

  const absolutePath = path.resolve(canonicalRootPath, file.relativePath);

  if (!pathIsInsideRoot(canonicalRootPath, absolutePath)) {
    throw new AppError(400, "The requested file escaped the Library root.");
  }

  let stats;

  try {
    stats = await lstat(absolutePath);
  } catch {
    throw new AppError(
      404,
      "The file no longer exists. Rescan the Library to refresh its catalog.",
    );
  }

  if (stats.isSymbolicLink() || !stats.isFile()) {
    throw new AppError(409, "The cataloged path is no longer a regular file.");
  }

  if (stats.size > maxLibraryTextPreviewBytes) {
    throw new AppError(
      413,
      `This file is too large to read. The current limit is ${Math.round(
        maxLibraryTextPreviewBytes / 1024,
      )} KB.`,
      {
        sizeBytes: stats.size,
        maximumBytes: maxLibraryTextPreviewBytes,
      },
    );
  }

  let canonicalFilePath: string;

  try {
    canonicalFilePath = await realpath(absolutePath);
  } catch {
    throw new AppError(404, "The file could not be resolved.");
  }

  if (!pathIsInsideRoot(canonicalRootPath, canonicalFilePath)) {
    throw new AppError(400, "The requested file resolved outside the Library root.");
  }

  try {
    await access(canonicalFilePath, fsConstants.R_OK);
  } catch {
    throw new AppError(403, "Archivist cannot read this file.");
  }

  let buffer: Buffer;

  try {
    buffer = await readFile(canonicalFilePath);
  } catch (error) {
    throw new AppError(500, "The file could not be read.", {
      cause: filesystemMessage(error, "Unknown filesystem error."),
    });
  }

  if (buffer.includes(0)) {
    throw new AppError(415, "This file appears to be binary.");
  }

  let content: string;

  try {
    content = new TextDecoder("utf-8", { fatal: true }).decode(buffer);
  } catch {
    throw new AppError(415, "This file is not valid UTF-8 text.");
  }

  return {
    file: {
      ...file,
      sizeBytes: stats.size,
      modifiedAt: stats.mtime.toISOString(),
    },
    content,
    encoding: "utf-8",
    lineCount: content.length === 0 ? 0 : content.split(/\r\n|\r|\n/).length,
    readAt: new Date().toISOString(),
  };
}

export async function readLibraryFilePreview(
  libraryId: string,
  fileId: string,
): Promise<LibraryFilePreview> {
  return readLibraryFileText(libraryId, fileId);
}

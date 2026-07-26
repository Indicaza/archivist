import { randomUUID } from "node:crypto";
import { constants as fsConstants } from "node:fs";
import {
  access,
  lstat,
  realpath,
  rename,
  unlink,
  writeFile,
} from "node:fs/promises";
import path from "node:path";
import { database } from "../../../database/database.js";
import { AppError } from "../../../errors/app-error.js";
import { getLibraryById } from "../models/Library.js";
import { getLibraryFileById } from "../models/LibraryFile.js";
import type { LibraryFilePreview } from "../types/LibraryFileTypes.js";
import {
  isSupportedLibraryTextFile,
  maxLibraryTextPreviewBytes,
} from "./LibraryFilePolicy.js";

type WriteLibraryFileInput = {
  content: string;
  expectedModifiedAt: string;
};

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

function filesystemMessage(
  error: unknown,
  fallback: string,
): string {
  if (error instanceof Error && error.message.trim()) {
    return error.message;
  }

  return fallback;
}

export async function writeLibraryFileText(
  libraryId: string,
  fileId: string,
  input: WriteLibraryFileInput,
): Promise<LibraryFilePreview> {
  const library = getLibraryById(libraryId);

  if (!library) {
    throw new AppError(404, "Library not found.");
  }

  if (library.archivedAt) {
    throw new AppError(
      409,
      "Archived Libraries cannot edit files.",
    );
  }

  const file = getLibraryFileById(libraryId, fileId);

  if (!file) {
    throw new AppError(404, "Library file not found.");
  }

  if (file.status !== "available") {
    throw new AppError(
      409,
      `This file is marked ${file.status}. `
        + "Rescan the Library before editing it.",
    );
  }

  if (!isSupportedLibraryTextFile(file.name, file.extension)) {
    throw new AppError(
      415,
      "This file type is not supported for text editing.",
    );
  }

  const contentBytes = Buffer.byteLength(input.content, "utf8");

  if (contentBytes > maxLibraryTextPreviewBytes) {
    throw new AppError(
      413,
      `This file is too large to save. The current limit is ${
        Math.round(maxLibraryTextPreviewBytes / 1024)
      } KB.`,
      {
        sizeBytes: contentBytes,
        maximumBytes: maxLibraryTextPreviewBytes,
      },
    );
  }

  let canonicalRootPath: string;

  try {
    canonicalRootPath = await realpath(library.rootPath);
  } catch (error) {
    throw new AppError(
      404,
      "The Library folder could not be resolved.",
      {
        cause: filesystemMessage(
          error,
          "Unknown filesystem error.",
        ),
      },
    );
  }

  const absolutePath = path.resolve(
    canonicalRootPath,
    file.relativePath,
  );

  if (!pathIsInsideRoot(canonicalRootPath, absolutePath)) {
    throw new AppError(
      400,
      "The requested file escaped the Library root.",
    );
  }

  let stats;

  try {
    stats = await lstat(absolutePath);
  } catch {
    throw new AppError(
      404,
      "The file no longer exists. "
        + "Rescan the Library to refresh its catalog.",
    );
  }

  if (stats.isSymbolicLink() || !stats.isFile()) {
    throw new AppError(
      409,
      "The cataloged path is no longer a regular file.",
    );
  }

  const currentModifiedAt = stats.mtime.toISOString();

  if (currentModifiedAt !== input.expectedModifiedAt) {
    throw new AppError(
      409,
      "This file changed on disk after it was opened.",
      {
        expectedModifiedAt: input.expectedModifiedAt,
        currentModifiedAt,
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
    throw new AppError(
      400,
      "The requested file resolved outside the Library root.",
    );
  }

  try {
    await access(canonicalFilePath, fsConstants.W_OK);
  } catch {
    throw new AppError(403, "Archivist cannot write this file.");
  }

  const temporaryPath = path.join(
    path.dirname(canonicalFilePath),
    `.${path.basename(canonicalFilePath)}.archivist-${
      process.pid
    }-${randomUUID()}.tmp`,
  );

  try {
    await writeFile(temporaryPath, input.content, {
      encoding: "utf8",
      mode: stats.mode,
    });
    await rename(temporaryPath, canonicalFilePath);
  } catch (error) {
    await unlink(temporaryPath).catch(() => undefined);

    throw new AppError(
      500,
      "The file could not be saved.",
      {
        cause: filesystemMessage(
          error,
          "Unknown filesystem error.",
        ),
      },
    );
  }

  const savedStats = await lstat(canonicalFilePath);
  const modifiedAt = savedStats.mtime.toISOString();

  database
    .prepare(
      `
        UPDATE library_files
        SET
          size_bytes = ?,
          modified_at = ?,
          status = 'available',
          updated_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
        WHERE id = ?
          AND library_id = ?
      `,
    )
    .run(
      savedStats.size,
      modifiedAt,
      fileId,
      libraryId,
    );

  const savedFile = getLibraryFileById(libraryId, fileId);

  if (!savedFile) {
    throw new Error(
      "The saved Library file could not be loaded.",
    );
  }

  return {
    file: savedFile,
    content: input.content,
    encoding: "utf-8",
    lineCount:
      input.content.length === 0
        ? 0
        : input.content.split(/\r\n|\r|\n/).length,
    readAt: new Date().toISOString(),
  };
}

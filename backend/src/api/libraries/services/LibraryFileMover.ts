import fs from "node:fs/promises";
import path from "node:path";
import { AppError } from "../../../errors/app-error.js";
import { getLibraryById } from "../models/Library.js";
import {
  getLibraryFileById,
  updateLibraryFileLocation,
} from "../models/LibraryFile.js";

function resolveInsideLibrary(rootPath: string, relativePath: string): string {
  const resolved = path.resolve(rootPath, relativePath);
  const offset = path.relative(rootPath, resolved);

  if (offset.startsWith("..") || path.isAbsolute(offset)) {
    throw new AppError(400, "The destination must stay inside the Library.");
  }

  return resolved;
}

export async function moveLibraryFile(
  libraryId: string,
  fileId: string,
  targetDirectory: string,
) {
  const library = getLibraryById(libraryId);
  const file = getLibraryFileById(libraryId, fileId);

  if (!library) {
    throw new AppError(404, "Library not found.");
  }

  if (!file) {
    throw new AppError(404, "Library file not found.");
  }

  if (file.status !== "available") {
    throw new AppError(409, "Only available files can be moved.");
  }

  const normalizedDirectory = targetDirectory
    .replaceAll("\\", "/")
    .replace(/^\/+|\/+$/g, "");
  const sourcePath = resolveInsideLibrary(library.rootPath, file.relativePath);
  const destinationDirectory = resolveInsideLibrary(
    library.rootPath,
    normalizedDirectory,
  );
  const destinationPath = path.join(destinationDirectory, file.name);

  const directoryStats = await fs.stat(destinationDirectory).catch(() => null);

  if (!directoryStats?.isDirectory()) {
    throw new AppError(400, "The destination folder does not exist.");
  }

  if (path.resolve(sourcePath) === path.resolve(destinationPath)) {
    return file;
  }

  const destinationExists = await fs
    .access(destinationPath)
    .then(() => true)
    .catch(() => false);

  if (destinationExists) {
    throw new AppError(409, `A file named ${file.name} already exists there.`);
  }

  await fs.rename(sourcePath, destinationPath);

  const relativePath = path
    .relative(library.rootPath, destinationPath)
    .split(path.sep)
    .join("/");

  return updateLibraryFileLocation(
    libraryId,
    fileId,
    relativePath,
    path.basename(destinationPath),
    path.extname(destinationPath).toLowerCase(),
  );
}

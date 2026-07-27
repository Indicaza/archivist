import { randomUUID } from "node:crypto";
import { spawn } from "node:child_process";
import {
  copyFile,
  lstat,
  mkdir,
  realpath,
  rename,
  writeFile,
} from "node:fs/promises";
import path from "node:path";
import { database } from "../../../database/database.js";
import { AppError } from "../../../errors/app-error.js";
import { getLibraryById } from "../models/Library.js";
import {
  getLibraryFileById,
  updateLibraryFileLocation,
} from "../models/LibraryFile.js";
import type { LibraryFile } from "../types/LibraryFileTypes.js";

export type CreateLibraryEntryInput = {
  parentDirectory: string;
  name: string;
  kind: "file" | "directory";
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

function catalogPath(value: string): string {
  return value.split(path.sep).join("/");
}

function validatedName(value: string): string {
  const name = value.trim();

  if (
    !name
    || name === "."
    || name === ".."
    || name.includes("/")
    || name.includes("\\")
    || /[\u0000-\u001f]/.test(name)
  ) {
    throw new AppError(400, "Invalid file or folder name.");
  }

  return name;
}

function normalizedRelativePath(value: string): string {
  const normalized = value
    .trim()
    .replaceAll("\\", "/")
    .replace(/^\/+|\/+$/g, "");
  const parts = normalized
    ? normalized.split("/")
    : [];

  if (parts.some((part) => !part || part === "." || part === "..")) {
    throw new AppError(400, "Invalid Library path.");
  }

  return parts.join(path.sep);
}

async function resolveLibraryRoot(libraryId: string): Promise<string> {
  const library = getLibraryById(libraryId);

  if (!library) {
    throw new AppError(404, "Library not found.");
  }

  if (library.archivedAt) {
    throw new AppError(
      409,
      "Archived Libraries cannot modify files.",
    );
  }

  try {
    return await realpath(library.rootPath);
  } catch {
    throw new AppError(
      404,
      "The Library folder could not be resolved.",
    );
  }
}

async function resolveParentDirectory(
  rootPath: string,
  relativeDirectory: string,
): Promise<string> {
  const normalized = normalizedRelativePath(relativeDirectory);
  const requested = path.resolve(rootPath, normalized);

  if (!pathIsInsideRoot(rootPath, requested)) {
    throw new AppError(
      400,
      "The requested directory escaped the Library root.",
    );
  }

  let canonical: string;

  try {
    canonical = await realpath(requested);
    const stats = await lstat(canonical);

    if (!stats.isDirectory() || stats.isSymbolicLink()) {
      throw new Error("Not a directory.");
    }
  } catch {
    throw new AppError(404, "The destination folder does not exist.");
  }

  if (!pathIsInsideRoot(rootPath, canonical)) {
    throw new AppError(
      400,
      "The destination folder resolved outside the Library root.",
    );
  }

  return canonical;
}

async function resolveCatalogedFile(
  libraryId: string,
  fileId: string,
): Promise<{
  file: LibraryFile;
  rootPath: string;
  absolutePath: string;
}> {
  const rootPath = await resolveLibraryRoot(libraryId);
  const file = getLibraryFileById(libraryId, fileId);

  if (!file || file.status !== "available") {
    throw new AppError(404, "Library file not found.");
  }

  const requested = path.resolve(rootPath, file.relativePath);

  if (!pathIsInsideRoot(rootPath, requested)) {
    throw new AppError(
      400,
      "The requested file escaped the Library root.",
    );
  }

  let absolutePath: string;

  try {
    absolutePath = await realpath(requested);
    const stats = await lstat(absolutePath);

    if (!stats.isFile() || stats.isSymbolicLink()) {
      throw new Error("Not a regular file.");
    }
  } catch {
    throw new AppError(404, "The file no longer exists.");
  }

  if (!pathIsInsideRoot(rootPath, absolutePath)) {
    throw new AppError(
      400,
      "The requested file resolved outside the Library root.",
    );
  }

  return {
    file,
    rootPath,
    absolutePath,
  };
}

async function ensureDestinationAvailable(
  destinationPath: string,
): Promise<void> {
  try {
    await lstat(destinationPath);
  } catch {
    return;
  }

  throw new AppError(
    409,
    "A file or folder with that name already exists.",
  );
}

function insertCatalogFile(
  libraryId: string,
  relativePath: string,
  name: string,
  stats: Awaited<ReturnType<typeof lstat>>,
): LibraryFile {
  const fileId = randomUUID();
  const extension = path.extname(name).toLowerCase();

  database
    .prepare(
      `
        INSERT INTO library_files (
          id,
          library_id,
          relative_path,
          name,
          extension,
          size_bytes,
          modified_at,
          status,
          last_seen_at
        )
        VALUES (
          ?,
          ?,
          ?,
          ?,
          ?,
          ?,
          ?,
          'available',
          strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
        )
      `,
    )
    .run(
      fileId,
      libraryId,
      relativePath,
      name,
      extension,
      stats.size,
      stats.mtime.toISOString(),
    );

  const file = getLibraryFileById(libraryId, fileId);

  if (!file) {
    throw new Error("The new Library file could not be loaded.");
  }

  return file;
}

export async function createLibraryEntry(
  libraryId: string,
  input: CreateLibraryEntryInput,
): Promise<{
  kind: "file" | "directory";
  relativePath: string;
  name: string;
  file: LibraryFile | null;
}> {
  const rootPath = await resolveLibraryRoot(libraryId);
  const parentPath = await resolveParentDirectory(
    rootPath,
    input.parentDirectory,
  );
  const name = validatedName(input.name);
  const destinationPath = path.join(parentPath, name);

  if (!pathIsInsideRoot(rootPath, destinationPath)) {
    throw new AppError(
      400,
      "The new entry escaped the Library root.",
    );
  }

  await ensureDestinationAvailable(destinationPath);

  if (input.kind === "directory") {
    await mkdir(destinationPath);

    return {
      kind: "directory",
      relativePath: catalogPath(
        path.relative(rootPath, destinationPath),
      ),
      name,
      file: null,
    };
  }

  await writeFile(destinationPath, "", {
    encoding: "utf8",
    flag: "wx",
  });
  const stats = await lstat(destinationPath);
  const relativePath = catalogPath(
    path.relative(rootPath, destinationPath),
  );

  return {
    kind: "file",
    relativePath,
    name,
    file: insertCatalogFile(
      libraryId,
      relativePath,
      name,
      stats,
    ),
  };
}

export async function renameLibraryFile(
  libraryId: string,
  fileId: string,
  requestedName: string,
): Promise<LibraryFile> {
  const { file, rootPath, absolutePath } =
    await resolveCatalogedFile(libraryId, fileId);
  const name = validatedName(requestedName);
  const destinationPath = path.join(
    path.dirname(absolutePath),
    name,
  );

  if (destinationPath !== absolutePath) {
    await ensureDestinationAvailable(destinationPath);
    await rename(absolutePath, destinationPath);
  }

  const relativePath = catalogPath(
    path.relative(rootPath, destinationPath),
  );

  return updateLibraryFileLocation(
    libraryId,
    fileId,
    relativePath,
    name,
    path.extname(name).toLowerCase(),
  );
}

export async function duplicateLibraryFile(
  libraryId: string,
  fileId: string,
  requestedName: string,
): Promise<LibraryFile> {
  const { file, rootPath, absolutePath } =
    await resolveCatalogedFile(libraryId, fileId);
  const name = validatedName(requestedName);
  const destinationPath = path.join(
    path.dirname(absolutePath),
    name,
  );

  await ensureDestinationAvailable(destinationPath);
  await copyFile(absolutePath, destinationPath);
  const stats = await lstat(destinationPath);
  const relativePath = catalogPath(
    path.relative(rootPath, destinationPath),
  );

  return insertCatalogFile(
    libraryId,
    relativePath,
    name,
    stats,
  );
}

export async function revealLibraryEntry(
  libraryId: string,
  relativePathValue: string,
): Promise<void> {
  const rootPath = await resolveLibraryRoot(libraryId);
  const relativePath = normalizedRelativePath(
    relativePathValue,
  );
  const requestedPath = path.resolve(rootPath, relativePath);

  if (!pathIsInsideRoot(rootPath, requestedPath)) {
    throw new AppError(
      400,
      "The requested entry escaped the Library root.",
    );
  }

  let targetPath: string;

  try {
    targetPath = await realpath(requestedPath);
  } catch {
    throw new AppError(404, "The requested entry no longer exists.");
  }

  if (!pathIsInsideRoot(rootPath, targetPath)) {
    throw new AppError(
      400,
      "The requested entry resolved outside the Library root.",
    );
  }

  let command: string;
  let args: string[];

  if (process.platform === "darwin") {
    command = "open";
    args = ["-R", targetPath];
  } else if (process.platform === "win32") {
    command = "explorer.exe";
    args = ["/select,", targetPath];
  } else {
    command = "xdg-open";
    args = [
      (await lstat(targetPath)).isDirectory()
        ? targetPath
        : path.dirname(targetPath),
    ];
  }

  const child = spawn(command, args, {
    detached: true,
    stdio: "ignore",
  });
  child.unref();
}

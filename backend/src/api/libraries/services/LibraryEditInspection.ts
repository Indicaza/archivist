import { createHash } from "node:crypto";
import { constants as fsConstants } from "node:fs";
import { access, lstat, readdir, readFile, realpath } from "node:fs/promises";
import path from "node:path";
import { TextDecoder } from "node:util";
import { AppError } from "../../../errors/app-error.js";
import { getLibraryById } from "../models/Library.js";
import {
  isCatalogableLibraryDirectory,
  isCatalogableLibraryFile,
  isSupportedLibraryTextFile,
  maxLibraryTextPreviewBytes,
} from "./LibraryFilePolicy.js";

export type LibraryCreateTargetInspection = {
  relativePath: string;
  parentRelativePath: string;
  name: string;
  expectedState: "absent";
};

export type LibraryDirectoryCreateTargetInspection = {
  relativePath: string;
  parentRelativePath: string;
  name: string;
  expectedState: "absent";
};

export type LibraryTextEditInspection = {
  relativePath: string;
  content: string;
  sha256: string;
  sizeBytes: number;
  modifiedAt: string;
};

export type LibraryEmptyDirectoryInspection = {
  relativePath: string;
  name: string;
  expectedState: "empty-directory";
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

function normalizeRelativePath(value: string): string {
  const trimmed = value.trim();

  if (
    !trimmed
    || path.posix.isAbsolute(trimmed)
    || path.win32.isAbsolute(trimmed)
    || /[\u0000-\u001f]/.test(trimmed)
  ) {
    throw new AppError(
      400,
      "AI edit paths must be safe Library-relative paths.",
    );
  }

  const normalized = trimmed.replaceAll("\\", "/");
  const parts = normalized.split("/");

  if (parts.some((part) => !part || part === "." || part === "..")) {
    throw new AppError(
      400,
      "AI edit paths must be safe Library-relative paths.",
    );
  }

  return parts.join("/");
}

async function requireCanonicalLibraryRoot(
  libraryId: string,
): Promise<string> {
  const library = getLibraryById(libraryId);

  if (!library) {
    throw new AppError(404, "Library not found.");
  }

  if (library.archivedAt) {
    throw new AppError(
      409,
      "Archived Libraries cannot accept AI edit proposals.",
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

async function requireCanonicalParent(
  rootPath: string,
  relativePath: string,
): Promise<string> {
  const parentRelativePath = path.posix.dirname(relativePath);
  const normalizedParent =
    parentRelativePath === "." ? "" : parentRelativePath;
  const requestedParent = path.resolve(
    rootPath,
    ...normalizedParent.split("/").filter(Boolean),
  );

  if (!pathIsInsideRoot(rootPath, requestedParent)) {
    throw new AppError(
      400,
      "The requested edit path escaped the Library root.",
    );
  }

  let canonicalParent: string;

  try {
    canonicalParent = await realpath(requestedParent);
  } catch {
    throw new AppError(
      404,
      "The destination folder does not exist.",
    );
  }

  if (!pathIsInsideRoot(rootPath, canonicalParent)) {
    throw new AppError(
      400,
      "The destination folder resolved outside the Library root.",
    );
  }

  const stats = await lstat(canonicalParent).catch(() => null);

  if (!stats?.isDirectory()) {
    throw new AppError(
      409,
      "The destination parent is not a directory.",
    );
  }

  try {
    await access(canonicalParent, fsConstants.W_OK);
  } catch {
    throw new AppError(
      403,
      "Archivist cannot write to the destination folder.",
    );
  }

  return canonicalParent;
}

export async function inspectLibraryCreateTarget(
  libraryId: string,
  requestedRelativePath: string,
): Promise<LibraryCreateTargetInspection> {
  const relativePath = normalizeRelativePath(requestedRelativePath);
  const rootPath = await requireCanonicalLibraryRoot(libraryId);
  const canonicalParent = await requireCanonicalParent(
    rootPath,
    relativePath,
  );
  const name = path.posix.basename(relativePath);
  const extension = path.posix.extname(name).toLowerCase();

  if (
    !isCatalogableLibraryFile(name)
    || !isSupportedLibraryTextFile(name, extension)
  ) {
    throw new AppError(
      415,
      "This file type is not supported for AI text creation.",
    );
  }

  const destinationPath = path.join(canonicalParent, name);

  if (!pathIsInsideRoot(rootPath, destinationPath)) {
    throw new AppError(
      400,
      "The requested edit path escaped the Library root.",
    );
  }

  const existing = await lstat(destinationPath).catch(() => null);

  if (existing) {
    throw new AppError(
      409,
      "The proposed create target already exists.",
    );
  }

  const parentRelativePath = path.posix.dirname(relativePath);

  return {
    relativePath,
    parentRelativePath:
      parentRelativePath === "." ? "" : parentRelativePath,
    name,
    expectedState: "absent",
  };
}

export async function inspectLibraryDirectoryCreateTarget(
  libraryId: string,
  requestedRelativePath: string,
): Promise<LibraryDirectoryCreateTargetInspection> {
  const relativePath = normalizeRelativePath(requestedRelativePath);
  const rootPath = await requireCanonicalLibraryRoot(libraryId);
  const canonicalParent = await requireCanonicalParent(
    rootPath,
    relativePath,
  );
  const name = path.posix.basename(relativePath);

  if (!isCatalogableLibraryDirectory(name)) {
    throw new AppError(
      415,
      "This folder name is not supported for AI directory creation.",
    );
  }

  const destinationPath = path.join(canonicalParent, name);

  if (!pathIsInsideRoot(rootPath, destinationPath)) {
    throw new AppError(
      400,
      "The requested edit path escaped the Library root.",
    );
  }

  const existing = await lstat(destinationPath).catch(() => null);

  if (existing) {
    throw new AppError(
      409,
      "The proposed directory target already exists.",
    );
  }

  const parentRelativePath = path.posix.dirname(relativePath);

  return {
    relativePath,
    parentRelativePath:
      parentRelativePath === "." ? "" : parentRelativePath,
    name,
    expectedState: "absent",
  };
}

export async function inspectLibraryEmptyDirectoryForEdit(
  libraryId: string,
  requestedRelativePath: string,
): Promise<LibraryEmptyDirectoryInspection> {
  const relativePath = normalizeRelativePath(requestedRelativePath);
  const rootPath = await requireCanonicalLibraryRoot(libraryId);
  const canonicalParent = await requireCanonicalParent(
    rootPath,
    relativePath,
  );
  const name = path.posix.basename(relativePath);
  const requestedPath = path.join(canonicalParent, name);

  if (!pathIsInsideRoot(rootPath, requestedPath)) {
    throw new AppError(
      400,
      "The requested edit path escaped the Library root.",
    );
  }

  const stats = await lstat(requestedPath).catch(() => null);

  if (!stats) {
    throw new AppError(
      404,
      "The AI edit directory no longer exists.",
    );
  }

  if (stats.isSymbolicLink()) {
    throw new AppError(
      409,
      "AI edits do not follow symbolic-link directories.",
    );
  }

  if (!stats.isDirectory()) {
    throw new AppError(
      409,
      "The AI edit directory is no longer a directory.",
    );
  }

  let canonicalDirectory: string;

  try {
    canonicalDirectory = await realpath(requestedPath);
  } catch {
    throw new AppError(
      404,
      "The AI edit directory could not be resolved.",
    );
  }

  if (!pathIsInsideRoot(rootPath, canonicalDirectory)) {
    throw new AppError(
      400,
      "The AI edit directory resolved outside the Library root.",
    );
  }

  let entries: string[];

  try {
    entries = await readdir(canonicalDirectory);
  } catch {
    throw new AppError(
      403,
      "Archivist cannot inspect the AI edit directory.",
    );
  }

  if (entries.length > 0) {
    throw new AppError(
      409,
      "The AI edit directory is no longer empty.",
    );
  }

  return {
    relativePath,
    name,
    expectedState: "empty-directory",
  };
}

export async function inspectLibraryTextFileForEdit(
  libraryId: string,
  requestedRelativePath: string,
): Promise<LibraryTextEditInspection> {
  const relativePath = normalizeRelativePath(requestedRelativePath);
  const rootPath = await requireCanonicalLibraryRoot(libraryId);
  const requestedPath = path.resolve(
    rootPath,
    ...relativePath.split("/"),
  );

  if (!pathIsInsideRoot(rootPath, requestedPath)) {
    throw new AppError(
      400,
      "The requested edit path escaped the Library root.",
    );
  }

  const requestedStats = await lstat(requestedPath).catch(() => null);

  if (!requestedStats) {
    throw new AppError(
      404,
      "The proposed edit source does not exist.",
    );
  }

  if (requestedStats.isSymbolicLink()) {
    throw new AppError(
      409,
      "AI edits do not follow symbolic-link files.",
    );
  }

  if (!requestedStats.isFile()) {
    throw new AppError(
      409,
      "The proposed edit source is not a regular file.",
    );
  }

  if (
    !isSupportedLibraryTextFile(
      path.basename(relativePath),
      path.extname(relativePath).toLowerCase(),
    )
  ) {
    throw new AppError(
      415,
      "This file type is not supported for AI text edits.",
    );
  }

  if (requestedStats.size > maxLibraryTextPreviewBytes) {
    throw new AppError(
      413,
      "This file exceeds the current safe AI text-edit limit.",
      {
        sizeBytes: requestedStats.size,
        maximumBytes: maxLibraryTextPreviewBytes,
      },
    );
  }

  let canonicalFilePath: string;

  try {
    canonicalFilePath = await realpath(requestedPath);
  } catch {
    throw new AppError(
      404,
      "The proposed edit source could not be resolved.",
    );
  }

  if (!pathIsInsideRoot(rootPath, canonicalFilePath)) {
    throw new AppError(
      400,
      "The proposed edit source resolved outside the Library root.",
    );
  }

  try {
    await access(canonicalFilePath, fsConstants.R_OK);
  } catch {
    throw new AppError(
      403,
      "Archivist cannot read the proposed edit source.",
    );
  }

  const buffer = await readFile(canonicalFilePath);

  if (buffer.includes(0)) {
    throw new AppError(
      415,
      "The proposed edit source appears to be binary.",
    );
  }

  let content: string;

  try {
    content = new TextDecoder("utf-8", {
      fatal: true,
    }).decode(buffer);
  } catch {
    throw new AppError(
      415,
      "The proposed edit source is not valid UTF-8 text.",
    );
  }

  return {
    relativePath,
    content,
    sha256: createHash("sha256").update(buffer).digest("hex"),
    sizeBytes: buffer.byteLength,
    modifiedAt: requestedStats.mtime.toISOString(),
  };
}

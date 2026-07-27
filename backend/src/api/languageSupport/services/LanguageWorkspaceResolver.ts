import fs from "node:fs";
import path from "node:path";
import { AppError } from "../../../errors/app-error.js";
import { getLibraryById } from "../../libraries/models/Library.js";
import type {
  LanguageServerDefinition,
  ResolvedLanguageWorkspace,
} from "../types/LanguageSupportTypes.js";

function realDirectory(
  candidatePath: string,
  errorMessage: string,
): string {
  let resolved: string;

  try {
    resolved = fs.realpathSync.native(
      path.resolve(candidatePath),
    );
  } catch {
    throw new AppError(404, errorMessage);
  }

  try {
    if (!fs.statSync(resolved).isDirectory()) {
      throw new AppError(409, errorMessage);
    }
  } catch (error) {
    if (error instanceof AppError) {
      throw error;
    }

    throw new AppError(404, errorMessage);
  }

  return resolved;
}

function isWithin(
  parentPath: string,
  childPath: string,
): boolean {
  const relative = path.relative(parentPath, childPath);

  return (
    relative === ""
    || (
      !relative.startsWith(`..${path.sep}`)
      && relative !== ".."
      && !path.isAbsolute(relative)
    )
  );
}

function resolveFilePath(
  libraryRoot: string,
  requestedFilePath: string | undefined,
): string | null {
  if (!requestedFilePath) {
    return null;
  }

  let resolved: string;

  try {
    resolved = fs.realpathSync.native(
      path.resolve(requestedFilePath),
    );
  } catch {
    throw new AppError(
      404,
      "The language-support file could not be resolved.",
    );
  }

  if (!isWithin(libraryRoot, resolved)) {
    throw new AppError(
      403,
      "The language-support file is outside the Library.",
    );
  }

  try {
    if (!fs.statSync(resolved).isFile()) {
      throw new AppError(
        409,
        "The language-support target is not a file.",
      );
    }
  } catch (error) {
    if (error instanceof AppError) {
      throw error;
    }

    throw new AppError(
      404,
      "The language-support file could not be resolved.",
    );
  }

  return resolved;
}

function containsRootMarker(
  directory: string,
  markers: readonly string[],
): boolean {
  return markers.some((marker) =>
    fs.existsSync(path.join(directory, marker))
  );
}

function findWorkspaceRoot(
  startDirectory: string,
  boundary: string,
  markers: readonly string[],
): string {
  let current = startDirectory;

  while (isWithin(boundary, current)) {
    if (containsRootMarker(current, markers)) {
      return current;
    }

    if (current === boundary) {
      break;
    }

    const parent = path.dirname(current);

    if (parent === current) {
      break;
    }

    current = parent;
  }

  return boundary;
}

export function resolveLanguageWorkspace(
  definition: LanguageServerDefinition,
  input: {
    libraryId: string;
    workspaceRoot?: string;
    filePath?: string;
  },
): ResolvedLanguageWorkspace {
  const library = getLibraryById(input.libraryId);

  if (!library) {
    throw new AppError(404, "Library not found.");
  }

  if (library.archivedAt) {
    throw new AppError(
      409,
      "Archived Libraries cannot start language servers.",
    );
  }

  const libraryRoot = realDirectory(
    library.rootPath,
    "The Library root could not be resolved.",
  );
  const requestedRoot = input.workspaceRoot
    ? realDirectory(
        input.workspaceRoot,
        "The requested workspace root could not be resolved.",
      )
    : libraryRoot;

  if (!isWithin(libraryRoot, requestedRoot)) {
    throw new AppError(
      403,
      "The requested workspace is outside the Library.",
    );
  }

  const filePath = resolveFilePath(
    libraryRoot,
    input.filePath,
  );

  if (
    filePath
    && !isWithin(requestedRoot, filePath)
  ) {
    throw new AppError(
      403,
      "The requested file is outside the workspace.",
    );
  }

  const startDirectory = filePath
    ? path.dirname(filePath)
    : requestedRoot;
  const workspaceRoot = findWorkspaceRoot(
    startDirectory,
    requestedRoot,
    definition.rootMarkers,
  );

  return {
    libraryId: library.id,
    libraryRoot,
    workspaceRoot,
    filePath,
  };
}

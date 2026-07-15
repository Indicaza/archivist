import fs from "node:fs";
import path from "node:path";
import { database } from "../../../database/database.js";
import { AppError } from "../../../errors/app-error.js";
import type {
  ArchiveLibraryResult,
  CreateLibraryInput,
  CreateLibraryResult,
  Library,
  UpdateLibraryInput,
} from "../types/LibraryTypes.js";

type LibraryRow = {
  id: string;
  name: string;
  description: string | null;
  root_path: string;
  archived_at: string | null;
  created_at: string;
  updated_at: string;
};

type AppSettingsRow = {
  selected_library_id: string | null;
};

function mapLibrary(row: LibraryRow): Library {
  return {
    id: row.id,
    name: row.name,
    description: row.description,
    rootPath: row.root_path,
    archivedAt: row.archived_at,
    createdAt: row.created_at,
    updatedAt: row.updated_at,
  };
}

function normalizeRootPath(rootPath: string): string {
  const resolvedPath = path.resolve(rootPath);

  if (!fs.existsSync(resolvedPath)) {
    throw new AppError(400, "The selected folder does not exist.");
  }

  const stats = fs.statSync(resolvedPath);

  if (!stats.isDirectory()) {
    throw new AppError(400, "The selected path is not a folder.");
  }

  try {
    fs.accessSync(resolvedPath, fs.constants.R_OK);
  } catch {
    throw new AppError(400, "Archivist cannot read the selected folder.");
  }

  return fs.realpathSync.native(resolvedPath);
}

function deriveLibraryName(rootPath: string): string {
  return path.basename(rootPath).trim() || "New Library";
}

function getSelectedLibraryId(): string | null {
  const row = database
    .prepare(
      `
        SELECT selected_library_id
        FROM app_settings
        WHERE id = 1
      `,
    )
    .get() as AppSettingsRow;

  return row.selected_library_id;
}

function setSelectedLibraryId(libraryId: string | null): void {
  database
    .prepare(
      `
        UPDATE app_settings
        SET
          selected_library_id = ?,
          updated_at = strftime(
            '%Y-%m-%dT%H:%M:%fZ',
            'now'
          )
        WHERE id = 1
      `,
    )
    .run(libraryId);
}

function findFallbackLibraryId(excludedLibraryId: string): string | null {
  const row = database
    .prepare(
      `
        SELECT id
        FROM libraries
        WHERE archived_at IS NULL
          AND id != ?
        ORDER BY updated_at DESC, created_at DESC
        LIMIT 1
      `,
    )
    .get(excludedLibraryId) as { id: string } | undefined;

  return row?.id ?? null;
}

export function getAllLibraries(): Library[] {
  const rows = database
    .prepare(
      `
        SELECT
          id,
          name,
          description,
          root_path,
          archived_at,
          created_at,
          updated_at
        FROM libraries
        WHERE archived_at IS NULL
        ORDER BY updated_at DESC, created_at DESC
      `,
    )
    .all() as LibraryRow[];

  return rows.map(mapLibrary);
}

export function getArchivedLibraries(): Library[] {
  const rows = database
    .prepare(
      `
        SELECT
          id,
          name,
          description,
          root_path,
          archived_at,
          created_at,
          updated_at
        FROM libraries
        WHERE archived_at IS NOT NULL
        ORDER BY archived_at DESC
      `,
    )
    .all() as LibraryRow[];

  return rows.map(mapLibrary);
}

export function getLibraryById(libraryId: string): Library | null {
  const row = database
    .prepare(
      `
        SELECT
          id,
          name,
          description,
          root_path,
          archived_at,
          created_at,
          updated_at
        FROM libraries
        WHERE id = ?
      `,
    )
    .get(libraryId) as LibraryRow | undefined;

  return row ? mapLibrary(row) : null;
}

export function createLibrary(
  input: CreateLibraryInput,
): CreateLibraryResult {
  const rootPath = normalizeRootPath(input.rootPath);

  const existingLibrary = database
    .prepare(
      `
        SELECT
          id,
          name,
          description,
          root_path,
          archived_at,
          created_at,
          updated_at
        FROM libraries
        WHERE root_path = ?
      `,
    )
    .get(rootPath) as LibraryRow | undefined;

  if (existingLibrary) {
    if (existingLibrary.archived_at) {
      throw new AppError(
        409,
        "This folder belongs to an archived Library. Restore it instead.",
        {
          code: "LIBRARY_ARCHIVED",
          libraryId: existingLibrary.id,
        },
      );
    }

    throw new AppError(
      409,
      "This folder is already registered as a Library.",
      {
        code: "LIBRARY_EXISTS",
        libraryId: existingLibrary.id,
      },
    );
  }

  const libraryId = crypto.randomUUID();
  const mainChatId = crypto.randomUUID();
  const name = input.name?.trim() || deriveLibraryName(rootPath);
  const description = input.description?.trim() || null;

  const createTransaction = database.transaction(() => {
    database
      .prepare(
        `
          INSERT INTO libraries (
            id,
            name,
            description,
            root_path
          )
          VALUES (?, ?, ?, ?)
        `,
      )
      .run(libraryId, name, description, rootPath);

    database
      .prepare(
        `
          INSERT INTO chats (
            id,
            library_id,
            title,
            type
          )
          VALUES (?, ?, ?, 'main')
        `,
      )
      .run(mainChatId, libraryId, "Main Timeline");

    if (getSelectedLibraryId() === null) {
      setSelectedLibraryId(libraryId);
    }

    const library = getLibraryById(libraryId);

    if (!library) {
      throw new Error("The newly created Library could not be loaded.");
    }

    return {
      library,
      selectedLibraryId: getSelectedLibraryId(),
    };
  });

  return createTransaction();
}

export function updateLibrary(
  libraryId: string,
  input: UpdateLibraryInput,
): Library {
  const currentLibrary = getLibraryById(libraryId);

  if (!currentLibrary) {
    throw new AppError(404, "Library not found.");
  }

  const name =
    input.name === undefined
      ? currentLibrary.name
      : input.name.trim();

  const description =
    input.description === undefined
      ? currentLibrary.description
      : input.description?.trim() || null;

  database
    .prepare(
      `
        UPDATE libraries
        SET
          name = ?,
          description = ?,
          updated_at = strftime(
            '%Y-%m-%dT%H:%M:%fZ',
            'now'
          )
        WHERE id = ?
      `,
    )
    .run(name, description, libraryId);

  const updatedLibrary = getLibraryById(libraryId);

  if (!updatedLibrary) {
    throw new Error("The updated Library could not be loaded.");
  }

  return updatedLibrary;
}

export function archiveLibrary(
  libraryId: string,
): ArchiveLibraryResult {
  const library = getLibraryById(libraryId);

  if (!library) {
    throw new AppError(404, "Library not found.");
  }

  if (library.archivedAt) {
    throw new AppError(409, "Library is already archived.");
  }

  const archiveTransaction = database.transaction(() => {
    database
      .prepare(
        `
          UPDATE libraries
          SET
            archived_at = strftime(
              '%Y-%m-%dT%H:%M:%fZ',
              'now'
            ),
            updated_at = strftime(
              '%Y-%m-%dT%H:%M:%fZ',
              'now'
            )
          WHERE id = ?
        `,
      )
      .run(libraryId);

    let selectedLibraryId = getSelectedLibraryId();

    if (selectedLibraryId === libraryId) {
      selectedLibraryId = findFallbackLibraryId(libraryId);
      setSelectedLibraryId(selectedLibraryId);
    }

    const archivedLibrary = getLibraryById(libraryId);

    if (!archivedLibrary) {
      throw new Error("The archived Library could not be loaded.");
    }

    return {
      library: archivedLibrary,
      selectedLibraryId,
    };
  });

  return archiveTransaction();
}

export function restoreLibrary(libraryId: string): Library {
  const library = getLibraryById(libraryId);

  if (!library) {
    throw new AppError(404, "Library not found.");
  }

  if (!library.archivedAt) {
    throw new AppError(409, "Library is already active.");
  }

  database
    .prepare(
      `
        UPDATE libraries
        SET
          archived_at = NULL,
          updated_at = strftime(
            '%Y-%m-%dT%H:%M:%fZ',
            'now'
          )
        WHERE id = ?
      `,
    )
    .run(libraryId);

  const restoredLibrary = getLibraryById(libraryId);

  if (!restoredLibrary) {
    throw new Error("The restored Library could not be loaded.");
  }

  return restoredLibrary;
}

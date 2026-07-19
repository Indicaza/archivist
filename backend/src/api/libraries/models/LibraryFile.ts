import { randomUUID } from "node:crypto";
import { database } from "../../../database/database.js";
import { AppError } from "../../../errors/app-error.js";
import type {
  LibraryFile,
  LibraryFileCatalog,
  LibraryFileStatus,
  LibraryScan,
  LibraryScanStatus,
  ScannedLibraryFile,
} from "../types/LibraryFileTypes.js";

type LibraryFileRow = {
  id: string;
  library_id: string;
  relative_path: string;
  name: string;
  extension: string;
  size_bytes: number;
  modified_at: string;
  status: LibraryFileStatus;
  last_seen_at: string | null;
  created_at: string;
  updated_at: string;
};

type LibraryScanRow = {
  id: string;
  library_id: string;
  status: LibraryScanStatus;
  started_at: string;
  completed_at: string | null;
  discovered_file_count: number;
  cataloged_file_count: number;
  ignored_entry_count: number;
  error_count: number;
  error_message: string | null;
};

type CompleteLibraryScanInput = {
  scanId: string;
  libraryId: string;
  files: ScannedLibraryFile[];
  discoveredFileCount: number;
  ignoredEntryCount: number;
  errorCount: number;
};

function mapLibraryFile(row: LibraryFileRow): LibraryFile {
  return {
    id: row.id,
    libraryId: row.library_id,
    relativePath: row.relative_path,
    name: row.name,
    extension: row.extension,
    sizeBytes: row.size_bytes,
    modifiedAt: row.modified_at,
    status: row.status,
    lastSeenAt: row.last_seen_at,
    createdAt: row.created_at,
    updatedAt: row.updated_at,
  };
}

function mapLibraryScan(row: LibraryScanRow): LibraryScan {
  return {
    id: row.id,
    libraryId: row.library_id,
    status: row.status,
    startedAt: row.started_at,
    completedAt: row.completed_at,
    discoveredFileCount: row.discovered_file_count,
    catalogedFileCount: row.cataloged_file_count,
    ignoredEntryCount: row.ignored_entry_count,
    errorCount: row.error_count,
    errorMessage: row.error_message,
  };
}

function getLibraryScanById(scanId: string): LibraryScan | null {
  const row = database
    .prepare(
      `
        SELECT
          id,
          library_id,
          status,
          started_at,
          completed_at,
          discovered_file_count,
          cataloged_file_count,
          ignored_entry_count,
          error_count,
          error_message
        FROM library_scans
        WHERE id = ?
      `,
    )
    .get(scanId) as LibraryScanRow | undefined;

  return row ? mapLibraryScan(row) : null;
}

export function getLatestLibraryScan(libraryId: string): LibraryScan | null {
  const row = database
    .prepare(
      `
        SELECT
          id,
          library_id,
          status,
          started_at,
          completed_at,
          discovered_file_count,
          cataloged_file_count,
          ignored_entry_count,
          error_count,
          error_message
        FROM library_scans
        WHERE library_id = ?
        ORDER BY started_at DESC, rowid DESC
        LIMIT 1
      `,
    )
    .get(libraryId) as LibraryScanRow | undefined;

  return row ? mapLibraryScan(row) : null;
}

export function getLibraryFileCatalog(libraryId: string): LibraryFileCatalog {
  const rows = database
    .prepare(
      `
        SELECT
          id,
          library_id,
          relative_path,
          name,
          extension,
          size_bytes,
          modified_at,
          status,
          last_seen_at,
          created_at,
          updated_at
        FROM library_files
        WHERE library_id = ?
        ORDER BY relative_path COLLATE NOCASE ASC
      `,
    )
    .all(libraryId) as LibraryFileRow[];

  return {
    files: rows.map(mapLibraryFile),
    latestScan: getLatestLibraryScan(libraryId),
  };
}

export function createLibraryScan(libraryId: string): LibraryScan {
  const runningScan = database
    .prepare(
      `
        SELECT id
        FROM library_scans
        WHERE library_id = ?
          AND status = 'running'
        LIMIT 1
      `,
    )
    .get(libraryId) as { id: string } | undefined;

  if (runningScan) {
    throw new AppError(409, "This Library is already being scanned.");
  }

  const scanId = randomUUID();

  database
    .prepare(
      `
        INSERT INTO library_scans (
          id,
          library_id,
          status
        )
        VALUES (?, ?, 'running')
      `,
    )
    .run(scanId, libraryId);

  const scan = getLibraryScanById(scanId);

  if (!scan) {
    throw new Error("The new Library scan could not be loaded.");
  }

  return scan;
}

export function completeLibraryScan(
  input: CompleteLibraryScanInput,
): LibraryScan {
  const upsertFile = database.prepare(
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
        last_seen_scan_id,
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
        ?,
        ?,
        strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
      )
      ON CONFLICT (library_id, relative_path)
      DO UPDATE SET
        name = excluded.name,
        extension = excluded.extension,
        size_bytes = excluded.size_bytes,
        modified_at = excluded.modified_at,
        status = excluded.status,
        last_seen_scan_id = excluded.last_seen_scan_id,
        last_seen_at = excluded.last_seen_at,
        updated_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
    `,
  );

  const markMissing = database.prepare(
    `
      UPDATE library_files
      SET
        status = 'missing',
        updated_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
      WHERE library_id = ?
        AND (
          last_seen_scan_id IS NULL
          OR last_seen_scan_id != ?
        )
        AND status != 'missing'
    `,
  );

  const updateScan = database.prepare(
    `
      UPDATE library_scans
      SET
        status = ?,
        completed_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now'),
        discovered_file_count = ?,
        cataloged_file_count = ?,
        ignored_entry_count = ?,
        error_count = ?,
        error_message = ?
      WHERE id = ?
        AND library_id = ?
        AND status = 'running'
    `,
  );

  const completeTransaction = database.transaction(() => {
    for (const file of input.files) {
      upsertFile.run(
        randomUUID(),
        input.libraryId,
        file.relativePath,
        file.name,
        file.extension,
        file.sizeBytes,
        file.modifiedAt,
        file.status,
        input.scanId,
      );
    }

    if (input.errorCount === 0) {
      markMissing.run(input.libraryId, input.scanId);
    }

    const status: LibraryScanStatus =
      input.errorCount > 0 ? "partial" : "complete";

    const errorMessage =
      input.errorCount > 0
        ? `The scan completed with ${input.errorCount} issue${
            input.errorCount === 1 ? "" : "s"
          }.`
        : null;

    const result = updateScan.run(
      status,
      input.discoveredFileCount,
      input.files.length,
      input.ignoredEntryCount,
      input.errorCount,
      errorMessage,
      input.scanId,
      input.libraryId,
    );

    if (result.changes !== 1) {
      throw new Error("The Library scan could not be completed.");
    }
  });

  completeTransaction();

  const scan = getLibraryScanById(input.scanId);

  if (!scan) {
    throw new Error("The completed Library scan could not be loaded.");
  }

  return scan;
}

export function failLibraryScan(
  scanId: string,
  libraryId: string,
  errorMessage: string,
): LibraryScan {
  const result = database
    .prepare(
      `
        UPDATE library_scans
        SET
          status = 'failed',
          completed_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now'),
          error_count = 1,
          error_message = ?
        WHERE id = ?
          AND library_id = ?
          AND status = 'running'
      `,
    )
    .run(errorMessage, scanId, libraryId);

  if (result.changes !== 1) {
    throw new Error("The failed Library scan could not be recorded.");
  }

  const scan = getLibraryScanById(scanId);

  if (!scan) {
    throw new Error("The failed Library scan could not be loaded.");
  }

  return scan;
}

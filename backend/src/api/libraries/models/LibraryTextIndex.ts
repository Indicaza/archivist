import { database } from "../../../database/database.js";
import type {
  LibraryTextDocument,
  MarkLibraryTextDocumentInput,
  ReplaceLibraryTextDocumentInput,
} from "../types/LibraryTextIndexTypes.js";

type LibraryTextDocumentRow = {
  library_file_id: string;
  library_id: string;
  status: LibraryTextDocument["status"];
  content_hash: string | null;
  source_modified_at: string;
  source_size_bytes: number;
  chunk_count: number;
  error_message: string | null;
  extracted_at: string;
  updated_at: string;
};

function mapLibraryTextDocument(
  row: LibraryTextDocumentRow,
): LibraryTextDocument {
  return {
    libraryFileId: row.library_file_id,
    libraryId: row.library_id,
    status: row.status,
    contentHash: row.content_hash,
    sourceModifiedAt: row.source_modified_at,
    sourceSizeBytes: row.source_size_bytes,
    chunkCount: row.chunk_count,
    errorMessage: row.error_message,
    extractedAt: row.extracted_at,
    updatedAt: row.updated_at,
  };
}

export function getLibraryTextDocument(
  libraryFileId: string,
): LibraryTextDocument | null {
  const row = database
    .prepare(
      `
        SELECT
          library_file_id,
          library_id,
          status,
          content_hash,
          source_modified_at,
          source_size_bytes,
          chunk_count,
          error_message,
          extracted_at,
          updated_at
        FROM library_documents
        WHERE library_file_id = ?
      `,
    )
    .get(libraryFileId) as LibraryTextDocumentRow | undefined;

  return row ? mapLibraryTextDocument(row) : null;
}

export function touchLibraryTextDocument(input: {
  libraryFileId: string;
  sourceModifiedAt: string;
  sourceSizeBytes: number;
}): void {
  database
    .prepare(
      `
        UPDATE library_documents
        SET
          source_modified_at = ?,
          source_size_bytes = ?,
          extracted_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now'),
          updated_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
        WHERE library_file_id = ?
      `,
    )
    .run(
      input.sourceModifiedAt,
      input.sourceSizeBytes,
      input.libraryFileId,
    );
}

export function replaceLibraryTextDocument(
  input: ReplaceLibraryTextDocumentInput,
): void {
  const upsertDocument = database.prepare(
    `
      INSERT INTO library_documents (
        library_file_id,
        library_id,
        status,
        content_hash,
        source_modified_at,
        source_size_bytes,
        chunk_count,
        error_message,
        extracted_at,
        updated_at
      )
      VALUES (
        ?,
        ?,
        ?,
        ?,
        ?,
        ?,
        ?,
        NULL,
        strftime('%Y-%m-%dT%H:%M:%fZ', 'now'),
        strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
      )
      ON CONFLICT (library_file_id)
      DO UPDATE SET
        library_id = excluded.library_id,
        status = excluded.status,
        content_hash = excluded.content_hash,
        source_modified_at = excluded.source_modified_at,
        source_size_bytes = excluded.source_size_bytes,
        chunk_count = excluded.chunk_count,
        error_message = NULL,
        extracted_at = excluded.extracted_at,
        updated_at = excluded.updated_at
    `,
  );

  const deleteChunks = database.prepare(
    `
      DELETE FROM library_chunks
      WHERE library_file_id = ?
    `,
  );

  const insertChunk = database.prepare(
    `
      INSERT INTO library_chunks (
        id,
        library_file_id,
        library_id,
        ordinal,
        start_line,
        end_line,
        content,
        estimated_tokens,
        content_hash
      )
      VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
    `,
  );

  const replaceTransaction = database.transaction(() => {
    upsertDocument.run(
      input.libraryFileId,
      input.libraryId,
      input.status,
      input.contentHash,
      input.sourceModifiedAt,
      input.sourceSizeBytes,
      input.chunks.length,
    );

    deleteChunks.run(input.libraryFileId);

    for (const chunk of input.chunks) {
      insertChunk.run(
        chunk.id,
        chunk.libraryFileId,
        chunk.libraryId,
        chunk.ordinal,
        chunk.startLine,
        chunk.endLine,
        chunk.content,
        chunk.estimatedTokens,
        chunk.contentHash,
      );
    }
  });

  replaceTransaction();
}

export function markLibraryTextDocument(
  input: MarkLibraryTextDocumentInput,
): void {
  const markTransaction = database.transaction(() => {
    database
      .prepare(
        `
          INSERT INTO library_documents (
            library_file_id,
            library_id,
            status,
            content_hash,
            source_modified_at,
            source_size_bytes,
            chunk_count,
            error_message,
            extracted_at,
            updated_at
          )
          VALUES (
            ?,
            ?,
            ?,
            NULL,
            ?,
            ?,
            0,
            ?,
            strftime('%Y-%m-%dT%H:%M:%fZ', 'now'),
            strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
          )
          ON CONFLICT (library_file_id)
          DO UPDATE SET
            library_id = excluded.library_id,
            status = excluded.status,
            content_hash = NULL,
            source_modified_at = excluded.source_modified_at,
            source_size_bytes = excluded.source_size_bytes,
            chunk_count = 0,
            error_message = excluded.error_message,
            extracted_at = excluded.extracted_at,
            updated_at = excluded.updated_at
        `,
      )
      .run(
        input.libraryFileId,
        input.libraryId,
        input.status,
        input.sourceModifiedAt,
        input.sourceSizeBytes,
        input.errorMessage,
      );

    database
      .prepare(
        `
          DELETE FROM library_chunks
          WHERE library_file_id = ?
        `,
      )
      .run(input.libraryFileId);
  });

  markTransaction();
}

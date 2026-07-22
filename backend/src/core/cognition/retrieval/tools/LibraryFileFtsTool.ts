import { database } from "../../../../database/database.js";
import type { ContextRetrievalTool } from "../ContextRetrievalTool.js";
import type {
  ContextCandidate,
  ContextRetrievalInput,
  ContextRetrievalResult,
} from "../ContextRetrievalTypes.js";

const DEFAULT_LIMIT = 12;
const MAXIMUM_LIMIT = 50;

type LibraryChunkSearchRow = {
  chunkId: string;
  libraryId: string;
  fileId: string;
  relativePath: string;
  fileName: string;
  extension: string;
  startLine: number;
  endLine: number;
  content: string;
  estimatedTokens: number;
  contentHash: string;
  rank: number;
  excerpt: string;
};

function clampLimit(limit: number | undefined): number {
  if (!Number.isInteger(limit)) {
    return DEFAULT_LIMIT;
  }

  return Math.min(MAXIMUM_LIMIT, Math.max(1, limit as number));
}

function tokenizeQuery(query: string): string[] {
  return (query.toLocaleLowerCase().match(/[\p{L}\p{N}_']+/gu) ?? [])
    .filter((token) => token.length >= 2)
    .slice(0, 24);
}

function escapeFtsToken(token: string): string {
  return `"${token.replaceAll('"', '""')}"`;
}

function createFtsQuery(query: string): string | null {
  const tokens = Array.from(new Set(tokenizeQuery(query)));

  if (tokens.length === 0) {
    return null;
  }

  return tokens.map(escapeFtsToken).join(" OR ");
}

function normalizeScore(rank: number): number {
  if (!Number.isFinite(rank)) {
    return 0;
  }

  return Number(Math.max(0, -rank).toFixed(6));
}

function mapCandidate(row: LibraryChunkSearchRow): ContextCandidate {
  return {
    id: row.chunkId,
    source: "library-file",
    content: row.content,
    estimatedTokens: row.estimatedTokens,
    score: normalizeScore(row.rank),
    reason: "Matched indexed Library text using SQLite FTS5.",
    metadata: {
      libraryId: row.libraryId,
      fileId: row.fileId,
      chunkId: row.chunkId,
      relativePath: row.relativePath,
      fileName: row.fileName,
      extension: row.extension,
      startLine: row.startLine,
      endLine: row.endLine,
      contentHash: row.contentHash,
      excerpt: row.excerpt,
      ftsRank: row.rank,
    },
  };
}

export class LibraryFileFtsTool implements ContextRetrievalTool {
  readonly id = "library-file-fts";
  readonly name = "Library Text Search";

  search(input: ContextRetrievalInput): ContextRetrievalResult {
    const startedAt = performance.now();
    const warnings: string[] = [];
    const normalizedQuery = input.query.trim();
    const ftsQuery = createFtsQuery(normalizedQuery);
    const limit = clampLimit(input.limit);

    if (!input.libraryId) {
      return {
        tool: this.id,
        query: normalizedQuery,
        candidates: [],
        searchedAt: new Date().toISOString(),
        durationMs: Number((performance.now() - startedAt).toFixed(3)),
        warnings: ["A Library ID is required for Library text search."],
      };
    }

    if (!ftsQuery) {
      return {
        tool: this.id,
        query: normalizedQuery,
        candidates: [],
        searchedAt: new Date().toISOString(),
        durationMs: Number((performance.now() - startedAt).toFixed(3)),
        warnings: ["The query did not contain any searchable terms."],
      };
    }

    const statement = database.prepare(`
      SELECT
        library_chunk_search.chunk_id AS chunkId,
        library_chunk_search.library_id AS libraryId,
        library_chunk_search.library_file_id AS fileId,
        library_files.relative_path AS relativePath,
        library_files.name AS fileName,
        library_files.extension AS extension,
        library_chunks.start_line AS startLine,
        library_chunks.end_line AS endLine,
        library_chunks.content AS content,
        library_chunks.estimated_tokens AS estimatedTokens,
        library_chunks.content_hash AS contentHash,
        bm25(
          library_chunk_search,
          0.0,
          0.0,
          0.0,
          0.4,
          0.6,
          1.0
        ) AS rank,
        snippet(
          library_chunk_search,
          5,
          '[',
          ']',
          ' … ',
          32
        ) AS excerpt
      FROM library_chunk_search
      INNER JOIN library_chunks
        ON library_chunks.id = library_chunk_search.chunk_id
      INNER JOIN library_files
        ON library_files.id = library_chunk_search.library_file_id
      INNER JOIN library_documents
        ON library_documents.library_file_id = library_files.id
      WHERE library_chunk_search MATCH @query
        AND library_chunk_search.library_id = @libraryId
        AND library_documents.status = 'indexed'
        AND library_files.status = 'available'
      ORDER BY
        rank ASC,
        library_files.relative_path COLLATE NOCASE ASC,
        library_chunks.ordinal ASC
      LIMIT @limit
    `);

    const rows = statement.all({
      query: ftsQuery,
      libraryId: input.libraryId,
      limit,
    }) as LibraryChunkSearchRow[];

    if (rows.length === 0) {
      warnings.push("No indexed Library text matched the search query.");
    }

    return {
      tool: this.id,
      query: normalizedQuery,
      candidates: rows.map(mapCandidate),
      searchedAt: new Date().toISOString(),
      durationMs: Number((performance.now() - startedAt).toFixed(3)),
      warnings,
    };
  }
}

export const libraryFileFtsTool = new LibraryFileFtsTool();

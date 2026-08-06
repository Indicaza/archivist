import { database } from "../../../../database/database.js";
import type { ContextRetrievalTool } from "../ContextRetrievalTool.js";
import type {
  ContextCandidate,
  ContextRetrievalInput,
  ContextRetrievalResult,
} from "../ContextRetrievalTypes.js";

const DEFAULT_LIMIT = 12;
const MAXIMUM_LIMIT = 50;
const CANDIDATE_MULTIPLIER = 4;
const MINIMUM_CANDIDATE_POOL = 24;

const ignoredSearchTokens = new Set([
  "a",
  "about",
  "also",
  "an",
  "and",
  "are",
  "as",
  "at",
  "be",
  "but",
  "by",
  "can",
  "check",
  "describe",
  "do",
  "does",
  "explain",
  "fits",
  "for",
  "from",
  "get",
  "give",
  "had",
  "has",
  "have",
  "hello",
  "hey",
  "hi",
  "how",
  "i",
  "in",
  "is",
  "it",
  "know",
  "me",
  "more",
  "my",
  "of",
  "on",
  "one",
  "or",
  "our",
  "overall",
  "please",
  "tell",
  "that",
  "the",
  "their",
  "this",
  "to",
  "tool",
  "tools",
  "use",
  "vibe",
  "was",
  "what",
  "when",
  "where",
  "which",
  "who",
  "why",
  "with",
  "you",
  "your",
]);

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

function canonicalSubjectToken(token: string): string {
  if (token.length > 4 && token.endsWith("ies")) {
    return `${token.slice(0, -3)}y`;
  }

  if (token.length > 3 && token.endsWith("s") && !token.endsWith("ss")) {
    return token.slice(0, -1);
  }

  return token;
}

function tokenizeQuery(query: string): string[] {
  const tokens = (
    query.toLocaleLowerCase().match(/[\p{L}\p{N}_]+/gu) ?? []
  ).filter(
    (token) => token.length >= 2 && !ignoredSearchTokens.has(token),
  );
  const expanded = tokens.flatMap((token) => [
    token,
    canonicalSubjectToken(token),
  ]);

  return Array.from(new Set(expanded)).slice(0, 24);
}

function tokenizePath(value: string): Set<string> {
  const normalized = value
    .replace(/([\p{Ll}\p{N}])([\p{Lu}])/gu, "$1 $2")
    .replaceAll("_", " ")
    .toLocaleLowerCase();

  return new Set(
    (normalized.match(/[\p{L}\p{N}]+/gu) ?? []).map(canonicalSubjectToken),
  );
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

function pathTokens(row: LibraryChunkSearchRow): Set<string> {
  return tokenizePath(`${row.relativePath}\n${row.fileName}`);
}

function contentTokenCount(content: string, token: string): number {
  const matches = content
    .toLocaleLowerCase()
    .match(new RegExp(`\\b${token}\\b`, "gu"));
  return Math.min(4, matches?.length ?? 0);
}

function subjectTokens(
  queryTokens: string[],
  rows: LibraryChunkSearchRow[],
): string[] {
  return queryTokens.filter((token) =>
    rows.some((row) => pathTokens(row).has(token)),
  );
}

function relevanceScore(
  row: LibraryChunkSearchRow,
  queryTokens: string[],
  directSubjects: string[],
): number {
  const rowPathTokens = pathTokens(row);
  const directPathMatches = directSubjects.filter((token) =>
    rowPathTokens.has(token),
  ).length;
  const directContentMatches = directSubjects.reduce(
    (total, token) => total + contentTokenCount(row.content, token),
    0,
  );
  const queryPathMatches = queryTokens.filter((token) =>
    rowPathTokens.has(token),
  ).length;
  const queryContentMatches = queryTokens.reduce(
    (total, token) => total + contentTokenCount(row.content, token),
    0,
  );

  return (
    directPathMatches * 10_000 +
    directContentMatches * 500 +
    queryPathMatches * 100 +
    queryContentMatches * 5 +
    normalizeScore(row.rank)
  );
}

const broadContextTokens = new Set([
  "background",
  "history",
  "lore",
  "overview",
  "category",
  "race",
  "setting",
  "theme",
  "themes",
  "tone",
  "vibe",
  "world",
  "type",
]);

const implementationIntentTokens = new Set([
  "code",
  "controller",
  "gameplay",
  "implementation",
  "mechanic",
  "mechanics",
  "script",
  "scripts",
  "system",
  "systems",
]);

const implementationPathTokens = new Set([
  "backend",
  "code",
  "controller",
  "frontend",
  "game",
  "mechanics",
  "script",
  "scripts",
  "src",
  "test",
  "tests",
]);

function isImplementationRow(row: LibraryChunkSearchRow): boolean {
  const tokens = pathTokens(row);
  return Array.from(implementationPathTokens).some((token) =>
    tokens.has(token),
  );
}

function subjectBalancedRows(
  query: string,
  rows: LibraryChunkSearchRow[],
  limit: number,
): LibraryChunkSearchRow[] {
  const queryTokens = Array.from(new Set(tokenizeQuery(query)));
  const directSubjects = subjectTokens(queryTokens, rows);

  if (directSubjects.length === 0) {
    return rows.slice(0, limit);
  }

  const specificSubjects = directSubjects.filter(
    (subject) => !broadContextTokens.has(subject),
  );
  const requestsBroadContext = queryTokens.some((token) =>
    broadContextTokens.has(token),
  );
  const requestsImplementation = queryTokens.some((token) =>
    implementationIntentTokens.has(token),
  );
  const ranked = [...rows].sort((first, second) => {
    const scoreDifference =
      relevanceScore(second, queryTokens, directSubjects) -
      relevanceScore(first, queryTokens, directSubjects);

    if (scoreDifference !== 0) {
      return scoreDifference;
    }

    return first.rank - second.rank;
  });
  const eligibleRows = ranked.filter(
    (row) =>
      requestsImplementation ||
      !isImplementationRow(row) ||
      specificSubjects.some((subject) => pathTokens(row).has(subject)),
  );
  const contextAllowance = requestsBroadContext ? 1 : 0;
  const targetLimit = Math.min(
    limit,
    specificSubjects.length > 0
      ? Math.min(6, specificSubjects.length * 2 + contextAllowance)
      : Math.min(4, eligibleRows.length),
  );
  const directRows = eligibleRows.filter((row) => {
    const tokens = pathTokens(row);
    return directSubjects.some((subject) => tokens.has(subject));
  });
  const supportingRows = eligibleRows.filter(
    (row) =>
      !directRows.some((directRow) => directRow.chunkId === row.chunkId) &&
      specificSubjects.some((subject) =>
        new RegExp(`\\b${subject}\\b`, "iu").test(row.content),
      ),
  );
  const fallbackRows = eligibleRows.filter(
    (row) =>
      !directRows.some((directRow) => directRow.chunkId === row.chunkId) &&
      !supportingRows.some(
        (supportingRow) => supportingRow.chunkId === row.chunkId,
      ),
  );
  const selected: LibraryChunkSearchRow[] = [];
  const selectedIds = new Set<string>();

  for (const subject of specificSubjects) {
    const subjectRow = directRows.find(
      (row) =>
        !selectedIds.has(row.chunkId) && pathTokens(row).has(subject),
    );

    if (subjectRow) {
      selected.push(subjectRow);
      selectedIds.add(subjectRow.chunkId);
    }
  }

  const broadSubjects = directSubjects.filter((token) =>
    broadContextTokens.has(token),
  );
  const contextRow = directRows.find((row) => {
    const tokens = pathTokens(row);
    return !selectedIds.has(row.chunkId)
      && broadSubjects.some((subject) => tokens.has(subject));
  });

  if (contextRow && selected.length < targetLimit) {
    selected.push(contextRow);
    selectedIds.add(contextRow.chunkId);
  }

  for (const row of [...directRows, ...supportingRows, ...fallbackRows]) {
    if (selected.length >= targetLimit) {
      break;
    }

    if (selectedIds.has(row.chunkId)) {
      continue;
    }

    selected.push(row);
    selectedIds.add(row.chunkId);
  }

  if (process.env.NODE_ENV !== "production") {
    console.info("[LibraryRetrievalSelection]", {
      query,
      detectedSubjects: directSubjects,
      specificSubjects,
      requestsBroadContext,
      requestsImplementation,
      candidateCount: rows.length,
      eligibleCandidateCount: eligibleRows.length,
      selectedCount: selected.length,
      targetLimit,
      suppressedImplementationPaths: ranked
        .filter((row) => !eligibleRows.includes(row))
        .map((row) => row.relativePath),
      selectedPaths: selected.map(
        (row) => `${row.relativePath}:${row.startLine}-${row.endLine}`,
      ),
    });
  }

  return selected;
}

function mapCandidate(row: LibraryChunkSearchRow): ContextCandidate {
  return {
    id: row.chunkId,
    source: "library-file",
    content: row.content,
    estimatedTokens: row.estimatedTokens,
    score: normalizeScore(row.rank),
    reason: "Matched indexed Library text using SQLite FTS5 with subject-aware reranking.",
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

    const candidateLimit = Math.min(
      MAXIMUM_LIMIT,
      Math.max(MINIMUM_CANDIDATE_POOL, limit * CANDIDATE_MULTIPLIER),
    );
    const rows = statement.all({
      query: ftsQuery,
      libraryId: input.libraryId,
      limit: candidateLimit,
    }) as LibraryChunkSearchRow[];

    if (rows.length === 0) {
      warnings.push("No indexed Library text matched the search query.");
    }

    return {
      tool: this.id,
      query: normalizedQuery,
      candidates: subjectBalancedRows(normalizedQuery, rows, limit).map(mapCandidate),
      searchedAt: new Date().toISOString(),
      durationMs: Number((performance.now() - startedAt).toFixed(3)),
      warnings,
    };
  }
}

export const libraryFileFtsTool = new LibraryFileFtsTool();

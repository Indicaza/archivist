import { database } from "../../../../database/database.js";
import { estimateTokens } from "../../conscious/context/utilities/estimateTokens.js";
import type { ContextRetrievalTool } from "../ContextRetrievalTool.js";
import type {
  ContextCandidate,
  ContextRetrievalInput,
  ContextRetrievalResult,
} from "../ContextRetrievalTypes.js";

const DEFAULT_LIMIT = 12;
const MAXIMUM_LIMIT = 50;

type MessageSearchRow = {
  messageId: string;
  chatId: string;
  chatTitle: string;
  role: "user" | "assistant" | "system";
  content: string;
  createdAt: string;
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

function mapCandidate(row: MessageSearchRow): ContextCandidate {
  return {
    id: row.messageId,
    source: "chat-message",
    content: row.content,
    estimatedTokens: estimateTokens(row.content),
    score: normalizeScore(row.rank),
    reason: "Matched indexed Chat-message terms using SQLite FTS5.",
    metadata: {
      messageId: row.messageId,
      chatId: row.chatId,
      chatTitle: row.chatTitle,
      role: row.role,
      createdAt: row.createdAt,
      excerpt: row.excerpt,
      ftsRank: row.rank,
    },
  };
}

export class ChatMessageFtsTool implements ContextRetrievalTool {
  readonly id = "chat-message-fts";
  readonly name = "Chat Message Search";

  search(input: ContextRetrievalInput): ContextRetrievalResult {
    const startedAt = performance.now();
    const warnings: string[] = [];

    const normalizedQuery = input.query.trim();
    const ftsQuery = createFtsQuery(normalizedQuery);
    const limit = clampLimit(input.limit);

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

    const chatFilter = input.chatId
      ? "AND message_search.chat_id = @chatId"
      : "";

    const statement = database.prepare(`
      SELECT
        message_search.message_id AS messageId,
        message_search.chat_id AS chatId,
        chats.title AS chatTitle,
        message_search.role AS role,
        message_search.content AS content,
        message_search.created_at AS createdAt,
        bm25(message_search) AS rank,
        snippet(
          message_search,
          3,
          '[',
          ']',
          ' … ',
          24
        ) AS excerpt
      FROM message_search
      INNER JOIN chats
        ON chats.id = message_search.chat_id
      WHERE message_search MATCH @query
        ${chatFilter}
      ORDER BY
        rank ASC,
        message_search.created_at DESC
      LIMIT @limit
    `);

    const rows = statement.all({
      query: ftsQuery,
      chatId: input.chatId ?? null,
      limit,
    }) as MessageSearchRow[];

    if (rows.length === 0) {
      warnings.push("No indexed Chat messages matched the search query.");
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

export const chatMessageFtsTool = new ChatMessageFtsTool();

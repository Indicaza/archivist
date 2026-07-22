export type ContextCandidateSource =
  | "chat-message"
  | "library-file"
  | "pinned-context"
  | "session-note";

export type ContextCandidate = {
  id: string;
  source: ContextCandidateSource;
  content: string;
  estimatedTokens: number;
  score: number;
  reason: string;
  metadata: Record<string, string | number | boolean | null>;
};

export type ContextRetrievalInput = {
  query: string;
  chatId?: string;
  libraryId?: string;
  limit?: number;
};

export type ContextRetrievalResult = {
  tool: string;
  query: string;
  candidates: ContextCandidate[];
  searchedAt: string;
  durationMs: number;
  warnings: string[];
};

export type LibraryDocumentStatus =
  | "indexed"
  | "empty"
  | "unavailable"
  | "failed";

export type LibraryTextDocument = {
  libraryFileId: string;
  libraryId: string;
  status: LibraryDocumentStatus;
  contentHash: string | null;
  sourceModifiedAt: string;
  sourceSizeBytes: number;
  chunkCount: number;
  errorMessage: string | null;
  extractedAt: string;
  updatedAt: string;
};

export type LibraryTextChunk = {
  id: string;
  libraryFileId: string;
  libraryId: string;
  ordinal: number;
  startLine: number;
  endLine: number;
  content: string;
  estimatedTokens: number;
  contentHash: string;
};

export type LibraryTextIndexIssue = {
  fileId: string;
  relativePath: string;
  message: string;
};

export type LibraryTextIndexSummary = {
  libraryId: string;
  processedFileCount: number;
  unchangedFileCount: number;
  indexedFileCount: number;
  emptyFileCount: number;
  unavailableFileCount: number;
  failedFileCount: number;
  chunkCount: number;
  durationMs: number;
  issues: LibraryTextIndexIssue[];
};

export type ReplaceLibraryTextDocumentInput = {
  libraryFileId: string;
  libraryId: string;
  status: Extract<LibraryDocumentStatus, "indexed" | "empty">;
  contentHash: string;
  sourceModifiedAt: string;
  sourceSizeBytes: number;
  chunks: LibraryTextChunk[];
};

export type MarkLibraryTextDocumentInput = {
  libraryFileId: string;
  libraryId: string;
  status: Extract<LibraryDocumentStatus, "unavailable" | "failed">;
  sourceModifiedAt: string;
  sourceSizeBytes: number;
  errorMessage: string;
};

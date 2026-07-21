export type LibraryFileStatus = "available" | "unreadable" | "missing";

export type LibraryScanStatus = "running" | "complete" | "partial" | "failed";

export type LibraryFile = {
  id: string;
  libraryId: string;
  relativePath: string;
  name: string;
  extension: string;
  sizeBytes: number;
  modifiedAt: string;
  status: LibraryFileStatus;
  lastSeenAt: string | null;
  createdAt: string;
  updatedAt: string;
};

export type LibraryFilePreview = {
  file: LibraryFile;
  content: string;
  encoding: "utf-8";
  lineCount: number;
  readAt: string;
};

export type LibraryScan = {
  id: string;
  libraryId: string;
  status: LibraryScanStatus;
  startedAt: string;
  completedAt: string | null;
  discoveredFileCount: number;
  catalogedFileCount: number;
  ignoredEntryCount: number;
  errorCount: number;
  errorMessage: string | null;
};

export type LibraryScanIssue = {
  relativePath: string | null;
  message: string;
};

export type LibraryFileCatalog = {
  files: LibraryFile[];
  latestScan: LibraryScan | null;
};

export type ScanLibraryResult = LibraryFileCatalog & {
  scan: LibraryScan;
  issues: LibraryScanIssue[];
};

export type ScannedLibraryFile = {
  relativePath: string;
  name: string;
  extension: string;
  sizeBytes: number;
  modifiedAt: string;
  status: Exclude<LibraryFileStatus, "missing">;
};

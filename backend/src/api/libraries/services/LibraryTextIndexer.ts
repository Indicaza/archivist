import { performance } from "node:perf_hooks";
import {
  getLibraryTextDocument,
  markLibraryTextDocument,
  replaceLibraryTextDocument,
  touchLibraryTextDocument,
} from "../models/LibraryTextIndex.js";
import type { LibraryFileCatalog } from "../types/LibraryFileTypes.js";
import type {
  LibraryTextIndexIssue,
  LibraryTextIndexSummary,
} from "../types/LibraryTextIndexTypes.js";
import { readLibraryFileText } from "./LibraryFileReader.js";
import {
  chunkLibraryText,
  hashLibraryText,
  normalizeLibraryText,
} from "./LibraryTextChunker.js";

const maximumReportedIssues = 100;

function getErrorMessage(error: unknown): string {
  if (error instanceof Error && error.message.trim()) {
    return error.message;
  }

  return "An unknown text-indexing error occurred.";
}

function addIssue(
  issues: LibraryTextIndexIssue[],
  issue: LibraryTextIndexIssue,
): void {
  if (issues.length < maximumReportedIssues) {
    issues.push(issue);
  }
}

export async function indexLibraryTextCatalog(
  libraryId: string,
  catalog: LibraryFileCatalog,
): Promise<LibraryTextIndexSummary> {
  const startedAt = performance.now();
  const issues: LibraryTextIndexIssue[] = [];

  let unchangedFileCount = 0;
  let indexedFileCount = 0;
  let emptyFileCount = 0;
  let unavailableFileCount = 0;
  let failedFileCount = 0;
  let chunkCount = 0;

  for (const file of catalog.files) {
    const existingDocument = getLibraryTextDocument(file.id);

    if (file.status !== "available") {
      markLibraryTextDocument({
        libraryFileId: file.id,
        libraryId: file.libraryId,
        status: "unavailable",
        sourceModifiedAt: file.modifiedAt,
        sourceSizeBytes: file.sizeBytes,
        errorMessage: `The catalog marks this file as ${file.status}.`,
      });

      unavailableFileCount += 1;
      continue;
    }

    if (
      existingDocument &&
      (existingDocument.status === "indexed" ||
        existingDocument.status === "empty") &&
      existingDocument.sourceModifiedAt === file.modifiedAt &&
      existingDocument.sourceSizeBytes === file.sizeBytes
    ) {
      unchangedFileCount += 1;
      chunkCount += existingDocument.chunkCount;
      continue;
    }

    try {
      const read = await readLibraryFileText(file.libraryId, file.id);
      const normalizedContent = normalizeLibraryText(read.content);
      const contentHash = hashLibraryText(normalizedContent);

      if (
        existingDocument &&
        existingDocument.contentHash === contentHash &&
        (existingDocument.status === "indexed" ||
          existingDocument.status === "empty")
      ) {
        touchLibraryTextDocument({
          libraryFileId: file.id,
          sourceModifiedAt: read.file.modifiedAt,
          sourceSizeBytes: read.file.sizeBytes,
        });

        unchangedFileCount += 1;
        chunkCount += existingDocument.chunkCount;
        continue;
      }

      const chunks = chunkLibraryText({
        libraryId: file.libraryId,
        libraryFileId: file.id,
        extension: file.extension,
        content: normalizedContent,
      });

      replaceLibraryTextDocument({
        libraryFileId: file.id,
        libraryId: file.libraryId,
        status: chunks.length === 0 ? "empty" : "indexed",
        contentHash,
        sourceModifiedAt: read.file.modifiedAt,
        sourceSizeBytes: read.file.sizeBytes,
        chunks,
      });

      chunkCount += chunks.length;

      if (chunks.length === 0) {
        emptyFileCount += 1;
      } else {
        indexedFileCount += 1;
      }
    } catch (error) {
      const message = getErrorMessage(error);

      markLibraryTextDocument({
        libraryFileId: file.id,
        libraryId: file.libraryId,
        status: "failed",
        sourceModifiedAt: file.modifiedAt,
        sourceSizeBytes: file.sizeBytes,
        errorMessage: message,
      });

      failedFileCount += 1;
      addIssue(issues, {
        fileId: file.id,
        relativePath: file.relativePath,
        message,
      });
    }
  }

  return {
    libraryId,
    processedFileCount: catalog.files.length,
    unchangedFileCount,
    indexedFileCount,
    emptyFileCount,
    unavailableFileCount,
    failedFileCount,
    chunkCount,
    durationMs: Number((performance.now() - startedAt).toFixed(3)),
    issues,
  };
}

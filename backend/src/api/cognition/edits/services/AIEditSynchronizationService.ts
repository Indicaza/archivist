import path from "node:path";
import {
  getLibraryFileByRelativePath,
  updateLibraryFileLocation,
} from "../../../libraries/models/LibraryFile.js";
import { markLibraryTextDocument } from "../../../libraries/models/LibraryTextIndex.js";
import { listLibraryDirectories } from "../../../libraries/services/LibraryDirectoryCatalog.js";
import { scanLibraryFiles } from "../../../libraries/services/LibraryFileScanner.js";
import {
  readLibraryGitStatus,
  type LibraryGitStatus,
} from "../../../libraries/services/LibraryGitStatus.js";
import type {
  AIEditOperation,
  AIEditProposal,
} from "../types/AIEditTypes.js";

type AIEditSynchronizationDirection = "forward" | "undo";

export type AIEditSynchronizationWarning = {
  stage:
    | "catalog-identity"
    | "catalog-index"
    | "directories"
    | "git-status";
  message: string;
};

export type AIEditSynchronizationResult = {
  status: "synchronized" | "warning";
  affectedPaths: string[];
  relocations: Array<{
    operationId: string;
    fileId: string;
    sourcePath: string;
    destinationPath: string;
  }>;
  catalog: {
    scanId: string;
    scanStatus: string;
    catalogFileCount: number;
    affectedFiles: Array<{
      id: string;
      relativePath: string;
      status: string;
      sizeBytes: number;
      modifiedAt: string;
    }>;
  } | null;
  directories: {
    count: number;
    affectedDirectories: string[];
  } | null;
  index: {
    processedFileCount: number;
    unchangedFileCount: number;
    indexedFileCount: number;
    emptyFileCount: number;
    unavailableFileCount: number;
    failedFileCount: number;
    chunkCount: number;
    issueCount: number;
  } | null;
  gitStatus: LibraryGitStatus | null;
  warnings: AIEditSynchronizationWarning[];
};

function synchronizationMessage(error: unknown): string {
  if (error instanceof Error && error.message.trim()) {
    return error.message;
  }

  return "An unknown post-edit synchronization error occurred.";
}

function synchronizedOperations(
  proposal: AIEditProposal,
  direction: AIEditSynchronizationDirection,
): AIEditOperation[] {
  const status = direction === "forward" ? "completed" : "undone";

  return proposal.operations
    .filter((operation) => operation.status === status)
    .sort((left, right) => left.ordinal - right.ordinal);
}

function operationPaths(operation: AIEditOperation): string[] {
  return Array.from(
    new Set(
      [operation.sourcePath, operation.destinationPath].filter(
        (value): value is string => typeof value === "string",
      ),
    ),
  );
}

function affectedPaths(operations: AIEditOperation[]): string[] {
  const paths: string[] = [];
  const seen = new Set<string>();

  for (const operation of operations) {
    for (const relativePath of operationPaths(operation)) {
      if (!seen.has(relativePath)) {
        seen.add(relativePath);
        paths.push(relativePath);
      }
    }
  }

  return paths;
}

function relevantDirectoryPaths(
  operations: AIEditOperation[],
  paths: string[],
): Set<string> {
  const directories = new Set<string>();

  for (const relativePath of paths) {
    const parent = path.posix.dirname(relativePath);

    if (parent !== ".") {
      directories.add(parent);
    }
  }

  for (const operation of operations) {
    if (
      operation.type === "create_directory"
      && operation.destinationPath
    ) {
      directories.add(operation.destinationPath);
    }
  }

  return directories;
}

function preserveRelocationIdentity(
  proposal: AIEditProposal,
  operations: AIEditOperation[],
  direction: AIEditSynchronizationDirection,
  warnings: AIEditSynchronizationWarning[],
): Array<{
  operationId: string;
  fileId: string;
  sourcePath: string;
  destinationPath: string;
}> {
  const relocations: Array<{
    operationId: string;
    fileId: string;
    sourcePath: string;
    destinationPath: string;
  }> = [];

  for (const operation of operations) {
    if (
      (operation.type !== "rename_file" && operation.type !== "move_file")
      || !operation.sourcePath
      || !operation.destinationPath
    ) {
      continue;
    }

    const currentPath =
      direction === "forward"
        ? operation.sourcePath
        : operation.destinationPath;
    const targetPath =
      direction === "forward"
        ? operation.destinationPath
        : operation.sourcePath;
    const currentFile = getLibraryFileByRelativePath(
      proposal.libraryId,
      currentPath,
    );

    if (!currentFile) {
      continue;
    }

    try {
      const targetExtension = path.posix
        .extname(targetPath)
        .toLowerCase();
      const updated = updateLibraryFileLocation(
        proposal.libraryId,
        currentFile.id,
        targetPath,
        path.posix.basename(targetPath),
        targetExtension,
      );

      if (currentFile.extension !== targetExtension) {
        markLibraryTextDocument({
          libraryFileId: currentFile.id,
          libraryId: proposal.libraryId,
          status: "unavailable",
          sourceModifiedAt: currentFile.modifiedAt,
          sourceSizeBytes: currentFile.sizeBytes,
          errorMessage: "Reindex required after file extension change.",
        });
      }

      relocations.push({
        operationId: operation.id,
        fileId: updated.id,
        sourcePath: currentPath,
        destinationPath: targetPath,
      });
    } catch (error) {
      warnings.push({
        stage: "catalog-identity",
        message:
          `Could not preserve catalog identity for ${
            currentPath
          } → ${targetPath}: ${synchronizationMessage(error)}`,
      });
    }
  }

  return relocations;
}

async function synchronizeAIEditProposal(
  proposal: AIEditProposal,
  direction: AIEditSynchronizationDirection,
): Promise<AIEditSynchronizationResult> {
  const expectedProposalStatus =
    direction === "forward" ? "completed" : "undone";

  if (proposal.status !== expectedProposalStatus) {
    throw new Error(
      `AI edit proposal ${proposal.id} must be ${expectedProposalStatus} before synchronization.`,
    );
  }

  const operations = synchronizedOperations(proposal, direction);
  const warnings: AIEditSynchronizationWarning[] = [];
  const paths = affectedPaths(operations);
  const pathSet = new Set(paths);
  const relocations = preserveRelocationIdentity(
    proposal,
    operations,
    direction,
    warnings,
  );

  let catalog: AIEditSynchronizationResult["catalog"] = null;
  let index: AIEditSynchronizationResult["index"] = null;

  try {
    const scanResult = await scanLibraryFiles(proposal.libraryId);

    catalog = {
      scanId: scanResult.scan.id,
      scanStatus: scanResult.scan.status,
      catalogFileCount: scanResult.files.length,
      affectedFiles: scanResult.files
        .filter((file) => pathSet.has(file.relativePath))
        .map((file) => ({
          id: file.id,
          relativePath: file.relativePath,
          status: file.status,
          sizeBytes: file.sizeBytes,
          modifiedAt: file.modifiedAt,
        })),
    };
    index = {
      processedFileCount: scanResult.index.processedFileCount,
      unchangedFileCount: scanResult.index.unchangedFileCount,
      indexedFileCount: scanResult.index.indexedFileCount,
      emptyFileCount: scanResult.index.emptyFileCount,
      unavailableFileCount: scanResult.index.unavailableFileCount,
      failedFileCount: scanResult.index.failedFileCount,
      chunkCount: scanResult.index.chunkCount,
      issueCount: scanResult.index.issues.length,
    };

    if (scanResult.scan.status !== "complete" || scanResult.issues.length > 0) {
      warnings.push({
        stage: "catalog-index",
        message:
          `Library catalog refresh completed with ${
            scanResult.issues.length
          } reported scan issue(s).`,
      });
    }

    if (
      scanResult.index.failedFileCount > 0
      || scanResult.index.issues.length > 0
    ) {
      warnings.push({
        stage: "catalog-index",
        message:
          `Library text indexing completed with ${
            scanResult.index.failedFileCount
          } failed file(s) and ${
            scanResult.index.issues.length
          } reported issue(s).`,
      });
    }
  } catch (error) {
    warnings.push({
      stage: "catalog-index",
      message: synchronizationMessage(error),
    });
  }

  let directories: AIEditSynchronizationResult["directories"] = null;

  try {
    const directoryPaths = await listLibraryDirectories(proposal.libraryId);
    const relevantDirectories = relevantDirectoryPaths(operations, paths);

    directories = {
      count: directoryPaths.length,
      affectedDirectories: directoryPaths.filter(
        (relativePath) => relevantDirectories.has(relativePath),
      ),
    };
  } catch (error) {
    warnings.push({
      stage: "directories",
      message: synchronizationMessage(error),
    });
  }

  let gitStatus: LibraryGitStatus | null = null;

  try {
    gitStatus = await readLibraryGitStatus(proposal.libraryId);
  } catch (error) {
    warnings.push({
      stage: "git-status",
      message: synchronizationMessage(error),
    });
  }

  return {
    status: warnings.length === 0 ? "synchronized" : "warning",
    affectedPaths: paths,
    relocations,
    catalog,
    directories,
    index,
    gitStatus,
    warnings,
  };
}

export function synchronizeCompletedAIEditProposal(
  proposal: AIEditProposal,
): Promise<AIEditSynchronizationResult> {
  return synchronizeAIEditProposal(proposal, "forward");
}

export function synchronizeUndoneAIEditProposal(
  proposal: AIEditProposal,
): Promise<AIEditSynchronizationResult> {
  return synchronizeAIEditProposal(proposal, "undo");
}

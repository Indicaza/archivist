import { createHash, randomUUID } from "node:crypto";
import { constants as fsConstants } from "node:fs";
import {
  access,
  link,
  lstat,
  mkdir,
  readFile,
  realpath,
  rename,
  rmdir,
  unlink,
  writeFile,
} from "node:fs/promises";
import path from "node:path";
import { AppError } from "../../../../errors/app-error.js";
import { appendAIRunEvent } from "../../runs/models/AIRun.js";
import { getLibraryById } from "../../../libraries/models/Library.js";
import {
  inspectLibraryCreateTarget,
  inspectLibraryDirectoryCreateTarget,
  inspectLibraryEmptyDirectoryForEdit,
  inspectLibraryTextFileForEdit,
} from "../../../libraries/services/LibraryEditInspection.js";
import {
  synchronizeCompletedAIEditProposal,
  synchronizeUndoneAIEditProposal,
  type AIEditSynchronizationResult,
} from "./AIEditSynchronizationService.js";
import {
  beginAIEditProposalExecution,
  beginAIEditProposalUndo,
  completeAIEditProposalExecution,
  completeAIEditProposalUndo,
  listAIEditProposalsByChatId,
  markAIEditProposalFailed,
  markAIEditProposalStale,
  markAIEditProposalUndoFailed,
  rejectAIEditProposal,
  requireAIEditProposal,
  restoreAIEditProposalAfterUndoFailure,
} from "../models/AIEditProposal.js";
import type {
  AIEditOperation,
  AIEditProposal,
} from "../types/AIEditTypes.js";

type RollbackAction = () => Promise<void>;

class AIEditProposalStaleError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "AIEditProposalStaleError";
  }
}

function errorMessage(error: unknown): string {
  if (error instanceof Error && error.message.trim()) {
    return error.message;
  }

  return "An unknown AI edit transaction error occurred.";
}

function isStaleFilesystemError(error: unknown): boolean {
  if (!(error instanceof Error)) {
    return false;
  }

  const code = (error as NodeJS.ErrnoException).code;

  return (
    code === "EEXIST"
    || code === "ENOENT"
    || code === "ENOTDIR"
    || code === "ELOOP"
    || code === "EACCES"
    || code === "EPERM"
    || code === "ENOTEMPTY"
  );
}

function pathIsInsideRoot(
  rootPath: string,
  candidatePath: string,
): boolean {
  const relativePath = path.relative(rootPath, candidatePath);

  return (
    relativePath !== ".."
    && !relativePath.startsWith(`..${path.sep}`)
    && !path.isAbsolute(relativePath)
  );
}

async function requireCanonicalLibraryRoot(
  libraryId: string,
): Promise<string> {
  const library = getLibraryById(libraryId);

  if (!library) {
    throw new AppError(404, "Library not found.");
  }

  if (library.archivedAt) {
    throw new AppError(
      409,
      "Archived Libraries cannot execute AI edit proposals.",
    );
  }

  try {
    return await realpath(library.rootPath);
  } catch {
    throw new AppError(
      404,
      "The Library folder could not be resolved.",
    );
  }
}

function absoluteLibraryPath(
  rootPath: string,
  relativePath: string,
): string {
  const absolutePath = path.resolve(
    rootPath,
    ...relativePath.split("/"),
  );

  if (!pathIsInsideRoot(rootPath, absolutePath)) {
    throw new AppError(
      400,
      "The AI edit operation escaped the Library root.",
    );
  }

  return absolutePath;
}

function requireSourcePath(operation: AIEditOperation): string {
  if (!operation.sourcePath) {
    throw new Error(
      `AI edit operation ${operation.id} is missing its source path.`,
    );
  }

  return operation.sourcePath;
}

function requireDestinationPath(operation: AIEditOperation): string {
  if (!operation.destinationPath) {
    throw new Error(
      `AI edit operation ${operation.id} is missing its destination path.`,
    );
  }

  return operation.destinationPath;
}

function requireExpectedHash(operation: AIEditOperation): string {
  if (!operation.expectedHash) {
    throw new Error(
      `AI edit operation ${operation.id} is missing its expected hash.`,
    );
  }

  return operation.expectedHash;
}

function requireAfterHash(operation: AIEditOperation): string {
  if (!operation.afterHash) {
    throw new Error(
      `AI edit operation ${operation.id} is missing its final hash.`,
    );
  }

  return operation.afterHash;
}

function requireAfterContent(operation: AIEditOperation): string {
  if (operation.afterContent === null) {
    throw new Error(
      `AI edit operation ${operation.id} is missing its proposed content.`,
    );
  }

  return operation.afterContent;
}

function requireBeforeContent(operation: AIEditOperation): string {
  if (operation.beforeContent === null) {
    throw new Error(
      `AI edit operation ${operation.id} is missing its original content.`,
    );
  }

  return operation.beforeContent;
}

async function requireWritablePath(absolutePath: string): Promise<void> {
  try {
    await access(absolutePath, fsConstants.W_OK);
  } catch {
    throw new AppError(
      403,
      "Archivist cannot write the approved edit source.",
    );
  }
}

async function requireWritableParent(absolutePath: string): Promise<void> {
  try {
    await access(path.dirname(absolutePath), fsConstants.W_OK);
  } catch {
    throw new AppError(
      403,
      "Archivist cannot modify the approved edit source folder.",
    );
  }
}

async function inspectCurrentOperation(
  proposal: AIEditProposal,
  operation: AIEditOperation,
): Promise<void> {
  if (operation.type === "create_file") {
    await inspectLibraryCreateTarget(
      proposal.libraryId,
      requireDestinationPath(operation),
    );
    return;
  }

  if (operation.type === "create_directory") {
    await inspectLibraryDirectoryCreateTarget(
      proposal.libraryId,
      requireDestinationPath(operation),
    );
    return;
  }

  if (operation.type === "patch_file") {
    const sourcePath = requireSourcePath(operation);
    const expectedHash = requireExpectedHash(operation);
    const inspection = await inspectLibraryTextFileForEdit(
      proposal.libraryId,
      sourcePath,
    );

    if (
      inspection.sha256 !== expectedHash
      || operation.beforeContent === null
      || inspection.content !== operation.beforeContent
    ) {
      throw new AIEditProposalStaleError(
        `The approved patch source "${sourcePath}" changed after proposal review.`,
      );
    }

    const rootPath = await requireCanonicalLibraryRoot(proposal.libraryId);
    await requireWritablePath(
      absoluteLibraryPath(rootPath, sourcePath),
    );
    return;
  }

  if (operation.type === "rename_file" || operation.type === "move_file") {
    const sourcePath = requireSourcePath(operation);
    const destinationPath = requireDestinationPath(operation);
    const expectedHash = requireExpectedHash(operation);
    const inspection = await inspectLibraryTextFileForEdit(
      proposal.libraryId,
      sourcePath,
    );

    if (inspection.sha256 !== expectedHash) {
      throw new AIEditProposalStaleError(
        `The approved ${operation.type} source "${sourcePath}" changed after proposal review.`,
      );
    }

    await inspectLibraryCreateTarget(
      proposal.libraryId,
      destinationPath,
    );

    const rootPath = await requireCanonicalLibraryRoot(proposal.libraryId);
    await requireWritableParent(
      absoluteLibraryPath(rootPath, sourcePath),
    );
    return;
  }

  throw new Error(
    `Unsupported AI edit operation type "${operation.type}".`,
  );
}

async function assertProposalCurrent(
  proposal: AIEditProposal,
): Promise<void> {
  const approvedOperations = proposal.operations
    .filter((operation) => operation.status === "approved")
    .sort((left, right) => left.ordinal - right.ordinal);

  if (approvedOperations.length === 0) {
    throw new AppError(
      409,
      "This AI edit proposal has no approved operations.",
    );
  }

  for (const operation of approvedOperations) {
    try {
      await inspectCurrentOperation(proposal, operation);
    } catch (error) {
      if (error instanceof AIEditProposalStaleError) {
        throw error;
      }

      throw new AIEditProposalStaleError(errorMessage(error));
    }
  }
}

async function hashFile(absolutePath: string): Promise<string> {
  const buffer = await readFile(absolutePath);
  return createHash("sha256").update(buffer).digest("hex");
}

async function executeCreateFile(
  rootPath: string,
  operation: AIEditOperation,
): Promise<{ rollback: RollbackAction; absolutePath: string }> {
  const destinationPath = absoluteLibraryPath(
    rootPath,
    requireDestinationPath(operation),
  );

  await writeFile(
    destinationPath,
    requireAfterContent(operation),
    {
      encoding: "utf8",
      flag: "wx",
    },
  );

  return {
    absolutePath: destinationPath,
    rollback: async () => {
      await unlink(destinationPath);
    },
  };
}

async function executeCreateDirectory(
  rootPath: string,
  operation: AIEditOperation,
): Promise<{ rollback: RollbackAction; absolutePath: null }> {
  const destinationPath = absoluteLibraryPath(
    rootPath,
    requireDestinationPath(operation),
  );

  await mkdir(destinationPath);

  return {
    absolutePath: null,
    rollback: async () => {
      await rmdir(destinationPath);
    },
  };
}

async function executePatchFile(
  rootPath: string,
  proposalId: string,
  operation: AIEditOperation,
): Promise<{ rollback: RollbackAction; absolutePath: string }> {
  const sourcePath = absoluteLibraryPath(
    rootPath,
    requireSourcePath(operation),
  );
  const stats = await lstat(sourcePath);
  const temporaryPath = path.join(
    path.dirname(sourcePath),
    `.${path.basename(sourcePath)}.archivist-${proposalId}-${randomUUID()}.tmp`,
  );

  try {
    await writeFile(
      temporaryPath,
      requireAfterContent(operation),
      {
        encoding: "utf8",
        flag: "wx",
        mode: stats.mode,
      },
    );
    await rename(temporaryPath, sourcePath);
  } catch (error) {
    await unlink(temporaryPath).catch(() => undefined);
    throw error;
  }

  return {
    absolutePath: sourcePath,
    rollback: async () => {
      if (operation.beforeContent === null) {
        throw new Error(
          `AI edit operation ${operation.id} has no rollback content.`,
        );
      }

      const rollbackPath = path.join(
        path.dirname(sourcePath),
        `.${path.basename(sourcePath)}.archivist-rollback-${randomUUID()}.tmp`,
      );

      try {
        await writeFile(
          rollbackPath,
          operation.beforeContent,
          {
            encoding: "utf8",
            flag: "wx",
            mode: stats.mode,
          },
        );
        await rename(rollbackPath, sourcePath);
      } catch (error) {
        await unlink(rollbackPath).catch(() => undefined);
        throw error;
      }
    },
  };
}

async function executeRelocateFile(
  rootPath: string,
  operation: AIEditOperation,
): Promise<{ rollback: RollbackAction; absolutePath: string }> {
  const sourcePath = absoluteLibraryPath(
    rootPath,
    requireSourcePath(operation),
  );
  const destinationPath = absoluteLibraryPath(
    rootPath,
    requireDestinationPath(operation),
  );

  await link(sourcePath, destinationPath);

  try {
    await unlink(sourcePath);
  } catch (error) {
    await unlink(destinationPath).catch(() => undefined);
    throw error;
  }

  return {
    absolutePath: destinationPath,
    rollback: async () => {
      await link(destinationPath, sourcePath);

      try {
        await unlink(destinationPath);
      } catch (error) {
        await unlink(sourcePath).catch(() => undefined);
        throw error;
      }
    },
  };
}

async function executeOperation(
  rootPath: string,
  proposalId: string,
  operation: AIEditOperation,
): Promise<{ rollback: RollbackAction; absolutePath: string | null }> {
  if (operation.type === "create_file") {
    return executeCreateFile(rootPath, operation);
  }

  if (operation.type === "create_directory") {
    return executeCreateDirectory(rootPath, operation);
  }

  if (operation.type === "patch_file") {
    return executePatchFile(rootPath, proposalId, operation);
  }

  if (operation.type === "rename_file" || operation.type === "move_file") {
    return executeRelocateFile(rootPath, operation);
  }

  throw new Error(
    `Unsupported AI edit operation type "${operation.type}".`,
  );
}

async function rollbackActions(
  rollbacks: RollbackAction[],
): Promise<string[]> {
  const errors: string[] = [];

  for (const rollback of [...rollbacks].reverse()) {
    try {
      await rollback();
    } catch (error) {
      errors.push(errorMessage(error));
    }
  }

  return errors;
}

function completedUndoOperations(
  proposal: AIEditProposal,
): AIEditOperation[] {
  return proposal.operations
    .filter((operation) => operation.status === "completed")
    .sort((left, right) => right.ordinal - left.ordinal);
}

async function inspectCurrentUndoOperation(
  proposal: AIEditProposal,
  operation: AIEditOperation,
): Promise<void> {
  if (operation.type === "create_file") {
    const destinationPath = requireDestinationPath(operation);
    const inspection = await inspectLibraryTextFileForEdit(
      proposal.libraryId,
      destinationPath,
    );

    if (
      inspection.sha256 !== requireAfterHash(operation)
      || inspection.content !== requireAfterContent(operation)
    ) {
      throw new AIEditProposalStaleError(
        `The created file "${destinationPath}" changed after Archivist wrote it.`,
      );
    }

    const rootPath = await requireCanonicalLibraryRoot(proposal.libraryId);
    await requireWritableParent(
      absoluteLibraryPath(rootPath, destinationPath),
    );
    return;
  }

  if (operation.type === "create_directory") {
    await inspectLibraryEmptyDirectoryForEdit(
      proposal.libraryId,
      requireDestinationPath(operation),
    );
    return;
  }

  if (operation.type === "patch_file") {
    const sourcePath = requireSourcePath(operation);
    const inspection = await inspectLibraryTextFileForEdit(
      proposal.libraryId,
      sourcePath,
    );

    if (
      inspection.sha256 !== requireAfterHash(operation)
      || inspection.content !== requireAfterContent(operation)
    ) {
      throw new AIEditProposalStaleError(
        `The patched file "${sourcePath}" changed after Archivist wrote it.`,
      );
    }

    const rootPath = await requireCanonicalLibraryRoot(proposal.libraryId);
    await requireWritableParent(
      absoluteLibraryPath(rootPath, sourcePath),
    );
    return;
  }

  if (operation.type === "rename_file" || operation.type === "move_file") {
    const sourcePath = requireSourcePath(operation);
    const destinationPath = requireDestinationPath(operation);
    const inspection = await inspectLibraryTextFileForEdit(
      proposal.libraryId,
      destinationPath,
    );

    if (inspection.sha256 !== requireAfterHash(operation)) {
      throw new AIEditProposalStaleError(
        `The relocated file "${destinationPath}" changed after Archivist wrote it.`,
      );
    }

    await inspectLibraryCreateTarget(
      proposal.libraryId,
      sourcePath,
    );

    const rootPath = await requireCanonicalLibraryRoot(proposal.libraryId);
    await requireWritableParent(
      absoluteLibraryPath(rootPath, destinationPath),
    );
    return;
  }

  throw new Error(
    `Unsupported AI edit operation type "${operation.type}".`,
  );
}

async function assertUndoCurrent(
  proposal: AIEditProposal,
): Promise<void> {
  const operations = completedUndoOperations(proposal);

  if (operations.length === 0) {
    throw new AppError(
      409,
      "This AI edit transaction has no completed operations to undo.",
    );
  }

  for (const operation of operations) {
    try {
      await inspectCurrentUndoOperation(proposal, operation);
    } catch (error) {
      if (error instanceof AIEditProposalStaleError) {
        throw error;
      }

      throw new AIEditProposalStaleError(errorMessage(error));
    }
  }
}

async function executeUndoCreateFile(
  rootPath: string,
  operation: AIEditOperation,
): Promise<{ rollback: RollbackAction }> {
  const destinationPath = absoluteLibraryPath(
    rootPath,
    requireDestinationPath(operation),
  );
  const stats = await lstat(destinationPath);
  const content = requireAfterContent(operation);

  await unlink(destinationPath);

  return {
    rollback: async () => {
      await writeFile(
        destinationPath,
        content,
        {
          encoding: "utf8",
          flag: "wx",
          mode: stats.mode,
        },
      );
    },
  };
}

async function executeUndoCreateDirectory(
  rootPath: string,
  operation: AIEditOperation,
): Promise<{ rollback: RollbackAction }> {
  const destinationPath = absoluteLibraryPath(
    rootPath,
    requireDestinationPath(operation),
  );
  const stats = await lstat(destinationPath);

  await rmdir(destinationPath);

  return {
    rollback: async () => {
      await mkdir(destinationPath, {
        mode: stats.mode,
      });
    },
  };
}

async function executeUndoPatchFile(
  rootPath: string,
  proposalId: string,
  operation: AIEditOperation,
): Promise<{ rollback: RollbackAction }> {
  const sourcePath = absoluteLibraryPath(
    rootPath,
    requireSourcePath(operation),
  );
  const stats = await lstat(sourcePath);
  const beforeContent = requireBeforeContent(operation);
  const afterContent = requireAfterContent(operation);
  const temporaryPath = path.join(
    path.dirname(sourcePath),
    `.${path.basename(sourcePath)}.archivist-undo-${proposalId}-${randomUUID()}.tmp`,
  );

  try {
    await writeFile(
      temporaryPath,
      beforeContent,
      {
        encoding: "utf8",
        flag: "wx",
        mode: stats.mode,
      },
    );
    await rename(temporaryPath, sourcePath);
  } catch (error) {
    await unlink(temporaryPath).catch(() => undefined);
    throw error;
  }

  return {
    rollback: async () => {
      const rollbackPath = path.join(
        path.dirname(sourcePath),
        `.${path.basename(sourcePath)}.archivist-undo-rollback-${randomUUID()}.tmp`,
      );

      try {
        await writeFile(
          rollbackPath,
          afterContent,
          {
            encoding: "utf8",
            flag: "wx",
            mode: stats.mode,
          },
        );
        await rename(rollbackPath, sourcePath);
      } catch (error) {
        await unlink(rollbackPath).catch(() => undefined);
        throw error;
      }
    },
  };
}

async function executeUndoRelocateFile(
  rootPath: string,
  operation: AIEditOperation,
): Promise<{ rollback: RollbackAction }> {
  const restoredPath = absoluteLibraryPath(
    rootPath,
    requireSourcePath(operation),
  );
  const currentPath = absoluteLibraryPath(
    rootPath,
    requireDestinationPath(operation),
  );

  await link(currentPath, restoredPath);

  try {
    await unlink(currentPath);
  } catch (error) {
    await unlink(restoredPath).catch(() => undefined);
    throw error;
  }

  return {
    rollback: async () => {
      await link(restoredPath, currentPath);

      try {
        await unlink(restoredPath);
      } catch (error) {
        await unlink(currentPath).catch(() => undefined);
        throw error;
      }
    },
  };
}

async function executeUndoOperation(
  rootPath: string,
  proposalId: string,
  operation: AIEditOperation,
): Promise<{ rollback: RollbackAction }> {
  if (operation.type === "create_file") {
    return executeUndoCreateFile(rootPath, operation);
  }

  if (operation.type === "create_directory") {
    return executeUndoCreateDirectory(rootPath, operation);
  }

  if (operation.type === "patch_file") {
    return executeUndoPatchFile(rootPath, proposalId, operation);
  }

  if (operation.type === "rename_file" || operation.type === "move_file") {
    return executeUndoRelocateFile(rootPath, operation);
  }

  throw new Error(
    `Unsupported AI edit operation type "${operation.type}".`,
  );
}

function synchronizationEventPayload(
  result: AIEditSynchronizationResult,
): Record<string, unknown> {
  return {
    affectedPaths: result.affectedPaths,
    relocations: result.relocations,
    catalog: result.catalog
      ? {
          scanId: result.catalog.scanId,
          scanStatus: result.catalog.scanStatus,
          catalogFileCount: result.catalog.catalogFileCount,
          affectedFileCount: result.catalog.affectedFiles.length,
        }
      : null,
    directories: result.directories,
    index: result.index,
    gitStatus: result.gitStatus
      ? {
          repository: result.gitStatus.repository,
          branch: result.gitStatus.branch,
          dirty: result.gitStatus.dirty,
          entryCount: result.gitStatus.entries.length,
          counts: result.gitStatus.counts,
        }
      : null,
    warningCount: result.warnings.length,
    warnings: result.warnings,
  };
}

function approvedOperationIds(proposal: AIEditProposal): string[] {
  return proposal.operations
    .filter((operation) => operation.status === "approved")
    .sort((left, right) => left.ordinal - right.ordinal)
    .map((operation) => operation.id);
}

function rejectedOperationIds(proposal: AIEditProposal): string[] {
  return proposal.operations
    .filter((operation) => operation.status === "rejected")
    .sort((left, right) => left.ordinal - right.ordinal)
    .map((operation) => operation.id);
}

export function rejectAIEditProposalForReview(
  proposalId: string,
): AIEditProposal {
  const rejected = rejectAIEditProposal(proposalId);

  appendAIRunEvent(
    rejected.runId,
    "edit.rejected",
    {
      proposalId: rejected.id,
      operationIds: rejected.operations.map((operation) => operation.id),
    },
  );

  return rejected;
}

export async function approveAIEditProposalForReview(
  proposalId: string,
  selectedOperationIds?: string[],
): Promise<{
  proposal: AIEditProposal;
  synchronization: AIEditSynchronizationResult;
}> {
  const executing = beginAIEditProposalExecution(
    proposalId,
    selectedOperationIds,
  );
  const approvedIds = approvedOperationIds(executing);
  const rejectedIds = rejectedOperationIds(executing);

  appendAIRunEvent(
    executing.runId,
    "edit.approved",
    {
      proposalId: executing.id,
      approvedOperationIds: approvedIds,
      rejectedOperationIds: rejectedIds,
      partial: rejectedIds.length > 0,
    },
  );

  let rootPath: string;

  try {
    await assertProposalCurrent(executing);
    rootPath = await requireCanonicalLibraryRoot(executing.libraryId);
  } catch (error) {
    const message = errorMessage(error);
    const stale = markAIEditProposalStale(executing.id, message);

    appendAIRunEvent(
      stale.runId,
      "edit.stale",
      {
        proposalId: stale.id,
        approvedOperationIds: approvedIds,
        message,
        writesPerformed: false,
        writesRetained: false,
      },
    );

    throw new AppError(
      409,
      "The AI edit proposal is stale and was not executed.",
      {
        proposalId: stale.id,
        cause: message,
      },
    );
  }

  appendAIRunEvent(
    executing.runId,
    "edit.verified",
    {
      proposalId: executing.id,
      approvedOperationIds: approvedIds,
      writesPerformed: false,
      writesRetained: false,
    },
  );

  const approvedOperations = executing.operations
    .filter((operation) => operation.status === "approved")
    .sort((left, right) => left.ordinal - right.ordinal);
  const rollbacks: RollbackAction[] = [];
  const afterHashes = new Map<string, string | null>();

  try {
    for (const operation of approvedOperations) {
      try {
        await inspectCurrentOperation(executing, operation);
      } catch (error) {
        if (error instanceof AIEditProposalStaleError) {
          throw error;
        }

        throw new AIEditProposalStaleError(errorMessage(error));
      }

      let executed: Awaited<ReturnType<typeof executeOperation>>;

      try {
        executed = await executeOperation(
          rootPath,
          executing.id,
          operation,
        );
      } catch (error) {
        if (isStaleFilesystemError(error)) {
          throw new AIEditProposalStaleError(errorMessage(error));
        }

        throw error;
      }

      rollbacks.push(executed.rollback);

      afterHashes.set(
        operation.id,
        executed.absolutePath
          ? await hashFile(executed.absolutePath)
          : null,
      );
    }

    completeAIEditProposalExecution(
      executing.id,
      afterHashes,
    );
  } catch (error) {
    const rollbackErrors = await rollbackActions(rollbacks);

    if (rollbackErrors.length > 0) {
      const message =
        `AI edit execution could not be rolled back completely: ${
          rollbackErrors.join("; ")
        }`;
      const failed = markAIEditProposalFailed(
        executing.id,
        "rollback_failed",
        message,
      );

      appendAIRunEvent(
        failed.runId,
        "edit.failed",
        {
          proposalId: failed.id,
          approvedOperationIds: approvedIds,
          errorCode: "rollback_failed",
          message,
          writesPerformed: rollbacks.length > 0,
          writesRetained: rollbackErrors.length > 0,
          rollbackErrors,
        },
      );

      throw new AppError(
        500,
        "The AI edit transaction failed and rollback was incomplete.",
        {
          proposalId: failed.id,
          cause: errorMessage(error),
          rollbackErrors,
        },
      );
    }

    if (error instanceof AIEditProposalStaleError) {
      const message = error.message;
      const stale = markAIEditProposalStale(executing.id, message);

      appendAIRunEvent(
        stale.runId,
        "edit.stale",
        {
          proposalId: stale.id,
          approvedOperationIds: approvedIds,
          message,
          writesPerformed: rollbacks.length > 0,
          writesRetained: false,
          rolledBackOperationCount: rollbacks.length,
        },
      );

      throw new AppError(
        409,
        "The AI edit proposal became stale and was rolled back.",
        {
          proposalId: stale.id,
          cause: message,
        },
      );
    }

    const message = errorMessage(error);
    const failed = markAIEditProposalFailed(
      executing.id,
      "execution_failed",
      message,
    );

    appendAIRunEvent(
      failed.runId,
      "edit.failed",
      {
        proposalId: failed.id,
        approvedOperationIds: approvedIds,
        errorCode: "execution_failed",
        message,
        writesPerformed: rollbacks.length > 0,
        writesRetained: false,
        rolledBackOperationCount: rollbacks.length,
      },
    );

    throw new AppError(
      500,
      "The AI edit transaction failed and was rolled back.",
      {
        proposalId: failed.id,
        cause: message,
      },
    );
  }

  const completed = requireAIEditProposal(executing.id);
  const changes = approvedOperations.map((operation) => ({
    operationId: operation.id,
    type: operation.type,
    sourcePath: operation.sourcePath,
    destinationPath: operation.destinationPath,
    afterHash: afterHashes.get(operation.id) ?? null,
  }));
  const affectedPaths = Array.from(
    new Set(
      changes.flatMap((change) =>
        [change.sourcePath, change.destinationPath].filter(
          (value): value is string => typeof value === "string",
        ),
      ),
    ),
  );

  appendAIRunEvent(
    completed.runId,
    "edit.completed",
    {
      proposalId: completed.id,
      completedOperationIds: approvedIds,
      rejectedOperationIds: rejectedIds,
      changes,
      affectedPaths,
      afterHashes: Object.fromEntries(afterHashes),
      writesPerformed: true,
      writesRetained: true,
    },
  );

  const synchronization =
    await synchronizeCompletedAIEditProposal(completed);

  appendAIRunEvent(
    completed.runId,
    synchronization.status === "synchronized"
      ? "edit.synchronized"
      : "edit.sync_warning",
    {
      proposalId: completed.id,
      ...synchronizationEventPayload(synchronization),
    },
  );

  return {
    proposal: completed,
    synchronization,
  };
}

export async function undoAIEditProposalForReview(
  proposalId: string,
): Promise<{
  proposal: AIEditProposal;
  synchronization: AIEditSynchronizationResult;
}> {
  const executing = beginAIEditProposalUndo(proposalId);
  const operations = completedUndoOperations(executing);
  const operationIds = operations.map((operation) => operation.id);

  let rootPath: string;

  try {
    await assertUndoCurrent(executing);
    rootPath = await requireCanonicalLibraryRoot(executing.libraryId);
  } catch (error) {
    const message = errorMessage(error);
    const restored = restoreAIEditProposalAfterUndoFailure(executing.id);

    appendAIRunEvent(
      restored.runId,
      "edit.undo_stale",
      {
        proposalId: restored.id,
        operationIds,
        message,
        writesPerformed: false,
        forwardChangesRetained: true,
      },
    );

    throw new AppError(
      409,
      "The completed AI edit transaction is stale and was not undone.",
      {
        proposalId: restored.id,
        cause: message,
      },
    );
  }

  appendAIRunEvent(
    executing.runId,
    "edit.undo_verified",
    {
      proposalId: executing.id,
      operationIds,
      writesPerformed: false,
      forwardChangesRetained: true,
    },
  );

  const rollbacks: RollbackAction[] = [];

  try {
    for (const operation of operations) {
      try {
        await inspectCurrentUndoOperation(executing, operation);
      } catch (error) {
        if (error instanceof AIEditProposalStaleError) {
          throw error;
        }

        throw new AIEditProposalStaleError(errorMessage(error));
      }

      try {
        const executed = await executeUndoOperation(
          rootPath,
          executing.id,
          operation,
        );
        rollbacks.push(executed.rollback);
      } catch (error) {
        if (isStaleFilesystemError(error)) {
          throw new AIEditProposalStaleError(errorMessage(error));
        }

        throw error;
      }
    }
  } catch (error) {
    const rollbackErrors = await rollbackActions(rollbacks);

    if (rollbackErrors.length > 0) {
      const message =
        `AI edit undo could not be rolled back completely: ${
          rollbackErrors.join("; ")
        }`;
      const failed = markAIEditProposalUndoFailed(
        executing.id,
        "undo_rollback_failed",
        message,
      );

      appendAIRunEvent(
        failed.runId,
        "edit.undo_failed",
        {
          proposalId: failed.id,
          operationIds,
          errorCode: "undo_rollback_failed",
          message,
          writesPerformed: rollbacks.length > 0,
          forwardChangesRetained: rollbackErrors.length === 0,
          rollbackErrors,
        },
      );

      throw new AppError(
        500,
        "The AI edit undo failed and rollback was incomplete.",
        {
          proposalId: failed.id,
          cause: errorMessage(error),
          rollbackErrors,
        },
      );
    }

    const restored = restoreAIEditProposalAfterUndoFailure(executing.id);

    if (error instanceof AIEditProposalStaleError) {
      const message = error.message;

      appendAIRunEvent(
        restored.runId,
        "edit.undo_stale",
        {
          proposalId: restored.id,
          operationIds,
          message,
          writesPerformed: rollbacks.length > 0,
          forwardChangesRetained: true,
          rolledBackOperationCount: rollbacks.length,
        },
      );

      throw new AppError(
        409,
        "The completed AI edit transaction became stale and was not undone.",
        {
          proposalId: restored.id,
          cause: message,
        },
      );
    }

    const message = errorMessage(error);

    appendAIRunEvent(
      restored.runId,
      "edit.undo_failed",
      {
        proposalId: restored.id,
        operationIds,
        errorCode: "undo_failed",
        message,
        writesPerformed: rollbacks.length > 0,
        forwardChangesRetained: true,
        rolledBackOperationCount: rollbacks.length,
      },
    );

    throw new AppError(
      500,
      "The AI edit undo failed and its partial reversals were rolled back.",
      {
        proposalId: restored.id,
        cause: message,
      },
    );
  }

  const undone = completeAIEditProposalUndo(executing.id);
  const changes = operations.map((operation) => {
    if (
      operation.type === "rename_file"
      || operation.type === "move_file"
    ) {
      return {
        operationId: operation.id,
        type: operation.type,
        sourcePath: operation.destinationPath,
        destinationPath: operation.sourcePath,
        restoredHash: operation.expectedHash,
      };
    }

    if (operation.type === "patch_file") {
      return {
        operationId: operation.id,
        type: operation.type,
        sourcePath: operation.sourcePath,
        destinationPath: operation.destinationPath,
        restoredHash: operation.expectedHash,
      };
    }

    return {
      operationId: operation.id,
      type: operation.type,
      sourcePath: operation.destinationPath,
      destinationPath: null,
      restoredHash: null,
    };
  });
  const affectedPaths = Array.from(
    new Set(
      changes.flatMap((change) =>
        [change.sourcePath, change.destinationPath].filter(
          (value): value is string => typeof value === "string",
        ),
      ),
    ),
  );

  appendAIRunEvent(
    undone.runId,
    "edit.undone",
    {
      proposalId: undone.id,
      undoneOperationIds: operationIds,
      changes,
      affectedPaths,
      writesPerformed: true,
      forwardChangesRetained: false,
    },
  );

  const synchronization =
    await synchronizeUndoneAIEditProposal(undone);

  appendAIRunEvent(
    undone.runId,
    synchronization.status === "synchronized"
      ? "edit.synchronized"
      : "edit.sync_warning",
    {
      proposalId: undone.id,
      direction: "undo",
      ...synchronizationEventPayload(synchronization),
    },
  );

  return {
    proposal: undone,
    synchronization,
  };
}

export function listAIEditProposalsForChatReview(
  chatId: string,
): AIEditProposal[] {
  return listAIEditProposalsByChatId(chatId);
}

export function getAIEditProposalForReview(
  proposalId: string,
): AIEditProposal {
  return requireAIEditProposal(proposalId);
}

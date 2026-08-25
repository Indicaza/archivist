import path from "node:path";
import { requireAIRun } from "../../runs/models/AIRun.js";
import { AppError } from "../../../../errors/app-error.js";
import { maxLibraryTextPreviewBytes } from "../../../libraries/services/LibraryFilePolicy.js";
import {
  appendAIEditProposalOperations,
  createAIEditProposal,
  listAIEditProposalsByRunId,
} from "../models/AIEditProposal.js";
import type {
  AIEditOperationDraft,
  AIEditOperationType,
  AIEditProposal,
  ProposeAIEditInput,
} from "../types/AIEditTypes.js";

const maximumOperationsPerProposal = 64;
const maximumSummaryCharacters = 2_000;
const sha256Pattern = /^[0-9a-f]{64}$/i;

function normalizeRelativePath(value: string, label: string): string {
  const trimmed = value.trim();

  if (
    !trimmed
    || path.posix.isAbsolute(trimmed)
    || path.win32.isAbsolute(trimmed)
    || /[\u0000-\u001f]/.test(trimmed)
  ) {
    throw new AppError(400, `${label} must be a safe Library-relative path.`);
  }

  const normalized = trimmed.replaceAll("\\", "/");
  const parts = normalized.split("/");

  if (parts.some((part) => !part || part === "." || part === "..")) {
    throw new AppError(400, `${label} must be a safe Library-relative path.`);
  }

  return parts.join("/");
}

function normalizeExpectedHash(value: string | null | undefined): string | null {
  if (value === null || value === undefined) {
    return null;
  }

  const normalized = value.trim().toLowerCase();

  if (!sha256Pattern.test(normalized)) {
    throw new AppError(400, "Expected file hashes must be SHA-256 hex values.");
  }

  return normalized;
}

function normalizeContent(
  value: string | null | undefined,
  label: string,
): string | null {
  if (value === null || value === undefined) {
    return null;
  }

  const sizeBytes = Buffer.byteLength(value, "utf8");

  if (sizeBytes > maxLibraryTextPreviewBytes) {
    throw new AppError(
      413,
      `${label} exceeds the current safe text-edit limit.`,
      {
        sizeBytes,
        maximumBytes: maxLibraryTextPreviewBytes,
      },
    );
  }

  return value;
}

function rejectUnexpected(
  operationType: AIEditOperationType,
  field: string,
  value: unknown,
): void {
  if (value !== null && value !== undefined) {
    throw new AppError(
      400,
      `${operationType} does not accept ${field}.`,
    );
  }
}

function normalizeOperation(
  operation: AIEditOperationDraft,
): AIEditOperationDraft {
  const expectedHash = normalizeExpectedHash(operation.expectedHash);
  const beforeContent = normalizeContent(
    operation.beforeContent,
    "Before content",
  );
  const afterContent = normalizeContent(
    operation.afterContent,
    "After content",
  );

  if (operation.type === "create_file") {
    rejectUnexpected(operation.type, "sourcePath", operation.sourcePath);
    rejectUnexpected(operation.type, "expectedHash", expectedHash);
    rejectUnexpected(operation.type, "beforeContent", beforeContent);

    if (afterContent === null) {
      throw new AppError(400, "create_file requires afterContent.");
    }

    return {
      type: operation.type,
      destinationPath: normalizeRelativePath(
        operation.destinationPath ?? "",
        "Destination path",
      ),
      afterContent,
    };
  }

  if (operation.type === "create_directory") {
    rejectUnexpected(operation.type, "sourcePath", operation.sourcePath);
    rejectUnexpected(operation.type, "expectedHash", expectedHash);
    rejectUnexpected(operation.type, "beforeContent", beforeContent);
    rejectUnexpected(operation.type, "afterContent", afterContent);

    return {
      type: operation.type,
      destinationPath: normalizeRelativePath(
        operation.destinationPath ?? "",
        "Destination path",
      ),
    };
  }

  if (operation.type === "patch_file") {
    rejectUnexpected(
      operation.type,
      "destinationPath",
      operation.destinationPath,
    );

    if (expectedHash === null) {
      throw new AppError(400, "patch_file requires an expectedHash.");
    }

    if (beforeContent === null || afterContent === null) {
      throw new AppError(
        400,
        "patch_file requires beforeContent and afterContent.",
      );
    }

    const sourcePath = normalizeRelativePath(
      operation.sourcePath ?? "",
      "Source path",
    );

    return {
      type: operation.type,
      sourcePath,
      destinationPath: sourcePath,
      expectedHash,
      beforeContent,
      afterContent,
    };
  }

  if (operation.type === "rename_file" || operation.type === "move_file") {
    rejectUnexpected(operation.type, "beforeContent", beforeContent);
    rejectUnexpected(operation.type, "afterContent", afterContent);

    if (expectedHash === null) {
      throw new AppError(
        400,
        `${operation.type} requires an expectedHash.`,
      );
    }

    const sourcePath = normalizeRelativePath(
      operation.sourcePath ?? "",
      "Source path",
    );
    const destinationPath = normalizeRelativePath(
      operation.destinationPath ?? "",
      "Destination path",
    );

    if (sourcePath === destinationPath) {
      throw new AppError(
        400,
        `${operation.type} must change the file path.`,
      );
    }

    if (
      operation.type === "rename_file"
      && path.posix.dirname(sourcePath) !== path.posix.dirname(destinationPath)
    ) {
      throw new AppError(
        400,
        "rename_file must keep the file in its current directory.",
      );
    }

    if (
      operation.type === "move_file"
      && path.posix.basename(sourcePath) !== path.posix.basename(destinationPath)
    ) {
      throw new AppError(
        400,
        "move_file must preserve the file name.",
      );
    }

    return {
      type: operation.type,
      sourcePath,
      destinationPath,
      expectedHash,
    };
  }

  throw new AppError(400, "Unsupported AI edit operation.");
}

function operationPaths(operation: AIEditOperationDraft): string[] {
  return Array.from(
    new Set(
      [operation.sourcePath, operation.destinationPath].filter(
        (value): value is string => typeof value === "string",
      ),
    ),
  );
}

function assertNoOperationConflicts(
  existingOperations: AIEditOperationDraft[],
  incomingOperations: AIEditOperationDraft[],
): void {
  const occupiedPaths = new Map<string, AIEditOperationType>();

  for (const operation of existingOperations) {
    for (const affectedPath of operationPaths(operation)) {
      occupiedPaths.set(affectedPath, operation.type);
    }
  }

  for (const operation of incomingOperations) {
    for (const affectedPath of operationPaths(operation)) {
      const existingType = occupiedPaths.get(affectedPath);

      if (existingType) {
        throw new AppError(
          409,
          `The active edit proposal already contains an operation affecting "${affectedPath}".`,
          {
            affectedPath,
            existingOperationType: existingType,
            incomingOperationType: operation.type,
          },
        );
      }
    }

    for (const affectedPath of operationPaths(operation)) {
      occupiedPaths.set(affectedPath, operation.type);
    }
  }
}

function mergedProposalSummary(
  existingSummary: string,
  incomingSummary: string,
): string {
  return existingSummary === incomingSummary
    ? existingSummary
    : "Multiple proposed Library changes";
}

export function proposeAIEdit(
  input: ProposeAIEditInput,
): AIEditProposal {
  const run = requireAIRun(input.runId);

  if (run.status !== "running") {
    throw new AppError(
      409,
      "New edit proposals can only be created by an active AI Run.",
    );
  }

  if (!run.libraryId) {
    throw new AppError(
      409,
      "This AI Run is not attached to a Library.",
    );
  }

  const summary = input.summary.trim();

  if (!summary || summary.length > maximumSummaryCharacters) {
    throw new AppError(
      400,
      `Edit proposal summaries must contain 1-${maximumSummaryCharacters} characters.`,
    );
  }

  if (
    input.operations.length === 0
    || input.operations.length > maximumOperationsPerProposal
  ) {
    throw new AppError(
      400,
      `Edit proposals must contain 1-${maximumOperationsPerProposal} operations.`,
    );
  }

  const normalizedOperations = input.operations.map(normalizeOperation);
  const skillId = input.skillId?.trim() || null;
  const proposedForRun = listAIEditProposalsByRunId(run.id).filter(
    (proposal) => proposal.status === "proposed",
  );

  if (proposedForRun.length > 1) {
    throw new AppError(
      409,
      "This AI Run has multiple active edit proposals and cannot safely add another operation.",
    );
  }

  const activeProposal = proposedForRun[0];

  if (!activeProposal) {
    assertNoOperationConflicts([], normalizedOperations);

    return createAIEditProposal({
      runId: run.id,
      chatId: run.chatId,
      libraryId: run.libraryId,
      agentId: run.agentId,
      provider: run.provider,
      model: run.model,
      skillId,
      summary,
      operations: normalizedOperations,
    });
  }

  if (activeProposal.skillId !== skillId) {
    throw new AppError(
      409,
      "The active edit proposal was created under different Skill provenance.",
    );
  }

  if (
    activeProposal.operations.length + normalizedOperations.length
    > maximumOperationsPerProposal
  ) {
    throw new AppError(
      400,
      `Edit proposals must contain at most ${maximumOperationsPerProposal} operations.`,
    );
  }

  assertNoOperationConflicts(
    activeProposal.operations,
    normalizedOperations,
  );

  return appendAIEditProposalOperations({
    proposalId: activeProposal.id,
    summary: mergedProposalSummary(activeProposal.summary, summary),
    operations: normalizedOperations,
  });
}

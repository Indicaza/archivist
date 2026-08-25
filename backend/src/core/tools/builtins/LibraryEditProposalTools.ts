import { createHash } from "node:crypto";
import { z } from "zod";
import { proposeAIEdit } from "../../../api/cognition/edits/services/AIEditProposalService.js";
import { aiRunEventBroker } from "../../../api/cognition/runs/services/AIRunEventBroker.js";
import {
  inspectLibraryCreateTarget,
  inspectLibraryDirectoryCreateTarget,
  inspectLibraryTextFileForEdit,
} from "../../../api/libraries/services/LibraryEditInspection.js";
import {
  AIToolError,
  type AIToolContext,
  type AIToolDefinition,
} from "../AIToolTypes.js";

const createFileInputSchema = z
  .object({
    path: z.string().min(1).max(1_024),
    content: z.string(),
  })
  .strict();

const createFileOutputSchema = z
  .object({
    proposalId: z.string().uuid(),
    operationId: z.string().uuid(),
    status: z.literal("proposed"),
    operationType: z.literal("create_file"),
    relativePath: z.string(),
    expectedState: z.literal("absent"),
    contentSha256: z.string().length(64),
    writesPerformed: z.literal(false),
  })
  .strict();

const patchFileInputSchema = z
  .object({
    path: z.string().min(1).max(1_024),
    content: z.string(),
  })
  .strict();

const patchFileOutputSchema = z
  .object({
    proposalId: z.string().uuid(),
    operationId: z.string().uuid(),
    status: z.literal("proposed"),
    operationType: z.literal("patch_file"),
    relativePath: z.string(),
    expectedHash: z.string().length(64),
    proposedContentSha256: z.string().length(64),
    beforeSizeBytes: z.number().int().nonnegative(),
    afterSizeBytes: z.number().int().nonnegative(),
    sourceModifiedAt: z.string(),
    writesPerformed: z.literal(false),
  })
  .strict();

const createDirectoryInputSchema = z
  .object({
    path: z.string().min(1).max(1_024),
  })
  .strict();

const createDirectoryOutputSchema = z
  .object({
    proposalId: z.string().uuid(),
    operationId: z.string().uuid(),
    status: z.literal("proposed"),
    operationType: z.literal("create_directory"),
    relativePath: z.string(),
    expectedState: z.literal("absent"),
    writesPerformed: z.literal(false),
  })
  .strict();

const relocateFileInputSchema = z
  .object({
    sourcePath: z.string().min(1).max(1_024),
    destinationPath: z.string().min(1).max(1_024),
  })
  .strict();

const renameFileOutputSchema = z
  .object({
    proposalId: z.string().uuid(),
    operationId: z.string().uuid(),
    status: z.literal("proposed"),
    operationType: z.literal("rename_file"),
    sourcePath: z.string(),
    destinationPath: z.string(),
    expectedHash: z.string().length(64),
    sourceSizeBytes: z.number().int().nonnegative(),
    sourceModifiedAt: z.string(),
    writesPerformed: z.literal(false),
  })
  .strict();

const moveFileOutputSchema = z
  .object({
    proposalId: z.string().uuid(),
    operationId: z.string().uuid(),
    status: z.literal("proposed"),
    operationType: z.literal("move_file"),
    sourcePath: z.string(),
    destinationPath: z.string(),
    expectedHash: z.string().length(64),
    sourceSizeBytes: z.number().int().nonnegative(),
    sourceModifiedAt: z.string(),
    writesPerformed: z.literal(false),
  })
  .strict();

type CreateFileInput = z.infer<typeof createFileInputSchema>;
type CreateFileOutput = z.infer<typeof createFileOutputSchema>;
type PatchFileInput = z.infer<typeof patchFileInputSchema>;
type PatchFileOutput = z.infer<typeof patchFileOutputSchema>;
type CreateDirectoryInput = z.infer<typeof createDirectoryInputSchema>;
type CreateDirectoryOutput = z.infer<typeof createDirectoryOutputSchema>;
type RelocateFileInput = z.infer<typeof relocateFileInputSchema>;
type RenameFileOutput = z.infer<typeof renameFileOutputSchema>;
type MoveFileOutput = z.infer<typeof moveFileOutputSchema>;

function requireLibraryId(context: AIToolContext): string {
  if (!context.libraryId) {
    throw new AIToolError(
      "invalid_input",
      "This AI tool requires an active Library.",
    );
  }

  return context.libraryId;
}

function throwIfAborted(signal: AbortSignal): void {
  if (!signal.aborted) {
    return;
  }

  const error = new Error("AI tool cancelled.");
  error.name = "AbortError";
  throw error;
}

export const createFileProposalTool: AIToolDefinition<
  CreateFileInput,
  CreateFileOutput
> = {
  id: "create_file",
  name: "Propose Library File Creation",
  description:
    "Create a reviewable proposal for a new UTF-8 text file in the active Library. This tool never writes the file; execution requires later user approval.",
  permission: "safe-local-mutation",
  executionMode: "proposal",
  inputSchema: createFileInputSchema,
  inputJsonSchema: {
    type: "object",
    properties: {
      path: {
        type: "string",
        minLength: 1,
        maxLength: 1024,
        description:
          "Library-relative destination path for the proposed new file.",
      },
      content: {
        type: "string",
        description:
          "Complete UTF-8 text content proposed for the new file.",
      },
    },
    required: ["path", "content"],
    additionalProperties: false,
  },
  outputSchema: createFileOutputSchema,
  timeoutMs: 10_000,
  async execute(input, context) {
    throwIfAborted(context.signal);
    const libraryId = requireLibraryId(context);
    const inspection = await inspectLibraryCreateTarget(
      libraryId,
      input.path,
    );
    throwIfAborted(context.signal);

    const proposal = proposeAIEdit({
      runId: context.runId,
      summary: `Create ${inspection.relativePath}`,
      operations: [
        {
          type: "create_file",
          destinationPath: inspection.relativePath,
          afterContent: input.content,
        },
      ],
    });
    const operation = proposal.operations[proposal.operations.length - 1];

    if (!operation) {
      throw new Error(
        "The create-file proposal did not retain its operation.",
      );
    }

    const output: CreateFileOutput = {
      proposalId: proposal.id,
      operationId: operation.id,
      status: "proposed",
      operationType: "create_file",
      relativePath: inspection.relativePath,
      expectedState: inspection.expectedState,
      contentSha256: createHash("sha256")
        .update(input.content, "utf8")
        .digest("hex"),
      writesPerformed: false,
    };

    aiRunEventBroker.append(context.runId, "edit.proposed", {
      proposalId: proposal.id,
      executionId: context.executionId,
      operationId: operation.id,
      operationCount: proposal.operations.length,
      operationTypes: proposal.operations.map((item) => item.type),
      affectedPaths: Array.from(
        new Set(
          proposal.operations.flatMap((item) =>
            [item.sourcePath, item.destinationPath].filter(
              (value): value is string => typeof value === "string",
            ),
          ),
        ),
      ),
      writesPerformed: false,
    });

    return output;
  },
  summarizeInput(input) {
    return {
      relativePath: input.path,
      contentCharacterCount: input.content.length,
    };
  },
  summarizeOutput(output) {
    return {
      proposalId: output.proposalId,
      relativePath: output.relativePath,
      expectedState: output.expectedState,
      writesPerformed: output.writesPerformed,
    };
  },
};

export const patchFileProposalTool: AIToolDefinition<
  PatchFileInput,
  PatchFileOutput
> = {
  id: "patch_file",
  name: "Propose Library File Patch",
  description:
    "Create a reviewable proposal to replace the complete UTF-8 text content of an existing Library file. Archivist snapshots the exact current content and SHA-256 first; this tool never writes the file.",
  permission: "safe-local-mutation",
  executionMode: "proposal",
  inputSchema: patchFileInputSchema,
  inputJsonSchema: {
    type: "object",
    properties: {
      path: {
        type: "string",
        minLength: 1,
        maxLength: 1024,
        description:
          "Library-relative path of the existing text file to change.",
      },
      content: {
        type: "string",
        description:
          "Complete replacement UTF-8 text content proposed for the file. Preserve any content that should remain unchanged.",
      },
    },
    required: ["path", "content"],
    additionalProperties: false,
  },
  outputSchema: patchFileOutputSchema,
  timeoutMs: 10_000,
  async execute(input, context) {
    throwIfAborted(context.signal);
    const libraryId = requireLibraryId(context);
    const inspection = await inspectLibraryTextFileForEdit(
      libraryId,
      input.path,
    );
    throwIfAborted(context.signal);

    if (inspection.content === input.content) {
      throw new AIToolError(
        "invalid_input",
        "patch_file requires proposed content to differ from the current file.",
      );
    }

    const proposal = proposeAIEdit({
      runId: context.runId,
      summary: `Patch ${inspection.relativePath}`,
      operations: [
        {
          type: "patch_file",
          sourcePath: inspection.relativePath,
          expectedHash: inspection.sha256,
          beforeContent: inspection.content,
          afterContent: input.content,
        },
      ],
    });
    const operation = proposal.operations[proposal.operations.length - 1];

    if (!operation) {
      throw new Error(
        "The patch-file proposal did not retain its operation.",
      );
    }

    const output: PatchFileOutput = {
      proposalId: proposal.id,
      operationId: operation.id,
      status: "proposed",
      operationType: "patch_file",
      relativePath: inspection.relativePath,
      expectedHash: inspection.sha256,
      proposedContentSha256: createHash("sha256")
        .update(input.content, "utf8")
        .digest("hex"),
      beforeSizeBytes: inspection.sizeBytes,
      afterSizeBytes: Buffer.byteLength(input.content, "utf8"),
      sourceModifiedAt: inspection.modifiedAt,
      writesPerformed: false,
    };

    aiRunEventBroker.append(context.runId, "edit.proposed", {
      proposalId: proposal.id,
      executionId: context.executionId,
      operationId: operation.id,
      operationCount: proposal.operations.length,
      operationTypes: proposal.operations.map((item) => item.type),
      affectedPaths: Array.from(
        new Set(
          proposal.operations.flatMap((item) =>
            [item.sourcePath, item.destinationPath].filter(
              (value): value is string => typeof value === "string",
            ),
          ),
        ),
      ),
      expectedHash: inspection.sha256,
      writesPerformed: false,
    });

    return output;
  },
  summarizeInput(input) {
    return {
      relativePath: input.path,
      contentCharacterCount: input.content.length,
    };
  },
  summarizeOutput(output) {
    return {
      proposalId: output.proposalId,
      relativePath: output.relativePath,
      expectedHash: output.expectedHash,
      writesPerformed: output.writesPerformed,
    };
  },
};

export const createDirectoryProposalTool: AIToolDefinition<
  CreateDirectoryInput,
  CreateDirectoryOutput
> = {
  id: "create_directory",
  name: "Propose Library Directory Creation",
  description:
    "Create a reviewable proposal for a new directory in the active Library. The parent directory must already exist. This tool never creates the directory; execution requires later user approval.",
  permission: "safe-local-mutation",
  executionMode: "proposal",
  inputSchema: createDirectoryInputSchema,
  inputJsonSchema: {
    type: "object",
    properties: {
      path: {
        type: "string",
        minLength: 1,
        maxLength: 1024,
        description:
          "Library-relative path for the proposed new directory. Its parent directory must already exist.",
      },
    },
    required: ["path"],
    additionalProperties: false,
  },
  outputSchema: createDirectoryOutputSchema,
  timeoutMs: 10_000,
  async execute(input, context) {
    throwIfAborted(context.signal);
    const libraryId = requireLibraryId(context);
    const inspection = await inspectLibraryDirectoryCreateTarget(
      libraryId,
      input.path,
    );
    throwIfAborted(context.signal);

    const proposal = proposeAIEdit({
      runId: context.runId,
      summary: `Create directory ${inspection.relativePath}`,
      operations: [
        {
          type: "create_directory",
          destinationPath: inspection.relativePath,
        },
      ],
    });
    const operation = proposal.operations[proposal.operations.length - 1];

    if (!operation) {
      throw new Error(
        "The create-directory proposal did not retain its operation.",
      );
    }

    const output: CreateDirectoryOutput = {
      proposalId: proposal.id,
      operationId: operation.id,
      status: "proposed",
      operationType: "create_directory",
      relativePath: inspection.relativePath,
      expectedState: inspection.expectedState,
      writesPerformed: false,
    };

    aiRunEventBroker.append(context.runId, "edit.proposed", {
      proposalId: proposal.id,
      executionId: context.executionId,
      operationId: operation.id,
      operationCount: proposal.operations.length,
      operationTypes: proposal.operations.map((item) => item.type),
      affectedPaths: Array.from(
        new Set(
          proposal.operations.flatMap((item) =>
            [item.sourcePath, item.destinationPath].filter(
              (value): value is string => typeof value === "string",
            ),
          ),
        ),
      ),
      writesPerformed: false,
    });

    return output;
  },
  summarizeInput(input) {
    return {
      relativePath: input.path,
    };
  },
  summarizeOutput(output) {
    return {
      proposalId: output.proposalId,
      relativePath: output.relativePath,
      expectedState: output.expectedState,
      writesPerformed: output.writesPerformed,
    };
  },
};

export const renameFileProposalTool: AIToolDefinition<
  RelocateFileInput,
  RenameFileOutput
> = {
  id: "rename_file",
  name: "Propose Library File Rename",
  description:
    "Create a reviewable proposal to rename an existing supported UTF-8 Library text file within its current directory. Archivist snapshots the exact current SHA-256 and requires the destination to be absent. This tool never renames the file.",
  permission: "safe-local-mutation",
  executionMode: "proposal",
  inputSchema: relocateFileInputSchema,
  inputJsonSchema: {
    type: "object",
    properties: {
      sourcePath: {
        type: "string",
        minLength: 1,
        maxLength: 1024,
        description:
          "Library-relative path of the existing text file to rename.",
      },
      destinationPath: {
        type: "string",
        minLength: 1,
        maxLength: 1024,
        description:
          "Library-relative destination path in the same directory, using the new file name.",
      },
    },
    required: ["sourcePath", "destinationPath"],
    additionalProperties: false,
  },
  outputSchema: renameFileOutputSchema,
  timeoutMs: 10_000,
  async execute(input, context) {
    throwIfAborted(context.signal);
    const libraryId = requireLibraryId(context);
    const source = await inspectLibraryTextFileForEdit(
      libraryId,
      input.sourcePath,
    );
    const destination = await inspectLibraryCreateTarget(
      libraryId,
      input.destinationPath,
    );
    throwIfAborted(context.signal);

    const proposal = proposeAIEdit({
      runId: context.runId,
      summary: `Rename ${source.relativePath} to ${destination.relativePath}`,
      operations: [
        {
          type: "rename_file",
          sourcePath: source.relativePath,
          destinationPath: destination.relativePath,
          expectedHash: source.sha256,
        },
      ],
    });
    const operation = proposal.operations[proposal.operations.length - 1];

    if (!operation) {
      throw new Error(
        "The rename-file proposal did not retain its operation.",
      );
    }

    const output: RenameFileOutput = {
      proposalId: proposal.id,
      operationId: operation.id,
      status: "proposed",
      operationType: "rename_file",
      sourcePath: source.relativePath,
      destinationPath: destination.relativePath,
      expectedHash: source.sha256,
      sourceSizeBytes: source.sizeBytes,
      sourceModifiedAt: source.modifiedAt,
      writesPerformed: false,
    };

    aiRunEventBroker.append(context.runId, "edit.proposed", {
      proposalId: proposal.id,
      executionId: context.executionId,
      operationId: operation.id,
      operationCount: proposal.operations.length,
      operationTypes: proposal.operations.map((item) => item.type),
      affectedPaths: Array.from(
        new Set(
          proposal.operations.flatMap((item) =>
            [item.sourcePath, item.destinationPath].filter(
              (value): value is string => typeof value === "string",
            ),
          ),
        ),
      ),
      expectedHash: source.sha256,
      writesPerformed: false,
    });

    return output;
  },
  summarizeInput(input) {
    return {
      sourcePath: input.sourcePath,
      destinationPath: input.destinationPath,
    };
  },
  summarizeOutput(output) {
    return {
      proposalId: output.proposalId,
      sourcePath: output.sourcePath,
      destinationPath: output.destinationPath,
      expectedHash: output.expectedHash,
      writesPerformed: output.writesPerformed,
    };
  },
};

export const moveFileProposalTool: AIToolDefinition<
  RelocateFileInput,
  MoveFileOutput
> = {
  id: "move_file",
  name: "Propose Library File Move",
  description:
    "Create a reviewable proposal to move an existing supported UTF-8 Library text file to another existing Library directory while preserving its file name. Archivist snapshots the exact current SHA-256 and requires the destination to be absent. This tool never moves the file.",
  permission: "safe-local-mutation",
  executionMode: "proposal",
  inputSchema: relocateFileInputSchema,
  inputJsonSchema: {
    type: "object",
    properties: {
      sourcePath: {
        type: "string",
        minLength: 1,
        maxLength: 1024,
        description:
          "Library-relative path of the existing text file to move.",
      },
      destinationPath: {
        type: "string",
        minLength: 1,
        maxLength: 1024,
        description:
          "Library-relative path in another existing directory. Preserve the source file name.",
      },
    },
    required: ["sourcePath", "destinationPath"],
    additionalProperties: false,
  },
  outputSchema: moveFileOutputSchema,
  timeoutMs: 10_000,
  async execute(input, context) {
    throwIfAborted(context.signal);
    const libraryId = requireLibraryId(context);
    const source = await inspectLibraryTextFileForEdit(
      libraryId,
      input.sourcePath,
    );
    const destination = await inspectLibraryCreateTarget(
      libraryId,
      input.destinationPath,
    );
    throwIfAborted(context.signal);

    const proposal = proposeAIEdit({
      runId: context.runId,
      summary: `Move ${source.relativePath} to ${destination.relativePath}`,
      operations: [
        {
          type: "move_file",
          sourcePath: source.relativePath,
          destinationPath: destination.relativePath,
          expectedHash: source.sha256,
        },
      ],
    });
    const operation = proposal.operations[proposal.operations.length - 1];

    if (!operation) {
      throw new Error(
        "The move-file proposal did not retain its operation.",
      );
    }

    const output: MoveFileOutput = {
      proposalId: proposal.id,
      operationId: operation.id,
      status: "proposed",
      operationType: "move_file",
      sourcePath: source.relativePath,
      destinationPath: destination.relativePath,
      expectedHash: source.sha256,
      sourceSizeBytes: source.sizeBytes,
      sourceModifiedAt: source.modifiedAt,
      writesPerformed: false,
    };

    aiRunEventBroker.append(context.runId, "edit.proposed", {
      proposalId: proposal.id,
      executionId: context.executionId,
      operationId: operation.id,
      operationCount: proposal.operations.length,
      operationTypes: proposal.operations.map((item) => item.type),
      affectedPaths: Array.from(
        new Set(
          proposal.operations.flatMap((item) =>
            [item.sourcePath, item.destinationPath].filter(
              (value): value is string => typeof value === "string",
            ),
          ),
        ),
      ),
      expectedHash: source.sha256,
      writesPerformed: false,
    });

    return output;
  },
  summarizeInput(input) {
    return {
      sourcePath: input.sourcePath,
      destinationPath: input.destinationPath,
    };
  },
  summarizeOutput(output) {
    return {
      proposalId: output.proposalId,
      sourcePath: output.sourcePath,
      destinationPath: output.destinationPath,
      expectedHash: output.expectedHash,
      writesPerformed: output.writesPerformed,
    };
  },
};

export const libraryEditProposalTools = [
  createFileProposalTool,
  patchFileProposalTool,
  createDirectoryProposalTool,
  renameFileProposalTool,
  moveFileProposalTool,
] as const;

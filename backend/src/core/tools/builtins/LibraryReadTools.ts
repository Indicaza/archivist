import { z } from "zod";
import { getLibraryFileCatalog } from "../../../api/libraries/models/LibraryFile.js";
import { readLibraryFileText } from "../../../api/libraries/services/LibraryFileReader.js";
import { libraryFileFtsTool } from "../../cognition/retrieval/tools/LibraryFileFtsTool.js";
import {
  AIToolError,
  type AIToolContext,
  type AIToolDefinition,
} from "../AIToolTypes.js";

const maximumDirectoryEntries = 200;
const maximumFilenameMatches = 100;
const maximumSearchMatches = 24;
const maximumReadCharacters = 64_000;
const maximumReadRangeLines = 160;
const maximumBatchReadRanges = 3;
const maximumBatchReadLines = 240;
const maximumBatchReadCharacters = 48_000;

const fileSummarySchema = z
  .object({
    fileId: z.string().min(1).max(2_048),
    relativePath: z.string(),
    name: z.string(),
    extension: z.string(),
    sizeBytes: z.number().int().nonnegative(),
    status: z.enum(["available", "unreadable", "missing"]),
  })
  .strict();

const directoryEntrySchema = z
  .object({
    type: z.enum(["directory", "file"]),
    name: z.string(),
    relativePath: z.string(),
    file: fileSummarySchema.nullable(),
  })
  .strict();

const searchMatchSchema = z
  .object({
    fileId: z.string().min(1).max(2_048),
    relativePath: z.string(),
    fileName: z.string(),
    startLine: z.number().int().positive(),
    endLine: z.number().int().positive(),
    content: z.string(),
    estimatedTokens: z.number().int().nonnegative(),
    score: z.number().nonnegative(),
    reason: z.string(),
  })
  .strict();

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

function normalizeRelativeDirectory(value: string): string {
  const normalized = value
    .replaceAll("\\", "/")
    .trim()
    .replace(/^\/+|\/+$/g, "");
  const parts = normalized.split("/").filter(Boolean);

  if (parts.some((part) => part === "." || part === "..")) {
    throw new AIToolError(
      "invalid_input",
      "Library directory paths may not contain traversal segments.",
    );
  }

  return parts.join("/");
}

function normalizeRelativeFileReference(value: string): string {
  const normalized = value
    .replaceAll("\\", "/")
    .trim()
    .replace(/^\/+/, "");
  const parts = normalized.split("/").filter(Boolean);

  if (
    parts.length === 0
    || parts.some((part) => part === "." || part === "..")
  ) {
    throw new AIToolError(
      "invalid_input",
      "Library file references must be a catalog ID or safe Library-relative path.",
    );
  }

  return parts.join("/");
}

function resolveLibraryFileId(
  libraryId: string,
  fileReference: string,
): string {
  const files = getLibraryFileCatalog(libraryId).files;
  const directMatch = files.find((file) => file.id === fileReference);

  if (directMatch) {
    return directMatch.id;
  }

  const relativePath = normalizeRelativeFileReference(fileReference);
  const pathMatch = files.find((file) => file.relativePath === relativePath);

  if (!pathMatch) {
    throw new AIToolError(
      "invalid_input",
      `Library file reference "${relativePath}" was not found in the active catalog.`,
    );
  }

  return pathMatch.id;
}

function fileSummary(file: ReturnType<typeof getLibraryFileCatalog>["files"][number]) {
  return {
    fileId: file.id,
    relativePath: file.relativePath,
    name: file.name,
    extension: file.extension,
    sizeBytes: file.sizeBytes,
    status: file.status,
  };
}

function metadataString(
  metadata: Record<string, unknown>,
  key: string,
): string {
  const value = metadata[key];
  return typeof value === "string" ? value : "";
}

function metadataNumber(
  metadata: Record<string, unknown>,
  key: string,
): number {
  const value = metadata[key];
  return typeof value === "number" && Number.isFinite(value) ? value : 0;
}

const listDirectoryInputSchema = z
  .object({
    path: z.string().max(1_024).default(""),
    limit: z.number().int().min(1).max(maximumDirectoryEntries).default(100),
  })
  .strict();

const listDirectoryOutputSchema = z
  .object({
    path: z.string(),
    entries: z.array(directoryEntrySchema),
    totalEntries: z.number().int().nonnegative(),
    truncated: z.boolean(),
  })
  .strict();

type ListDirectoryInput = z.infer<typeof listDirectoryInputSchema>;
type ListDirectoryOutput = z.infer<typeof listDirectoryOutputSchema>;

export const listDirectoryTool: AIToolDefinition<
  ListDirectoryInput,
  ListDirectoryOutput
> = {
  id: "list_directory",
  name: "List Library Directory",
  description:
    "List the immediate files and folders beneath one directory in the active Library catalog.",
  permission: "read-only",
  inputSchema: listDirectoryInputSchema,
  inputJsonSchema: {
    type: "object",
    properties: {
      path: { type: "string", maxLength: 1024 },
      limit: {
        type: "integer",
        minimum: 1,
        maximum: maximumDirectoryEntries,
      },
    },
    required: ["path", "limit"],
    additionalProperties: false,
  },
  outputSchema: listDirectoryOutputSchema,
  timeoutMs: 5_000,
  execute(input, context) {
    throwIfAborted(context.signal);
    const libraryId = requireLibraryId(context);
    const directory = normalizeRelativeDirectory(input.path);
    const prefix = directory ? `${directory}/` : "";
    const entries = new Map<
      string,
      z.infer<typeof directoryEntrySchema>
    >();

    for (const file of getLibraryFileCatalog(libraryId).files) {
      if (!file.relativePath.startsWith(prefix)) {
        continue;
      }

      const remainder = file.relativePath.slice(prefix.length);

      if (!remainder) {
        continue;
      }

      const separatorIndex = remainder.indexOf("/");

      if (separatorIndex >= 0) {
        const name = remainder.slice(0, separatorIndex);
        const relativePath = directory ? `${directory}/${name}` : name;
        entries.set(`directory:${relativePath}`, {
          type: "directory",
          name,
          relativePath,
          file: null,
        });
        continue;
      }

      entries.set(`file:${file.id}`, {
        type: "file",
        name: file.name,
        relativePath: file.relativePath,
        file: fileSummary(file),
      });
    }

    const sortedEntries = Array.from(entries.values()).sort((first, second) => {
      if (first.type !== second.type) {
        return first.type === "directory" ? -1 : 1;
      }

      return first.name.localeCompare(second.name, undefined, {
        sensitivity: "base",
      });
    });

    return {
      path: directory,
      entries: sortedEntries.slice(0, input.limit),
      totalEntries: sortedEntries.length,
      truncated: sortedEntries.length > input.limit,
    };
  },
  summarizeInput(input) {
    return {
      path: input.path || "/",
      limit: input.limit,
    };
  },
  summarizeOutput(output) {
    return {
      path: output.path || "/",
      entryCount: output.entries.length,
      totalEntries: output.totalEntries,
      truncated: output.truncated,
    };
  },
};

const searchFilenamesInputSchema = z
  .object({
    query: z.string().trim().min(1).max(256),
    limit: z.number().int().min(1).max(maximumFilenameMatches).default(25),
  })
  .strict();

const searchFilenamesOutputSchema = z
  .object({
    query: z.string(),
    matches: z.array(fileSummarySchema),
    totalMatches: z.number().int().nonnegative(),
    truncated: z.boolean(),
  })
  .strict();

type SearchFilenamesInput = z.infer<typeof searchFilenamesInputSchema>;
type SearchFilenamesOutput = z.infer<typeof searchFilenamesOutputSchema>;

export const searchFilenamesTool: AIToolDefinition<
  SearchFilenamesInput,
  SearchFilenamesOutput
> = {
  id: "search_filenames",
  name: "Search Library Filenames",
  description:
    "Find active Library files whose name or relative path contains a case-insensitive text query.",
  permission: "read-only",
  inputSchema: searchFilenamesInputSchema,
  inputJsonSchema: {
    type: "object",
    properties: {
      query: { type: "string", minLength: 1, maxLength: 256 },
      limit: {
        type: "integer",
        minimum: 1,
        maximum: maximumFilenameMatches,
      },
    },
    required: ["query", "limit"],
    additionalProperties: false,
  },
  outputSchema: searchFilenamesOutputSchema,
  timeoutMs: 5_000,
  execute(input, context) {
    throwIfAborted(context.signal);
    const libraryId = requireLibraryId(context);
    const normalizedQuery = input.query.toLocaleLowerCase();
    const matches = getLibraryFileCatalog(libraryId).files.filter((file) =>
      `${file.name}\n${file.relativePath}`
        .toLocaleLowerCase()
        .includes(normalizedQuery),
    );

    return {
      query: input.query,
      matches: matches.slice(0, input.limit).map(fileSummary),
      totalMatches: matches.length,
      truncated: matches.length > input.limit,
    };
  },
  summarizeInput(input) {
    return {
      query: input.query,
      limit: input.limit,
    };
  },
  summarizeOutput(output) {
    return {
      matchCount: output.matches.length,
      totalMatches: output.totalMatches,
      truncated: output.truncated,
    };
  },
};

const searchLibraryInputSchema = z
  .object({
    query: z.string().trim().min(1).max(2_000),
    limit: z.number().int().min(1).max(maximumSearchMatches).default(8),
  })
  .strict();

const searchLibraryOutputSchema = z
  .object({
    query: z.string(),
    matches: z.array(searchMatchSchema),
    warnings: z.array(z.string()),
    searchedAt: z.string(),
    durationMs: z.number().nonnegative(),
  })
  .strict();

type SearchLibraryInput = z.infer<typeof searchLibraryInputSchema>;
type SearchLibraryOutput = z.infer<typeof searchLibraryOutputSchema>;

export const searchLibraryTool: AIToolDefinition<
  SearchLibraryInput,
  SearchLibraryOutput
> = {
  id: "search_library",
  name: "Search Library Text",
  description:
    "Search indexed Library text with SQLite FTS5 and return grounded file excerpts with line ranges.",
  permission: "read-only",
  inputSchema: searchLibraryInputSchema,
  inputJsonSchema: {
    type: "object",
    properties: {
      query: { type: "string", minLength: 1, maxLength: 2000 },
      limit: {
        type: "integer",
        minimum: 1,
        maximum: maximumSearchMatches,
      },
    },
    required: ["query", "limit"],
    additionalProperties: false,
  },
  outputSchema: searchLibraryOutputSchema,
  timeoutMs: 5_000,
  execute(input, context) {
    throwIfAborted(context.signal);
    const libraryId = requireLibraryId(context);
    const result = libraryFileFtsTool.search({
      libraryId,
      query: input.query,
      limit: input.limit,
    });

    return {
      query: result.query,
      matches: result.candidates.map((candidate) => ({
        fileId: metadataString(candidate.metadata, "fileId"),
        relativePath: metadataString(candidate.metadata, "relativePath"),
        fileName: metadataString(candidate.metadata, "fileName"),
        startLine: Math.max(1, metadataNumber(candidate.metadata, "startLine")),
        endLine: Math.max(1, metadataNumber(candidate.metadata, "endLine")),
        content: candidate.content,
        estimatedTokens: candidate.estimatedTokens,
        score: candidate.score,
        reason: candidate.reason,
      })),
      warnings: result.warnings,
      searchedAt: result.searchedAt,
      durationMs: result.durationMs,
    };
  },
  summarizeInput(input) {
    return {
      query: input.query,
      limit: input.limit,
    };
  },
  summarizeOutput(output) {
    return {
      matchCount: output.matches.length,
      fileCount: new Set(output.matches.map((match: SearchLibraryOutput["matches"][number]) => match.fileId)).size,
      warningCount: output.warnings.length,
      durationMs: output.durationMs,
    };
  },
};

const readFileInputSchema = z
  .object({
    fileId: z.string().min(1).max(2_048),
  })
  .strict();

const readFileOutputSchema = z
  .object({
    file: fileSummarySchema,
    content: z.string(),
    encoding: z.literal("utf-8"),
    lineCount: z.number().int().nonnegative(),
    totalCharacterCount: z.number().int().nonnegative(),
    truncated: z.boolean(),
    readAt: z.string(),
  })
  .strict();

type ReadFileInput = z.infer<typeof readFileInputSchema>;
type ReadFileOutput = z.infer<typeof readFileOutputSchema>;

export const readFileTool: AIToolDefinition<ReadFileInput, ReadFileOutput> = {
  id: "read_file",
  name: "Read Library File",
  description:
    "Read a guarded UTF-8 Library file by catalog ID or exact Library-relative path, capped for safe tool output.",
  permission: "read-only",
  inputSchema: readFileInputSchema,
  inputJsonSchema: {
    type: "object",
    properties: {
      fileId: {
        type: "string",
        minLength: 1,
        maxLength: 2048,
        description:
          "Catalog UUID from a source file-id attribute, or the exact Library-relative path from its path attribute.",
      },
    },
    required: ["fileId"],
    additionalProperties: false,
  },
  outputSchema: readFileOutputSchema,
  timeoutMs: 10_000,
  async execute(input, context) {
    throwIfAborted(context.signal);
    const libraryId = requireLibraryId(context);
    const resolvedFileId = resolveLibraryFileId(libraryId, input.fileId);
    const preview = await readLibraryFileText(libraryId, resolvedFileId);
    throwIfAborted(context.signal);

    return {
      file: fileSummary(preview.file),
      content: preview.content.slice(0, maximumReadCharacters),
      encoding: preview.encoding,
      lineCount: preview.lineCount,
      totalCharacterCount: preview.content.length,
      truncated: preview.content.length > maximumReadCharacters,
      readAt: preview.readAt,
    };
  },
  summarizeInput(input) {
    return {
      fileReference: input.fileId,
    };
  },
  summarizeOutput(output) {
    return {
      relativePath: output.file.relativePath,
      lineCount: output.lineCount,
      characterCount: output.content.length,
      truncated: output.truncated,
    };
  },
};

const readFileRangeInputSchema = z
  .object({
    fileId: z.string().min(1).max(2_048),
    startLine: z.number().int().positive(),
    endLine: z.number().int().positive(),
  })
  .strict()
  .refine((input: { startLine: number; endLine: number }) => input.endLine >= input.startLine, {
    message: "endLine must be greater than or equal to startLine.",
    path: ["endLine"],
  })
  .refine(
    (input: { startLine: number; endLine: number }) =>
      input.endLine - input.startLine + 1 <= maximumReadRangeLines,
    {
      message: `A file range may include at most ${maximumReadRangeLines} lines.`,
      path: ["endLine"],
    },
  );

const readFileRangeOutputSchema = z
  .object({
    file: fileSummarySchema,
    requestedStartLine: z.number().int().positive(),
    requestedEndLine: z.number().int().positive(),
    startLine: z.number().int().positive(),
    endLine: z.number().int().positive(),
    totalLineCount: z.number().int().nonnegative(),
    content: z.string(),
    totalCharacterCount: z.number().int().nonnegative(),
    truncated: z.boolean(),
    readAt: z.string(),
  })
  .strict();

type ReadFileRangeInput = z.infer<typeof readFileRangeInputSchema>;
type ReadFileRangeOutput = z.infer<typeof readFileRangeOutputSchema>;

export const readFileRangeTool: AIToolDefinition<
  ReadFileRangeInput,
  ReadFileRangeOutput
> = {
  id: "read_file_range",
  name: "Read Library File Range",
  description:
    `Read at most ${maximumReadRangeLines} lines from a guarded UTF-8 Library file by catalog ID or exact Library-relative path. Prefer copying one supplied source block's path and line range exactly.`,
  permission: "read-only",
  inputSchema: readFileRangeInputSchema,
  inputJsonSchema: {
    type: "object",
    properties: {
      fileId: {
        type: "string",
        minLength: 1,
        maxLength: 2048,
        description:
          "Catalog UUID from a source file-id attribute, or the exact Library-relative path from its path attribute.",
      },
      startLine: { type: "integer", minimum: 1 },
      endLine: { type: "integer", minimum: 1 },
    },
    required: ["fileId", "startLine", "endLine"],
    additionalProperties: false,
  },
  outputSchema: readFileRangeOutputSchema,
  timeoutMs: 10_000,
  async execute(input, context) {
    throwIfAborted(context.signal);
    const libraryId = requireLibraryId(context);
    const resolvedFileId = resolveLibraryFileId(libraryId, input.fileId);
    const preview = await readLibraryFileText(libraryId, resolvedFileId);
    throwIfAborted(context.signal);

    if (preview.lineCount === 0 || input.startLine > preview.lineCount) {
      throw new AIToolError(
        "invalid_input",
        `startLine ${input.startLine} is outside this ${preview.lineCount}-line file.`,
      );
    }

    const lines = preview.content.split(/\r\n|\r|\n/);
    const endLine = Math.min(input.endLine, lines.length);
    const fullContent = lines.slice(input.startLine - 1, endLine).join("\n");

    return {
      file: fileSummary(preview.file),
      requestedStartLine: input.startLine,
      requestedEndLine: input.endLine,
      startLine: input.startLine,
      endLine,
      totalLineCount: preview.lineCount,
      content: fullContent.slice(0, maximumReadCharacters),
      totalCharacterCount: fullContent.length,
      truncated: fullContent.length > maximumReadCharacters,
      readAt: preview.readAt,
    };
  },
  summarizeInput(input) {
    return {
      fileReference: input.fileId,
      startLine: input.startLine,
      endLine: input.endLine,
    };
  },
  summarizeOutput(output) {
    return {
      relativePath: output.file.relativePath,
      startLine: output.startLine,
      endLine: output.endLine,
      characterCount: output.content.length,
      truncated: output.truncated,
    };
  },
};

const readFileRangesInputSchema = z
  .object({
    ranges: z
      .array(readFileRangeInputSchema)
      .min(1)
      .max(maximumBatchReadRanges),
  })
  .strict()
  .refine(
    (input: { ranges: ReadFileRangeInput[] }) =>
      input.ranges.reduce(
        (total, range) => total + range.endLine - range.startLine + 1,
        0,
      ) <= maximumBatchReadLines,
    {
      message: `A batched file read may request at most ${maximumBatchReadLines} total lines.`,
      path: ["ranges"],
    },
  );

const readFileRangesOutputSchema = z
  .object({
    ranges: z.array(readFileRangeOutputSchema),
    rangeCount: z.number().int().nonnegative(),
    totalCharacterCount: z.number().int().nonnegative(),
    returnedCharacterCount: z.number().int().nonnegative(),
    truncated: z.boolean(),
  })
  .strict();

type ReadFileRangesInput = z.infer<typeof readFileRangesInputSchema>;
type ReadFileRangesOutput = z.infer<typeof readFileRangesOutputSchema>;

export const readFileRangesTool: AIToolDefinition<
  ReadFileRangesInput,
  ReadFileRangesOutput
> = {
  id: "read_file_ranges",
  name: "Read Library File Ranges",
  description:
    `Read up to ${maximumBatchReadRanges} guarded Library excerpts in one tool call. Each range is capped at ${maximumReadRangeLines} lines and the batch at ${maximumBatchReadLines} total lines. Prefer copying supplied source handles exactly.`,
  permission: "read-only",
  inputSchema: readFileRangesInputSchema,
  inputJsonSchema: {
    type: "object",
    properties: {
      ranges: {
        type: "array",
        minItems: 1,
        maxItems: maximumBatchReadRanges,
        items: {
          type: "object",
          properties: {
            fileId: {
              type: "string",
              minLength: 1,
              maxLength: 2048,
              description:
                "Catalog UUID from a source file-id attribute, or the exact Library-relative path from its path attribute.",
            },
            startLine: { type: "integer", minimum: 1 },
            endLine: { type: "integer", minimum: 1 },
          },
          required: ["fileId", "startLine", "endLine"],
          additionalProperties: false,
        },
      },
    },
    required: ["ranges"],
    additionalProperties: false,
  },
  outputSchema: readFileRangesOutputSchema,
  timeoutMs: 15_000,
  async execute(input, context) {
    const results: ReadFileRangeOutput[] = [];
    let remainingCharacters = maximumBatchReadCharacters;
    let totalCharacterCount = 0;
    let truncated = false;

    for (const range of input.ranges) {
      throwIfAborted(context.signal);
      const result = await readFileRangeTool.execute(range, context);
      totalCharacterCount += result.totalCharacterCount;

      const content = result.content.slice(0, remainingCharacters);
      const rangeTruncated =
        result.truncated || content.length < result.content.length;
      truncated ||= rangeTruncated;
      remainingCharacters = Math.max(0, remainingCharacters - content.length);

      results.push({
        ...result,
        content,
        truncated: rangeTruncated,
      });
    }

    return {
      ranges: results,
      rangeCount: results.length,
      totalCharacterCount,
      returnedCharacterCount: results.reduce(
        (total, result) => total + result.content.length,
        0,
      ),
      truncated,
    };
  },
  summarizeInput(input) {
    return {
      rangeCount: input.ranges.length,
      totalRequestedLines: input.ranges.reduce(
        (total, range) => total + range.endLine - range.startLine + 1,
        0,
      ),
      paths: input.ranges.map((range) => range.fileId),
    };
  },
  summarizeOutput(output) {
    return {
      rangeCount: output.rangeCount,
      paths: output.ranges.map((range) => range.file.relativePath),
      returnedCharacterCount: output.returnedCharacterCount,
      truncated: output.truncated,
    };
  },
};

export const libraryReadTools = [
  listDirectoryTool,
  searchFilenamesTool,
  searchLibraryTool,
  readFileTool,
  readFileRangeTool,
  readFileRangesTool,
] as const;

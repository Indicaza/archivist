import type { ZodType } from "zod";

export type AIToolPermissionLevel =
  | "read-only"
  | "safe-local-mutation"
  | "consequential"
  | "external-provider-action";

export type AIToolExecutionStatus =
  | "requested"
  | "running"
  | "completed"
  | "failed"
  | "cancelled";

export type AIToolErrorCode =
  | "tool_not_found"
  | "invalid_input"
  | "permission_denied"
  | "invalid_output"
  | "timed_out"
  | "cancelled"
  | "execution_failed";

export type AIToolContext = {
  executionId: string;
  runId: string;
  chatId: string;
  libraryId: string | null;
  signal: AbortSignal;
};

export type AIToolDefinition<TInput, TOutput> = {
  id: string;
  name: string;
  description: string;
  permission: AIToolPermissionLevel;
  inputSchema: ZodType<TInput>;
  inputJsonSchema?: Record<string, unknown>;
  outputSchema: ZodType<TOutput>;
  timeoutMs: number;
  execute: (
    input: TInput,
    context: AIToolContext,
  ) => Promise<TOutput> | TOutput;
  summarizeInput?: (input: TInput) => Record<string, unknown>;
  summarizeOutput?: (output: TOutput) => Record<string, unknown>;
};

export type AnyAIToolDefinition = AIToolDefinition<any, any>;

export type AIToolExecution = {
  id: string;
  runId: string;
  toolId: string;
  permission: AIToolPermissionLevel;
  status: AIToolExecutionStatus;
  input: unknown;
  output: unknown;
  errorCode: AIToolErrorCode | null;
  errorMessage: string | null;
  requestedAt: string;
  startedAt: string | null;
  completedAt: string | null;
  durationMs: number | null;
};

export type ExecuteAIToolRequest = {
  runId: string;
  chatId: string;
  libraryId: string | null;
  toolId: string;
  input: unknown;
  grantedPermissions?: ReadonlySet<AIToolPermissionLevel>;
  signal?: AbortSignal;
};

export class AIToolError extends Error {
  readonly code: AIToolErrorCode;
  readonly details?: unknown;

  constructor(code: AIToolErrorCode, message: string, details?: unknown) {
    super(message);
    this.name = "AIToolError";
    this.code = code;
    this.details = details;
  }
}

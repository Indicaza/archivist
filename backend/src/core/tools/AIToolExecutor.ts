import { aiRunEventBroker } from "../../api/cognition/runs/services/AIRunEventBroker.js";
import { aiToolRegistry, AIToolRegistry } from "./AIToolRegistry.js";
import {
  cancelAIToolExecution,
  completeAIToolExecution,
  createAIToolExecution,
  failAIToolExecution,
  startAIToolExecution,
} from "./models/AIToolExecution.js";
import {
  AIToolError,
  type AIToolErrorCode,
  type AIToolExecution,
  type AIToolPermissionLevel,
  type ExecuteAIToolRequest,
} from "./AIToolTypes.js";

const defaultGrantedPermissions = new Set<AIToolPermissionLevel>([
  "read-only",
]);

function elapsedMilliseconds(startedAt: number): number {
  return Number((performance.now() - startedAt).toFixed(3));
}

function errorMessage(error: unknown): string {
  if (error instanceof Error && error.message.trim()) {
    return error.message;
  }

  return "The AI tool failed unexpectedly.";
}


function safeSummary<T>(
  summarize: ((value: T) => Record<string, unknown>) | undefined,
  value: T,
): Record<string, unknown> {
  if (!summarize) {
    return {};
  }

  try {
    return summarize(value);
  } catch {
    return {
      summaryUnavailable: true,
    };
  }
}

function logAITool(
  event: string,
  details: Record<string, unknown>,
): void {
  if (process.env.ARCHIVIST_AI_TOOL_LOGS === "0") {
    return;
  }

  console.info("[AITool]", {
    event,
    ...details,
  });
}

function emitToolEvent(
  runId: string,
  eventType:
    | "tool.requested"
    | "tool.started"
    | "tool.completed"
    | "tool.failed"
    | "tool.cancelled",
  payload: Record<string, unknown>,
): void {
  aiRunEventBroker.append(runId, eventType, payload);
}

function normalizedToolError(
  error: unknown,
  input: {
    callerCancelled: boolean;
    timedOut: boolean;
  },
): AIToolError {
  if (input.timedOut) {
    return new AIToolError("timed_out", "The AI tool exceeded its timeout.");
  }

  if (
    input.callerCancelled ||
    (error instanceof Error && error.name === "AbortError")
  ) {
    return new AIToolError("cancelled", "The AI tool was cancelled.");
  }

  if (error instanceof AIToolError) {
    return error;
  }

  return new AIToolError("execution_failed", errorMessage(error));
}

function finishFailedExecution(input: {
  executionId: string;
  runId: string;
  toolId: string;
  error: AIToolError;
  durationMs: number;
}): AIToolExecution {
  const cancelled = input.error.code === "cancelled";
  const execution = cancelled
    ? cancelAIToolExecution(
        input.executionId,
        input.error.message,
        input.durationMs,
      )
    : failAIToolExecution(
        input.executionId,
        input.error.code,
        input.error.message,
        input.durationMs,
      );
  const eventType = cancelled ? "tool.cancelled" : "tool.failed";

  emitToolEvent(input.runId, eventType, {
    executionId: input.executionId,
    toolId: input.toolId,
    errorCode: input.error.code,
    message: input.error.message,
    durationMs: input.durationMs,
  });
  logAITool(eventType, {
    executionId: input.executionId,
    runId: input.runId,
    toolId: input.toolId,
    errorCode: input.error.code,
    message: input.error.message,
    durationMs: input.durationMs,
  });

  return execution;
}

export class AIToolExecutor {
  constructor(private readonly registry: AIToolRegistry) {}

  async execute(request: ExecuteAIToolRequest): Promise<AIToolExecution> {
    const tool = this.registry.require(request.toolId);
    const startedAt = performance.now();
    const execution = createAIToolExecution({
      runId: request.runId,
      toolId: tool.id,
      permission: tool.permission,
      toolInput: request.input,
    });
    emitToolEvent(request.runId, "tool.requested", {
      executionId: execution.id,
      toolId: tool.id,
      toolName: tool.name,
      permission: tool.permission,
    });
    logAITool("tool.requested", {
      executionId: execution.id,
      runId: request.runId,
      chatId: request.chatId,
      libraryId: request.libraryId,
      toolId: tool.id,
      permission: tool.permission,
    });

    const parsedInput = tool.inputSchema.safeParse(request.input);

    if (!parsedInput.success) {
      const error = new AIToolError(
        "invalid_input",
        `AI tool "${tool.id}" received invalid input.`,
        {
          issues: parsedInput.error.issues,
        },
      );
      finishFailedExecution({
        executionId: execution.id,
        runId: request.runId,
        toolId: tool.id,
        error,
        durationMs: elapsedMilliseconds(startedAt),
      });
      throw error;
    }

    const grantedPermissions =
      request.grantedPermissions ?? defaultGrantedPermissions;

    if (!grantedPermissions.has(tool.permission)) {
      const error = new AIToolError(
        "permission_denied",
        `AI tool "${tool.id}" requires ${tool.permission} permission.`,
        {
          requiredPermission: tool.permission,
          grantedPermissions: Array.from(grantedPermissions),
        },
      );
      finishFailedExecution({
        executionId: execution.id,
        runId: request.runId,
        toolId: tool.id,
        error,
        durationMs: elapsedMilliseconds(startedAt),
      });
      throw error;
    }

    const inputSummary = safeSummary(
      tool.summarizeInput,
      parsedInput.data,
    );

    startAIToolExecution(execution.id);
    emitToolEvent(request.runId, "tool.started", {
      executionId: execution.id,
      toolId: tool.id,
      toolName: tool.name,
      permission: tool.permission,
      input: inputSummary,
    });
    logAITool("tool.started", {
      executionId: execution.id,
      runId: request.runId,
      toolId: tool.id,
      timeoutMs: tool.timeoutMs,
      input: inputSummary,
    });

    const controller = new AbortController();
    let timedOut = false;
    let callerCancelled = request.signal?.aborted ?? false;
    const cancelFromCaller = (): void => {
      callerCancelled = true;
      controller.abort(request.signal?.reason);
    };

    if (request.signal?.aborted) {
      cancelFromCaller();
    } else {
      request.signal?.addEventListener("abort", cancelFromCaller, {
        once: true,
      });
    }

    const timeout = setTimeout(() => {
      timedOut = true;
      controller.abort(new Error(`AI tool ${tool.id} timed out.`));
    }, tool.timeoutMs);

    try {
      const output = await tool.execute(parsedInput.data, {
        executionId: execution.id,
        runId: request.runId,
        chatId: request.chatId,
        libraryId: request.libraryId,
        signal: controller.signal,
      });
      const parsedOutput = tool.outputSchema.safeParse(output);

      if (!parsedOutput.success) {
        throw new AIToolError(
          "invalid_output",
          `AI tool "${tool.id}" returned invalid output.`,
          {
            issues: parsedOutput.error.issues,
          },
        );
      }

      const durationMs = elapsedMilliseconds(startedAt);
      const completed = completeAIToolExecution(
        execution.id,
        parsedOutput.data,
        durationMs,
      );
      const outputSummary = safeSummary(
        tool.summarizeOutput,
        parsedOutput.data,
      );

      emitToolEvent(request.runId, "tool.completed", {
        executionId: execution.id,
        toolId: tool.id,
        toolName: tool.name,
        durationMs,
        output: outputSummary,
      });
      logAITool("tool.completed", {
        executionId: execution.id,
        runId: request.runId,
        toolId: tool.id,
        durationMs,
        output: outputSummary,
      });

      return completed;
    } catch (error) {
      const normalizedError = normalizedToolError(error, {
        callerCancelled,
        timedOut,
      });
      finishFailedExecution({
        executionId: execution.id,
        runId: request.runId,
        toolId: tool.id,
        error: normalizedError,
        durationMs: elapsedMilliseconds(startedAt),
      });
      throw normalizedError;
    } finally {
      clearTimeout(timeout);
      request.signal?.removeEventListener("abort", cancelFromCaller);
    }
  }
}

export const aiToolExecutor = new AIToolExecutor(aiToolRegistry);

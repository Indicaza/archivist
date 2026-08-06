import { database } from "../../../database/database.js";
import type {
  AIToolErrorCode,
  AIToolExecution,
  AIToolExecutionStatus,
  AIToolPermissionLevel,
} from "../AIToolTypes.js";

type AIToolExecutionRow = {
  id: string;
  run_id: string;
  tool_id: string;
  permission_level: AIToolPermissionLevel;
  status: AIToolExecutionStatus;
  input_json: string;
  output_json: string | null;
  error_code: AIToolErrorCode | null;
  error_message: string | null;
  requested_at: string;
  started_at: string | null;
  completed_at: string | null;
  duration_ms: number | null;
};

function parseJson(value: string | null): unknown {
  if (value === null) {
    return null;
  }

  try {
    return JSON.parse(value) as unknown;
  } catch {
    return null;
  }
}

function serializeJson(value: unknown): string {
  const serialized = JSON.stringify(value);
  return serialized === undefined ? "null" : serialized;
}

function mapExecution(row: AIToolExecutionRow): AIToolExecution {
  return {
    id: row.id,
    runId: row.run_id,
    toolId: row.tool_id,
    permission: row.permission_level,
    status: row.status,
    input: parseJson(row.input_json),
    output: parseJson(row.output_json),
    errorCode: row.error_code,
    errorMessage: row.error_message,
    requestedAt: row.requested_at,
    startedAt: row.started_at,
    completedAt: row.completed_at,
    durationMs: row.duration_ms,
  };
}

const executionSelect = `
  SELECT
    id,
    run_id,
    tool_id,
    permission_level,
    status,
    input_json,
    output_json,
    error_code,
    error_message,
    requested_at,
    started_at,
    completed_at,
    duration_ms
  FROM ai_tool_executions
`;

export function getAIToolExecutionById(
  executionId: string,
): AIToolExecution | null {
  const row = database
    .prepare(`${executionSelect} WHERE id = ?`)
    .get(executionId) as AIToolExecutionRow | undefined;

  return row ? mapExecution(row) : null;
}

export function requireAIToolExecution(
  executionId: string,
): AIToolExecution {
  const execution = getAIToolExecutionById(executionId);

  if (!execution) {
    throw new Error(`AI tool execution ${executionId} could not be loaded.`);
  }

  return execution;
}

export function listAIToolExecutionsByRunId(
  runId: string,
): AIToolExecution[] {
  const rows = database
    .prepare(
      `${executionSelect}
       WHERE run_id = ?
       ORDER BY requested_at ASC, rowid ASC`,
    )
    .all(runId) as AIToolExecutionRow[];

  return rows.map(mapExecution);
}

export function createAIToolExecution(input: {
  runId: string;
  toolId: string;
  permission: AIToolPermissionLevel;
  toolInput: unknown;
}): AIToolExecution {
  const executionId = crypto.randomUUID();

  database
    .prepare(
      `
        INSERT INTO ai_tool_executions (
          id,
          run_id,
          tool_id,
          permission_level,
          status,
          input_json
        )
        VALUES (?, ?, ?, ?, 'requested', ?)
      `,
    )
    .run(
      executionId,
      input.runId,
      input.toolId,
      input.permission,
      serializeJson(input.toolInput),
    );

  return requireAIToolExecution(executionId);
}

export function startAIToolExecution(executionId: string): AIToolExecution {
  const result = database
    .prepare(
      `
        UPDATE ai_tool_executions
        SET
          status = 'running',
          started_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
        WHERE id = ?
          AND status = 'requested'
      `,
    )
    .run(executionId);

  if (result.changes !== 1) {
    throw new Error(`AI tool execution ${executionId} could not be started.`);
  }

  return requireAIToolExecution(executionId);
}

export function completeAIToolExecution(
  executionId: string,
  output: unknown,
  durationMs: number,
): AIToolExecution {
  const result = database
    .prepare(
      `
        UPDATE ai_tool_executions
        SET
          status = 'completed',
          output_json = ?,
          error_code = NULL,
          error_message = NULL,
          completed_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now'),
          duration_ms = ?
        WHERE id = ?
          AND status = 'running'
      `,
    )
    .run(serializeJson(output), durationMs, executionId);

  if (result.changes !== 1) {
    throw new Error(`AI tool execution ${executionId} could not be completed.`);
  }

  return requireAIToolExecution(executionId);
}

function finishAIToolExecution(
  executionId: string,
  status: "failed" | "cancelled",
  errorCode: AIToolErrorCode,
  errorMessage: string,
  durationMs: number,
): AIToolExecution {
  const result = database
    .prepare(
      `
        UPDATE ai_tool_executions
        SET
          status = ?,
          error_code = ?,
          error_message = ?,
          completed_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now'),
          duration_ms = ?
        WHERE id = ?
          AND status IN ('requested', 'running')
      `,
    )
    .run(status, errorCode, errorMessage, durationMs, executionId);

  if (result.changes !== 1) {
    throw new Error(`AI tool execution ${executionId} could not be finalized.`);
  }

  return requireAIToolExecution(executionId);
}

export function failAIToolExecution(
  executionId: string,
  errorCode: AIToolErrorCode,
  errorMessage: string,
  durationMs: number,
): AIToolExecution {
  return finishAIToolExecution(
    executionId,
    "failed",
    errorCode,
    errorMessage,
    durationMs,
  );
}

export function cancelAIToolExecution(
  executionId: string,
  errorMessage: string,
  durationMs: number,
): AIToolExecution {
  return finishAIToolExecution(
    executionId,
    "cancelled",
    "cancelled",
    errorMessage,
    durationMs,
  );
}

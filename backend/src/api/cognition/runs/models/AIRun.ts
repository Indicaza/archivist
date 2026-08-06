import { database } from "../../../../database/database.js";
import { AppError } from "../../../../errors/app-error.js";
import type {
  AIRun,
  AIRunEvent,
  AIRunEventType,
  AIRunStatus,
  CreateAIRunInput,
} from "../types/AIRunTypes.js";

type AIRunRow = {
  id: string;
  chat_id: string;
  library_id: string | null;
  agent_id: string;
  user_message_id: string;
  assistant_message_id: string;
  context_run_id: string | null;
  context_compiler_id: string;
  context_compiler_version: number;
  status: AIRunStatus;
  phase: AIRunEventType;
  provider: string;
  model: string;
  final_response: string | null;
  error_code: string | null;
  error_message: string | null;
  started_at: string;
  completed_at: string | null;
  cancelled_at: string | null;
};

type AIRunEventRow = {
  id: string;
  run_id: string;
  sequence: number;
  event_type: AIRunEventType;
  payload_json: string;
  created_at: string;
};

const terminalStatuses = new Set<AIRunStatus>([
  "completed",
  "cancelled",
  "failed",
]);

const phaseEvents = new Set<AIRunEventType>([
  "run.started",
  "retrieval.started",
  "retrieval.completed",
  "context.started",
  "context.completed",
  "model.started",
  "model.completed",
  "tool.requested",
  "tool.started",
  "tool.completed",
  "tool.failed",
  "tool.cancelled",
  "run.cancelled",
  "run.failed",
  "run.completed",
]);

function parsePayload(value: string): Record<string, unknown> {
  try {
    const parsed = JSON.parse(value) as unknown;

    if (typeof parsed === "object" && parsed !== null && !Array.isArray(parsed)) {
      return parsed as Record<string, unknown>;
    }
  } catch {
    return {};
  }

  return {};
}

function mapAIRun(row: AIRunRow): AIRun {
  return {
    id: row.id,
    chatId: row.chat_id,
    libraryId: row.library_id,
    agentId: row.agent_id,
    userMessageId: row.user_message_id,
    assistantMessageId: row.assistant_message_id,
    contextRunId: row.context_run_id,
    contextCompiler: {
      id: row.context_compiler_id,
      version: row.context_compiler_version,
    },
    status: row.status,
    phase: row.phase,
    provider: row.provider,
    model: row.model,
    finalResponse: row.final_response,
    errorCode: row.error_code,
    errorMessage: row.error_message,
    startedAt: row.started_at,
    completedAt: row.completed_at,
    cancelledAt: row.cancelled_at,
  };
}

function mapAIRunEvent(row: AIRunEventRow): AIRunEvent {
  return {
    id: row.id,
    runId: row.run_id,
    sequence: row.sequence,
    eventType: row.event_type,
    payload: parsePayload(row.payload_json),
    createdAt: row.created_at,
  };
}

const runSelect = `
  SELECT
    id,
    chat_id,
    library_id,
    agent_id,
    user_message_id,
    assistant_message_id,
    context_run_id,
    context_compiler_id,
    context_compiler_version,
    status,
    phase,
    provider,
    model,
    final_response,
    error_code,
    error_message,
    started_at,
    completed_at,
    cancelled_at
  FROM ai_runs
`;

export function isAIRunTerminal(status: AIRunStatus): boolean {
  return terminalStatuses.has(status);
}

export function getAIRunById(runId: string): AIRun | null {
  const row = database
    .prepare(`${runSelect} WHERE id = ?`)
    .get(runId) as AIRunRow | undefined;

  return row ? mapAIRun(row) : null;
}

export function requireAIRun(runId: string): AIRun {
  const run = getAIRunById(runId);

  if (!run) {
    throw new AppError(404, "AI Run not found.");
  }

  return run;
}

export function listAIRunsByChatId(
  chatId: string,
  limit = 50,
): AIRun[] {
  const rows = database
    .prepare(
      `${runSelect}
       WHERE chat_id = ?
       ORDER BY started_at DESC
       LIMIT ?`,
    )
    .all(chatId, limit) as AIRunRow[];

  return rows.map(mapAIRun);
}

export function getActiveAIRunByChatId(chatId: string): AIRun | null {
  const row = database
    .prepare(
      `${runSelect}
       WHERE chat_id = ?
         AND status = 'running'
       ORDER BY started_at DESC
       LIMIT 1`,
    )
    .get(chatId) as AIRunRow | undefined;

  return row ? mapAIRun(row) : null;
}

export function createAIRun(input: CreateAIRunInput): AIRun {
  const activeRun = getActiveAIRunByChatId(input.chatId);

  if (activeRun) {
    throw new AppError(409, "This Chat already has an active AI Run.", {
      runId: activeRun.id,
    });
  }

  const runId = crypto.randomUUID();

  database
    .prepare(
      `
        INSERT INTO ai_runs (
          id,
          chat_id,
          library_id,
          agent_id,
          user_message_id,
          assistant_message_id,
          context_compiler_id,
          context_compiler_version,
          status,
          phase,
          provider,
          model
        )
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, 'running', 'run.started', ?, ?)
      `,
    )
    .run(
      runId,
      input.chatId,
      input.libraryId,
      input.agentId,
      input.userMessageId,
      input.assistantMessageId,
      input.contextCompiler.id,
      input.contextCompiler.version,
      input.provider,
      input.model,
    );

  return requireAIRun(runId);
}

export function appendAIRunEvent(
  runId: string,
  eventType: AIRunEventType,
  payload: Record<string, unknown> = {},
): AIRunEvent {
  const appendTransaction = database.transaction(() => {
    requireAIRun(runId);

    const sequenceRow = database
      .prepare(
        `
          SELECT COALESCE(MAX(sequence), 0) + 1 AS sequence
          FROM ai_run_events
          WHERE run_id = ?
        `,
      )
      .get(runId) as { sequence: number };

    const eventId = crypto.randomUUID();

    database
      .prepare(
        `
          INSERT INTO ai_run_events (
            id,
            run_id,
            sequence,
            event_type,
            payload_json
          )
          VALUES (?, ?, ?, ?, ?)
        `,
      )
      .run(
        eventId,
        runId,
        sequenceRow.sequence,
        eventType,
        JSON.stringify(payload),
      );

    if (phaseEvents.has(eventType)) {
      database
        .prepare(
          `
            UPDATE ai_runs
            SET phase = ?
            WHERE id = ?
          `,
        )
        .run(eventType, runId);
    }

    const eventRow = database
      .prepare(
        `
          SELECT
            id,
            run_id,
            sequence,
            event_type,
            payload_json,
            created_at
          FROM ai_run_events
          WHERE id = ?
        `,
      )
      .get(eventId) as AIRunEventRow | undefined;

    if (!eventRow) {
      throw new Error("The newly created AI Run event could not be loaded.");
    }

    return mapAIRunEvent(eventRow);
  });

  return appendTransaction();
}

export function listAIRunEvents(
  runId: string,
  afterSequence = 0,
): AIRunEvent[] {
  requireAIRun(runId);

  const rows = database
    .prepare(
      `
        SELECT
          id,
          run_id,
          sequence,
          event_type,
          payload_json,
          created_at
        FROM ai_run_events
        WHERE run_id = ?
          AND sequence > ?
        ORDER BY sequence ASC
      `,
    )
    .all(runId, afterSequence) as AIRunEventRow[];

  return rows.map(mapAIRunEvent);
}

export function completeAIRun(
  runId: string,
  finalResponse: string,
  contextRunId: string | null,
): AIRun {
  database
    .prepare(
      `
        UPDATE ai_runs
        SET
          status = 'completed',
          phase = 'run.completed',
          final_response = ?,
          context_run_id = ?,
          error_code = NULL,
          error_message = NULL,
          completed_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
        WHERE id = ?
          AND status = 'running'
      `,
    )
    .run(finalResponse, contextRunId, runId);

  return requireAIRun(runId);
}

export function cancelAIRun(runId: string, finalResponse: string): AIRun {
  database
    .prepare(
      `
        UPDATE ai_runs
        SET
          status = 'cancelled',
          phase = 'run.cancelled',
          final_response = ?,
          error_code = NULL,
          error_message = NULL,
          completed_at = COALESCE(
            completed_at,
            strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
          ),
          cancelled_at = COALESCE(
            cancelled_at,
            strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
          )
        WHERE id = ?
          AND status IN ('running', 'cancelled')
      `,
    )
    .run(finalResponse, runId);

  return requireAIRun(runId);
}

export function failAIRun(
  runId: string,
  errorCode: string,
  errorMessage: string,
  finalResponse: string,
): AIRun {
  database
    .prepare(
      `
        UPDATE ai_runs
        SET
          status = 'failed',
          phase = 'run.failed',
          final_response = ?,
          error_code = ?,
          error_message = ?,
          completed_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
        WHERE id = ?
          AND status = 'running'
      `,
    )
    .run(finalResponse, errorCode, errorMessage, runId);

  return requireAIRun(runId);
}

export function recoverInterruptedAIRuns(): number {
  const rows = database
    .prepare(
      `
        SELECT
          ai_runs.id,
          ai_runs.assistant_message_id,
          messages.content AS message_content,
          messages.status AS message_status,
          context_runs.id AS context_run_id
        FROM ai_runs
        INNER JOIN messages
          ON messages.id = ai_runs.assistant_message_id
        LEFT JOIN context_runs
          ON context_runs.assistant_message_id = ai_runs.assistant_message_id
        WHERE ai_runs.status = 'running'
        ORDER BY ai_runs.started_at ASC
      `,
    )
    .all() as Array<{
      id: string;
      assistant_message_id: string;
      message_content: string;
      message_status: "streaming" | "complete" | "cancelled" | "failed";
      context_run_id: string | null;
    }>;

  if (rows.length === 0) {
    return 0;
  }

  const recoverTransaction = database.transaction(() => {
    for (const row of rows) {
      const sequenceRow = database
        .prepare(
          `
            SELECT COALESCE(MAX(sequence), 0) + 1 AS sequence
            FROM ai_run_events
            WHERE run_id = ?
          `,
        )
        .get(row.id) as { sequence: number };

      if (row.message_status === "complete") {
        database
          .prepare(
            `
              UPDATE ai_runs
              SET
                status = 'completed',
                phase = 'run.completed',
                final_response = ?,
                context_run_id = ?,
                completed_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
              WHERE id = ?
            `,
          )
          .run(row.message_content, row.context_run_id, row.id);

        database
          .prepare(
            `
              INSERT INTO ai_run_events (
                id,
                run_id,
                sequence,
                event_type,
                payload_json
              )
              VALUES (?, ?, ?, 'run.completed', ?)
            `,
          )
          .run(
            crypto.randomUUID(),
            row.id,
            sequenceRow.sequence,
            JSON.stringify({
              recovered: true,
              assistantMessageId: row.assistant_message_id,
              contextRunId: row.context_run_id,
              characterCount: row.message_content.length,
            }),
          );
        continue;
      }

      if (row.message_status === "cancelled") {
        database
          .prepare(
            `
              UPDATE ai_runs
              SET
                status = 'cancelled',
                phase = 'run.cancelled',
                final_response = ?,
                completed_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now'),
                cancelled_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
              WHERE id = ?
            `,
          )
          .run(row.message_content, row.id);

        database
          .prepare(
            `
              INSERT INTO ai_run_events (
                id,
                run_id,
                sequence,
                event_type,
                payload_json
              )
              VALUES (?, ?, ?, 'run.cancelled', ?)
            `,
          )
          .run(
            crypto.randomUUID(),
            row.id,
            sequenceRow.sequence,
            JSON.stringify({
              recovered: true,
              assistantMessageId: row.assistant_message_id,
              characterCount: row.message_content.length,
            }),
          );
        continue;
      }

      const deltaRows = database
        .prepare(
          `
            SELECT payload_json
            FROM ai_run_events
            WHERE run_id = ?
              AND event_type = 'model.delta'
            ORDER BY sequence ASC
          `,
        )
        .all(row.id) as Array<{ payload_json: string }>;

      const streamedResponse = deltaRows
        .map((eventRow) => {
          const payload = parsePayload(eventRow.payload_json);
          return typeof payload.delta === "string" ? payload.delta : "";
        })
        .join("");
      const partialResponse = streamedResponse || row.message_content;

      database
        .prepare(
          `
            UPDATE messages
            SET
              content = ?,
              status = 'failed',
              updated_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
            WHERE id = ?
          `,
        )
        .run(partialResponse, row.assistant_message_id);

      database
        .prepare(
          `
            INSERT INTO ai_run_events (
              id,
              run_id,
              sequence,
              event_type,
              payload_json
            )
            VALUES (?, ?, ?, 'run.failed', ?)
          `,
        )
        .run(
          crypto.randomUUID(),
          row.id,
          sequenceRow.sequence,
          JSON.stringify({
            recovered: true,
            errorCode: "process_interrupted",
            message: "Archivist stopped before the AI Run completed.",
          }),
        );

      database
        .prepare(
          `
            UPDATE ai_runs
            SET
              status = 'failed',
              phase = 'run.failed',
              final_response = ?,
              error_code = 'process_interrupted',
              error_message = 'Archivist stopped before the AI Run completed.',
              completed_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
            WHERE id = ?
          `,
        )
        .run(partialResponse, row.id);
    }
  });

  recoverTransaction();
  return rows.length;
}

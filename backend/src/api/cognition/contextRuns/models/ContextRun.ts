import { database } from "../../../../database/database.js";
import { AppError } from "../../../../errors/app-error.js";
import { requireActiveChat } from "../../../chats/models/Chat.js";
import type {
  ContextRun,
  CreateContextRunInput,
} from "../types/ContextRunTypes.js";

type ContextRunRow = {
  id: string;
  chat_id: string;
  user_message_id: string;
  assistant_message_id: string;
  provider: string;
  model: string;
  agent_id: string;
  snapshot_json: string;
  created_at: string;
};

type ContextRunSnapshot = Omit<
  ContextRun,
  | "id"
  | "chatId"
  | "userMessageId"
  | "assistantMessageId"
  | "provider"
  | "model"
  | "agentId"
  | "createdAt"
>;

function parseSnapshot(row: ContextRunRow): ContextRunSnapshot {
  const parsed = JSON.parse(row.snapshot_json) as ContextRunSnapshot;

  if (
    !parsed
    || typeof parsed !== "object"
    || !parsed.compiler
    || !parsed.manifest
    || !Array.isArray(parsed.warnings)
    || !Array.isArray(parsed.sources)
  ) {
    throw new Error(`Context run ${row.id} contains an invalid snapshot.`);
  }

  return parsed;
}

function mapContextRun(row: ContextRunRow): ContextRun {
  return {
    id: row.id,
    chatId: row.chat_id,
    userMessageId: row.user_message_id,
    assistantMessageId: row.assistant_message_id,
    provider: row.provider,
    model: row.model,
    agentId: row.agent_id,
    ...parseSnapshot(row),
    createdAt: row.created_at,
  };
}

export function createContextRun(input: CreateContextRunInput): ContextRun {
  const contextRunId = crypto.randomUUID();
  const snapshot: ContextRunSnapshot = {
    compiler: input.compiler,
    manifest: input.manifest,
    warnings: input.warnings,
    sources: input.sources,
  };

  database
    .prepare(
      `
        INSERT INTO context_runs (
          id,
          chat_id,
          user_message_id,
          assistant_message_id,
          provider,
          model,
          agent_id,
          snapshot_json
        )
        VALUES (?, ?, ?, ?, ?, ?, ?, ?)
      `,
    )
    .run(
      contextRunId,
      input.chatId,
      input.userMessageId,
      input.assistantMessageId,
      input.provider,
      input.model,
      input.agentId,
      JSON.stringify(snapshot),
    );

  const contextRun = getContextRunByAssistantMessage(
    input.chatId,
    input.assistantMessageId,
  );

  if (!contextRun) {
    throw new Error("The newly created context run could not be loaded.");
  }

  return contextRun;
}

export function getContextRunByAssistantMessage(
  chatId: string,
  assistantMessageId: string,
): ContextRun | null {
  requireActiveChat(chatId);

  const row = database
    .prepare(
      `
        SELECT
          id,
          chat_id,
          user_message_id,
          assistant_message_id,
          provider,
          model,
          agent_id,
          snapshot_json,
          created_at
        FROM context_runs
        WHERE chat_id = ?
          AND assistant_message_id = ?
        LIMIT 1
      `,
    )
    .get(chatId, assistantMessageId) as ContextRunRow | undefined;

  return row ? mapContextRun(row) : null;
}

export function requireContextRunByAssistantMessage(
  chatId: string,
  assistantMessageId: string,
): ContextRun {
  const contextRun = getContextRunByAssistantMessage(
    chatId,
    assistantMessageId,
  );

  if (!contextRun) {
    throw new AppError(
      404,
      "No stored context is available for this response.",
    );
  }

  return contextRun;
}

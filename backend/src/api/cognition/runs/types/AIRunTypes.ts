export type AIRunStatus = "running" | "completed" | "cancelled" | "failed";

export type AIRunEventType =
  | "run.started"
  | "retrieval.started"
  | "retrieval.completed"
  | "context.started"
  | "context.completed"
  | "model.started"
  | "model.delta"
  | "model.completed"
  | "run.cancelled"
  | "run.failed"
  | "run.completed";

export type AIRun = {
  id: string;
  chatId: string;
  libraryId: string | null;
  agentId: string;
  userMessageId: string;
  assistantMessageId: string;
  contextRunId: string | null;
  contextCompiler: {
    id: string;
    version: number;
  };
  status: AIRunStatus;
  phase: AIRunEventType;
  provider: string;
  model: string;
  finalResponse: string | null;
  errorCode: string | null;
  errorMessage: string | null;
  startedAt: string;
  completedAt: string | null;
  cancelledAt: string | null;
};

export type AIRunEvent = {
  id: string;
  runId: string;
  sequence: number;
  eventType: AIRunEventType;
  payload: Record<string, unknown>;
  createdAt: string;
};

export type CreateAIRunInput = {
  chatId: string;
  libraryId: string | null;
  agentId: string;
  userMessageId: string;
  assistantMessageId: string;
  contextCompiler: {
    id: string;
    version: number;
  };
  provider: string;
  model: string;
};

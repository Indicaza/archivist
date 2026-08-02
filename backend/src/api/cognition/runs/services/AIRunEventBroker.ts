import {
  appendAIRunEvent,
  requireAIRun,
} from "../models/AIRun.js";
import type {
  AIRunEvent,
  AIRunEventType,
} from "../types/AIRunTypes.js";

type AIRunEventListener = (event: AIRunEvent) => void;

function logAIRunEvent(event: AIRunEvent): void {
  if (
    event.eventType === "model.delta" ||
    process.env.ARCHIVIST_AI_RUN_LOGS === "0"
  ) {
    return;
  }

  const run = requireAIRun(event.runId);
  const startedAt = new Date(run.startedAt).getTime();
  const createdAt = new Date(event.createdAt).getTime();
  const elapsedMs =
    Number.isFinite(startedAt) && Number.isFinite(createdAt)
      ? Math.max(0, Math.round(createdAt - startedAt))
      : null;

  console.info("[AIRun]", {
    runId: event.runId,
    chatId: run.chatId,
    sequence: event.sequence,
    eventType: event.eventType,
    elapsedMs,
    status: run.status,
    phase: run.phase,
    provider: run.provider,
    model: run.model,
    ...event.payload,
  });
}

class AIRunEventBroker {
  private readonly listeners = new Map<string, Set<AIRunEventListener>>();

  append(
    runId: string,
    eventType: AIRunEventType,
    payload: Record<string, unknown> = {},
  ): AIRunEvent {
    const event = appendAIRunEvent(runId, eventType, payload);
    const runListeners = this.listeners.get(runId);

    logAIRunEvent(event);

    if (runListeners) {
      for (const listener of runListeners) {
        listener(event);
      }
    }

    return event;
  }

  subscribe(runId: string, listener: AIRunEventListener): () => void {
    const runListeners = this.listeners.get(runId) ?? new Set();
    runListeners.add(listener);
    this.listeners.set(runId, runListeners);

    return () => {
      const currentListeners = this.listeners.get(runId);

      if (!currentListeners) {
        return;
      }

      currentListeners.delete(listener);

      if (currentListeners.size === 0) {
        this.listeners.delete(runId);
      }
    };
  }
}

export const aiRunEventBroker = new AIRunEventBroker();

import type { RequestHandler, Response } from "express";
import { AppError } from "../../../../errors/app-error.js";
import {
  isAIRunTerminal,
  listAIRunEvents,
  requireAIRun,
} from "../models/AIRun.js";
import {
  aiRunEventQuerySchema,
  aiRunIdParamsSchema,
} from "../schemas/AIRunSchemas.js";
import { aiRunEventBroker } from "../services/AIRunEventBroker.js";
import { cancelChatRun } from "../services/ChatRunService.js";
import type { AIRunEvent } from "../types/AIRunTypes.js";

function parseRunId(params: unknown): string {
  const parsed = aiRunIdParamsSchema.safeParse(params);

  if (!parsed.success) {
    throw new AppError(400, "Invalid AI Run ID.", parsed.error.flatten());
  }

  return parsed.data.runId;
}

function writeSseEvent(response: Response, event: AIRunEvent): void {
  response.write(`id: ${event.sequence}\n`);
  response.write(`event: ${event.eventType}\n`);
  response.write(`data: ${JSON.stringify(event)}\n\n`);
}

export const getAIRun: RequestHandler = (request, response) => {
  const runId = parseRunId(request.params);

  response.json({
    ok: true,
    run: requireAIRun(runId),
  });
};

export const getAIRunEvents: RequestHandler = (request, response) => {
  const runId = parseRunId(request.params);
  const query = aiRunEventQuerySchema.safeParse(request.query);

  if (!query.success) {
    throw new AppError(400, "Invalid AI Run event query.", query.error.flatten());
  }

  const headerSequence = Number(request.get("Last-Event-ID") ?? "0");
  const afterSequence =
    query.data.after ??
    (Number.isInteger(headerSequence) && headerSequence >= 0
      ? headerSequence
      : 0);
  const run = requireAIRun(runId);

  response.status(200);
  response.set({
    "Content-Type": "text/event-stream",
    "Cache-Control": "no-cache, no-transform",
    Connection: "keep-alive",
    "X-Accel-Buffering": "no",
  });
  response.flushHeaders();

  let lastSequence = afterSequence;
  let replaying = true;
  let closed = false;
  const bufferedEvents: AIRunEvent[] = [];
  let heartbeat: ReturnType<typeof setInterval> | null = null;
  let unsubscribe = (): void => {};

  const close = (): void => {
    if (closed) {
      return;
    }

    closed = true;

    if (heartbeat) {
      clearInterval(heartbeat);
    }

    unsubscribe();

    if (!response.writableEnded) {
      response.end();
    }
  };

  const send = (event: AIRunEvent): void => {
    if (closed || event.sequence <= lastSequence) {
      return;
    }

    lastSequence = event.sequence;
    writeSseEvent(response, event);

    if (
      event.eventType === "run.completed" ||
      event.eventType === "run.cancelled" ||
      event.eventType === "run.failed"
    ) {
      close();
    }
  };

  unsubscribe = aiRunEventBroker.subscribe(runId, (event) => {
    if (replaying) {
      bufferedEvents.push(event);
      return;
    }

    send(event);
  });

  heartbeat = setInterval(() => {
    if (!closed) {
      response.write(": heartbeat\n\n");
    }
  }, 15_000);

  for (const event of listAIRunEvents(runId, afterSequence)) {
    send(event);
  }

  replaying = false;

  for (const event of bufferedEvents.sort(
    (first, second) => first.sequence - second.sequence,
  )) {
    send(event);
  }

  if (isAIRunTerminal(run.status) && !closed) {
    close();
  }

  request.on("close", close);
};

export const postCancelAIRun: RequestHandler = (request, response) => {
  const runId = parseRunId(request.params);

  response.json({
    ok: true,
    run: cancelChatRun(runId),
  });
};

#!/usr/bin/env node

import fs from "node:fs";
import os from "node:os";
import path from "node:path";

const temporaryRoot = fs.mkdtempSync(
  path.join(os.tmpdir(), "archivist-ai-runtime-"),
);
const libraryPath = path.join(temporaryRoot, "Runtime Library");
const databasePath = path.join(temporaryRoot, "archivist.db");
const originalWorkingDirectory = process.cwd();

fs.mkdirSync(libraryPath);
fs.writeFileSync(
  path.join(libraryPath, "runtime.md"),
  "# Attached runtime evidence\n\nThis file is explicitly attached to the Chat.\n",
  "utf8",
);
fs.writeFileSync(
  path.join(libraryPath, "retrieval.md"),
  "# Copper orchard\n\nCopper orchard evidence must be discovered through automatic Library retrieval.\n",
  "utf8",
);

process.env.ARCHIVIST_DB_PATH = databasePath;
process.env.OPENAI_API_KEY = "sk-archivist-ai-runtime-smoke-test";
process.env.NODE_ENV = "test";
process.chdir(temporaryRoot);

let server;
let closeDatabase;

function assert(condition, message) {
  if (!condition) {
    throw new Error(message);
  }
}

function abortError() {
  const error = new Error("Fake provider stream cancelled.");
  error.name = "AbortError";
  return error;
}

function wait(milliseconds, signal) {
  return new Promise((resolve, reject) => {
    if (signal?.aborted) {
      reject(abortError());
      return;
    }

    const timer = setTimeout(() => {
      signal?.removeEventListener("abort", onAbort);
      resolve();
    }, milliseconds);

    const onAbort = () => {
      clearTimeout(timer);
      signal?.removeEventListener("abort", onAbort);
      reject(abortError());
    };

    signal?.addEventListener("abort", onAbort, { once: true });
  });
}

async function requestJson(baseUrl, route, options = {}) {
  const response = await fetch(`${baseUrl}${route}`, {
    ...options,
    headers: {
      "content-type": "application/json",
      ...options.headers,
    },
  });
  const payload = await response.json();

  if (!response.ok) {
    const error = new Error(
      payload?.error?.message ?? `${response.status} ${response.statusText}`,
    );
    error.status = response.status;
    error.payload = payload;
    throw error;
  }

  return payload;
}

async function expectStatus(expectedStatus, action) {
  try {
    await action();
  } catch (error) {
    if (error?.status === expectedStatus) {
      return;
    }

    throw error;
  }

  throw new Error(`Expected HTTP ${expectedStatus}, but the request succeeded.`);
}

function parseSseBlock(block) {
  const data = block
    .split("\n")
    .filter((line) => line.startsWith("data:"))
    .map((line) => line.slice(5).trim())
    .join("\n");

  return data ? JSON.parse(data) : null;
}

async function collectRunEvents(baseUrl, runId, onEvent) {
  const response = await fetch(`${baseUrl}/runs/${runId}/events`, {
    headers: {
      accept: "text/event-stream",
    },
  });

  if (!response.ok || !response.body) {
    throw new Error(`Could not open the AI Run event stream for ${runId}.`);
  }

  const terminalEvents = new Set([
    "run.completed",
    "run.cancelled",
    "run.failed",
  ]);
  const reader = response.body.getReader();
  const decoder = new TextDecoder();
  const events = [];
  let buffer = "";
  let terminal = false;

  while (!terminal) {
    const { value, done } = await reader.read();
    buffer += decoder.decode(value ?? new Uint8Array(), { stream: !done });
    buffer = buffer.replaceAll("\r\n", "\n");

    let boundary = buffer.indexOf("\n\n");

    while (boundary >= 0) {
      const block = buffer.slice(0, boundary);
      buffer = buffer.slice(boundary + 2);
      const event = parseSseBlock(block);

      if (event) {
        events.push(event);
        await onEvent?.(event);
        terminal = terminalEvents.has(event.eventType);
      }

      if (terminal) {
        break;
      }

      boundary = buffer.indexOf("\n\n");
    }

    if (done) {
      break;
    }
  }

  await reader.cancel().catch(() => {});
  return events;
}

function assertOrderedEvents(events, expectedTypes) {
  let cursor = -1;

  for (const expectedType of expectedTypes) {
    cursor = events.findIndex(
      (event, index) => index > cursor && event.eventType === expectedType,
    );

    assert(cursor >= 0, `Missing ordered AI Run event: ${expectedType}`);
  }
}

function eventTimeline(events) {
  return events
    .filter((event) => event.eventType !== "model.delta")
    .map((event) => `${event.sequence}:${event.eventType}`)
    .join(" → ");
}

async function main() {
  const { aiProviderRegistry } = await import(
    "../backend/dist/core/ai/AIProviderRegistry.js"
  );

  let generationCount = 0;

  aiProviderRegistry.register({
    providerId: "openai",
    displayName: "Deterministic OpenAI test provider",
    async generateText(input) {
      return {
        text: "Archivist runtime verified.",
        provider: "openai",
        model: input.generation.model,
      };
    },
    async streamText(input, options = {}) {
      generationCount += 1;
      const chunks =
        generationCount === 1
          ? ["Archivist ", "runtime ", "verified."]
          : Array.from(
              { length: 24 },
              (_, index) => `stream-${String(index + 1).padStart(2, "0")} `,
            );
      let text = "";

      for (const chunk of chunks) {
        await wait(generationCount === 1 ? 8 : 25, options.signal);

        if (options.signal?.aborted) {
          throw abortError();
        }

        text += chunk;
        await options.onDelta?.(chunk);
      }

      return {
        text,
        provider: "openai",
        model: input.generation.model,
      };
    },
    async discoverModels() {
      return [
        {
          provider: "openai",
          modelId: "gpt-5-mini",
          createdAt: null,
          ownedBy: "archivist-test",
        },
      ];
    },
    async checkHealth() {
      return {
        provider: "openai",
        status: "connected",
        checkedAt: new Date().toISOString(),
        message: null,
      };
    },
  });

  const [
    { app },
    databaseModule,
    runModel,
    chatModel,
    chatCompletion,
  ] = await Promise.all([
    import("../backend/dist/app.js"),
    import("../backend/dist/database/database.js"),
    import("../backend/dist/api/cognition/runs/models/AIRun.js"),
    import("../backend/dist/api/chats/models/Chat.js"),
    import("../backend/dist/api/chats/services/ChatCompletionService.js"),
  ]);
  closeDatabase = databaseModule.closeDatabase;

  await new Promise((resolve) => {
    server = app.listen(0, "127.0.0.1", resolve);
  });

  const address = server.address();
  const baseUrl = `http://127.0.0.1:${address.port}/api`;
  const library = (
    await requestJson(baseUrl, "/libraries", {
      method: "POST",
      body: JSON.stringify({
        rootPath: libraryPath,
        name: "Runtime Library",
      }),
    })
  ).library;
  await requestJson(baseUrl, `/libraries/${library.id}/scan`, {
    method: "POST",
  });
  const libraryFiles = (
    await requestJson(baseUrl, `/libraries/${library.id}/files`)
  ).files;
  const attachedFile = libraryFiles.find(
    (file) => file.relativePath === "runtime.md",
  );

  assert(attachedFile, "The runtime attachment fixture must be indexed.");

  const chat = (
    await requestJson(baseUrl, "/chats", {
      method: "POST",
      body: JSON.stringify({
        libraryId: library.id,
        title: "Runtime verification",
      }),
    })
  ).chat;

  await requestJson(baseUrl, `/chats/${chat.id}/attachments`, {
    method: "POST",
    body: JSON.stringify({
      libraryId: library.id,
      fileId: attachedFile.id,
    }),
  });

  const completedStart = await requestJson(baseUrl, `/chats/${chat.id}/runs`, {
    method: "POST",
    body: JSON.stringify({
      content: "Verify the copper orchard AI runtime evidence.",
    }),
  });
  const completedEvents = await collectRunEvents(
    baseUrl,
    completedStart.run.id,
  );

  assertOrderedEvents(completedEvents, [
    "run.started",
    "retrieval.started",
    "retrieval.completed",
    "context.started",
    "context.completed",
    "model.started",
    "model.delta",
    "model.completed",
    "run.completed",
  ]);

  const startedEvent = completedEvents.find(
    (event) => event.eventType === "run.started",
  );
  const retrievalEvent = completedEvents.find(
    (event) => event.eventType === "retrieval.completed",
  );
  const contextEvent = completedEvents.find(
    (event) => event.eventType === "context.completed",
  );

  assert(
    startedEvent?.payload?.attachedFiles?.some(
      (file) => file.relativePath === "runtime.md",
    ),
    "Run activity must snapshot user-attached file names.",
  );
  assert(
    retrievalEvent?.payload?.attachedSources?.some(
      (source) => source.relativePath === "runtime.md",
    ),
    "Run activity must report attached files that entered context.",
  );
  assert(
    retrievalEvent?.payload?.retrievedSources?.some(
      (source) => source.metadata?.relativePath === "retrieval.md",
    ),
    "Run activity must report automatically retrieved Library sources.",
  );
  assert(
    Number(contextEvent?.payload?.sourceCount ?? 0) >= 2 &&
      Number(contextEvent?.payload?.estimatedInputTokens ?? 0) > 0,
    "Run activity must report compiled source and token counts.",
  );

  const completedDelta = completedEvents
    .filter((event) => event.eventType === "model.delta")
    .map((event) => event.payload.delta)
    .join("");
  const completedSnapshot = (
    await requestJson(baseUrl, `/runs/${completedStart.run.id}`)
  ).run;

  assert(
    completedDelta === "Archivist runtime verified.",
    "Streamed deltas must preserve the provider response exactly.",
  );
  assert(
    completedSnapshot.status === "completed" &&
      completedSnapshot.finalResponse === completedDelta,
    "A completed AI Run must persist its final response.",
  );

  const cancelledStart = await requestJson(baseUrl, `/chats/${chat.id}/runs`, {
    method: "POST",
    body: JSON.stringify({ content: "Start a cancellable response." }),
  });
  let collisionVerified = false;
  let cancellationRequested = false;

  const cancelledEvents = await collectRunEvents(
    baseUrl,
    cancelledStart.run.id,
    async (event) => {
      if (event.eventType === "model.started" && !collisionVerified) {
        collisionVerified = true;
        await expectStatus(409, () =>
          requestJson(baseUrl, `/chats/${chat.id}/runs`, {
            method: "POST",
            body: JSON.stringify({ content: "This must be rejected." }),
          }),
        );
      }

      if (event.eventType === "model.delta" && !cancellationRequested) {
        cancellationRequested = true;
        await requestJson(baseUrl, `/runs/${cancelledStart.run.id}/cancel`, {
          method: "POST",
        });
      }
    },
  );

  assert(collisionVerified, "A second active Run must be rejected for one Chat.");
  assert(cancellationRequested, "The cancellation test must observe streamed output.");
  assertOrderedEvents(cancelledEvents, [
    "run.started",
    "retrieval.started",
    "retrieval.completed",
    "context.started",
    "context.completed",
    "model.started",
    "model.delta",
    "run.cancelled",
  ]);
  assert(
    !cancelledEvents.some(
      (event) =>
        event.eventType === "model.completed" ||
        event.eventType === "run.completed",
    ),
    "Cancelled Runs must not continue into model or Run completion.",
  );

  const cancelledSnapshot = (
    await requestJson(baseUrl, `/runs/${cancelledStart.run.id}`)
  ).run;
  assert(
    cancelledSnapshot.status === "cancelled" &&
      cancelledSnapshot.finalResponse.length > 0,
    "Cancellation must preserve the partial response.",
  );

  const replayedEvents = await collectRunEvents(
    baseUrl,
    cancelledStart.run.id,
  );
  assert(
    replayedEvents.map((event) => event.sequence).join(",") ===
      cancelledEvents.map((event) => event.sequence).join(","),
    "A terminal Run must replay the same durable event sequence.",
  );

  const messages = (
    await requestJson(baseUrl, `/chats/${chat.id}/messages`)
  ).messages;
  const cancelledMessage = messages.find(
    (message) => message.id === cancelledStart.assistantMessage.id,
  );
  assert(
    cancelledMessage?.status === "cancelled" &&
      cancelledMessage.content === cancelledSnapshot.finalResponse,
    "The cancelled assistant message must match the durable Run snapshot.",
  );

  const interruptedSession = chatCompletion.beginChatTurn(
    chat.id,
    "Recover an interrupted response.",
  );
  const interruptedRun = runModel.createAIRun({
    chatId: chat.id,
    libraryId: interruptedSession.libraryId,
    agentId: interruptedSession.agent.id,
    userMessageId: interruptedSession.userMessage.id,
    assistantMessageId: interruptedSession.assistantMessage.id,
    contextCompiler: interruptedSession.agent.context.compiler,
    provider: interruptedSession.agent.generation.provider,
    model: interruptedSession.agent.generation.model,
  });
  runModel.appendAIRunEvent(interruptedRun.id, "run.started", {
    recoveredByTest: true,
  });
  runModel.appendAIRunEvent(interruptedRun.id, "model.delta", {
    delta: "partial recovery",
  });

  const recoveredCount = runModel.recoverInterruptedAIRuns();
  const recoveredRun = runModel.requireAIRun(interruptedRun.id);
  const recoveredMessage = chatModel.getMessageById(
    interruptedSession.assistantMessage.id,
  );

  assert(recoveredCount === 1, "Exactly one interrupted Run must be recovered.");
  assert(
    recoveredRun.status === "failed" &&
      recoveredRun.errorCode === "process_interrupted" &&
      recoveredRun.finalResponse === "partial recovery",
    "Interrupted Run recovery must retain streamed output and explain the failure.",
  );
  assert(
    recoveredMessage?.status === "failed" &&
      recoveredMessage.content === "partial recovery",
    "Interrupted Run recovery must synchronize the assistant message.",
  );

  console.log("AI runtime smoke test: PASS");
  console.log(`  completion: ${eventTimeline(completedEvents)}`);
  console.log(`  cancellation: ${eventTimeline(cancelledEvents)}`);
  console.log("  attachment and retrieval activity metadata");
  console.log("  durable SSE replay");
  console.log("  one active Run per Chat");
  console.log("  partial response persistence");
  console.log("  interrupted Run recovery");
}

main()
  .catch((error) => {
    console.error(
      `AI runtime smoke test: FAIL\n${error instanceof Error ? error.stack : String(error)}`,
    );
    process.exitCode = 1;
  })
  .finally(async () => {
    if (server) {
      await new Promise((resolve) => server.close(resolve));
    }

    closeDatabase?.();
    process.chdir(originalWorkingDirectory);

    if (temporaryRoot.startsWith(os.tmpdir())) {
      fs.rmSync(temporaryRoot, {
        recursive: true,
        force: true,
      });
    }
  });

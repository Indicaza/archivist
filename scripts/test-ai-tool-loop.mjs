#!/usr/bin/env node

import fs from "node:fs";
import os from "node:os";
import path from "node:path";

const temporaryRoot = fs.mkdtempSync(
  path.join(os.tmpdir(), "archivist-ai-tool-loop-"),
);
const libraryPath = path.join(temporaryRoot, "Tool Loop Library");
const databasePath = path.join(temporaryRoot, "archivist.db");
const originalWorkingDirectory = process.cwd();

fs.mkdirSync(path.join(libraryPath, "Lore", "Deepkin"), {
  recursive: true,
});
fs.writeFileSync(
  path.join(libraryPath, "Lore", "Deepkin", "Funeral.md"),
  [
    "# Deepkin funerals",
    "",
    "Deepkin mourners cast black salt into the tide before the funeral bell rings.",
    "The bell is sounded once for memory and once for the sea.",
    "",
  ].join("\n"),
  "utf8",
);

fs.mkdirSync(path.join(libraryPath, "Characters", "Races"), {
  recursive: true,
});
fs.mkdirSync(path.join(libraryPath, "World_Setting"), {
  recursive: true,
});
fs.mkdirSync(path.join(libraryPath, "Game_Mechanics"), {
  recursive: true,
});
fs.writeFileSync(
  path.join(libraryPath, "Characters", "Races", "Gnomes.md"),
  [
    "# Gnomes",
    "",
    "Gnomes are warm forest alchemists who build communal homes into roots and stone.",
    "Their festivals, healing craft, and creature bonds make them a source of respite.",
    "",
  ].join("\n"),
  "utf8",
);
fs.writeFileSync(
  path.join(libraryPath, "Characters", "Races", "Mosslings.md"),
  [
    "# Mosslings",
    "",
    "Mosslings wear cute moss bodies around fragile printed nervous systems.",
    "They connect forest life to the Cradle and make nature feel comforting but wrong.",
    "",
  ].join("\n"),
  "utf8",
);
fs.writeFileSync(
  path.join(libraryPath, "World_Setting", "Overview.md"),
  [
    "# Setting",
    "",
    "The setting mixes rustic communities with uncanny printed life and mythified machines.",
    "",
  ].join("\n"),
  "utf8",
);
fs.writeFileSync(
  path.join(libraryPath, "Game_Mechanics", "CharacterController.md"),
  "Tools control movement in the overall game setting.\n",
  "utf8",
);

process.env.ARCHIVIST_DB_PATH = databasePath;
process.env.OPENAI_API_KEY = "sk-archivist-ai-tool-loop-smoke-test";
process.env.NODE_ENV = "test";
process.chdir(temporaryRoot);

let server;
let closeDatabase;

function assert(condition, message) {
  if (!condition) {
    throw new Error(message);
  }
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
    throw new Error(
      payload?.error?.message ?? `${response.status} ${response.statusText}`,
    );
  }

  return payload;
}

function parseSseBlock(block) {
  const data = block
    .split("\n")
    .filter((line) => line.startsWith("data:"))
    .map((line) => line.slice(5).trim())
    .join("\n");

  return data ? JSON.parse(data) : null;
}

async function collectRunEvents(baseUrl, runId) {
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

async function main() {
  const { aiProviderRegistry } = await import(
    "../backend/dist/core/ai/AIProviderRegistry.js"
  );

  const verifiedText = "Mosslings fit the uncanny setting more closely than Gnomes.";
  const directText = "Mosslings fit the uncanny setting more closely than Gnomes.";
  let observedToolNames = [];
  let observedToolCalls = 0;
  let observedDirectCalls = 0;

  aiProviderRegistry.register({
    providerId: "openai",
    displayName: "Deterministic tool-loop provider",
    async generateText() {
      throw new Error("The tool-loop test must not use generateText().");
    },
    async streamText(input, options = {}) {
      observedDirectCalls += 1;

      assert(
        input.instructions.includes(
          "No model-directed verification tool is exposed",
        ),
        "Sufficient automatic retrieval must not expose verification tools unless requested.",
      );
      assert(
        input.messages.some(
          (message) =>
            message.role === "system" &&
            message.content.includes("<retrieved-library-evidence>"),
        ),
        "Direct grounded answers must retain automatic Library evidence.",
      );

      await options.onDelta?.(directText);

      return {
        text: directText,
        provider: "openai",
        model: input.generation.model,
        toolCallCount: 0,
        toolRoundCount: 0,
      };
    },
    async streamTextWithTools(input, options) {
      observedToolNames = options.tools.map((tool) => tool.name).sort();

      assert(
        input.instructions.includes(
          "Use one read_file_ranges call to verify every needed excerpt",
        ),
        "The model must be told to batch verification after retrieval.",
      );

      const retrievalMessage = input.messages.find(
        (message) =>
          message.role === "system" &&
          message.content.includes("<retrieved-library-evidence>"),
      );
      assert(
        retrievalMessage,
        "The model must receive automatic retrieval evidence before tools.",
      );

      assert(
        input.messages.length <= 8 &&
          !input.messages.some((message) =>
            message.content.includes("Old unrelated lore turn 0"),
          ),
        "Standalone Library questions must use a focused recent-history window.",
      );

      const sourceHandles = Array.from(
        retrievalMessage.content.matchAll(
          /<source[^>]*file-id="([^"]+)"[^>]*path="([^"]+)"[^>]*start-line="(\d+)"[^>]*end-line="(\d+)"[^>]*>/g,
        ),
      );
      const gnomeHandle = sourceHandles.find((match) =>
        match[2].endsWith("Characters/Races/Gnomes.md"),
      );
      const mosslingHandle = sourceHandles.find((match) =>
        match[2].endsWith("Characters/Races/Mosslings.md"),
      );
      assert(
        gnomeHandle && mosslingHandle,
        "Subject-aware retrieval must include both requested race files.",
      );

      const read = await options.onToolCall({
        callId: "call-read-batch",
        name: "read_file_ranges",
        arguments: {
          ranges: [gnomeHandle, mosslingHandle].map((handle) => ({
            fileId: handle[1],
            startLine: Number(handle[3]),
            endLine: Number(handle[4]),
          })),
        },
        round: 1,
      });
      observedToolCalls += 1;

      assert(read.output?.ok === true, "Batched Library range read must succeed.");
      assert(
        read.output.result.rangeCount === 2 &&
          read.output.result.ranges.some((range) =>
            range.content.includes("forest alchemists"),
          ) &&
          read.output.result.ranges.some((range) =>
            range.content.includes("printed nervous systems"),
          ),
        "The model must receive both grounded race excerpts in one tool result.",
      );

      await options.onDelta?.(verifiedText);

      return {
        text: verifiedText,
        provider: "openai",
        model: input.generation.model,
        toolCallCount: 1,
        toolRoundCount: 1,
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
    toolExecutionModel,
    toolProviderAdapter,
    chatModel,
  ] = await Promise.all([
    import("../backend/dist/app.js"),
    import("../backend/dist/database/database.js"),
    import("../backend/dist/core/tools/models/AIToolExecution.js"),
    import("../backend/dist/core/tools/AIToolProviderAdapter.js"),
    import("../backend/dist/api/chats/models/Chat.js"),
  ]);
  closeDatabase = databaseModule.closeDatabase;

  const allToolNames = toolProviderAdapter
    .listModelAvailableAITools({ includeDiscoveryTools: true })
    .map((tool) => tool.name)
    .sort();
  const verificationToolNames = toolProviderAdapter
    .listModelAvailableAITools({
      includeDiscoveryTools: false,
      includeFullFileRead: false,
    })
    .map((tool) => tool.name)
    .sort();

  assert(
    allToolNames.join(",") ===
      "list_directory,read_file,read_file_range,read_file_ranges,search_filenames,search_library",
    "The full read-only tool set must remain available when retrieval is empty.",
  );
  assert(
    verificationToolNames.join(",") === "read_file_ranges",
    "Existing retrieval evidence must expose only bounded range verification.",
  );

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
        name: "Tool Loop Library",
      }),
    })
  ).library;

  await requestJson(baseUrl, `/libraries/${library.id}/scan`, {
    method: "POST",
  });

  const chat = (
    await requestJson(baseUrl, "/chats", {
      method: "POST",
      body: JSON.stringify({
        libraryId: library.id,
        title: "AI tool-loop verification",
      }),
    })
  ).chat;

  for (let index = 0; index < 12; index += 1) {
    chatModel.createMessage(chat.id, {
      role: "user",
      content: `Old unrelated lore turn ${index}.`,
      status: "complete",
    });
    chatModel.createMessage(chat.id, {
      role: "assistant",
      content: `Old unrelated answer ${index}.`,
      status: "complete",
    });
  }

  const started = await requestJson(baseUrl, `/chats/${chat.id}/runs`, {
    method: "POST",
    body: JSON.stringify({
      content: "Use tools to compare the Mosslings and Gnomes and judge which fits the overall setting.",
    }),
  });
  const events = await collectRunEvents(baseUrl, started.run.id);
  const retrievalCompleted = events.find(
    (event) => event.eventType === "retrieval.completed",
  );
  const retrievedPaths = (retrievalCompleted?.payload?.retrievedSources ?? [])
    .map((source) => source?.metadata?.relativePath ?? "");
  assert(
    retrievedPaths.some((value) => value.endsWith("Characters/Races/Gnomes.md")) &&
      retrievedPaths.some((value) => value.endsWith("Characters/Races/Mosslings.md")),
    "Subject-aware retrieval must reserve evidence for both named subjects.",
  );
  assert(
    !retrievedPaths.some((value) => value.endsWith("Game_Mechanics/CharacterController.md")),
    "Generic tool wording must not displace the named lore subjects.",
  );

  assertOrderedEvents(events, [
    "run.started",
    "retrieval.started",
    "retrieval.completed",
    "context.started",
    "context.completed",
    "model.started",
    "tool.requested",
    "tool.started",
    "tool.completed",
    "model.delta",
    "model.completed",
    "run.completed",
  ]);

  const expectedTools = ["read_file_ranges"];
  assert(
    observedToolNames.join(",") === expectedTools.join(","),
    "The model must receive only batched bounded verification after retrieval.",
  );
  assert(
    observedToolCalls === 1,
    "The provider must verify all retrieved ranges in one tool round.",
  );

  const modelStarted = events.find(
    (event) => event.eventType === "model.started",
  );
  const modelCompleted = events.find(
    (event) => event.eventType === "model.completed",
  );
  assert(
    modelStarted?.payload?.toolsEnabled === true &&
      modelStarted?.payload?.toolCount === 1 &&
      modelStarted?.payload?.discoveryToolsSuppressed === true &&
      modelStarted?.payload?.focusedHistory === true &&
      Number(modelStarted?.payload?.omittedHistoryMessageCount ?? 0) >= 18 &&
      Number(modelStarted?.payload?.initialRetrievalSourceCount ?? 0) > 0 &&
      modelStarted?.payload?.verificationRequested === true &&
      modelStarted?.payload?.verificationToolsAvailable === true,
    "The Run must report retrieval-aware tool suppression.",
  );
  assert(
    modelCompleted?.payload?.toolCallCount === 1 &&
      modelCompleted?.payload?.toolRoundCount === 1,
    "The Run must report completed tool call and round counts.",
  );

  const executions = toolExecutionModel.listAIToolExecutionsByRunId(
    started.run.id,
  );
  assert(executions.length === 1, "Exactly one batched verification read must persist.");
  assert(
    executions.every((execution) => execution.status === "completed"),
    "Every model-requested tool execution must complete.",
  );
  assert(
    executions[0]?.toolId === "read_file_ranges",
    "Automatic retrieval must not be repeated as a model-directed search.",
  );

  const run = (
    await requestJson(baseUrl, `/runs/${started.run.id}`)
  ).run;
  assert(
    run.status === "completed" && run.finalResponse === verifiedText,
    "The grounded final response must persist on the AI Run.",
  );

  const directChat = (
    await requestJson(baseUrl, "/chats", {
      method: "POST",
      body: JSON.stringify({
        libraryId: library.id,
        title: "Grounded answer without explicit verification",
      }),
    })
  ).chat;
  const directStarted = await requestJson(
    baseUrl,
    `/chats/${directChat.id}/runs`,
    {
      method: "POST",
      body: JSON.stringify({
        content: "Compare the Mosslings and Gnomes and judge which fits the overall setting.",
      }),
    },
  );
  const directEvents = await collectRunEvents(
    baseUrl,
    directStarted.run.id,
  );

  assertOrderedEvents(directEvents, [
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
  assert(
    !directEvents.some((event) => event.eventType.startsWith("tool.")),
    "A sufficient automatic retrieval turn must not create tool events without an explicit request.",
  );

  const directModelStarted = directEvents.find(
    (event) => event.eventType === "model.started",
  );
  const directModelCompleted = directEvents.find(
    (event) => event.eventType === "model.completed",
  );
  assert(
    directModelStarted?.payload?.toolsEnabled === false &&
      directModelStarted?.payload?.toolCount === 0 &&
      directModelStarted?.payload?.verificationRequested === false &&
      directModelStarted?.payload?.verificationToolsAvailable === false &&
      Number(directModelStarted?.payload?.initialRetrievalSourceCount ?? 0) > 0,
    "Automatic evidence must bypass model-directed tools when verification was not requested.",
  );
  assert(
    directModelCompleted?.payload?.toolCallCount === 0 &&
      directModelCompleted?.payload?.toolRoundCount === 0 &&
      observedDirectCalls === 1,
    "The direct grounded answer must use one provider round and zero tool rounds.",
  );
  assert(
    toolExecutionModel.listAIToolExecutionsByRunId(
      directStarted.run.id,
    ).length === 0,
    "A direct grounded answer must not persist phantom tool executions.",
  );

  const directRun = (
    await requestJson(baseUrl, `/runs/${directStarted.run.id}`)
  ).run;
  assert(
    directRun.status === "completed" &&
      directRun.finalResponse === directText,
    "The direct grounded response must persist on the AI Run.",
  );

  console.log("AI model tool-loop smoke test: PASS");
  console.log(`  model tools: ${observedToolNames.join(", ")}`);
  console.log("  subject-aware retrieval → one requested batch read → streamed answer");
  console.log("  sufficient automatic evidence → direct answer with zero tool rounds");
  console.log("  discovery and verification schemas suppressed by turn intent");
  console.log("  focused recent-history context and cost diagnostics");
  console.log("  durable tool events and execution records");
  console.log("  bounded read-only model tool access");
}

main()
  .catch((error) => {
    console.error("AI model tool-loop smoke test: FAIL");
    console.error(error);
    process.exitCode = 1;
  })
  .finally(async () => {
    if (server) {
      await new Promise((resolve) => server.close(resolve));
    }

    closeDatabase?.();
    process.chdir(originalWorkingDirectory);
    fs.rmSync(temporaryRoot, {
      recursive: true,
      force: true,
    });
  });

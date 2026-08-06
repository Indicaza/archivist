#!/usr/bin/env node

import fs from "node:fs";
import os from "node:os";
import path from "node:path";

const temporaryRoot = fs.mkdtempSync(
  path.join(os.tmpdir(), "archivist-ai-tools-"),
);
const libraryPath = path.join(temporaryRoot, "Tool Library");
const databasePath = path.join(temporaryRoot, "archivist.db");
const originalWorkingDirectory = process.cwd();

fs.mkdirSync(path.join(libraryPath, "Lore", "Deepkin"), {
  recursive: true,
});
fs.mkdirSync(path.join(libraryPath, "Lore", "Elari"), {
  recursive: true,
});
fs.writeFileSync(
  path.join(libraryPath, "README.md"),
  "# Tool Library\n\nA deterministic fixture for Archivist read tools.\n",
  "utf8",
);
fs.writeFileSync(
  path.join(libraryPath, "Lore", "Deepkin", "Funeral.md"),
  [
    "# Deepkin Funeral Rites",
    "",
    "Deepkin funeral bells are forged from black salt and river iron.",
    "The third bell marks the beginning of the ancestor vigil.",
    "The vigil lasts until the underground tide changes direction.",
  ].join("\n"),
  "utf8",
);
fs.writeFileSync(
  path.join(libraryPath, "Lore", "Elari", "Origins.md"),
  [
    "# Elari Origins",
    "",
    "The Elari claim they awoke beneath the first violet aurora.",
    "Their oldest songs disagree about whether the awakening was natural.",
  ].join("\n"),
  "utf8",
);

process.env.ARCHIVIST_DB_PATH = databasePath;
process.env.OPENAI_API_KEY = "sk-archivist-ai-tool-smoke-test";
process.env.NODE_ENV = "test";
process.chdir(temporaryRoot);

let server;
let closeDatabase;

function assert(condition, message) {
  if (!condition) {
    throw new Error(message);
  }
}

function passthroughSchema(validate = () => true) {
  return {
    safeParse(value) {
      if (validate(value)) {
        return {
          success: true,
          data: value,
        };
      }

      return {
        success: false,
        error: {
          issues: [
            {
              code: "custom",
              message: "Test schema rejected the value.",
              path: [],
            },
          ],
        },
      };
    },
  };
}

function abortError() {
  const error = new Error("Test tool cancelled.");
  error.name = "AbortError";
  return error;
}

function wait(milliseconds, signal) {
  return new Promise((resolve, reject) => {
    if (signal.aborted) {
      reject(abortError());
      return;
    }

    const timer = setTimeout(() => {
      signal.removeEventListener("abort", onAbort);
      resolve();
    }, milliseconds);

    const onAbort = () => {
      clearTimeout(timer);
      signal.removeEventListener("abort", onAbort);
      reject(abortError());
    };

    signal.addEventListener("abort", onAbort, {
      once: true,
    });
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

async function expectToolError(expectedCode, action) {
  try {
    await action();
  } catch (error) {
    assert(
      error?.code === expectedCode,
      `Expected AI tool error ${expectedCode}, received ${error?.code ?? error}.`,
    );
    return;
  }

  throw new Error(`Expected AI tool error ${expectedCode}, but the action succeeded.`);
}

async function main() {
  const [
    { app },
    databaseModule,
    chatCompletion,
    chatModel,
    runModel,
    { aiRunEventBroker },
    { AIToolRegistry, aiToolRegistry },
    { aiToolExecutor },
    toolExecutionModel,
    { registerBuiltInAITools },
  ] = await Promise.all([
    import("../backend/dist/app.js"),
    import("../backend/dist/database/database.js"),
    import("../backend/dist/api/chats/services/ChatCompletionService.js"),
    import("../backend/dist/api/chats/models/Chat.js"),
    import("../backend/dist/api/cognition/runs/models/AIRun.js"),
    import("../backend/dist/api/cognition/runs/services/AIRunEventBroker.js"),
    import("../backend/dist/core/tools/AIToolRegistry.js"),
    import("../backend/dist/core/tools/AIToolExecutor.js"),
    import("../backend/dist/core/tools/models/AIToolExecution.js"),
    import("../backend/dist/core/tools/registerBuiltInAITools.js"),
  ]);
  closeDatabase = databaseModule.closeDatabase;
  registerBuiltInAITools();

  const registeredIds = aiToolRegistry.list().map((tool) => tool.id);
  assert(
    registeredIds.join(",") ===
      [
        "list_directory",
        "read_file",
        "read_file_range",
        "read_file_ranges",
        "search_filenames",
        "search_library",
      ].join(","),
    `Unexpected built-in AI tool registry: ${registeredIds.join(", ")}`,
  );

  const duplicateRegistry = new AIToolRegistry();
  const duplicateDefinition = {
    id: "duplicate_test",
    name: "Duplicate test",
    description: "Used to prove duplicate tool IDs are rejected.",
    permission: "read-only",
    inputSchema: passthroughSchema(),
    outputSchema: passthroughSchema(),
    timeoutMs: 1_000,
    execute(input) {
      return input;
    },
  };
  duplicateRegistry.register(duplicateDefinition);

  let duplicateRejected = false;

  try {
    duplicateRegistry.register(duplicateDefinition);
  } catch {
    duplicateRejected = true;
  }

  assert(duplicateRejected, "Duplicate AI tool IDs must be rejected.");

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
        name: "Tool Library",
      }),
    })
  ).library;
  await requestJson(baseUrl, `/libraries/${library.id}/scan`, {
    method: "POST",
  });
  const files = (
    await requestJson(baseUrl, `/libraries/${library.id}/files`)
  ).files;
  const funeralFile = files.find(
    (file) => file.relativePath === "Lore/Deepkin/Funeral.md",
  );

  assert(funeralFile, "The Deepkin funeral fixture must be cataloged.");

  const chat = (
    await requestJson(baseUrl, "/chats", {
      method: "POST",
      body: JSON.stringify({
        libraryId: library.id,
        title: "AI tool verification",
      }),
    })
  ).chat;
  const session = chatCompletion.beginChatTurn(
    chat.id,
    "Verify the read-only AI tool foundation.",
  );
  const run = runModel.createAIRun({
    chatId: chat.id,
    libraryId: library.id,
    agentId: session.agent.id,
    userMessageId: session.userMessage.id,
    assistantMessageId: session.assistantMessage.id,
    contextCompiler: session.agent.context.compiler,
    provider: session.agent.generation.provider,
    model: session.agent.generation.model,
  });
  aiRunEventBroker.append(run.id, "run.started", {
    smokeTest: "ai-tools",
  });

  const execute = (toolId, input, options = {}) =>
    aiToolExecutor.execute({
      runId: run.id,
      chatId: chat.id,
      libraryId: library.id,
      toolId,
      input,
      ...options,
    });

  const rootListing = await execute("list_directory", {
    path: "",
    limit: 20,
  });
  assert(
    rootListing.output.entries.some(
      (entry) => entry.type === "directory" && entry.relativePath === "Lore",
    ) &&
      rootListing.output.entries.some(
        (entry) => entry.type === "file" && entry.relativePath === "README.md",
      ),
    "list_directory must return immediate files and folders.",
  );

  const loreListing = await execute("list_directory", {
    path: "Lore",
    limit: 20,
  });
  assert(
    loreListing.output.entries.some(
      (entry) => entry.type === "directory" && entry.relativePath === "Lore/Deepkin",
    ),
    "list_directory must navigate catalog subdirectories.",
  );

  const filenameSearch = await execute("search_filenames", {
    query: "funeral",
    limit: 10,
  });
  assert(
    filenameSearch.output.matches.some(
      (match) => match.relativePath === "Lore/Deepkin/Funeral.md",
    ),
    "search_filenames must match catalog paths case-insensitively.",
  );

  const librarySearch = await execute("search_library", {
    query: "black salt funeral bell",
    limit: 8,
  });
  assert(
    librarySearch.output.matches.some(
      (match) =>
        match.relativePath === "Lore/Deepkin/Funeral.md" &&
        match.content.includes("black salt"),
    ),
    "search_library must return indexed excerpts and line metadata.",
  );

  const fileRead = await execute("read_file", {
    fileId: funeralFile.id,
  });
  assert(
    fileRead.output.file.relativePath === "Lore/Deepkin/Funeral.md" &&
      fileRead.output.content.includes("ancestor vigil") &&
      fileRead.output.truncated === false,
    "read_file must use the guarded Library reader.",
  );

  const rangeRead = await execute("read_file_range", {
    fileId: funeralFile.id,
    startLine: 3,
    endLine: 4,
  });
  assert(
    rangeRead.output.startLine === 3 &&
      rangeRead.output.endLine === 4 &&
      rangeRead.output.content.includes("black salt") &&
      rangeRead.output.content.includes("third bell"),
    "read_file_range must return the exact requested 1-based line range.",
  );

  const batchedRead = await execute("read_file_ranges", {
    ranges: [
      {
        fileId: funeralFile.id,
        startLine: 1,
        endLine: 2,
      },
      {
        fileId: "Lore/Deepkin/Funeral.md",
        startLine: 3,
        endLine: 4,
      },
    ],
  });
  assert(
    batchedRead.output.rangeCount === 2 &&
      batchedRead.output.ranges[0].content.includes("Deepkin") &&
      batchedRead.output.ranges[1].content.includes("black salt") &&
      batchedRead.output.truncated === false,
    "read_file_ranges must batch guarded UUID and path reads in one execution.",
  );

  await expectToolError("invalid_input", () =>
    execute("search_filenames", {
      query: "",
      limit: 10,
    }),
  );

  let mutationExecuted = false;
  aiToolRegistry.register({
    id: "test_mutation",
    name: "Test mutation",
    description: "Must be blocked without mutation permission.",
    permission: "safe-local-mutation",
    inputSchema: passthroughSchema(),
    outputSchema: passthroughSchema(),
    timeoutMs: 1_000,
    execute(input) {
      mutationExecuted = true;
      return input;
    },
  });
  await expectToolError("permission_denied", () =>
    execute("test_mutation", {
      shouldNotRun: true,
    }),
  );
  assert(
    mutationExecuted === false,
    "Permission denial must stop the tool before execution.",
  );

  aiToolRegistry.register({
    id: "test_cancel",
    name: "Test cancellation",
    description: "Wait until the caller cancels the execution.",
    permission: "read-only",
    inputSchema: passthroughSchema(),
    outputSchema: passthroughSchema(),
    timeoutMs: 1_000,
    async execute(_input, context) {
      await wait(500, context.signal);
      return {
        completed: true,
      };
    },
  });
  const cancellationController = new AbortController();
  const cancellation = execute(
    "test_cancel",
    {},
    {
      signal: cancellationController.signal,
    },
  );
  setTimeout(() => cancellationController.abort(), 20);
  await expectToolError("cancelled", () => cancellation);

  aiToolRegistry.register({
    id: "test_timeout",
    name: "Test timeout",
    description: "Wait longer than the registered tool timeout.",
    permission: "read-only",
    inputSchema: passthroughSchema(),
    outputSchema: passthroughSchema(),
    timeoutMs: 20,
    async execute(_input, context) {
      await wait(500, context.signal);
      return {
        completed: true,
      };
    },
  });
  await expectToolError("timed_out", () => execute("test_timeout", {}));

  aiToolRegistry.register({
    id: "test_invalid_output",
    name: "Test invalid output",
    description: "Return a value rejected by the output schema.",
    permission: "read-only",
    inputSchema: passthroughSchema(),
    outputSchema: passthroughSchema(
      (value) => Boolean(value && value.valid === true),
    ),
    timeoutMs: 1_000,
    execute() {
      return {
        valid: false,
      };
    },
  });
  await expectToolError("invalid_output", () =>
    execute("test_invalid_output", {}),
  );

  const executions = toolExecutionModel.listAIToolExecutionsByRunId(run.id);
  const events = runModel.listAIRunEvents(run.id);
  const completedExecutions = executions.filter(
    (execution) => execution.status === "completed",
  );
  const failedExecutions = executions.filter(
    (execution) => execution.status === "failed",
  );
  const cancelledExecutions = executions.filter(
    (execution) => execution.status === "cancelled",
  );

  assert(
    completedExecutions.length === 7,
    `Expected seven completed Library read executions, found ${completedExecutions.length}.`,
  );
  assert(
    failedExecutions.some(
      (execution) => execution.errorCode === "invalid_input",
    ) &&
      failedExecutions.some(
        (execution) => execution.errorCode === "permission_denied",
      ) &&
      failedExecutions.some(
        (execution) => execution.errorCode === "timed_out",
      ) &&
      failedExecutions.some(
        (execution) => execution.errorCode === "invalid_output",
      ),
    "Failed tool executions must retain structured error codes.",
  );
  assert(
    cancelledExecutions.length === 1 &&
      cancelledExecutions[0].errorCode === "cancelled",
    "Cancelled tool executions must persist distinctly from failures.",
  );
  assert(
    events.some((event) => event.eventType === "tool.requested") &&
      events.some((event) => event.eventType === "tool.started") &&
      events.some((event) => event.eventType === "tool.completed") &&
      events.some((event) => event.eventType === "tool.failed") &&
      events.some((event) => event.eventType === "tool.cancelled"),
    "The AI Run trace must include every tool lifecycle event type.",
  );
  assert(
    events
      .filter((event) => event.eventType === "tool.requested")
      .every((event) =>
        executions.some(
          (execution) => execution.id === event.payload.executionId,
        ),
      ),
    "Every requested tool event must point to a durable execution record.",
  );

  chatModel.updateMessage(session.assistantMessage.id, {
    content: "AI tool foundation verified.",
    status: "complete",
  });
  runModel.completeAIRun(run.id, "AI tool foundation verified.", null);
  aiRunEventBroker.append(run.id, "run.completed", {
    smokeTest: "ai-tools",
    executionCount: executions.length,
  });

  console.log("AI tools smoke test: PASS");
  console.log(`  registered tools: ${registeredIds.join(", ")}`);
  console.log("  strict input and output validation");
  console.log("  default read-only permission policy");
  console.log("  cancellation and timeout propagation");
  console.log("  durable execution records and Run events");
  console.log("  Library catalog, FTS5, and guarded reader integration");
}

main()
  .catch((error) => {
    console.error(
      `AI tools smoke test: FAIL\n${error instanceof Error ? error.stack : String(error)}`,
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

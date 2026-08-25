#!/usr/bin/env node

import { createHash } from "node:crypto";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";

const temporaryRoot = fs.mkdtempSync(
  path.join(os.tmpdir(), "archivist-ai-edit-proposals-"),
);
const libraryPath = path.join(temporaryRoot, "Edit Proposal Library");
const databasePath = path.join(temporaryRoot, "archivist.db");
const originalWorkingDirectory = process.cwd();

fs.mkdirSync(path.join(libraryPath, "Lore"), {
  recursive: true,
});
fs.mkdirSync(path.join(libraryPath, "Archive"), {
  recursive: true,
});
const existingContent = "# Existing\n\nOld text.\n";
fs.writeFileSync(
  path.join(libraryPath, "Lore", "Existing.md"),
  existingContent,
  "utf8",
);
const renameContent = "# Rename me\n\nKeep this content.\n";
const moveContent = "# Move me\n\nKeep this content too.\n";
fs.writeFileSync(
  path.join(libraryPath, "Lore", "RenameMe.md"),
  renameContent,
  "utf8",
);
fs.writeFileSync(
  path.join(libraryPath, "Lore", "MoveMe.md"),
  moveContent,
  "utf8",
);
const outsidePath = path.join(temporaryRoot, "Outside");
fs.mkdirSync(outsidePath);
fs.symlinkSync(
  outsidePath,
  path.join(libraryPath, "Escape"),
  process.platform === "win32" ? "junction" : "dir",
);

process.env.ARCHIVIST_DB_PATH = databasePath;
process.env.OPENAI_API_KEY = "sk-archivist-ai-edit-proposal-smoke-test";
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
    const error = new Error(
      payload?.error?.message ?? `${response.status} ${response.statusText}`,
    );
    error.status = response.status;
    error.payload = payload;
    throw error;
  }

  return payload;
}

async function expectFailure(messagePattern, action) {
  try {
    await action();
  } catch (error) {
    assert(
      messagePattern.test(String(error?.message || error)),
      `Unexpected failure: ${error?.message || error}`,
    );
    return;
  }

  throw new Error(`Expected failure matching ${messagePattern}.`);
}

async function main() {
  const [
    { app },
    databaseModule,
    chatCompletion,
    runModel,
    editProposalModel,
    editProposalService,
    editInspection,
    toolProviderAdapter,
    { aiToolExecutor },
    { registerBuiltInAITools },
  ] = await Promise.all([
    import("../backend/dist/app.js"),
    import("../backend/dist/database/database.js"),
    import("../backend/dist/api/chats/services/ChatCompletionService.js"),
    import("../backend/dist/api/cognition/runs/models/AIRun.js"),
    import("../backend/dist/api/cognition/edits/models/AIEditProposal.js"),
    import("../backend/dist/api/cognition/edits/services/AIEditProposalService.js"),
    import("../backend/dist/api/libraries/services/LibraryEditInspection.js"),
    import("../backend/dist/core/tools/AIToolProviderAdapter.js"),
    import("../backend/dist/core/tools/AIToolExecutor.js"),
    import("../backend/dist/core/tools/registerBuiltInAITools.js"),
  ]);
  closeDatabase = databaseModule.closeDatabase;
  registerBuiltInAITools();

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
        name: "Edit Proposal Library",
      }),
    })
  ).library;
  const inspectedExisting =
    await editInspection.inspectLibraryTextFileForEdit(
      library.id,
      "Lore/Existing.md",
    );
  const expectedExistingHash = createHash("sha256")
    .update(existingContent, "utf8")
    .digest("hex");

  assert(
    inspectedExisting.relativePath === "Lore/Existing.md"
      && inspectedExisting.content === existingContent
      && inspectedExisting.sha256 === expectedExistingHash,
    "AI edit inspection must snapshot exact UTF-8 content and SHA-256.",
  );

  const inspectedCreate =
    await editInspection.inspectLibraryCreateTarget(
      library.id,
      "Lore/Mosslings.md",
    );

  assert(
    inspectedCreate.relativePath === "Lore/Mosslings.md"
      && inspectedCreate.parentRelativePath === "Lore"
      && inspectedCreate.expectedState === "absent",
    "Create-target inspection must prove the destination is currently absent.",
  );

  await expectFailure(/already exists/i, async () => {
    await editInspection.inspectLibraryCreateTarget(
      library.id,
      "Lore/Existing.md",
    );
  });

  await expectFailure(/outside the Library root/i, async () => {
    await editInspection.inspectLibraryCreateTarget(
      library.id,
      "Escape/Outside.md",
    );
  });

  await expectFailure(/not supported for AI text creation/i, async () => {
    await editInspection.inspectLibraryCreateTarget(
      library.id,
      "Lore/Payload.exe",
    );
  });

  const inspectedDirectory =
    await editInspection.inspectLibraryDirectoryCreateTarget(
      library.id,
      "Lore/ProposedFolder",
    );

  assert(
    inspectedDirectory.relativePath === "Lore/ProposedFolder"
      && inspectedDirectory.parentRelativePath === "Lore"
      && inspectedDirectory.expectedState === "absent",
    "Directory-target inspection must prove a safe destination is currently absent.",
  );

  await expectFailure(/directory target already exists/i, async () => {
    await editInspection.inspectLibraryDirectoryCreateTarget(
      library.id,
      "Lore",
    );
  });

  await expectFailure(/not supported for AI directory creation/i, async () => {
    await editInspection.inspectLibraryDirectoryCreateTarget(
      library.id,
      "Lore/Blocked.app",
    );
  });

  const proposalOnlyTools = toolProviderAdapter
    .listModelAvailableAITools({
      includeDiscoveryTools: false,
      includeFullFileRead: false,
      includeVerificationTools: false,
      includeMutationProposalTools: true,
    })
    .map((tool) => tool.name)
    .sort();

  assert(
    proposalOnlyTools.join(",")
      === "create_directory,create_file,move_file,patch_file,rename_file",
    "Explicit mutation turns must expose the complete proposal-only mutation primitive set when reads are suppressed.",
  );

  const chat = (
    await requestJson(baseUrl, "/chats", {
      method: "POST",
      body: JSON.stringify({
        libraryId: library.id,
        title: "Safe edit proposal verification",
      }),
    })
  ).chat;
  const session = chatCompletion.beginChatTurn(
    chat.id,
    "Propose safe Library edits without writing them.",
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
  const expectedHash = inspectedExisting.sha256;
  const proposal = editProposalService.proposeAIEdit({
    runId: run.id,
    summary: "Create one lore note and prepare one bounded patch.",
    operations: [
      {
        type: "create_file",
        destinationPath: "Lore/Mosslings.md",
        afterContent: "# Mosslings\n\nDraft lore.\n",
      },
      {
        type: "patch_file",
        sourcePath: "Lore/Existing.md",
        expectedHash,
        beforeContent: "# Existing\n\nOld text.\n",
        afterContent: "# Existing\n\nRevised text.\n",
      },
    ],
  });

  assert(proposal.status === "proposed", "New proposals must remain proposed.");
  assert(
    proposal.runId === run.id
      && proposal.chatId === chat.id
      && proposal.libraryId === library.id
      && proposal.agentId === run.agentId
      && proposal.provider === run.provider
      && proposal.model === run.model,
    "Proposal provenance must be snapshotted from the initiating Run.",
  );
  assert(
    proposal.operations.length === 2
      && proposal.operations[0].ordinal === 1
      && proposal.operations[0].type === "create_file"
      && proposal.operations[1].ordinal === 2
      && proposal.operations[1].type === "patch_file"
      && proposal.operations[1].expectedHash === expectedHash,
    "Proposal operations must persist in deterministic order.",
  );
  assert(
    !fs.existsSync(path.join(libraryPath, "Lore", "Mosslings.md")),
    "Creating a proposal must never mutate the Library.",
  );

  const loaded = editProposalModel.getAIEditProposalById(proposal.id);
  const listed = editProposalModel.listAIEditProposalsByRunId(run.id);

  assert(
    loaded?.id === proposal.id
      && loaded.operations.length === 2
      && listed.length === 1
      && listed[0].id === proposal.id,
    "Edit proposals and their operations must reload durably.",
  );

  runModel.completeAIRun(run.id, "Proposal foundation verified.", null);

  const aggregationChat = (
    await requestJson(baseUrl, "/chats", {
      method: "POST",
      body: JSON.stringify({
        libraryId: library.id,
        title: "Aggregated edit proposal verification",
      }),
    })
  ).chat;
  const aggregationSession = chatCompletion.beginChatTurn(
    aggregationChat.id,
    "Propose multiple safe Library edits without writing them.",
  );
  const aggregationRun = runModel.createAIRun({
    chatId: aggregationChat.id,
    libraryId: library.id,
    agentId: aggregationSession.agent.id,
    userMessageId: aggregationSession.userMessage.id,
    assistantMessageId: aggregationSession.assistantMessage.id,
    contextCompiler: aggregationSession.agent.context.compiler,
    provider: aggregationSession.agent.generation.provider,
    model: aggregationSession.agent.generation.model,
  });

  await expectFailure(/requires safe-local-mutation permission/i, async () => {
    await aiToolExecutor.execute({
      runId: aggregationRun.id,
      chatId: aggregationChat.id,
      libraryId: library.id,
      toolId: "create_file",
      input: {
        path: "Lore/Denied.md",
        content: "This proposal must not be created.",
      },
      grantedPermissions: new Set(),
    });
  });

  const toolExecution = await aiToolExecutor.execute({
    runId: aggregationRun.id,
    chatId: aggregationChat.id,
    libraryId: library.id,
    toolId: "create_file",
    input: {
      path: "Lore/ToolDraft.md",
      content: "# Tool Draft\n\nReview me before writing.\n",
    },
  });

  assert(
    toolExecution.status === "completed"
      && toolExecution.permission === "safe-local-mutation"
      && toolExecution.output?.status === "proposed"
      && toolExecution.output?.operationType === "create_file"
      && toolExecution.output?.relativePath === "Lore/ToolDraft.md"
      && toolExecution.output?.writesPerformed === false,
    "The create_file tool must complete as a proposal without performing a write.",
  );
  assert(
    !fs.existsSync(path.join(libraryPath, "Lore", "ToolDraft.md")),
    "The create_file tool must never write before approval.",
  );

  const patchedContent = "# Existing\n\nTool revised text.\n";
  const patchExecution = await aiToolExecutor.execute({
    runId: aggregationRun.id,
    chatId: aggregationChat.id,
    libraryId: library.id,
    toolId: "patch_file",
    input: {
      path: "Lore/Existing.md",
      content: patchedContent,
    },
  });
  const patchedContentHash = createHash("sha256")
    .update(patchedContent, "utf8")
    .digest("hex");

  assert(
    patchExecution.status === "completed"
      && patchExecution.permission === "safe-local-mutation"
      && patchExecution.output?.status === "proposed"
      && patchExecution.output?.operationType === "patch_file"
      && patchExecution.output?.relativePath === "Lore/Existing.md"
      && patchExecution.output?.expectedHash === expectedExistingHash
      && patchExecution.output?.proposedContentSha256 === patchedContentHash
      && patchExecution.output?.writesPerformed === false,
    "The patch_file tool must snapshot the exact source hash and return a proposal without writing.",
  );
  assert(
    toolExecution.output?.proposalId === patchExecution.output?.proposalId,
    "Multiple proposal tools in one AI Run must converge on one proposal ID.",
  );
  assert(
    fs.readFileSync(path.join(libraryPath, "Lore", "Existing.md"), "utf8")
      === existingContent,
    "The patch_file tool must leave the source file byte-for-byte unchanged before approval.",
  );

  const createDirectoryExecution = await aiToolExecutor.execute({
    runId: aggregationRun.id,
    chatId: aggregationChat.id,
    libraryId: library.id,
    toolId: "create_directory",
    input: {
      path: "Lore/ProposedFolder",
    },
  });

  assert(
    createDirectoryExecution.status === "completed"
      && createDirectoryExecution.output?.operationType === "create_directory"
      && createDirectoryExecution.output?.relativePath === "Lore/ProposedFolder"
      && createDirectoryExecution.output?.expectedState === "absent"
      && createDirectoryExecution.output?.writesPerformed === false,
    "The create_directory tool must create only a reviewable proposal.",
  );
  assert(
    !fs.existsSync(path.join(libraryPath, "Lore", "ProposedFolder")),
    "The create_directory tool must never create the directory before approval.",
  );

  const renameExpectedHash = createHash("sha256")
    .update(renameContent, "utf8")
    .digest("hex");
  const renameExecution = await aiToolExecutor.execute({
    runId: aggregationRun.id,
    chatId: aggregationChat.id,
    libraryId: library.id,
    toolId: "rename_file",
    input: {
      sourcePath: "Lore/RenameMe.md",
      destinationPath: "Lore/Renamed.md",
    },
  });

  assert(
    renameExecution.status === "completed"
      && renameExecution.output?.operationType === "rename_file"
      && renameExecution.output?.sourcePath === "Lore/RenameMe.md"
      && renameExecution.output?.destinationPath === "Lore/Renamed.md"
      && renameExecution.output?.expectedHash === renameExpectedHash
      && renameExecution.output?.writesPerformed === false,
    "The rename_file tool must snapshot the source hash and propose an absent same-directory destination.",
  );
  assert(
    fs.readFileSync(path.join(libraryPath, "Lore", "RenameMe.md"), "utf8")
      === renameContent
      && !fs.existsSync(path.join(libraryPath, "Lore", "Renamed.md")),
    "The rename_file tool must leave both source and destination unchanged before approval.",
  );

  const moveExpectedHash = createHash("sha256")
    .update(moveContent, "utf8")
    .digest("hex");
  const moveExecution = await aiToolExecutor.execute({
    runId: aggregationRun.id,
    chatId: aggregationChat.id,
    libraryId: library.id,
    toolId: "move_file",
    input: {
      sourcePath: "Lore/MoveMe.md",
      destinationPath: "Archive/MoveMe.md",
    },
  });

  assert(
    moveExecution.status === "completed"
      && moveExecution.output?.operationType === "move_file"
      && moveExecution.output?.sourcePath === "Lore/MoveMe.md"
      && moveExecution.output?.destinationPath === "Archive/MoveMe.md"
      && moveExecution.output?.expectedHash === moveExpectedHash
      && moveExecution.output?.writesPerformed === false,
    "The move_file tool must snapshot the source hash and propose an absent destination while preserving the name.",
  );
  assert(
    fs.readFileSync(path.join(libraryPath, "Lore", "MoveMe.md"), "utf8")
      === moveContent
      && !fs.existsSync(path.join(libraryPath, "Archive", "MoveMe.md")),
    "The move_file tool must leave both source and destination unchanged before approval.",
  );

  assert(
    toolExecution.output?.proposalId === createDirectoryExecution.output?.proposalId
      && toolExecution.output?.proposalId === renameExecution.output?.proposalId
      && toolExecution.output?.proposalId === moveExecution.output?.proposalId,
    "All proposal-only mutation primitives in one AI Run must converge on one proposal ID.",
  );

  const aggregatedProposals =
    editProposalModel.listAIEditProposalsByRunId(aggregationRun.id);
  const aggregatedProposal = aggregatedProposals[0];
  const createOperation = aggregatedProposal?.operations[0];
  const patchOperation = aggregatedProposal?.operations[1];
  const createDirectoryOperation = aggregatedProposal?.operations[2];
  const renameOperation = aggregatedProposal?.operations[3];
  const moveOperation = aggregatedProposal?.operations[4];

  assert(
    aggregatedProposals.length === 1
      && aggregatedProposal?.summary === "Multiple proposed Library changes"
      && aggregatedProposal.operations.length === 5
      && createOperation?.ordinal === 1
      && createOperation.type === "create_file"
      && createOperation.destinationPath === "Lore/ToolDraft.md"
      && patchOperation?.ordinal === 2
      && patchOperation.type === "patch_file"
      && patchOperation.sourcePath === "Lore/Existing.md"
      && patchOperation.destinationPath === "Lore/Existing.md"
      && patchOperation.expectedHash === expectedExistingHash
      && patchOperation.beforeContent === existingContent
      && patchOperation.afterContent === patchedContent
      && createDirectoryOperation?.ordinal === 3
      && createDirectoryOperation.type === "create_directory"
      && createDirectoryOperation.destinationPath === "Lore/ProposedFolder"
      && renameOperation?.ordinal === 4
      && renameOperation.type === "rename_file"
      && renameOperation.sourcePath === "Lore/RenameMe.md"
      && renameOperation.destinationPath === "Lore/Renamed.md"
      && renameOperation.expectedHash === renameExpectedHash
      && moveOperation?.ordinal === 5
      && moveOperation.type === "move_file"
      && moveOperation.sourcePath === "Lore/MoveMe.md"
      && moveOperation.destinationPath === "Archive/MoveMe.md"
      && moveOperation.expectedHash === moveExpectedHash
      && toolExecution.output?.operationId === createOperation.id
      && patchExecution.output?.operationId === patchOperation.id
      && createDirectoryExecution.output?.operationId === createDirectoryOperation.id
      && renameExecution.output?.operationId === renameOperation.id
      && moveExecution.output?.operationId === moveOperation.id,
    "Proposal tools must append the complete primitive set in deterministic order to one active Run proposal.",
  );

  const aggregationEvents = runModel.listAIRunEvents(aggregationRun.id);
  const proposalEvents = aggregationEvents.filter(
    (event) => event.eventType === "edit.proposed",
  );

  assert(
    proposalEvents.length === 5
      && proposalEvents.every(
        (event) => event.payload.proposalId === aggregatedProposal.id,
      )
      && proposalEvents.map((event) => event.payload.operationCount).join(",")
        === "1,2,3,4,5"
      && proposalEvents[4].payload.operationTypes?.join(",")
        === "create_file,patch_file,create_directory,rename_file,move_file"
      && proposalEvents[4].payload.affectedPaths?.includes(
        "Lore/ToolDraft.md",
      )
      && proposalEvents[4].payload.affectedPaths?.includes(
        "Lore/Existing.md",
      )
      && proposalEvents[4].payload.affectedPaths?.includes(
        "Lore/ProposedFolder",
      )
      && proposalEvents[4].payload.affectedPaths?.includes(
        "Lore/RenameMe.md",
      )
      && proposalEvents[4].payload.affectedPaths?.includes(
        "Lore/Renamed.md",
      )
      && proposalEvents[4].payload.affectedPaths?.includes(
        "Lore/MoveMe.md",
      )
      && proposalEvents[4].payload.affectedPaths?.includes(
        "Archive/MoveMe.md",
      )
      && proposalEvents[4].payload.writesPerformed === false,
    "Run trace events must retain one proposal identity while reporting cumulative operations across every primitive.",
  );

  await expectFailure(/already contains an operation affecting/i, async () => {
    await aiToolExecutor.execute({
      runId: aggregationRun.id,
      chatId: aggregationChat.id,
      libraryId: library.id,
      toolId: "create_file",
      input: {
        path: "Lore/ToolDraft.md",
        content: "# Conflicting Draft\n",
      },
    });
  });

  await expectFailure(/differ from the current file/i, async () => {
    await aiToolExecutor.execute({
      runId: aggregationRun.id,
      chatId: aggregationChat.id,
      libraryId: library.id,
      toolId: "patch_file",
      input: {
        path: "Lore/Existing.md",
        content: existingContent,
      },
    });
  });

  await expectFailure(/current directory/i, async () => {
    await aiToolExecutor.execute({
      runId: aggregationRun.id,
      chatId: aggregationChat.id,
      libraryId: library.id,
      toolId: "rename_file",
      input: {
        sourcePath: "Lore/RenameMe.md",
        destinationPath: "Archive/Renamed.md",
      },
    });
  });

  await expectFailure(/preserve the file name/i, async () => {
    await aiToolExecutor.execute({
      runId: aggregationRun.id,
      chatId: aggregationChat.id,
      libraryId: library.id,
      toolId: "move_file",
      input: {
        sourcePath: "Lore/MoveMe.md",
        destinationPath: "Archive/ChangedName.md",
      },
    });
  });

  await expectFailure(/already exists/i, async () => {
    await aiToolExecutor.execute({
      runId: aggregationRun.id,
      chatId: aggregationChat.id,
      libraryId: library.id,
      toolId: "rename_file",
      input: {
        sourcePath: "Lore/RenameMe.md",
        destinationPath: "Lore/Existing.md",
      },
    });
  });

  await expectFailure(/safe Library-relative path/i, async () => {
    editProposalService.proposeAIEdit({
      runId: aggregationRun.id,
      summary: "Reject traversal.",
      operations: [
        {
          type: "create_file",
          destinationPath: "../escape.md",
          afterContent: "nope",
        },
      ],
    });
  });

  await expectFailure(/expectedHash/i, async () => {
    editProposalService.proposeAIEdit({
      runId: aggregationRun.id,
      summary: "Reject an unguarded patch.",
      operations: [
        {
          type: "patch_file",
          sourcePath: "Lore/Existing.md",
          beforeContent: "old",
          afterContent: "new",
        },
      ],
    });
  });

  runModel.completeAIRun(
    aggregationRun.id,
    "Aggregated proposal foundation verified.",
    null,
  );

  await expectFailure(/active AI Run/i, async () => {
    editProposalService.proposeAIEdit({
      runId: aggregationRun.id,
      summary: "Reject late proposal.",
      operations: [
        {
          type: "create_directory",
          destinationPath: "Late",
        },
      ],
    });
  });

  const schemaVersion = databaseModule.database.pragma("user_version", {
    simple: true,
  });

  assert(schemaVersion === 20, `Expected schema version 20, found ${schemaVersion}.`);

  console.log("AI edit proposal smoke test: PASS");
  console.log("  durable proposal and operation persistence");
  console.log("  proposal-time existence, symlink containment, and SHA-256 inspection");
  console.log("  model-visible create_file, patch_file, create_directory, rename_file, and move_file proposal tools");
  console.log("  explicit permission denial remains authoritative");
  console.log("  Run provenance and cumulative edit.proposed trace");
  console.log("  one active proposal per Run with deterministic operation ordinals");
  console.log("  duplicate/conflicting affected-path rejection");
  console.log("  exact patch, rename, and move SHA-256 stale-state metadata");
  console.log("  no filesystem mutation during proposal creation");
  console.log("  traversal and operation-shape rejection");
  console.log("  active-Run proposal boundary");
}

main()
  .catch((error) => {
    console.error(
      `AI edit proposal smoke test: FAIL\n${error instanceof Error ? error.stack : String(error)}`,
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

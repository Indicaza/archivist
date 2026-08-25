#!/usr/bin/env node

import { execFileSync } from "node:child_process";
import { createHash } from "node:crypto";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";

const temporaryRoot = fs.mkdtempSync(
  path.join(os.tmpdir(), "archivist-ai-edit-transactions-"),
);
const libraryPath = path.join(temporaryRoot, "Transaction Library");
const databasePath = path.join(temporaryRoot, "archivist.db");
const originalWorkingDirectory = process.cwd();

fs.mkdirSync(path.join(libraryPath, "Lore"), {
  recursive: true,
});
fs.mkdirSync(path.join(libraryPath, "Archive"), {
  recursive: true,
});

const patchBefore = "# Patch\n\nBefore.\n";
const patchAfter = "# Patch\n\nAfter approval.\n";
const renameContent = "# Rename\n\nPreserve me.\n";
const moveContent = "# Move\n\nPreserve me too.\n";
const staleBefore = "# Stale\n\nOriginal.\n";
const staleProposed = "# Stale\n\nProposed.\n";
const staleExternal = "# Stale\n\nChanged externally.\n";
const undoStaleExternal = "# Patch\n\nUser changed this after approval.\n";

fs.writeFileSync(
  path.join(libraryPath, "Lore", "Patch.md"),
  patchBefore,
  "utf8",
);
fs.writeFileSync(
  path.join(libraryPath, "Lore", "Rename.md"),
  renameContent,
  "utf8",
);
fs.writeFileSync(
  path.join(libraryPath, "Lore", "Move.md"),
  moveContent,
  "utf8",
);
fs.writeFileSync(
  path.join(libraryPath, "Lore", "Stale.md"),
  staleBefore,
  "utf8",
);

execFileSync("git", ["init", "--quiet"], {
  cwd: libraryPath,
  stdio: "ignore",
});
execFileSync("git", ["config", "user.email", "archivist@test.local"], {
  cwd: libraryPath,
  stdio: "ignore",
});
execFileSync("git", ["config", "user.name", "Archivist Test"], {
  cwd: libraryPath,
  stdio: "ignore",
});
execFileSync("git", ["add", "."], {
  cwd: libraryPath,
  stdio: "ignore",
});
execFileSync("git", ["commit", "--quiet", "-m", "Baseline"], {
  cwd: libraryPath,
  stdio: "ignore",
});

process.env.ARCHIVIST_DB_PATH = databasePath;
process.env.OPENAI_API_KEY = "sk-archivist-ai-edit-transaction-smoke-test";
process.env.NODE_ENV = "test";
process.chdir(temporaryRoot);

let server;
let closeDatabase;

function assert(condition, message) {
  if (!condition) {
    throw new Error(message);
  }
}

function sha256(content) {
  return createHash("sha256").update(content, "utf8").digest("hex");
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

async function expectFailure(status, messagePattern, action) {
  try {
    await action();
  } catch (error) {
    assert(
      error?.status === status,
      `Expected HTTP ${status}, received ${error?.status ?? "unknown"}.`,
    );
    assert(
      messagePattern.test(String(error?.message || error)),
      `Unexpected failure: ${error?.message || error}`,
    );
    return error;
  }

  throw new Error(
    `Expected HTTP ${status} failure matching ${messagePattern}.`,
  );
}

async function main() {
  const [
    { app },
    databaseModule,
    chatCompletion,
    runModel,
    editProposalModel,
    editProposalService,
    libraryFileModel,
    libraryTextIndexModel,
  ] = await Promise.all([
    import("../backend/dist/app.js"),
    import("../backend/dist/database/database.js"),
    import("../backend/dist/api/chats/services/ChatCompletionService.js"),
    import("../backend/dist/api/cognition/runs/models/AIRun.js"),
    import("../backend/dist/api/cognition/edits/models/AIEditProposal.js"),
    import("../backend/dist/api/cognition/edits/services/AIEditProposalService.js"),
    import("../backend/dist/api/libraries/models/LibraryFile.js"),
    import("../backend/dist/api/libraries/models/LibraryTextIndex.js"),
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
        name: "Transaction Library",
      }),
    })
  ).library;

  const initialScan = await requestJson(
    baseUrl,
    `/libraries/${library.id}/scan`,
    {
      method: "POST",
      body: JSON.stringify({}),
    },
  );
  const initialCatalog = await requestJson(
    baseUrl,
    `/libraries/${library.id}/files`,
  );
  const initialFilesByPath = new Map(
    initialCatalog.files.map((file) => [file.relativePath, file]),
  );
  const initialPatchFile = initialFilesByPath.get("Lore/Patch.md");
  const initialRenameFile = initialFilesByPath.get("Lore/Rename.md");
  const initialMoveFile = initialFilesByPath.get("Lore/Move.md");

  assert(
    initialScan.scan?.status === "complete"
      && initialScan.index?.failedFileCount === 0
      && initialScan.gitStatus?.repository === true
      && initialScan.gitStatus?.dirty === false
      && initialPatchFile
      && initialRenameFile
      && initialMoveFile,
    "Transaction fixtures must begin with a clean catalog, text index, and Git baseline.",
  );

  async function createProposal(summary, operations) {
    const chat = (
      await requestJson(baseUrl, "/chats", {
        method: "POST",
        body: JSON.stringify({
          libraryId: library.id,
          title: summary,
        }),
      })
    ).chat;
    const session = chatCompletion.beginChatTurn(
      chat.id,
      summary,
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
    const proposal = editProposalService.proposeAIEdit({
      runId: run.id,
      summary,
      operations,
    });

    runModel.completeAIRun(
      run.id,
      "Proposal ready for human review.",
      null,
    );

    return {
      chat,
      run: runModel.requireAIRun(run.id),
      proposal,
    };
  }

  const whole = await createProposal(
    "Approve the complete primitive transaction.",
    [
      {
        type: "create_file",
        destinationPath: "Lore/Created.md",
        afterContent: "# Created\n\nApproved content.\n",
      },
      {
        type: "patch_file",
        sourcePath: "Lore/Patch.md",
        expectedHash: sha256(patchBefore),
        beforeContent: patchBefore,
        afterContent: patchAfter,
      },
      {
        type: "create_directory",
        destinationPath: "Lore/NewDirectory",
      },
      {
        type: "rename_file",
        sourcePath: "Lore/Rename.md",
        destinationPath: "Lore/Renamed.md",
        expectedHash: sha256(renameContent),
      },
      {
        type: "move_file",
        sourcePath: "Lore/Move.md",
        destinationPath: "Archive/Move.md",
        expectedHash: sha256(moveContent),
      },
    ],
  );

  const review = await requestJson(
    baseUrl,
    `/cognition/edits/proposals/${whole.proposal.id}`,
  );

  assert(
    review.proposal.id === whole.proposal.id
      && review.proposal.status === "proposed"
      && review.proposal.operations.length === 5
      && review.proposal.operations.every(
        (operation) => operation.afterHash === null,
      ),
    "Proposal review must expose the durable pre-execution transaction.",
  );

  const wholeApproval = await requestJson(
    baseUrl,
    `/cognition/edits/proposals/${whole.proposal.id}/approve`,
    {
      method: "POST",
      body: JSON.stringify({}),
    },
  );
  const wholeApproved = wholeApproval.proposal;
  const wholeSynchronization = wholeApproval.synchronization;

  assert(
    wholeApproved.status === "completed"
      && wholeApproved.reviewedAt
      && wholeApproved.completedAt
      && wholeApproved.operations.every(
        (operation) => operation.status === "completed",
      ),
    "Whole-proposal approval must complete every selected operation.",
  );
  assert(
    fs.readFileSync(
      path.join(libraryPath, "Lore", "Created.md"),
      "utf8",
    ) === "# Created\n\nApproved content.\n"
      && fs.readFileSync(
        path.join(libraryPath, "Lore", "Patch.md"),
        "utf8",
      ) === patchAfter
      && fs.statSync(
        path.join(libraryPath, "Lore", "NewDirectory"),
      ).isDirectory()
      && !fs.existsSync(path.join(libraryPath, "Lore", "Rename.md"))
      && fs.readFileSync(
        path.join(libraryPath, "Lore", "Renamed.md"),
        "utf8",
      ) === renameContent
      && !fs.existsSync(path.join(libraryPath, "Lore", "Move.md"))
      && fs.readFileSync(
        path.join(libraryPath, "Archive", "Move.md"),
        "utf8",
      ) === moveContent,
    "Whole-proposal approval must execute every primitive exactly once.",
  );

  const completedByType = Object.fromEntries(
    wholeApproved.operations.map((operation) => [operation.type, operation]),
  );

  assert(
    completedByType.create_file.afterHash
      === sha256("# Created\n\nApproved content.\n")
      && completedByType.patch_file.afterHash === sha256(patchAfter)
      && completedByType.rename_file.afterHash === sha256(renameContent)
      && completedByType.move_file.afterHash === sha256(moveContent)
      && completedByType.create_directory.afterHash === null,
    "Completed file operations must persist authoritative final SHA-256 hashes.",
  );

  assert(
    wholeSynchronization?.status === "synchronized"
      && wholeSynchronization.catalog?.scanStatus === "complete"
      && wholeSynchronization.index?.failedFileCount === 0
      && wholeSynchronization.directories?.affectedDirectories?.includes(
        "Lore/NewDirectory",
      )
      && wholeSynchronization.gitStatus?.repository === true
      && wholeSynchronization.gitStatus?.dirty === true
      && wholeSynchronization.affectedPaths.includes("Lore/Created.md")
      && wholeSynchronization.affectedPaths.includes("Lore/Patch.md")
      && wholeSynchronization.affectedPaths.includes("Lore/Rename.md")
      && wholeSynchronization.affectedPaths.includes("Lore/Renamed.md")
      && wholeSynchronization.affectedPaths.includes("Lore/Move.md")
      && wholeSynchronization.affectedPaths.includes("Archive/Move.md")
      && wholeSynchronization.relocations.length === 2,
    "Successful execution must synchronously refresh catalog, index, directories, and Git status.",
  );

  const synchronizedCatalog = await requestJson(
    baseUrl,
    `/libraries/${library.id}/files`,
  );
  const synchronizedFilesByPath = new Map(
    synchronizedCatalog.files.map((file) => [file.relativePath, file]),
  );
  const synchronizedPatchFile = synchronizedFilesByPath.get("Lore/Patch.md");
  const synchronizedCreatedFile = synchronizedFilesByPath.get("Lore/Created.md");
  const synchronizedRenamedFile = synchronizedFilesByPath.get("Lore/Renamed.md");
  const synchronizedMovedFile = synchronizedFilesByPath.get("Archive/Move.md");

  assert(
    synchronizedPatchFile?.id === initialPatchFile.id
      && synchronizedCreatedFile?.status === "available"
      && synchronizedRenamedFile?.id === initialRenameFile.id
      && synchronizedMovedFile?.id === initialMoveFile.id
      && !synchronizedFilesByPath.has("Lore/Rename.md")
      && !synchronizedFilesByPath.has("Lore/Move.md")
      && synchronizedCatalog.directories.includes("Lore/NewDirectory"),
    "Post-transaction catalog refresh must preserve file identity across patch, rename, and move while exposing new entries.",
  );

  assert(
    libraryTextIndexModel.getLibraryTextDocument(
      synchronizedPatchFile.id,
    )?.contentHash === sha256(patchAfter)
      && libraryTextIndexModel.getLibraryTextDocument(
        synchronizedCreatedFile.id,
      )?.status === "indexed"
      && libraryTextIndexModel.getLibraryTextDocument(
        initialRenameFile.id,
      )?.status === "indexed"
      && libraryTextIndexModel.getLibraryTextDocument(
        initialMoveFile.id,
      )?.status === "indexed",
    "Post-transaction indexing must refresh patched content and keep relocated document identity live.",
  );

  const synchronizedGitPaths = new Set(
    wholeSynchronization.gitStatus.entries.map((entry) => entry.path),
  );

  assert(
    synchronizedGitPaths.has("Lore/Patch.md")
      && synchronizedGitPaths.has("Lore/Created.md")
      && synchronizedGitPaths.has("Lore/Renamed.md")
      && synchronizedGitPaths.has("Archive/Move.md"),
    "Post-transaction Git refresh must expose the approved filesystem changes.",
  );

  const wholeEvents = runModel.listAIRunEvents(whole.run.id);
  const wholeReviewEvents = wholeEvents
    .filter((event) => event.eventType.startsWith("edit."))
    .map((event) => event.eventType);

  assert(
    wholeReviewEvents.join(",")
      === "edit.approved,edit.verified,edit.completed,edit.synchronized",
    "Approved proposals must append durable approval, verification, completion, and synchronization trace events after the originating Run.",
  );

  const wholeUndo = await requestJson(
    baseUrl,
    `/cognition/edits/proposals/${whole.proposal.id}/undo`,
    {
      method: "POST",
      body: JSON.stringify({}),
    },
  );
  const wholeUndone = wholeUndo.proposal;
  const wholeUndoSynchronization = wholeUndo.synchronization;

  assert(
    wholeUndone.status === "undone"
      && wholeUndone.operations.every(
        (operation) => operation.status === "undone",
      )
      && wholeUndone.operations.every(
        (operation, index) =>
          operation.afterHash === wholeApproved.operations[index]?.afterHash,
      ),
    "Undo must preserve the completed audit record while marking executed operations undone.",
  );
  assert(
    !fs.existsSync(path.join(libraryPath, "Lore", "Created.md"))
      && fs.readFileSync(
        path.join(libraryPath, "Lore", "Patch.md"),
        "utf8",
      ) === patchBefore
      && !fs.existsSync(
        path.join(libraryPath, "Lore", "NewDirectory"),
      )
      && fs.readFileSync(
        path.join(libraryPath, "Lore", "Rename.md"),
        "utf8",
      ) === renameContent
      && !fs.existsSync(path.join(libraryPath, "Lore", "Renamed.md"))
      && fs.readFileSync(
        path.join(libraryPath, "Lore", "Move.md"),
        "utf8",
      ) === moveContent
      && !fs.existsSync(path.join(libraryPath, "Archive", "Move.md")),
    "Whole-transaction undo must reverse every completed primitive in reverse order.",
  );
  assert(
    wholeUndoSynchronization?.status === "synchronized"
      && wholeUndoSynchronization.catalog?.scanStatus === "complete"
      && wholeUndoSynchronization.index?.failedFileCount === 0
      && wholeUndoSynchronization.gitStatus?.repository === true
      && wholeUndoSynchronization.gitStatus?.dirty === false
      && wholeUndoSynchronization.relocations.length === 2,
    "Undo must reuse post-transaction synchronization and restore the clean Git baseline.",
  );

  const undoneCatalog = await requestJson(
    baseUrl,
    `/libraries/${library.id}/files`,
  );
  const undoneFilesByPath = new Map(
    undoneCatalog.files.map((file) => [file.relativePath, file]),
  );
  const undonePatchFile = undoneFilesByPath.get("Lore/Patch.md");
  const undoneRenameFile = undoneFilesByPath.get("Lore/Rename.md");
  const undoneMoveFile = undoneFilesByPath.get("Lore/Move.md");
  const undoneCreatedFile = undoneFilesByPath.get("Lore/Created.md");

  assert(
    undonePatchFile?.id === initialPatchFile.id
      && undoneRenameFile?.id === initialRenameFile.id
      && undoneMoveFile?.id === initialMoveFile.id
      && undoneCreatedFile?.status === "missing"
      && !undoneFilesByPath.has("Lore/Renamed.md")
      && !undoneFilesByPath.has("Archive/Move.md")
      && !undoneCatalog.directories.includes("Lore/NewDirectory"),
    "Undo synchronization must restore relocated file identities and mark removed created files missing.",
  );
  assert(
    libraryTextIndexModel.getLibraryTextDocument(
      initialPatchFile.id,
    )?.contentHash === sha256(patchBefore)
      && libraryTextIndexModel.getLibraryTextDocument(
        initialRenameFile.id,
      )?.status === "indexed"
      && libraryTextIndexModel.getLibraryTextDocument(
        initialMoveFile.id,
      )?.status === "indexed",
    "Undo synchronization must restore patched index content and relocated document identity.",
  );

  const wholeUndoEvents = runModel.listAIRunEvents(whole.run.id)
    .filter((event) => event.eventType.startsWith("edit."))
    .map((event) => event.eventType);

  assert(
    wholeUndoEvents.join(",")
      === "edit.approved,edit.verified,edit.completed,edit.synchronized,edit.undo_verified,edit.undone,edit.synchronized",
    "Undo must append durable verification, reversal, and synchronization trace events.",
  );

  const rejected = await createProposal(
    "Reject one proposed file.",
    [
      {
        type: "create_file",
        destinationPath: "Lore/Rejected.md",
        afterContent: "# Rejected\n",
      },
    ],
  );
  const rejectedResult = (
    await requestJson(
      baseUrl,
      `/cognition/edits/proposals/${rejected.proposal.id}/reject`,
      {
        method: "POST",
        body: JSON.stringify({}),
      },
    )
  ).proposal;

  assert(
    rejectedResult.status === "rejected"
      && rejectedResult.operations[0]?.status === "rejected"
      && rejectedResult.reviewedAt
      && !fs.existsSync(path.join(libraryPath, "Lore", "Rejected.md")),
    "Rejecting a proposal must persist the decision without mutating the Library.",
  );
  assert(
    runModel.listAIRunEvents(rejected.run.id).some(
      (event) => event.eventType === "edit.rejected",
    ),
    "Rejected proposals must append a durable edit.rejected event.",
  );

  const partial = await createProposal(
    "Approve only one proposed file.",
    [
      {
        type: "create_file",
        destinationPath: "Lore/PartialA.md",
        afterContent: "# Partial A\n",
      },
      {
        type: "create_file",
        destinationPath: "Lore/PartialB.md",
        afterContent: "# Partial B\n",
      },
    ],
  );
  const partialSelectedId = partial.proposal.operations[0].id;
  const partialApproved = (
    await requestJson(
      baseUrl,
      `/cognition/edits/proposals/${partial.proposal.id}/approve`,
      {
        method: "POST",
        body: JSON.stringify({
          operationIds: [partialSelectedId],
        }),
      },
    )
  ).proposal;

  assert(
    partialApproved.status === "completed"
      && partialApproved.operations[0]?.status === "completed"
      && partialApproved.operations[1]?.status === "rejected"
      && partialApproved.operations[0]?.afterHash === sha256("# Partial A\n")
      && partialApproved.operations[1]?.afterHash === null
      && fs.readFileSync(
        path.join(libraryPath, "Lore", "PartialA.md"),
        "utf8",
      ) === "# Partial A\n"
      && !fs.existsSync(path.join(libraryPath, "Lore", "PartialB.md")),
    "Selected approval must execute only selected operations and permanently reject the rest.",
  );

  const partialApprovedEvent = runModel
    .listAIRunEvents(partial.run.id)
    .find((event) => event.eventType === "edit.approved");

  assert(
    partialApprovedEvent?.payload?.partial === true
      && partialApprovedEvent?.payload?.approvedOperationIds?.length === 1
      && partialApprovedEvent?.payload?.rejectedOperationIds?.length === 1,
    "Partial approval trace metadata must identify approved and rejected operations.",
  );

  const stale = await createProposal(
    "Prove stale multi-operation approval fails closed.",
    [
      {
        type: "create_file",
        destinationPath: "Lore/ShouldNotExist.md",
        afterContent: "# Never written\n",
      },
      {
        type: "patch_file",
        sourcePath: "Lore/Stale.md",
        expectedHash: sha256(staleBefore),
        beforeContent: staleBefore,
        afterContent: staleProposed,
      },
    ],
  );

  fs.writeFileSync(
    path.join(libraryPath, "Lore", "Stale.md"),
    staleExternal,
    "utf8",
  );

  await expectFailure(
    409,
    /stale and was not executed/i,
    async () => {
      await requestJson(
        baseUrl,
        `/cognition/edits/proposals/${stale.proposal.id}/approve`,
        {
          method: "POST",
          body: JSON.stringify({}),
        },
      );
    },
  );

  const staleReloaded = editProposalModel.requireAIEditProposal(
    stale.proposal.id,
  );

  assert(
    staleReloaded.status === "stale"
      && staleReloaded.errorCode === "proposal_stale"
      && staleReloaded.operations.every(
        (operation) => operation.status === "failed",
      )
      && !fs.existsSync(
        path.join(libraryPath, "Lore", "ShouldNotExist.md"),
      )
      && fs.readFileSync(
        path.join(libraryPath, "Lore", "Stale.md"),
        "utf8",
      ) === staleExternal,
    "A stale approved batch must fail closed before the first filesystem mutation.",
  );

  const staleEvents = runModel.listAIRunEvents(stale.run.id);
  assert(
    staleEvents.some((event) => event.eventType === "edit.approved")
      && staleEvents.some((event) => event.eventType === "edit.stale")
      && !staleEvents.some((event) => event.eventType === "edit.verified")
      && !staleEvents.some((event) => event.eventType === "edit.completed"),
    "Stale proposal traces must stop before verification completion or writes.",
  );

  const invalidSelection = await createProposal(
    "Reject invalid selected operation IDs.",
    [
      {
        type: "create_file",
        destinationPath: "Lore/Selection.md",
        afterContent: "# Selection\n",
      },
    ],
  );

  await expectFailure(
    400,
    /belong to this proposal/i,
    async () => {
      await requestJson(
        baseUrl,
        `/cognition/edits/proposals/${invalidSelection.proposal.id}/approve`,
        {
          method: "POST",
          body: JSON.stringify({
            operationIds: ["00000000-0000-4000-8000-000000000099"],
          }),
        },
      );
    },
  );

  assert(
    editProposalModel.requireAIEditProposal(
      invalidSelection.proposal.id,
    ).status === "proposed"
      && !fs.existsSync(path.join(libraryPath, "Lore", "Selection.md")),
    "Invalid selected approval must roll back its database review transaction and leave the proposal open.",
  );

  await requestJson(
    baseUrl,
    `/cognition/edits/proposals/${invalidSelection.proposal.id}/reject`,
    {
      method: "POST",
      body: JSON.stringify({}),
    },
  );

  const syncWarningProposal = await createProposal(
    "Filesystem completion survives a derived-state refresh warning.",
    [
      {
        type: "create_file",
        destinationPath: "Lore/SyncWarning.md",
        afterContent: "# Sync warning\n",
      },
    ],
  );
  const heldScan = libraryFileModel.createLibraryScan(library.id);
  const syncWarningApproval = await requestJson(
    baseUrl,
    `/cognition/edits/proposals/${syncWarningProposal.proposal.id}/approve`,
    {
      method: "POST",
      body: JSON.stringify({}),
    },
  );

  assert(
    syncWarningApproval.proposal.status === "completed"
      && syncWarningApproval.synchronization?.status === "warning"
      && syncWarningApproval.synchronization?.warnings?.some(
        (warning) => warning.stage === "catalog-index",
      )
      && fs.readFileSync(
        path.join(libraryPath, "Lore", "SyncWarning.md"),
        "utf8",
      ) === "# Sync warning\n"
      && runModel.listAIRunEvents(syncWarningProposal.run.id).some(
        (event) => event.eventType === "edit.sync_warning",
      ),
    "Derived-state refresh failure must report a warning without rolling back a completed filesystem transaction.",
  );

  libraryFileModel.failLibraryScan(
    heldScan.id,
    library.id,
    "Released synchronization-warning test scan.",
  );
  await requestJson(
    baseUrl,
    `/libraries/${library.id}/scan`,
    {
      method: "POST",
      body: JSON.stringify({}),
    },
  );

  await expectFailure(
    409,
    /latest completed AI edit transaction/i,
    async () => {
      await requestJson(
        baseUrl,
        `/cognition/edits/proposals/${partial.proposal.id}/undo`,
        {
          method: "POST",
          body: JSON.stringify({}),
        },
      );
    },
  );

  assert(
    editProposalModel.requireAIEditProposal(
      partial.proposal.id,
    ).status === "completed"
      && fs.existsSync(path.join(libraryPath, "Lore", "PartialA.md")),
    "Undo must enforce latest-completed transaction ordering without changing the older transaction.",
  );

  const syncWarningUndo = await requestJson(
    baseUrl,
    `/cognition/edits/proposals/${syncWarningProposal.proposal.id}/undo`,
    {
      method: "POST",
      body: JSON.stringify({}),
    },
  );

  assert(
    syncWarningUndo.proposal.status === "undone"
      && syncWarningUndo.proposal.operations[0]?.status === "undone"
      && !fs.existsSync(path.join(libraryPath, "Lore", "SyncWarning.md"))
      && syncWarningUndo.synchronization?.gitStatus?.repository === true,
    "Undo must remove the latest created file and synchronize the Library afterward.",
  );

  const partialUndo = await requestJson(
    baseUrl,
    `/cognition/edits/proposals/${partial.proposal.id}/undo`,
    {
      method: "POST",
      body: JSON.stringify({}),
    },
  );

  assert(
    partialUndo.proposal.status === "undone"
      && partialUndo.proposal.operations[0]?.status === "undone"
      && partialUndo.proposal.operations[1]?.status === "rejected"
      && !fs.existsSync(path.join(libraryPath, "Lore", "PartialA.md"))
      && !fs.existsSync(path.join(libraryPath, "Lore", "PartialB.md")),
    "Undo of a partially approved transaction must reverse only the completed operation and preserve rejected history.",
  );

  const undoDirectory = await createProposal(
    "Only remove an Archivist-created directory when it is still empty.",
    [
      {
        type: "create_directory",
        destinationPath: "Lore/UndoDirectory",
      },
    ],
  );

  await requestJson(
    baseUrl,
    `/cognition/edits/proposals/${undoDirectory.proposal.id}/approve`,
    {
      method: "POST",
      body: JSON.stringify({}),
    },
  );

  fs.writeFileSync(
    path.join(libraryPath, "Lore", "UndoDirectory", "User.md"),
    "# User content\n",
    "utf8",
  );

  await expectFailure(
    409,
    /stale and was not undone/i,
    async () => {
      await requestJson(
        baseUrl,
        `/cognition/edits/proposals/${undoDirectory.proposal.id}/undo`,
        {
          method: "POST",
          body: JSON.stringify({}),
        },
      );
    },
  );

  assert(
    editProposalModel.requireAIEditProposal(
      undoDirectory.proposal.id,
    ).status === "completed"
      && fs.readFileSync(
        path.join(
          libraryPath,
          "Lore",
          "UndoDirectory",
          "User.md",
        ),
        "utf8",
      ) === "# User content\n",
    "Undo must refuse to remove a created directory after user content appears inside it.",
  );

  fs.unlinkSync(
    path.join(libraryPath, "Lore", "UndoDirectory", "User.md"),
  );

  const retriedDirectoryUndo = await requestJson(
    baseUrl,
    `/cognition/edits/proposals/${undoDirectory.proposal.id}/undo`,
    {
      method: "POST",
      body: JSON.stringify({}),
    },
  );

  assert(
    retriedDirectoryUndo.proposal.status === "undone"
      && !fs.existsSync(
        path.join(libraryPath, "Lore", "UndoDirectory"),
      ),
    "A stale undo must remain retryable after the user restores the recorded post-transaction state.",
  );

  const undoStale = await createProposal(
    "Refuse to undo over later user changes.",
    [
      {
        type: "patch_file",
        sourcePath: "Lore/Patch.md",
        expectedHash: sha256(patchBefore),
        beforeContent: patchBefore,
        afterContent: patchAfter,
      },
    ],
  );

  await requestJson(
    baseUrl,
    `/cognition/edits/proposals/${undoStale.proposal.id}/approve`,
    {
      method: "POST",
      body: JSON.stringify({}),
    },
  );

  fs.writeFileSync(
    path.join(libraryPath, "Lore", "Patch.md"),
    undoStaleExternal,
    "utf8",
  );

  await expectFailure(
    409,
    /stale and was not undone/i,
    async () => {
      await requestJson(
        baseUrl,
        `/cognition/edits/proposals/${undoStale.proposal.id}/undo`,
        {
          method: "POST",
          body: JSON.stringify({}),
        },
      );
    },
  );

  const undoStaleReloaded = editProposalModel.requireAIEditProposal(
    undoStale.proposal.id,
  );
  const undoStaleEvents = runModel.listAIRunEvents(undoStale.run.id);

  assert(
    undoStaleReloaded.status === "completed"
      && undoStaleReloaded.operations[0]?.status === "completed"
      && fs.readFileSync(
        path.join(libraryPath, "Lore", "Patch.md"),
        "utf8",
      ) === undoStaleExternal
      && undoStaleEvents.some(
        (event) => event.eventType === "edit.undo_stale",
      )
      && !undoStaleEvents.some(
        (event) => event.eventType === "edit.undone",
      ),
    "Undo must fail closed over later user edits while preserving the completed transaction record and user content.",
  );

  const schemaVersion = databaseModule.database.pragma("user_version", {
    simple: true,
  });

  assert(
    schemaVersion === 20,
    `Expected schema version 20, found ${schemaVersion}.`,
  );

  console.log("AI edit transaction smoke test: PASS");
  console.log("  proposal review GET endpoint");
  console.log("  whole-proposal approval across all five mutation primitives");
  console.log("  selected-operation approval with unselected rejection");
  console.log("  explicit proposal rejection with zero writes");
  console.log("  authoritative final SHA-256 persistence");
  console.log("  durable post-Run edit approval/verification/completion trace");
  console.log("  full-batch stale preflight before the first filesystem mutation");
  console.log("  invalid approval selection leaves the proposal reviewable");
  console.log("  rollback-backed execution boundary");
  console.log("  post-transaction catalog and text-index refresh");
  console.log("  rename/move catalog identity preservation");
  console.log("  post-transaction Git and directory refresh");
  console.log("  synchronization warnings never roll back completed writes");
  console.log("  latest-completed transaction undo stack semantics");
  console.log("  reverse-order undo across all five mutation primitives");
  console.log("  undo catalog/index/Git synchronization and identity restoration");
  console.log("  partial transaction undo preserves rejected operations");
  console.log("  created-directory undo requires the directory to remain empty");
  console.log("  stale undo fails closed over later user edits");
}

main()
  .catch((error) => {
    console.error(
      `AI edit transaction smoke test: FAIL\n${error instanceof Error ? error.stack : String(error)}`,
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

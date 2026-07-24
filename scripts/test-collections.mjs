#!/usr/bin/env node

import fs from "node:fs";
import os from "node:os";
import path from "node:path";

const temporaryRoot = fs.mkdtempSync(
  path.join(os.tmpdir(), "archivist-collections-"),
);
const firstLibraryPath = path.join(temporaryRoot, "First Library");
const secondLibraryPath = path.join(temporaryRoot, "Second Library");
const databasePath = path.join(temporaryRoot, "archivist.db");

fs.mkdirSync(firstLibraryPath);
fs.mkdirSync(secondLibraryPath);
process.env.ARCHIVIST_DB_PATH = databasePath;
process.env.OPENAI_API_KEY ??= "sk-archivist-collections-smoke-test";

let server;
let closeDatabase;

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

function assert(condition, message) {
  if (!condition) {
    throw new Error(message);
  }
}

async function main() {
  const [{ app }, databaseModule, defaultsModule] = await Promise.all([
    import("../backend/dist/app.js"),
    import("../backend/dist/database/database.js"),
    import("../backend/dist/api/agents/models/AgentDefaults.js"),
  ]);
  closeDatabase = databaseModule.closeDatabase;

  await new Promise((resolve) => {
    server = app.listen(0, "127.0.0.1", resolve);
  });

  const address = server.address();
  const baseUrl = `http://127.0.0.1:${address.port}/api`;
  const defaultAgentId = defaultsModule.ARCHIVIST_DEFAULT_AGENT_ID;
  const firstLibrary = (
    await requestJson(baseUrl, "/libraries", {
      method: "POST",
      body: JSON.stringify({
        rootPath: firstLibraryPath,
        name: "First Library",
      }),
    })
  ).library;
  const secondLibrary = (
    await requestJson(baseUrl, "/libraries", {
      method: "POST",
      body: JSON.stringify({
        rootPath: secondLibraryPath,
        name: "Second Library",
      }),
    })
  ).library;
  const firstChat = (
    await requestJson(baseUrl, "/chats", {
      method: "POST",
      body: JSON.stringify({
        libraryId: firstLibrary.id,
        title: "First Chat",
      }),
    })
  ).chat;
  const secondChat = (
    await requestJson(baseUrl, "/chats", {
      method: "POST",
      body: JSON.stringify({
        libraryId: secondLibrary.id,
        title: "Second Chat",
      }),
    })
  ).chat;
  const parent = (
    await requestJson(baseUrl, "/collections", {
      method: "POST",
      body: JSON.stringify({
        name: "Shard",
        libraryIds: [firstLibrary.id],
        chatIds: [secondChat.id],
        agentIds: [defaultAgentId],
        defaultAgentId,
      }),
    })
  ).collection;
  const child = (
    await requestJson(baseUrl, "/collections", {
      method: "POST",
      body: JSON.stringify({
        name: "Lore",
        parentCollectionId: parent.id,
        libraryIds: [secondLibrary.id],
      }),
    })
  ).collection;
  const scope = (
    await requestJson(baseUrl, `/collections/${parent.id}/scope`)
  ).scope;

  assert(
    scope.collectionIds.includes(parent.id) &&
      scope.collectionIds.includes(child.id),
    "A parent Collection must include its active descendants.",
  );
  assert(
    scope.libraryIds.includes(firstLibrary.id) &&
      scope.libraryIds.includes(secondLibrary.id),
    "A Collection scope must include descendant Library references.",
  );
  assert(
    scope.chatIds.includes(firstChat.id) &&
      scope.chatIds.includes(secondChat.id),
    "A Collection scope must include direct and Library-owned Chats.",
  );
  assert(
    scope.directChatIds.length === 1 &&
      scope.directChatIds[0] === secondChat.id,
    "Direct Chat references must remain distinct from discovered Library Chats.",
  );
  assert(
    scope.defaultAgentId === defaultAgentId,
    "The Collection default Agent must survive scope compilation.",
  );

  const chatAfterCollection = (
    await requestJson(baseUrl, `/chats/${secondChat.id}`)
  ).chat;
  assert(
    chatAfterCollection.libraryId === secondLibrary.id,
    "Adding a Chat to a Collection must not change Chat ownership.",
  );

  const selectedState = (
    await requestJson(baseUrl, "/app-state/selected-collection", {
      method: "PATCH",
      body: JSON.stringify({
        selectedCollectionId: parent.id,
      }),
    })
  ).appState;
  assert(
    selectedState.selectedCollectionId === parent.id,
    "Collection selection must persist in app state.",
  );
  assert(
    scope.libraryIds.includes(selectedState.selectedLibraryId),
    "Collection selection must reconcile the selected Library.",
  );
  assert(
    scope.chatIds.includes(selectedState.selectedChatId),
    "Collection selection must reconcile the selected Chat.",
  );

  await expectStatus(409, () =>
    requestJson(baseUrl, `/collections/${parent.id}`, {
      method: "PATCH",
      body: JSON.stringify({
        parentCollectionId: child.id,
      }),
    }),
  );

  const updatedParent = (
    await requestJson(baseUrl, `/collections/${parent.id}`, {
      method: "PATCH",
      body: JSON.stringify({
        name: "Shard World",
        libraryIds: [],
        chatIds: [firstChat.id],
        agentIds: [defaultAgentId],
        defaultAgentId,
      }),
    })
  ).collection;
  assert(
    updatedParent.name === "Shard World" &&
      updatedParent.libraryIds.length === 0 &&
      updatedParent.chatIds[0] === firstChat.id,
    "Collection metadata and references must update atomically.",
  );

  const archiveResult = await requestJson(
    baseUrl,
    `/collections/${parent.id}/archive`,
    {
      method: "POST",
    },
  );
  assert(
    archiveResult.selectedCollectionId === null,
    "Archiving the selected Collection must return to All Work.",
  );

  const archivedCollections = (
    await requestJson(baseUrl, "/collections/archived")
  ).collections;
  assert(
    archivedCollections.some((collection) => collection.id === parent.id) &&
      archivedCollections.some((collection) => collection.id === child.id),
    "Archiving a Collection must preserve and archive its subtree.",
  );

  await requestJson(baseUrl, `/collections/${parent.id}/restore`, {
    method: "POST",
  });
  await requestJson(baseUrl, `/collections/${child.id}/restore`, {
    method: "POST",
  });

  console.log("Collections smoke test: PASS");
  console.log("  nesting and descendant scope");
  console.log("  reference-only ownership");
  console.log("  app-state selection reconciliation");
  console.log("  cycle prevention");
  console.log("  atomic membership updates");
  console.log("  subtree archive and restore");
}

main()
  .catch((error) => {
    console.error(
      `Collections smoke test: FAIL\n${error instanceof Error ? error.stack : String(error)}`,
    );
    process.exitCode = 1;
  })
  .finally(async () => {
    if (server) {
      await new Promise((resolve) => server.close(resolve));
    }

    closeDatabase?.();

    if (temporaryRoot.startsWith(os.tmpdir())) {
      fs.rmSync(temporaryRoot, {
        recursive: true,
        force: true,
      });
    }
  });

#!/usr/bin/env node

import fs from "node:fs";
import os from "node:os";
import path from "node:path";

const temporaryRoot = fs.mkdtempSync(
  path.join(os.tmpdir(), "archivist-chat-agents-"),
);
const libraryPath = path.join(temporaryRoot, "Library");
const databasePath = path.join(temporaryRoot, "archivist.db");

fs.mkdirSync(libraryPath);
process.env.ARCHIVIST_DB_PATH = databasePath;
process.env.OPENAI_API_KEY ??= "sk-archivist-chat-agents-smoke-test";

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
  const library = (
    await requestJson(baseUrl, "/libraries", {
      method: "POST",
      body: JSON.stringify({
        rootPath: libraryPath,
        name: "Roster Library",
      }),
    })
  ).library;
  const firstAgent = (
    await requestJson(baseUrl, "/agents", {
      method: "POST",
      body: JSON.stringify({
        name: "First Specialist",
      }),
    })
  ).agent;
  const secondAgent = (
    await requestJson(baseUrl, "/agents", {
      method: "POST",
      body: JSON.stringify({
        name: "Second Specialist",
      }),
    })
  ).agent;
  const createdChat = (
    await requestJson(baseUrl, "/chats", {
      method: "POST",
      body: JSON.stringify({
        libraryId: library.id,
        title: "Roster Chat",
      }),
    })
  ).chat;

  assert(
    createdChat.agentId === defaultAgentId &&
      createdChat.agentIds.length === 1 &&
      createdChat.agentIds[0] === defaultAgentId,
    "A new Chat must attach its initial active Agent.",
  );

  const attachedChat = (
    await requestJson(baseUrl, `/chats/${createdChat.id}/agents`, {
      method: "POST",
      body: JSON.stringify({
        agentId: firstAgent.id,
      }),
    })
  ).chat;

  assert(
    attachedChat.agentIds.includes(defaultAgentId) &&
      attachedChat.agentIds.includes(firstAgent.id),
    "Attaching an existing Agent must extend the Chat roster.",
  );

  const firstActiveChat = (
    await requestJson(baseUrl, `/chats/${createdChat.id}`, {
      method: "PATCH",
      body: JSON.stringify({
        agentId: firstAgent.id,
      }),
    })
  ).chat;

  assert(
    firstActiveChat.agentId === firstAgent.id,
    "An attached Agent must become the Chat's active Agent.",
  );

  const withoutDefault = (
    await requestJson(
      baseUrl,
      `/chats/${createdChat.id}/agents/${defaultAgentId}`,
      {
        method: "DELETE",
      },
    )
  ).chat;

  assert(
    withoutDefault.agentIds.length === 1 &&
      withoutDefault.agentIds[0] === firstAgent.id,
    "A non-active Agent must detach without changing the active Agent.",
  );

  await expectStatus(409, () =>
    requestJson(
      baseUrl,
      `/chats/${createdChat.id}/agents/${firstAgent.id}`,
      {
        method: "DELETE",
      },
    ),
  );

  await requestJson(baseUrl, `/chats/${createdChat.id}/agents`, {
    method: "POST",
    body: JSON.stringify({
      agentId: secondAgent.id,
    }),
  });
  await requestJson(baseUrl, `/chats/${createdChat.id}`, {
    method: "PATCH",
    body: JSON.stringify({
      agentId: secondAgent.id,
    }),
  });

  const deletion = await requestJson(baseUrl, `/agents/${secondAgent.id}`, {
    method: "DELETE",
  });
  const afterDeletion = (
    await requestJson(baseUrl, `/chats/${createdChat.id}`)
  ).chat;

  assert(
    deletion.reassignedChatCount === 1 &&
      afterDeletion.agentId === defaultAgentId &&
      afterDeletion.agentIds.includes(defaultAgentId) &&
      afterDeletion.agentIds.includes(firstAgent.id) &&
      !afterDeletion.agentIds.includes(secondAgent.id),
    "Deleting the active Agent must preserve the roster and reassign safely.",
  );

  await expectStatus(409, () =>
    requestJson(baseUrl, `/agents/${firstAgent.id}/archive`, {
      method: "POST",
    }),
  );

  await requestJson(
    baseUrl,
    `/chats/${createdChat.id}/agents/${firstAgent.id}`,
    {
      method: "DELETE",
    },
  );
  await requestJson(baseUrl, `/agents/${firstAgent.id}/archive`, {
    method: "POST",
  });

  console.log("Chat Agent roster smoke test: PASS");
  console.log("  initial Agent backfill contract");
  console.log("  attach and active selection");
  console.log("  active-Agent detach protection");
  console.log("  deletion fallback and roster preservation");
  console.log("  archive protection for attached Agents");
}

main()
  .catch((error) => {
    console.error(
      `Chat Agent roster smoke test: FAIL\n${error instanceof Error ? error.stack : String(error)}`,
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

// Reaching the running Archivist's live recorder from outside the app (the
// CLI, agents, the browser dashboard). Only possible while the person has
// turned on "Let agents record" in Observatory > Live: the app then writes
// <Observatory folder>/live/agent-access.json (readable only by this user)
// with its loopback address and a key, and deletes it when turned off or on
// quit. The key is only ever sent to 127.0.0.1.
import { readFile, stat } from "node:fs/promises";
import path from "node:path";
import { isAlive } from "./jobs.mjs";

export const ACCESS_SCHEMA = "archivist.observatory.live-access";
const ENDPOINT = /^http:\/\/127\.0\.0\.1:([0-9]{1,5})\/observatory\/api\/live\/$/;
const TOKEN = /^[A-Za-z0-9_-]{20,128}$/;
const LIVE_OPERATION = /^(status|start|stop|mark)$/;

export class LiveAccessError extends Error {
  constructor(code, message) {
    super(message);
    this.code = code;
  }
}

export function accessFile(root) {
  return path.join(root, "live", "agent-access.json");
}

const HOW_TO_ENABLE = "In Archivist, open the Observatory panel, go to Live, and turn on \"Let agents record\".";

// { endpoint, token, pid } or throws LiveAccessError.
export async function readAccess(root, { alive = isAlive } = {}) {
  const file = accessFile(root);
  let text;
  try {
    const info = await stat(file);
    if (info.size > 64 * 1024) throw new LiveAccessError("unreadable-record", `${file} is too large to be an access file.`);
    text = await readFile(file, "utf8");
  } catch (error) {
    if (error instanceof LiveAccessError) throw error;
    throw new LiveAccessError("agent-access-off", `Agent access to the live recorder is off, or Archivist isn't running. ${HOW_TO_ENABLE}`);
  }
  let access;
  try {
    access = JSON.parse(text);
  } catch {
    throw new LiveAccessError("unreadable-record", `${file} is not valid JSON.`);
  }
  const port = ENDPOINT.exec(access?.endpoint ?? "")?.[1];
  if (access?.schema !== ACCESS_SCHEMA || !port || Number(port) < 1 || Number(port) > 65535 || !TOKEN.test(access.token ?? "")) {
    throw new LiveAccessError("unreadable-record", `${file} is not an Archivist live access file.`);
  }
  if (!Number.isInteger(access.pid) || !alive(access.pid)) {
    throw new LiveAccessError("app-not-running", `The Archivist that turned on agent access (pid ${access.pid}) is no longer running. Open Archivist and turn it on again.`);
  }
  return { endpoint: access.endpoint, token: access.token, pid: access.pid };
}

// Sends one live operation to the app and returns its envelope's result, or
// throws LiveAccessError with the app's error code.
export async function callLive(root, operation, input = {}, { fetchImpl = fetch, timeoutMs = 8000, alive = isAlive } = {}) {
  if (!LIVE_OPERATION.test(operation)) throw new LiveAccessError("unknown-operation", `Unknown live operation "${operation}".`);
  const { endpoint, token } = await readAccess(root, { alive });
  let response;
  try {
    response = await fetchImpl(`${endpoint}${operation}`, {
      method: "POST",
      headers: { "Content-Type": "application/json", "X-Observatory-Agent-Token": token },
      body: JSON.stringify(input),
      redirect: "error",
      signal: AbortSignal.timeout(timeoutMs),
    });
  } catch (error) {
    throw new LiveAccessError("app-unreachable", `Archivist didn't answer at ${endpoint} (${error.name === "TimeoutError" ? "timed out" : error.message}).`);
  }
  let body;
  try {
    body = await response.json();
  } catch {
    throw new LiveAccessError("app-unreachable", `Archivist answered ${response.status} without a JSON body.`);
  }
  if (!body || typeof body !== "object" || typeof body.ok !== "boolean") {
    throw new LiveAccessError("app-unreachable", "Archivist's answer was not a control envelope.");
  }
  if (!body.ok) throw new LiveAccessError(body.error?.code ?? "internal", body.error?.message ?? "The live recorder refused.");
  return body.result;
}

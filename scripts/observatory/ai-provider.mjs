// The AI provider a UI Lab run talks to. Every run gets one of three, and
// the app's environment points at it (OPENAI_BASE_URL, OPENAI_API_KEY,
// ARCHIVIST_LOCAL_AI_URL), so a run can never reach a real provider by
// accident, whatever keys the caller's shell, backend/.env or the Keychain
// hold:
//
//   - offline (every scenario without an `ai` section): a dead local address.
//     Nothing answers; an AI turn would fail fast.
//   - mock (`ai.provider: "mock"`): a deterministic local stand-in for the
//     OpenAI Responses API, scripted per request: delays, a slow stream,
//     retries, an interrupted stream, a failure, a tool call. No API calls.
//   - real (`ai.provider: "real"`), only with --allow-provider-calls: a local
//     proxy in front of the provider that caps the number of requests and
//     the output tokens of each, and keeps only counts. Never part of a
//     preset unless that preset is the opt-in Provider Latency audit.
//
// The mock and the proxy record request counts, byte sizes and timings,
// never bodies or headers.
import http from "node:http";

export const OFFLINE_BASE_URL = "http://127.0.0.1:9/v1"; // the discard port: refused at once
export const OFFLINE_API_KEY = "observatory-offline-no-key";
export const MOCK_MODEL = "gpt-5-mini";
export const DEFAULT_UPSTREAM = "https://api.openai.com/v1";
export const PROVIDER_LIMITS = { maxRequests: 6, maxOutputTokens: 64 };

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const sse = (event) => `event: ${event.type}\ndata: ${JSON.stringify(event)}\n\n`;

// Named behaviours, for scenarios and tests. A step answers one POST.
export const BEHAVIOURS = {
  fast: [{ text: "Done.", chunks: 1 }],
  "slow-first-byte": [{ firstByteDelayMs: 600, text: "The answer arrives late.", chunks: 4 }],
  "slow-stream": [{ firstByteDelayMs: 80, text: "This answer streams slowly with long pauses between its pieces of text.", chunks: 12, gapMs: 90, pauseAt: 6, pauseMs: 1200 }],
  retry: [{ status: 503, retryAfterMs: 50 }, { text: "Answered after one retry.", chunks: 3 }],
  interrupted: [{ text: "This stream is cut off before it ends.", chunks: 8, gapMs: 30, interruptAfter: 4 }],
  failure: [{ status: 400, error: "invalid request (mock)" }],
  "tool-round": [
    { toolCall: { name: "list_directory", arguments: JSON.stringify({ path: "", limit: 10 }) }, firstByteDelayMs: 40 },
    { text: "The library has a few files.", chunks: 5, gapMs: 20, usage: { input: 900, output: 12 } },
  ],
  "cancel-me": [{ firstByteDelayMs: 50, text: "A long answer that the scenario cancels part way through.", chunks: 40, gapMs: 100 }],
};

function words(text, chunks) {
  const parts = text.split(/(?<= )/);
  const size = Math.max(1, Math.ceil(parts.length / Math.max(1, chunks)));
  const out = [];
  for (let index = 0; index < parts.length; index += size) out.push(parts.slice(index, index + size).join(""));
  return out;
}

// A local stand-in for POST /v1/responses (and GET /v1/models). `script` is
// a list of steps, used in order and the last one repeated; or name a
// behaviour. Returns { baseUrl, stats(), close() }.
export async function startMockProvider({ script = BEHAVIOURS.fast, behaviour = null } = {}) {
  const steps = behaviour ? BEHAVIOURS[behaviour] : script;
  if (!Array.isArray(steps) || !steps.length) throw new Error(`Unknown mock behaviour ${behaviour}`);
  const stats = { requests: 0, models: 0, bytesReceived: 0, bytesSent: 0, statuses: [], interrupted: 0, cancelledByClient: 0 };
  const server = http.createServer(async (request, response) => {
    let size = 0;
    for await (const chunk of request) size += chunk.length;
    stats.bytesReceived += size;
    if (request.method === "GET" && request.url.endsWith("/models")) {
      stats.models += 1;
      response.writeHead(200, { "content-type": "application/json" });
      response.end(JSON.stringify({ object: "list", data: [{ id: MOCK_MODEL, created: 1, owned_by: "observatory-mock" }] }));
      return;
    }
    if (request.method !== "POST" || !/\/(responses|chat\/completions)$/.test(request.url)) {
      response.writeHead(404).end();
      return;
    }
    const step = steps[Math.min(stats.requests, steps.length - 1)];
    stats.requests += 1;
    const id = `resp_mock_${stats.requests}`;
    let closed = false;
    let cut = false;
    response.on("close", () => {
      if (!response.writableEnded && !cut) {
        closed = true;
        stats.cancelledByClient += 1;
      }
    });
    if (step.firstByteDelayMs) await sleep(step.firstByteDelayMs);
    if (step.status && step.status !== 200) {
      stats.statuses.push(step.status);
      const body = JSON.stringify({ error: { message: step.error ?? "busy (mock)" } });
      response.writeHead(step.status, { "content-type": "application/json", ...(step.retryAfterMs != null ? { "retry-after-ms": String(step.retryAfterMs) } : {}) });
      response.end(body);
      stats.bytesSent += body.length;
      return;
    }
    stats.statuses.push(200);
    response.writeHead(200, { "content-type": "text/event-stream", "cache-control": "no-cache" });
    const write = (event) => {
      if (closed) return false;
      const text = sse(event);
      stats.bytesSent += text.length;
      response.write(text);
      return true;
    };
    write({ type: "response.created", response: { id } });
    if (step.toolCall) {
      write({ type: "response.output_item.done", item: { type: "function_call", call_id: `call_${stats.requests}`, name: step.toolCall.name, arguments: step.toolCall.arguments } });
    }
    const pieces = step.text ? words(step.text, step.chunks ?? 1) : [];
    for (let index = 0; index < pieces.length; index += 1) {
      if (index > 0 && step.gapMs) await sleep(step.gapMs);
      if (step.pauseAt === index && step.pauseMs) await sleep(step.pauseMs);
      if (step.interruptAfter != null && index >= step.interruptAfter) {
        stats.interrupted += 1;
        cut = true;
        response.socket?.destroy();
        return;
      }
      if (!write({ type: "response.output_text.delta", delta: pieces[index] })) return;
    }
    const usage = step.usage ? { usage: { input_tokens: step.usage.input, output_tokens: step.usage.output } } : {};
    write({ type: "response.completed", response: { id, ...usage } });
    response.end();
  });
  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  return {
    baseUrl: `http://127.0.0.1:${server.address().port}/v1`,
    stats: () => ({ ...stats, statuses: [...stats.statuses] }),
    close: () => new Promise((resolve) => { server.closeAllConnections?.(); server.close(() => resolve()); }),
  };
}

// Caps one request body: its output tokens, and nothing else. Returns the
// new body, or null if it isn't JSON (then it is refused, not forwarded).
export function capRequestBody(buffer, maxOutputTokens) {
  let body;
  try {
    body = JSON.parse(buffer.toString("utf8"));
  } catch {
    return null;
  }
  if (!body || typeof body !== "object") return null;
  const key = "messages" in body && !("input" in body) ? "max_tokens" : "max_output_tokens";
  const asked = Number(body[key]);
  body[key] = Number.isFinite(asked) && asked > 0 ? Math.min(asked, maxOutputTokens) : maxOutputTokens;
  return Buffer.from(JSON.stringify(body));
}

// The opt-in path: a local proxy in front of the real provider. The app
// sends its own credentials; the proxy passes them on and never records
// them. It refuses past maxRequests (429, so the app stops) and caps each
// request's output tokens. Only counts, sizes and timings are kept.
export async function startProviderProxy({ upstream = DEFAULT_UPSTREAM, maxRequests = PROVIDER_LIMITS.maxRequests, maxOutputTokens = PROVIDER_LIMITS.maxOutputTokens, fetchImpl = fetch } = {}) {
  const base = new URL(upstream.endsWith("/") ? upstream : `${upstream}/`);
  const stats = { requests: 0, refused: 0, forwarded: 0, statuses: [], bytesSent: 0, bytesReceived: 0, firstByteMs: [], durationMs: [], maxRequests, maxOutputTokens, upstreamHost: base.host };
  const server = http.createServer(async (request, response) => {
    const chunks = [];
    for await (const chunk of request) chunks.push(chunk);
    const isModels = request.method === "GET" && request.url.endsWith("/models");
    if (!isModels) {
      stats.requests += 1;
      if (stats.requests > maxRequests) {
        stats.refused += 1;
        response.writeHead(429, { "content-type": "application/json", "x-should-retry": "false" });
        response.end(JSON.stringify({ error: { message: `Observatory provider audit limit: ${maxRequests} requests.` } }));
        return;
      }
    }
    const body = isModels ? null : capRequestBody(Buffer.concat(chunks), maxOutputTokens);
    if (!isModels && !body) {
      stats.refused += 1;
      response.writeHead(400).end();
      return;
    }
    const target = new URL(request.url.replace(/^\/v1\//, ""), base);
    const headers = {};
    for (const name of ["authorization", "content-type", "accept", "openai-organization", "openai-project"]) {
      if (request.headers[name]) headers[name] = request.headers[name];
    }
    const started = performance.now();
    try {
      const upstreamResponse = await fetchImpl(target, { method: request.method, headers, body: body ?? undefined });
      stats.forwarded += 1;
      stats.statuses.push(upstreamResponse.status);
      stats.bytesSent += body?.length ?? 0;
      const pass = {};
      for (const name of ["content-type", "retry-after", "retry-after-ms", "x-should-retry"]) {
        const value = upstreamResponse.headers.get(name);
        if (value) pass[name] = value;
      }
      response.writeHead(upstreamResponse.status, pass);
      let first = true;
      if (upstreamResponse.body) {
        for await (const chunk of upstreamResponse.body) {
          if (first) {
            stats.firstByteMs.push(Math.round(performance.now() - started));
            first = false;
          }
          stats.bytesReceived += chunk.length;
          response.write(chunk);
        }
      }
      response.end();
    } catch {
      stats.statuses.push(0);
      if (!response.headersSent) response.writeHead(502);
      response.end();
    } finally {
      stats.durationMs.push(Math.round(performance.now() - started));
    }
  });
  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  return {
    baseUrl: `http://127.0.0.1:${server.address().port}/v1`,
    stats: () => ({ ...stats, statuses: [...stats.statuses], firstByteMs: [...stats.firstByteMs], durationMs: [...stats.durationMs] }),
    close: () => new Promise((resolve) => { server.closeAllConnections?.(); server.close(() => resolve()); }),
  };
}

export class ProviderRefused extends Error {}

// Which provider a scenario gets, and the environment that points the app at
// it. Call close() when the app has exited.
export async function providerFor(scenario, { allowProviderCalls = false, upstream = process.env.OPENAI_BASE_URL || DEFAULT_UPSTREAM } = {}) {
  const ai = scenario.ai ?? null;
  if (!ai || ai.provider === "offline") {
    return {
      env: { OPENAI_BASE_URL: OFFLINE_BASE_URL, OPENAI_API_KEY: OFFLINE_API_KEY, ARCHIVIST_LOCAL_AI_URL: OFFLINE_BASE_URL },
      summary: { provider: "offline", calls: "none possible: the app points at a closed local port" },
      close: async () => null,
    };
  }
  if (ai.provider === "mock") {
    const mock = await startMockProvider({ behaviour: ai.behaviour ?? null, script: ai.script ?? BEHAVIOURS.fast });
    return {
      env: { OPENAI_BASE_URL: mock.baseUrl, OPENAI_API_KEY: OFFLINE_API_KEY, ARCHIVIST_LOCAL_AI_URL: OFFLINE_BASE_URL },
      summary: { provider: "mock", behaviour: ai.behaviour ?? "script", calls: "local mock only", expected: { retries: ai.expected?.retries === true, streamPauses: ai.expected?.streamPauses === true } },
      close: async () => {
        const stats = mock.stats();
        await mock.close();
        return stats;
      },
    };
  }
  if (ai.provider === "real") {
    if (allowProviderCalls !== true) {
      throw new ProviderRefused(`Scenario ${scenario.id} calls the real AI provider. That costs money and sends synthetic prompts to it, so it only runs with --allow-provider-calls (the Provider Latency audit asks first).`);
    }
    const limits = { maxRequests: Math.min(PROVIDER_LIMITS.maxRequests, ai.maxRequests ?? PROVIDER_LIMITS.maxRequests), maxOutputTokens: Math.min(PROVIDER_LIMITS.maxOutputTokens, ai.maxOutputTokens ?? PROVIDER_LIMITS.maxOutputTokens) };
    const proxy = await startProviderProxy({ upstream, ...limits });
    // The app keeps its own key (environment, backend/.env or Keychain): the
    // key variable is left alone, only the address goes through the proxy.
    return {
      env: { OPENAI_BASE_URL: proxy.baseUrl, ARCHIVIST_LOCAL_AI_URL: OFFLINE_BASE_URL },
      keepKey: true,
      summary: { provider: "real", calls: "real provider, through a capped local proxy", ...limits, upstreamHost: new URL(upstream).host },
      close: async () => {
        const stats = proxy.stats();
        await proxy.close();
        return stats;
      },
    };
  }
  throw new ProviderRefused(`Unknown ai.provider ${ai.provider} in scenario ${scenario.id}.`);
}

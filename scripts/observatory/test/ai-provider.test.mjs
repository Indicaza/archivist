// The AI audit providers: the offline address, the scripted local mock and
// the capped proxy in front of the real provider. No test here reaches a
// real provider: the proxy is tested against a local fake upstream.
import { test } from "node:test";
import assert from "node:assert/strict";
import http from "node:http";
import { readFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { BEHAVIOURS, OFFLINE_BASE_URL, PROVIDER_LIMITS, ProviderRefused, capRequestBody, providerFor, startMockProvider, startProviderProxy } from "../ai-provider.mjs";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const scenario = async (id) => ({ id, ...JSON.parse(await readFile(path.join(HERE, "..", "ui-scenarios", `${id}.json`), "utf8")) });

const post = (baseUrl, body = { model: "m", input: "synthetic", stream: true }, headers = {}) =>
  fetch(`${baseUrl}/responses`, { method: "POST", headers: { "content-type": "application/json", ...headers }, body: JSON.stringify(body) });

const events = (text) => text.split("\n\n").filter(Boolean).map((block) => JSON.parse(block.split("\n").find((line) => line.startsWith("data: ")).slice(6)));

test("the mock streams a scripted answer with usage, and keeps only counts", async () => {
  const mock = await startMockProvider({ script: [{ text: "one two three four", chunks: 2, usage: { input: 11, output: 4 } }] });
  try {
    const models = await (await fetch(`${mock.baseUrl}/models`)).json();
    assert.equal(models.data[0].id, "gpt-5-mini");
    const response = await post(mock.baseUrl);
    assert.equal(response.headers.get("content-type"), "text/event-stream");
    const stream = events(await response.text());
    assert.deepEqual(stream.map((event) => event.type), ["response.created", "response.output_text.delta", "response.output_text.delta", "response.completed"]);
    assert.equal(stream.filter((event) => event.delta).map((event) => event.delta).join(""), "one two three four");
    assert.deepEqual(stream.at(-1).response.usage, { input_tokens: 11, output_tokens: 4 });
    const stats = mock.stats();
    assert.equal(stats.requests, 1);
    assert.equal(stats.models, 1);
    assert.deepEqual(stats.statuses, [200]);
    assert.ok(!JSON.stringify(stats).includes("synthetic"), "request bodies are counted, not kept");
  } finally {
    await mock.close();
  }
});

test("the mock refuses, then answers, as a retry script says", async () => {
  const mock = await startMockProvider({ behaviour: "retry" });
  try {
    const first = await post(mock.baseUrl);
    assert.equal(first.status, 503);
    assert.equal(first.headers.get("retry-after-ms"), "50");
    await first.text();
    const second = await post(mock.baseUrl);
    assert.equal(second.status, 200);
    assert.match(await second.text(), /response\.completed/);
    assert.deepEqual(mock.stats().statuses, [503, 200]);
  } finally {
    await mock.close();
  }
});

test("the mock delays the first byte, pauses mid-stream and offers a tool call", async () => {
  const mock = await startMockProvider({ script: [
    { toolCall: { name: "list_directory", arguments: "{}" } },
    { firstByteDelayMs: 60, text: "a b c d", chunks: 4, pauseAt: 2, pauseMs: 80 },
  ] });
  try {
    const tool = events(await (await post(mock.baseUrl)).text());
    assert.equal(tool[1].item.type, "function_call");
    assert.equal(tool[1].item.name, "list_directory");
    const started = performance.now();
    const response = await post(mock.baseUrl);
    assert.ok(performance.now() - started >= 55, "first byte waited");
    const reader = response.body.getReader();
    const arrivals = [];
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      if (new TextDecoder().decode(value).includes("output_text.delta")) arrivals.push(performance.now());
    }
    const gaps = arrivals.slice(1).map((at, index) => at - arrivals[index]);
    assert.ok(Math.max(...gaps) >= 70, `one long pause in ${gaps}`);
  } finally {
    await mock.close();
  }
});

test("the mock cuts an interrupted stream before it completes, and a failure is final", async () => {
  const mock = await startMockProvider({ behaviour: "interrupted" });
  try {
    const response = await post(mock.baseUrl);
    await assert.rejects(response.text());
    assert.equal(mock.stats().interrupted, 1);
    assert.equal(mock.stats().cancelledByClient, 0, "the mock's own cut is not a client cancel");
  } finally {
    await mock.close();
  }
  const failing = await startMockProvider({ behaviour: "failure" });
  try {
    for (let i = 0; i < 2; i += 1) assert.equal((await post(failing.baseUrl)).status, 400);
  } finally {
    await failing.close();
  }
});

test("the mock counts a client that hangs up mid-stream as a cancel", async () => {
  const mock = await startMockProvider({ behaviour: "cancel-me" });
  try {
    const response = await post(mock.baseUrl, undefined, {});
    const reader = response.body.getReader();
    await reader.read();
    await reader.cancel();
    for (let i = 0; i < 50 && !mock.stats().cancelledByClient; i += 1) await new Promise((resolve) => setTimeout(resolve, 20));
    assert.equal(mock.stats().cancelledByClient, 1);
  } finally {
    await mock.close();
  }
});

test("every named behaviour is a non-empty script", () => {
  for (const [name, steps] of Object.entries(BEHAVIOURS)) assert.ok(Array.isArray(steps) && steps.length, name);
});

test("the proxy caps output tokens in both request shapes and refuses what isn't JSON", () => {
  const capped = (body) => JSON.parse(capRequestBody(Buffer.from(JSON.stringify(body)), 64).toString());
  assert.equal(capped({ input: "x", max_output_tokens: 4000 }).max_output_tokens, 64);
  assert.equal(capped({ input: "x" }).max_output_tokens, 64);
  assert.equal(capped({ input: "x", max_output_tokens: 10 }).max_output_tokens, 10);
  assert.equal(capped({ messages: [], max_tokens: 900 }).max_tokens, 64);
  assert.equal(capRequestBody(Buffer.from("not json"), 64), null);
});

async function fakeUpstream() {
  const seen = [];
  const server = http.createServer(async (request, response) => {
    const chunks = [];
    for await (const chunk of request) chunks.push(chunk);
    seen.push({ url: request.url, headers: request.headers, body: Buffer.concat(chunks).toString() });
    response.writeHead(200, { "content-type": "text/event-stream", "set-cookie": "secret=1", "x-request-id": "abc" });
    response.end("event: response.completed\ndata: {\"type\":\"response.completed\"}\n\n");
  });
  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  return { url: `http://127.0.0.1:${server.address().port}/v1`, seen, close: () => new Promise((resolve) => { server.closeAllConnections?.(); server.close(resolve); }) };
}

test("the proxy forwards only the provider headers, caps tokens, stops at its limit, and keeps no content", async () => {
  const upstream = await fakeUpstream();
  const proxy = await startProviderProxy({ upstream: upstream.url, maxRequests: 2, maxOutputTokens: 32 });
  try {
    const headers = { authorization: "Bearer sk-test-secret", cookie: "session=1", "x-custom": "private" };
    for (let i = 0; i < 2; i += 1) {
      const response = await post(proxy.baseUrl, { model: "m", input: "synthetic prompt", max_output_tokens: 999 }, headers);
      assert.equal(response.status, 200);
      assert.equal(response.headers.get("set-cookie"), null, "only the provider's retry and type headers come back");
      await response.text();
    }
    const third = await post(proxy.baseUrl, undefined, headers);
    assert.equal(third.status, 429);
    assert.equal(third.headers.get("x-should-retry"), "false");
    assert.equal(upstream.seen.length, 2, "the third request never left the machine");
    assert.equal(upstream.seen[0].url, "/v1/responses");
    assert.equal(upstream.seen[0].headers.authorization, "Bearer sk-test-secret", "the app's own key is passed on");
    assert.equal(upstream.seen[0].headers.cookie, undefined);
    assert.equal(upstream.seen[0].headers["x-custom"], undefined);
    assert.equal(JSON.parse(upstream.seen[0].body).max_output_tokens, 32);
    const stats = proxy.stats();
    assert.equal(stats.forwarded, 2);
    assert.equal(stats.refused, 1);
    assert.equal(stats.firstByteMs.length, 2);
    const text = JSON.stringify(stats);
    for (const word of ["sk-test-secret", "synthetic", "Bearer", "session"]) assert.ok(!text.includes(word), word);
  } finally {
    await proxy.close();
    await upstream.close();
  }
});

test("offline scenarios point the app at a closed port and never at a provider", async () => {
  const provider = await providerFor(await scenario("ai-pipeline").then((entry) => ({ ...entry, ai: { provider: "offline" } })));
  assert.equal(provider.env.OPENAI_BASE_URL, OFFLINE_BASE_URL);
  assert.ok(!provider.keepKey);
  assert.equal(await provider.close(), null);
});

test("the mocked scenarios start their own local mock", async () => {
  for (const id of ["ai-pipeline", "ai-turn"]) {
    const provider = await providerFor(await scenario(id));
    try {
      assert.match(provider.env.OPENAI_BASE_URL, /^http:\/\/127\.0\.0\.1:\d+\/v1$/);
      assert.ok(!provider.keepKey, "the real key is removed from the app's environment");
      assert.equal(provider.summary.provider, "mock");
      assert.deepEqual(provider.summary.expected, id === "ai-turn" ? { retries: true, streamPauses: true } : { retries: false, streamPauses: false });
    } finally {
      const stats = await provider.close();
      assert.equal(stats.requests, 0);
    }
  }
});

test("the provider scenario is refused without explicit permission, and capped with it", async () => {
  const definition = await scenario("ai-provider");
  await assert.rejects(providerFor(definition), ProviderRefused);
  await assert.rejects(providerFor(definition, { allowProviderCalls: "yes" }), ProviderRefused, "only true counts");
  const provider = await providerFor(definition, { allowProviderCalls: true, upstream: "http://127.0.0.1:9/v1" });
  try {
    assert.equal(provider.keepKey, true);
    assert.ok(provider.summary.maxRequests <= PROVIDER_LIMITS.maxRequests);
    assert.ok(provider.summary.maxOutputTokens <= PROVIDER_LIMITS.maxOutputTokens);
  } finally {
    const stats = await provider.close();
    assert.equal(stats.requests, 0, "starting the proxy sends nothing");
  }
});

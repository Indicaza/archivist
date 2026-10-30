import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { readdir, readFile, writeFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import test from "node:test";
import { promisify } from "node:util";
import { formatBuildSummary } from "../report.mjs";
import { tempDir } from "./helpers.mjs";

const CLI = path.join(path.dirname(fileURLToPath(import.meta.url)), "..", "cli.mjs");
const run = promisify(execFile);

async function cli(args, root) {
  try {
    const { stdout } = await run(process.execPath, [CLI, ...args], { env: { ...process.env, ARCHIVIST_OBSERVATORY_DIR: root } });
    return { code: 0, stdout };
  } catch (error) {
    return { code: error.code, stdout: error.stdout, stderr: error.stderr };
  }
}

test("ingesting a runtime log keeps the original and links only on request", async () => {
  const root = await tempDir();
  const log = path.join(root, "frontend.log");
  const original = "hello\narchivist.wells.timing: minimize “A” model 0.01 ms, publish 2 ms\nweird\n";
  await writeFile(log, original);
  const result = await cli(["ingest-runtime", log, "--label", "wells"], root);
  assert.equal(result.code, 0, result.stderr);
  const [runId] = await readdir(path.join(root, "runs"));
  const dir = path.join(root, "runs", runId);
  assert.equal(await readFile(path.join(dir, "runtime.log"), "utf8"), original);
  const summary = JSON.parse(await readFile(path.join(dir, "summary.json"), "utf8"));
  assert.equal(summary.schemaVersion, 1);
  assert.equal(summary.kind, "runtime-log");
  assert.equal(summary.label, "wells");
  assert.deepEqual(summary.links, []);
  assert.equal(summary.timing.recognizedEvents, 1);
});

test("a missing runtime log or unknown build run is a usage error", async () => {
  const root = await tempDir();
  assert.equal((await cli(["ingest-runtime", path.join(root, "nope.log")], root)).code, 2);
  const log = path.join(root, "a.log");
  await writeFile(log, "");
  assert.equal((await cli(["ingest-runtime", log, "--build-run", "missing"], root)).code, 2);
  assert.equal((await cli(["show"], root)).code, 0);
});

test("the terminal summary names the failed phase and the log", () => {
  const text = formatBuildSummary({
    status: "failure",
    label: null,
    source: { branch: "main", commit: "abcdef1234567", dirty: false },
    totalDurationMs: 3000,
    phases: [
      { name: "ide-bundle", status: "success", durationMs: 100, details: { cacheReused: true } },
      { name: "configure", status: "failure", durationMs: 2900, details: {}, error: { message: "Exited with code 1.", excerpt: ["CMake Error at x"] } },
      { name: "native-build", status: "skipped" },
    ],
  }, { summaryPath: "/r/summary.json", logPath: "/r/build.log" });
  assert.match(text, /Build:\s+FAILURE/);
  assert.match(text, /main @ abcdef123/);
  assert.match(text, /IDE bundle:\s+cached/);
  assert.match(text, /Configure failure: Exited with code 1\./);
  assert.match(text, /CMake Error at x/);
  assert.match(text, /Log:\s+\/r\/build\.log/);
});

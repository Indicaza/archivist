// Everything about one run that an agent elsewhere would need, gathered into
// <root>/exports/<exportId>/ and <root>/exports/<exportId>.zip, with a
// manifest that says which file to read first. Redacted by default; raw
// logs only when asked for, and marked as such.
import { createHash } from "node:crypto";
import { mkdir, readFile, stat, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { artifactFile, MAX_ARTIFACT_BYTES, readRunSummary } from "../dashboard/records.mjs";
import { diagnosticReport } from "../dashboard/public/diagnostic.mjs";
import { logExcerpt, makeRedactor, REDACTION_RULES, redactJson } from "../dashboard/public/redact.mjs";
import { REPO_ROOT } from "../paths.mjs";
import { makeRunId } from "../storage.mjs";
import { zipBuffer } from "./zip.mjs";

export const EVIDENCE_SCHEMA = "archivist.observatory.evidence";
export const EVIDENCE_SCHEMA_VERSION = 1;
const LOG_ARTIFACTS = ["log"];

export function exportsDir(root) {
  return path.join(root, "exports");
}

export function redactorFor(root) {
  return makeRedactor({ root, repo: REPO_ROOT, home: os.homedir() });
}

async function readArtifact(dir, summary, name) {
  const file = await artifactFile(dir, summary, name);
  if (!file) return null;
  if ((await stat(file)).size > MAX_ARTIFACT_BYTES) return { file, tooLarge: true, text: null };
  return { file, tooLarge: false, text: await readFile(file, "utf8") };
}

function parseJson(text) {
  try {
    return text == null ? null : JSON.parse(text);
  } catch {
    return null;
  }
}

// What a report needs from a run's folder. { loaded, events, scenario, logText }
export async function loadRunEvidence(root, runId) {
  const loaded = await readRunSummary(root, runId);
  if (!loaded?.summary) return { loaded };
  const { dir, summary } = loaded;
  const events = parseJson((await readArtifact(dir, summary, "events"))?.text);
  const scenario = parseJson((await readArtifact(dir, summary, "scenario"))?.text);
  const log = await readArtifact(dir, summary, "log");
  return {
    loaded,
    events: Array.isArray(events?.actions) ? events.actions : null,
    eventsDocument: events,
    scenario,
    log,
  };
}

const sha256 = (buffer) => createHash("sha256").update(buffer).digest("hex");

// category: this run's regression analysis (regression.mjs), or null.
export async function exportEvidence(root, runId, { category = null, includeRawLogs = false, now = new Date() } = {}) {
  const evidence = await loadRunEvidence(root, runId);
  if (!evidence.loaded?.summary) {
    return { error: evidence.loaded ? evidence.loaded.error : "No such run." };
  }
  const { summary, dir: runDir } = evidence.loaded;
  const redact = redactorFor(root);
  const report = diagnosticReport({
    runId, summary, events: evidence.events, scenario: evidence.scenario, category, logText: evidence.log?.text ?? null, redact,
  });

  const exportId = makeRunId("evidence", now);
  const base = path.join(exportsDir(root), exportId);
  await mkdir(base, { recursive: true });
  const files = [];
  const add = async (name, content, entry) => {
    const data = Buffer.isBuffer(content) ? content : Buffer.from(typeof content === "string" ? content : `${JSON.stringify(content, null, 2)}\n`, "utf8");
    await mkdir(path.dirname(path.join(base, name)), { recursive: true });
    await writeFile(path.join(base, name), data);
    files.push({ path: name, bytes: data.length, sha256: sha256(data), redacted: true, ...entry });
    return data;
  };

  await add("report.md", report.markdown, { role: "report", important: true, description: "The diagnostic report (what Copy for AI copies). Read this first." });
  await add("diagnostic.json", report.facts, { role: "facts", important: true, schema: report.facts.schema, schemaVersion: report.facts.schemaVersion, description: "The report's facts as data." });
  await add("summary.json", redactJson(summary, redact), { role: "summary", important: true, schema: `archivist.observatory.${summary.kind ?? "run"}`, schemaVersion: summary.schemaVersion ?? null, description: "The run's summary.json, with paths and secrets redacted." });
  if (evidence.eventsDocument) {
    await add("ui-events.json", redactJson(evidence.eventsDocument, redact), { role: "events", schema: evidence.eventsDocument.schema ?? null, schemaVersion: evidence.eventsDocument.schemaVersion ?? null, description: "Every UI action the in-app driver recorded, with its timings." });
  }
  if (evidence.scenario) {
    await add("scenario.json", redactJson(evidence.scenario, redact), { role: "scenario", description: "The exact scenario definition that ran." });
  }
  if (category) {
    await add("comparison.json", category, { role: "comparison", schema: "archivist.observatory.regression-report#category", schemaVersion: 1, description: "This run compared with its baseline." });
  }
  if (evidence.log?.text != null) {
    const excerpt = logExcerpt(evidence.log.text, redact, { maxLines: 40 });
    const tail = evidence.log.text.split(/\r?\n/).filter(Boolean).slice(-20).map(redact);
    await add("log-excerpt.txt", [
      `Redacted excerpt of ${path.basename(evidence.log.file)} (${excerpt.totalLines} lines in full).`, "",
      "Lines mentioning errors or warnings:", ...(excerpt.lines.length ? excerpt.lines : ["(none)"]), "",
      "Last 20 lines:", ...tail, "",
    ].join("\n"), { role: "log-excerpt", description: "Error and warning lines and the end of the log, redacted." });
  }
  if (includeRawLogs) {
    for (const name of LOG_ARTIFACTS) {
      const artifact = await readArtifact(runDir, summary, name);
      if (!artifact || artifact.tooLarge) continue;
      await add(`raw/${path.basename(artifact.file)}`, await readFile(artifact.file), {
        role: "raw-log", redacted: false, sensitive: true,
        description: "The full log, NOT redacted. It may contain paths, file names or other private text.",
      });
    }
  }

  const manifest = {
    schema: EVIDENCE_SCHEMA,
    schemaVersion: EVIDENCE_SCHEMA_VERSION,
    exportId,
    createdAt: now.toISOString(),
    runId,
    kind: summary.kind ?? null,
    status: summary.status ?? null,
    readFirst: ["report.md", "diagnostic.json"],
    redaction: { applied: true, rules: REDACTION_RULES, rawLogsIncluded: includeRawLogs },
    files,
  };
  await add("manifest.json", manifest, { role: "manifest" });
  files.pop();

  const entries = await Promise.all([...files.map((file) => file.path), "manifest.json"].map(async (name) => ({
    name: `${exportId}/${name}`, data: await readFile(path.join(base, name)),
  })));
  const zipPath = `${base}.zip`;
  await writeFile(zipPath, zipBuffer(entries, now));
  return { exportId, dir: base, zip: zipPath, manifest };
}

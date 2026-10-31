// Audits on disk and as operations (observatory.audit.*).
//
// An audit is a normal test job (control/jobs.mjs) whose request names a
// preset; the same runner builds and runs the scenarios. When the job ends,
// the runner calls writeAuditRecord: one more run folder, kind "audit", that
// lists the runs the job made, the findings the rules found in them and the
// headline numbers. Comparisons with earlier audits are worked out when
// asked for, from the runs themselves, so a changed budget or a deleted run
// is never hidden behind a stored verdict.
//
// operations.mjs owns the envelope, validation and error type; it passes
// them in here so the two modules don't import each other.
import { copyFile, readFile, stat } from "node:fs/promises";
import path from "node:path";
import { loadBudgets } from "../comparisons.mjs";
import { artifactFile, listRunIds, loadRunsById, readRunSummary } from "../dashboard/records.mjs";
import {
  AUDIT_SCHEMA, AUDIT_SCHEMA_VERSION, auditHighlights, auditMarkdown, blindSpots, compareAudits, countBySeverity, evaluateFindings,
  FINDINGS_SCHEMA, PRESETS, previousAudit, RULES, RULES_VERSION, SEVERITIES,
} from "../dashboard/public/audit.mjs";
import { normalizeRun } from "../dashboard/public/normalize.mjs";
import { redactJson } from "../dashboard/public/redact.mjs";
import { collectMachine, collectSource } from "../metadata.mjs";
import { createRunDir, SCHEMA_VERSION, writeJsonAtomic } from "../storage.mjs";
import { exportEvidence, redactorFor } from "./evidence.mjs";
import { cancelJob, JOB_ID, listJobIds, MAX_REPEAT, readJob, refreshJob } from "./jobs.mjs";

export const MAX_TIMELINE_ACTIONS = 200;

// The audit record for a finished job. Never throws: a failure is returned as
// { error } and the job keeps its own status.
export async function writeAuditRecord(root, job, { repoRoot, status }) {
  try {
    const preset = PRESETS[job.request.audit.preset];
    const ids = [job.buildRunId, ...job.runs.map((entry) => entry.runId)].filter(Boolean);
    const children = [];
    for (const runId of ids) {
      const loaded = await readRunSummary(root, runId);
      children.push({ runId, summary: loaded?.summary ?? null });
    }
    const readable = children.filter((child) => child.summary);
    const findings = evaluateFindings(readable);
    const { runId, dir } = await createRunDir(root, "audit");
    const [source, environment] = await Promise.all([collectSource(repoRoot), collectMachine()]);
    const finishedAt = new Date().toISOString();
    const summary = {
      schemaVersion: SCHEMA_VERSION,
      kind: "audit",
      schema: AUDIT_SCHEMA,
      auditSchemaVersion: AUDIT_SCHEMA_VERSION,
      runId,
      label: `${preset.name} audit`,
      createdAt: job.startedAt ?? job.createdAt,
      finishedAt,
      durationMs: Date.parse(finishedAt) - Date.parse(job.startedAt ?? job.createdAt),
      status: status === "completed" ? "completed" : status,
      jobId: job.jobId,
      jobStatus: status,
      error: job.error ?? null,
      preset: preset.id,
      presetName: preset.name,
      rulesVersion: RULES_VERSION,
      plan: { scenarios: job.request.scenarios, repeat: job.request.repeat, build: job.request.build },
      source,
      environment,
      children: children.map(({ runId: id, summary: child }) => {
        const entry = job.runs.find((run) => run.runId === id);
        return {
          runId: id,
          kind: child?.kind ?? (id === job.buildRunId ? "build" : "ui-scenario"),
          scenario: child?.scenario?.id ?? entry?.scenario ?? null,
          round: entry?.round ?? null,
          status: child?.status ?? "unreadable",
        };
      }),
      counts: countBySeverity(findings),
      findings,
      highlights: auditHighlights(readable),
      blindSpots: blindSpots({ plan: { repeat: job.request.repeat, build: job.request.build } }),
      artifacts: { findings: "findings.json" },
    };
    await writeJsonAtomic(path.join(dir, "findings.json"), { schema: FINDINGS_SCHEMA, schemaVersion: 1, rulesVersion: RULES_VERSION, rules: RULES, findings });
    const log = path.join(root, "jobs", job.jobId, "job.log");
    if ((await stat(log).catch(() => null))?.isFile()) {
      await copyFile(log, path.join(dir, "job.log"));
      summary.artifacts.log = "job.log";
    }
    await writeJsonAtomic(path.join(dir, "summary.json"), summary);
    return { runId };
  } catch (error) {
    return { error: error.message };
  }
}

function compactAudit(run) {
  return {
    auditId: run.runId, status: run.status, createdAt: run.createdAt, finishedAt: run.finishedAt,
    preset: run.audit?.preset ?? null, jobId: run.audit?.jobId ?? null,
    commit: run.source?.commit ?? null, branch: run.source?.branch ?? null, dirty: run.source?.dirty ?? null,
    runIds: run.audit?.runIds ?? [], counts: run.audit?.counts ?? null,
    topFindings: (run.audit?.findings ?? []).slice(0, 5).map((finding) => ({ id: finding.id, severity: finding.severity, summary: finding.summary })),
  };
}

export function auditOperations({ ControlError, requireRun, runIdPattern, describeTarget, scenarioDefinitions, startTests, jobView }) {
  async function requireAudit(ctx, auditId) {
    const loaded = await requireRun(ctx, auditId);
    if (loaded.summary.kind !== "audit") throw new ControlError("invalid-input", `Run ${auditId} is a ${loaded.summary.kind ?? "unknown"} run, not an audit.`);
    return { ...loaded, run: normalizeRun(auditId, loaded.summary) };
  }

  async function comparisonFor(ctx, audit, baselineAuditId = null) {
    let baseline;
    if (baselineAuditId) {
      baseline = (await requireAudit(ctx, baselineAuditId)).run;
    } else {
      const ids = (await listRunIds(ctx.root)).filter((id) => id.includes("-audit-") && id < audit.runId);
      baseline = previousAudit(await loadRunsById(ctx.root, ids), audit);
    }
    if (!baseline) return null;
    const runs = await loadRunsById(ctx.root, [...audit.audit.runIds, ...baseline.audit.runIds]);
    const { budgets } = await loadBudgets(ctx.root);
    return compareAudits({ audit, baseline, runs, budgets });
  }

  async function uiOperations(ctx, runId) {
    const loaded = await requireRun(ctx, runId);
    if (loaded.summary.kind !== "ui-scenario") throw new ControlError("invalid-input", `Run ${runId} is a ${loaded.summary.kind ?? "unknown"} run; timelines come from UI scenario runs.`);
    const file = await artifactFile(loaded.dir, loaded.summary, "operations");
    if (!file) throw new ControlError("not-found", `Run ${runId} has no operations (${loaded.summary.operations?.reason ?? "this app build didn't report them"}).`);
    return { summary: loaded.summary, operations: JSON.parse(await readFile(file, "utf8")) };
  }

  const AUDIT_ID = { type: "string", pattern: runIdPattern, required: true };
  return {
    "observatory.audit.presets": {
      description: "The audit presets: what each runs, how many runs it makes, and whether it can run now (binary built, scenarios present).",
      cli: "npm run observatory -- audit presets --json",
      input: {},
      sideEffects: "none",
      async run(ctx) {
        const [target, scenarios] = await Promise.all([describeTarget(ctx), scenarioDefinitions(ctx.scenarioDir)]);
        return {
          rulesVersion: RULES_VERSION,
          target: { binary: target.binary, exists: target.exists, modifiedAt: target.modifiedAt ?? null },
          presets: Object.values(PRESETS).map((preset) => {
            const missing = preset.scenarios.filter((id) => !scenarios.some((entry) => entry.id === id));
            return {
              ...preset,
              runs: preset.scenarios.length * preset.repeat + (preset.build ? 1 : 0),
              missingScenarios: missing,
              ready: !missing.length && (preset.build || target.exists),
              blockedBy: missing.length ? `scenarios missing: ${missing.join(", ")}` : !preset.build && !target.exists ? "no binary in build/frontend-debug; run the Build preset first" : null,
            };
          }),
        };
      },
    },
    "observatory.audit.run": {
      description: "Start an audit in the background: the preset's build and/or UI scenarios as one test job, then findings. Returns the job at once; poll observatory.audit.status.",
      cli: "npm run observatory -- audit run --preset quick|ui|ai|ai-turn|build|complete [--repeat <n>] [--wait] --json  (provider: also --allow-provider-calls)",
      input: {
        preset: { type: "string", enum: Object.keys(PRESETS), required: true },
        repeat: { type: "integer", min: 1, max: MAX_REPEAT, description: "Rounds of each scenario (default: the preset's)" },
        allowProviderCalls: { type: "boolean", default: false, description: "Required, and only allowed, for the provider preset: it calls the real AI provider (capped requests and tokens)." },
      },
      sideEffects: "Starts a background job like observatory.tests.run. Build presets run an incremental build of build/frontend-debug (never a clean build; nothing is deleted). Writes one Observatory run per build and scenario round, and an audit run at the end.",
      async run(ctx, input) {
        const preset = PRESETS[input.preset];
        if (preset.providerCalls && input.allowProviderCalls !== true) {
          throw new ControlError("invalid-input", `The ${preset.name} preset calls the real AI provider: ${preset.description} Run it again with allowProviderCalls: true (CLI: --allow-provider-calls) to confirm.`);
        }
        if (!preset.providerCalls && input.allowProviderCalls === true) {
          throw new ControlError("invalid-input", `allowProviderCalls is only for the provider preset; ${preset.name} never calls the AI provider.`);
        }
        const scenarios = await scenarioDefinitions(ctx.scenarioDir);
        const missing = preset.scenarios.filter((id) => !scenarios.some((entry) => entry.id === id));
        if (missing.length) throw new ControlError("invalid-input", `The ${preset.name} preset needs scenarios that aren't here: ${missing.join(", ")}.`);
        const repeat = preset.scenarios.length ? input.repeat ?? preset.repeat : 1;
        const job = await startTests(ctx, { scenarios: preset.scenarios, repeat, build: preset.build, audit: { preset: preset.id }, ...(preset.providerCalls ? { allowProviderCalls: true } : {}) });
        return { job, preset };
      },
    },
    "observatory.audit.status": {
      description: "An audit job's state and progress; once finished, its audit id, finding counts and top findings. Without jobId: the newest audit job.",
      cli: "npm run observatory -- audit status [<job-id>] --json",
      input: { jobId: { type: "string", pattern: JOB_ID.source } },
      sideEffects: "Marks a job interrupted if its process is gone.",
      async run(ctx, input) {
        let jobId = input.jobId;
        if (!jobId) {
          for (const id of (await listJobIds(ctx.root)).reverse()) {
            if ((await readJob(ctx.root, id))?.job?.request?.audit) {
              jobId = id;
              break;
            }
          }
        }
        if (!jobId) return { job: null, audit: null };
        const loaded = await refreshJob(ctx.root, jobId);
        if (!loaded) throw new ControlError("not-found", `No job ${jobId}.`);
        if (!loaded.job) throw new ControlError("unreadable-record", `Job ${jobId}: ${loaded.error}`);
        const audit = loaded.job.auditRunId ? (await loadRunsById(ctx.root, [loaded.job.auditRunId]))[0] : null;
        return { job: await jobView(ctx, loaded.job), audit: audit?.audit ? compactAudit(audit) : null };
      },
    },
    "observatory.audit.cancel": {
      description: "Stop a running audit job (the same as observatory.tests.cancel). The audit is still written, marked cancelled, with what finished.",
      cli: "npm run observatory -- audit cancel <job-id> --json",
      input: { jobId: { type: "string", pattern: JOB_ID.source, required: true } },
      sideEffects: "Sends SIGINT to the job's runner process after checking it is that job's runner.",
      async run(ctx, input) {
        const result = await cancelJob(ctx.root, input.jobId);
        if (result.error === "not-found") throw new ControlError("not-found", `No job ${input.jobId}.`);
        if (result.error === "unreadable-record") throw new ControlError("unreadable-record", result.message);
        if (result.error === "not-running") throw new ControlError("not-running", `Job ${input.jobId} already finished (${result.job.status}).`, { status: result.job.status });
        return { job: await jobView(ctx, result.job), cancelRequested: true };
      },
    },
    "observatory.audit.list": {
      description: "Recorded audits, newest first, with finding counts and their top findings.",
      cli: "npm run observatory -- audit list [--limit <n>] --json",
      input: { preset: { type: "string", enum: Object.keys(PRESETS) }, limit: { type: "integer", min: 1, max: 200, default: 20 } },
      sideEffects: "none",
      async run(ctx, input) {
        const ids = (await listRunIds(ctx.root)).filter((id) => id.includes("-audit-"));
        const runs = (await loadRunsById(ctx.root, ids)).filter((run) => run.kind === "audit" && (!input.preset || run.audit?.preset === input.preset));
        return { total: runs.length, audits: runs.slice(0, input.limit).map(compactAudit) };
      },
    },
    "observatory.audit.findings": {
      description: "An audit's findings (each with rule, subsystem, severity, measured impact, evidence run ids, confidence and next step) and the rules that produced them.",
      cli: "npm run observatory -- audit findings <audit-id> [--severity high|medium|low|info] [--subsystem <name>] --json",
      input: {
        auditId: AUDIT_ID,
        severity: { type: "string", enum: SEVERITIES },
        subsystem: { type: "string", pattern: "^[a-z][a-z0-9-]{0,31}$" },
      },
      sideEffects: "none",
      async run(ctx, input) {
        const { run } = await requireAudit(ctx, input.auditId);
        const findings = run.audit.findings.filter((finding) => (!input.severity || finding.severity === input.severity) && (!input.subsystem || finding.subsystem === input.subsystem));
        return { auditId: run.runId, preset: run.audit.preset, rulesVersion: run.audit.rulesVersion, counts: run.audit.counts, findings, rules: RULES };
      },
    },
    "observatory.audit.compare": {
      description: "Compare an audit with an earlier one (default: the previous audit with the same preset): per category, the regression rules (several runs on each side, no overlap), and which findings appeared or went away.",
      cli: "npm run observatory -- audit compare <audit-id> [<baseline-audit-id>] --json",
      input: { auditId: AUDIT_ID, baselineAuditId: { type: "string", pattern: runIdPattern } },
      sideEffects: "none",
      async run(ctx, input) {
        const { run } = await requireAudit(ctx, input.auditId);
        const comparison = await comparisonFor(ctx, run, input.baselineAuditId);
        return comparison ?? { status: "no-baseline", verdict: "insufficient-evidence", reasons: [`no earlier ${run.audit.preset} audit to compare with`] };
      },
    },
    "observatory.audit.report": {
      description: "The Copy for AI report for an audit: findings with rules and evidence ids, slowest operations, build summary, the comparison with the previous audit and the blind spots. Redacted.",
      cli: "npm run observatory -- audit report <audit-id> [--json]",
      input: { auditId: AUDIT_ID },
      sideEffects: "none",
      async run(ctx, input) {
        const { run, summary } = await requireAudit(ctx, input.auditId);
        const comparison = await comparisonFor(ctx, run);
        const redact = redactorFor(ctx.root);
        return { markdown: auditMarkdown({ auditRun: run, summary, comparison, redact }), audit: redactJson(summary, redact), comparison };
      },
    },
    "observatory.audit.timeline": {
      description: "A window of one UI run's timeline: actions with their top-level operations, markers and stalls, in ms since the scenario began.",
      cli: "npm run observatory -- audit timeline <run-id> [--from <ms>] [--to <ms>] --json",
      input: {
        runId: { type: "string", pattern: runIdPattern, required: true },
        fromMs: { type: "integer", min: 0, max: 86_400_000, default: 0 },
        toMs: { type: "integer", min: 1, max: 86_400_000 },
        limit: { type: "integer", min: 1, max: MAX_TIMELINE_ACTIONS, default: 50 },
      },
      sideEffects: "none",
      async run(ctx, input) {
        const { operations } = await uiOperations(ctx, input.runId);
        const runStart = operations.runStartMs ?? 0;
        const to = input.toMs ?? Infinity;
        const actions = [];
        for (const action of operations.actions ?? []) {
          const at = action.startMs ?? 0;
          if (at + (action.durationMs ?? 0) < input.fromMs || at > to) continue;
          actions.push({
            index: action.index, startMs: action.startMs ?? null, phase: action.phase, step: action.step, label: action.label ?? action.op, durationMs: action.durationMs,
            kind: action.kind ?? null, window: action.window ?? null, timings: action.timings ?? null,
            coveredMs: action.coveredMs, unattributedMs: action.unattributedMs, afterWindowMs: action.afterWindowMs ?? null,
            operations: (action.operations ?? []).map((node) => ({ name: node.name, startMs: node.startMs, durationMs: node.durationMs, selfMs: node.selfMs, children: node.children?.length ?? 0 })),
            stalls: action.stalls, outlived: action.outlived,
          });
          if (actions.length >= input.limit) break;
        }
        return {
          runId: input.runId, runStartMs: runStart, window: { fromMs: input.fromMs, toMs: input.toMs ?? null },
          note: "Action times are ms since the scenario began; operation startMs is ms since its action began.",
          actions, truncated: actions.length >= input.limit,
          stalls: (operations.stalls ?? []).filter((stall) => stall.atMs >= input.fromMs && stall.atMs <= to),
          between: (operations.between ?? []).filter((op) => op.startMs + op.durationMs >= input.fromMs && op.startMs <= to),
          startup: operations.startup ?? null,
        };
      },
    },
    "observatory.audit.operation": {
      description: "Everything measured inside one action of a UI run: the full operation tree (time containment, with self time), markers, stalls and unattributed time.",
      cli: "npm run observatory -- audit operation <run-id> <action-index> --json",
      input: { runId: { type: "string", pattern: runIdPattern, required: true }, action: { type: "integer", min: 0, max: 1_000_000, required: true } },
      sideEffects: "none",
      async run(ctx, input) {
        const { operations } = await uiOperations(ctx, input.runId);
        const action = (operations.actions ?? []).find((entry) => entry.index === input.action);
        if (!action) throw new ControlError("not-found", `Run ${input.runId} has no action ${input.action}.`);
        return {
          runId: input.runId, action,
          basis: "Spans nest by time containment (inside, not necessarily called by). selfMs = a span minus the union of its children; unattributedMs = the response window (input to settled; see window.basis) minus the union of the spans inside it, so coveredMs + unattributedMs = window.ms; afterWindowMs is span time after the window. Runs before ui-operations v2 measured against the whole action.",
        };
      },
    },
    "observatory.audit.export": {
      description: "Write an evidence bundle for an audit: its report, findings, rules, redacted summary, the comparison, and each run's own report and summary, as a folder and a .zip in <Observatory folder>/exports.",
      cli: "npm run observatory -- audit export <audit-id> [--raw-logs] --json",
      input: { auditId: AUDIT_ID, includeRawLogs: { type: "boolean", default: false } },
      sideEffects: "Writes <Observatory folder>/exports/<export-id>/ and <export-id>.zip. Never changes a run.",
      async run(ctx, input) {
        const { run, summary } = await requireAudit(ctx, input.auditId);
        const comparison = await comparisonFor(ctx, run);
        const redact = redactorFor(ctx.root);
        const result = await exportEvidence(ctx.root, input.auditId, {
          includeRawLogs: input.includeRawLogs,
          audit: { markdown: auditMarkdown({ auditRun: run, summary, comparison, redact }), comparison, childRunIds: run.audit.runIds },
        });
        if (result.error) throw new ControlError("unreadable-record", result.error);
        return result;
      },
    },
  };
}

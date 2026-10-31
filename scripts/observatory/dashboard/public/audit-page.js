// Audits on the page: the "Run an audit" card on Record, the audit report
// (findings, operations inside each action, build diagnostics, rules) and
// audit-to-audit comparison. Every button calls the same observatory.audit.*
// operations an agent calls; the findings and comparison come from the
// shared audit.mjs, so the page, the CLI and Copy for AI agree.
import { auditMarkdown, compareAudits, findingMarkdown, PRESETS, previousAudit, RULES, SEVERITIES } from "./audit.mjs";
import { artifactUrl, control, hasControlKey, loadBaselines, runDetails } from "./data.js";
import { empty, gitText, h, kv, ms, statusPill, table, when } from "./dom.js";
import { copyText } from "./lab.js";
import { makeRedactor } from "./redact.mjs";

let context = { getState: () => ({ runs: [], root: "" }), refresh: async () => {}, go: () => {} };

export function configureAudit(value) {
  context = value;
}

const ACTIVE = new Set(["queued", "starting", "running"]);
const SEVERITY_TEXT = { high: "High", medium: "Medium", low: "Low", info: "Info" };
const VERDICT = {
  regression: ["Regression", "bad"],
  improvement: ["Improvement", "good"],
  "no-meaningful-change": ["No meaningful change", ""],
  "insufficient-evidence": ["Not enough runs to tell", "warn"],
  incompatible: ["Not comparable", "bad"],
};
let generation = 0;

const onRecordPage = () => /^#\/(record)?($|\?)/.test(location.hash) || location.hash === "";

function verdictPill(verdict) {
  const [text, tone] = VERDICT[verdict] ?? [verdict, ""];
  return h("span", { class: `pill ${tone}` }, text);
}

function severityDot(severity) {
  return h("span", { class: `sev sev-${severity}`, title: SEVERITY_TEXT[severity] ?? severity, "aria-label": SEVERITY_TEXT[severity] ?? severity });
}

// ---- the Run an audit card ---------------------------------------------------------

export function auditHero() {
  const card = h("section", { class: "hero audit-hero", "aria-live": "polite" });
  const head = h("div", { class: "hero-head" }, h("h2", {}, "Run an audit"),
    h("p", { class: "muted" }, "One press: Observatory runs the scenarios (and a build, if you ask), then lists what is slow, why it thinks so, and what to look at next."));
  if (!hasControlKey()) {
    card.append(head, h("div", { class: "notice warn" }, h("strong", {}, "This page is read-only."),
      h("p", {}, "Audits run from the Observatory panel in Archivist, from ", h("code", { class: "cmd" }, "npm run observatory:dashboard -- --control"), ", or from a terminal: ",
        h("code", { class: "cmd" }, "npm run observatory -- audit run --preset quick --wait"))));
    return card;
  }
  const mine = ++generation;
  const here = () => mine === generation && onRecordPage();
  let preset = "quick";
  let timer = null;
  let watching = false;
  let presets = null;
  const body = h("div");
  card.append(head, body);

  const presetChips = () => h("div", { class: "chips presets", role: "radiogroup", "aria-label": "Audit preset" }, presets.presets.map((entry) => h("button", {
    type: "button", role: "radio", class: `chip${entry.id === preset ? " on" : ""}`, "aria-checked": entry.id === preset ? "true" : "false",
    title: entry.description,
    onclick: () => {
      preset = entry.id;
      idle();
    },
  }, entry.name)));

  const idle = (last = null, message = "") => {
    const chosen = presets.presets.find((entry) => entry.id === preset);
    const start = h("button", { class: "primary big", type: "button", disabled: !chosen.ready || null, onclick: () => run(chosen) }, "Run audit");
    body.replaceChildren(
      h("div", { class: "hero-controls" }, presetChips(), start),
      h("p", { class: "small preset-text" }, chosen.description, " ",
        h("span", { class: "muted" }, `${chosen.runs} run${chosen.runs === 1 ? "" : "s"}.`),
        chosen.ready ? null : h("span", { class: "verdict-worse" }, ` Can't run yet: ${chosen.blockedBy}.`)),
      ...[message ? h("p", { class: "small verdict-worse", role: "status" }, message) : null, last ? lastAudit(last) : null].filter(Boolean));
  };

  const run = async (chosen) => {
    if (chosen.build && !window.confirm(`${chosen.name} audit: this runs an incremental build of build/frontend-debug first (nothing is cleaned, but it can take minutes, and the binary on disk is replaced; a running Archivist is not restarted). Go ahead?`)) return;
    const response = await control("observatory.audit.run", { preset: chosen.id });
    if (!response.ok) {
      idle(null, response.error.message);
      return;
    }
    watching = true;
    poll(response.result.job.jobId);
  };

  const poll = async (jobId) => {
    clearTimeout(timer);
    if (!here()) return;
    const response = await control("observatory.audit.status", jobId ? { jobId } : {});
    if (!here()) return;
    if (!response.ok) {
      idle(null, response.error.message);
      return;
    }
    const { job, audit } = response.result;
    if (job && ACTIVE.has(job.status)) {
      watching = true;
      body.replaceChildren(progress(job));
      timer = setTimeout(() => poll(job.jobId), 1000);
      return;
    }
    if (watching && job?.auditRunId) {
      // Finished while this page watched: open the report.
      await context.refresh();
      context.go(`#/run/${job.auditRunId}`);
      return;
    }
    idle(audit && job ? { audit, job } : null);
  };

  const progress = (job) => {
    const current = job.progress?.current;
    const done = job.progress?.runsDone ?? 0;
    const planned = (job.progress?.runsPlanned ?? 0) + (job.plan?.build ? 1 : 0);
    const finished = done + (job.plan?.build && job.buildRunId ? 1 : 0);
    const name = PRESETS[job.plan?.audit?.preset]?.name ?? "Audit";
    const bar = h("div", { class: "bar" }, h("span"));
    // Only what the runner reports: runs finished of runs planned.
    bar.firstChild.style.width = `${planned ? Math.min(100, Math.round((finished / planned) * 100)) : 0}%`;
    const cancel = h("button", {
      type: "button",
      onclick: async () => {
        cancel.disabled = true;
        await control("observatory.audit.cancel", { jobId: job.jobId });
      },
    }, job.cancelRequestedAt ? "Stopping…" : "Cancel");
    if (job.cancelRequestedAt) cancel.disabled = true;
    return h("div", { class: "audit-progress" },
      h("div", { class: "row-between" }, h("strong", {}, `${name} audit running`), h("span", { class: "muted small" }, job.elapsedMs == null ? "" : ms(job.elapsedMs))),
      bar,
      h("p", { class: "small" }, current
        ? `${current.phase === "building" ? "Building" : current.scenario ? `${current.scenario}${current.round ? `, round ${current.round}` : ""}: ${current.phase}` : current.phase}${current.actionsDone != null ? ` · ${current.actionsDone} of ${current.actionsPlanned ?? "?"} actions` : ""}`
        : "Starting…",
      h("span", { class: "muted" }, ` · ${finished} of ${planned} runs done`)),
      h("p", { class: "muted small" }, "It runs a separate copy of the app with a throwaway profile; your Archivist isn't touched. You can leave this page."),
      cancel);
  };

  const lastAudit = ({ audit, job }) => {
    const counts = audit.counts ?? {};
    return h("a", { class: "last-audit", href: `#/run/${audit.auditId}` },
      h("span", { class: "muted small" }, `Last audit · ${PRESETS[audit.preset]?.name ?? audit.preset} · ${when(audit.finishedAt ?? job.finishedAt)}`),
      h("span", { class: "counts" }, SEVERITIES.filter((severity) => counts[severity]).map((severity) => h("span", { class: "count" }, severityDot(severity), counts[severity]))),
      audit.topFindings?.[0] ? h("span", { class: "small top-finding" }, audit.topFindings[0].summary) : h("span", { class: "small muted" }, "No rule's threshold was met."));
  };

  (async () => {
    const response = await control("observatory.audit.presets");
    if (!here()) return;
    if (!response.ok) {
      body.replaceChildren(h("div", { class: "notice bad" }, `Audits aren't available: ${response.error.message}`));
      return;
    }
    presets = response.result;
    idle();
    poll(null);
  })();
  return card;
}

// ---- the audit report ---------------------------------------------------------------

async function comparisonFor(run) {
  const { runs } = context.getState();
  const baseline = previousAudit(runs, run);
  if (!baseline) return null;
  const files = await loadBaselines().catch(() => null);
  if (!files?.budgets) return null;
  return compareAudits({ audit: run, baseline, runs, budgets: files.budgets });
}

function copyAudit(run, summary, comparison) {
  const status = h("span", { class: "muted small", role: "status" });
  const preview = h("details", { class: "report-preview", hidden: true });
  const button = h("button", {
    class: "primary", type: "button",
    onclick: async () => {
      const text = auditMarkdown({ auditRun: run, summary, comparison, redact: makeRedactor({ root: context.getState().root || null }) });
      const copied = await copyText(text);
      status.textContent = copied ? `Copied ${Math.round(text.length / 100) / 10} KB of Markdown: findings, rules, evidence ids and the comparison. Paths are redacted.` : "Couldn't reach the clipboard; the report is below.";
      preview.replaceChildren(h("summary", {}, "Show the report"), h("pre", { class: "log report" }, text));
      preview.hidden = false;
      if (!copied) preview.open = true;
    },
  }, "Copy for AI");
  return h("div", { class: "copy-ai" }, h("div", { class: "row" }, button, status), preview);
}

function exportAudit(runId) {
  if (!hasControlKey()) return h("p", { class: "muted small" }, "Evidence bundle: ", h("code", { class: "cmd" }, `npm run observatory -- audit export ${runId}`));
  const status = h("span", { class: "small", role: "status" });
  const button = h("button", {
    type: "button",
    onclick: async () => {
      button.disabled = true;
      status.textContent = "Exporting…";
      const response = await control("observatory.audit.export", { auditId: runId });
      button.disabled = false;
      if (!response.ok) {
        status.replaceChildren(h("span", { class: "verdict-worse" }, response.error.message));
        return;
      }
      status.replaceChildren(`Saved ${response.result.manifest.files.length + 1} files (redacted): `, h("code", { class: "cmd" }, response.result.zip), " ",
        h("button", { type: "button", onclick: () => copyText(response.result.zip) }, "Copy path"));
    },
  }, "Export evidence");
  return h("div", { class: "export" }, h("div", { class: "row" }, button, status));
}

function tile(label, value, sub, tone = "") {
  return h("div", { class: `tile ${tone}` }, h("span", { class: "tile-label" }, label), h("span", { class: "tile-value" }, value), sub ? h("span", { class: "tile-sub" }, sub) : null);
}

let currentAuditId = null;

export async function auditView(run, summary) {
  const audit = run.audit;
  currentAuditId = run.runId;
  const comparison = await comparisonFor(run);
  const parts = [];
  const counts = audit.counts;
  const passed = audit.children.filter((child) => child.status === "passed" || child.status === "success").length;
  parts.push(h("div", { class: "action-bar" },
    comparison ? h("a", { class: "button", href: `#/compare?a=${comparison.baselineAuditId}&b=${run.runId}` }, "Compare with previous audit") : null,
    h("div", { class: "action-ai" }, copyAudit(run, summary, comparison), exportAudit(run.runId))));
  parts.push(h("div", { class: "tiles" },
    tile("High", counts.high, "findings", counts.high ? "bad" : ""),
    tile("Medium", counts.medium, "findings"),
    tile("Low and info", counts.low + counts.info, "findings"),
    tile("Runs", `${passed}/${audit.children.length}`, "passed"),
    ...Object.entries(audit.highlights?.scenarios ?? {}).map(([id, entry]) => {
      const ai = audit.highlights?.ai?.[id];
      if (ai) return tile(entry.name, ms(ai.medians?.firstVisibleTextMs ?? ai.medians?.turnMs), ai.medians?.firstVisibleTextMs != null ? "first text shown" : "AI turn, median");
      return tile(entry.name, ms(id === "startup" && entry.firstFrameMedianMs != null ? entry.firstFrameMedianMs : entry.settledMedianMs), id === "startup" ? "first frame" : "median settled");
    }),
    audit.highlights?.build ? tile("Build", ms(audit.highlights.build.totalMs), audit.highlights.build.classification ?? "") : null));
  parts.push(comparisonStrip(run, comparison));
  parts.push(findingsSection(audit.findings, run));
  const uiRuns = audit.children.filter((child) => child.kind === "ui-scenario");
  if (uiRuns.length) parts.push(await inspectorSection(uiRuns));
  const build = audit.children.find((child) => child.kind === "build");
  if (build) parts.push(buildSection(build.runId));
  parts.push(h("details", { class: "fold plain" }, h("summary", {}, `Runs in this audit (${audit.children.length})`),
    table(["Run", "Kind", "Scenario", "Round", "Status"], audit.children.map((child) => ({
      cells: [h("a", { class: "mono", href: `#/run/${child.runId}` }, child.runId), child.kind, child.scenario ?? "—", child.round ?? "—", statusPill(child.status)],
    })))));
  parts.push(rulesFold(), h("details", { class: "fold plain" }, h("summary", {}, "What this audit can't see"),
    h("ul", { class: "small" }, (summary.blindSpots ?? []).map((line) => h("li", {}, line)))));
  parts.push(h("details", { class: "fold plain" }, h("summary", {}, "About this audit"), kv([
    ["Preset", `${PRESETS[audit.preset]?.name ?? audit.preset} (${audit.preset})`], ["Plan", `${audit.plan?.build ? "incremental build, then " : ""}${audit.plan?.scenarios?.join(", ") || "no scenarios"}${audit.plan?.scenarios?.length ? ` × ${audit.plan.repeat}` : ""}`],
    ["Took", ms(audit.durationMs)], ["Source", gitText(run)], ["Job", h("span", { class: "mono" }, audit.jobId ?? "—")], ["Rules", `v${audit.rulesVersion}`],
    ["Agents", h("code", { class: "cmd" }, `npm run observatory -- audit findings ${run.runId} --json`)]])));
  return parts;
}

function comparisonStrip(run, comparison) {
  if (!comparison) {
    return h("div", { class: "notice" }, h("strong", {}, "First audit with this preset. "),
      "Run it again after a change to compare; with three or more rounds per scenario the comparison can call a regression.");
  }
  if (comparison.status === "incompatible") return h("div", { class: "notice warn" }, h("strong", {}, "Not comparable with the previous audit: "), comparison.blockers.join("; "));
  const notable = comparison.categories.filter((category) => category.verdict !== "no-meaningful-change");
  return h("div", { class: `notice ${comparison.verdict === "regression" ? "bad" : comparison.verdict === "incompatible" ? "warn" : ""}` },
    h("div", { class: "row-between" }, h("span", {}, h("strong", {}, "Against the previous audit: "), verdictPill(comparison.verdict)),
      h("a", { href: `#/compare?a=${comparison.baselineAuditId}&b=${run.runId}` }, "Details")),
    notable.length ? h("ul", {}, notable.map((category) => h("li", {}, `${category.label}: ${(VERDICT[category.verdict] ?? [category.verdict])[0].toLowerCase()}${category.reasons?.length ? ` (${category.reasons[0]})` : ""}`))) : null,
    comparison.findings?.appeared?.length ? h("p", { class: "small" }, `${comparison.findings.appeared.length} new finding${comparison.findings.appeared.length === 1 ? "" : "s"}, ${comparison.findings.resolved.length} gone.`) : null);
}

// ---- findings, like a Problems panel -------------------------------------------------

function findingsSection(findings) {
  let severity = "all";
  let subsystem = "";
  let query = "";
  const list = h("div", { class: "findings" });
  const subsystems = [...new Set(findings.map((finding) => finding.subsystem))].sort();
  const chips = h("div", { class: "chips", role: "tablist", "aria-label": "Severity" });
  const render = () => {
    chips.replaceChildren(...["all", ...SEVERITIES].map((value) => {
      const n = value === "all" ? findings.length : findings.filter((finding) => finding.severity === value).length;
      if (value !== "all" && !n) return null;
      return h("button", { type: "button", class: `chip${severity === value ? " on" : ""}`, role: "tab", "aria-selected": severity === value ? "true" : "false", onclick: () => { severity = value; render(); } },
        value === "all" ? "All" : [severityDot(value), SEVERITY_TEXT[value]], h("span", { class: "chip-n" }, n));
    }).filter(Boolean));
    const q = query.toLowerCase();
    const shown = findings.filter((finding) => (severity === "all" || finding.severity === severity) && (!subsystem || finding.subsystem === subsystem)
      && (!q || `${finding.title} ${finding.summary} ${finding.rule} ${finding.subsystem}`.toLowerCase().includes(q)));
    list.replaceChildren(...(shown.length ? shown.map((finding) => findingRow(finding)) : [empty(findings.length ? "No findings match." : "No rule's threshold was met.", findings.length ? null : "That's not proof nothing is slow: read what the audit can't see, below.")]));
  };
  const select = h("select", { "aria-label": "Subsystem", onchange: (event) => { subsystem = event.target.value; render(); } },
    h("option", { value: "" }, "Every subsystem"), subsystems.map((name) => h("option", { value: name }, name)));
  const search = h("input", { type: "search", placeholder: "Filter findings", "aria-label": "Filter findings", class: "search", oninput: (event) => { query = event.target.value; render(); } });
  render();
  return h("section", { class: "card findings-card", id: "findings" },
    h("div", { class: "page-head" }, h("h3", {}, `Findings `, h("span", { class: "muted small" }, findings.length)), h("span", { class: "muted small" }, "most severe first")),
    h("div", { class: "toolbar" }, chips, h("div", { class: "toolbar-end" }, subsystems.length > 1 ? select : null, findings.length > 4 ? search : null)),
    list);
}

function findingRow(finding) {
  const rule = RULES.find((entry) => entry.id === finding.rule);
  const inspect = finding.evidence?.find((item) => item.step != null || item.operation);
  return h("details", { class: `finding finding-${finding.severity}` },
    h("summary", {},
      severityDot(finding.severity),
      h("span", { class: "finding-main" }, h("span", { class: "finding-title" }, finding.title), h("span", { class: "finding-text" }, finding.summary)),
      h("span", { class: "finding-meta" }, h("span", { class: "tag" }, finding.subsystem), h("span", { class: `conf conf-${finding.confidence?.level}` }, finding.confidence?.text ?? ""))),
    h("div", { class: "finding-body" },
      kv([
        ["Impact", finding.impact?.text ?? "—"],
        ["Next", finding.nextStep],
        ["Rule", [h("code", {}, finding.rule), ` ${rule?.when ?? ""}`]],
        ["Basis", rule?.basis ?? null],
      ]),
      finding.evidence?.length ? h("div", { class: "evidence" }, h("span", { class: "muted small" }, "Evidence"),
        h("ul", {}, finding.evidence.map((item) => h("li", { class: "small" },
          h("a", { class: "mono", href: `#/run/${item.runId}` }, item.runId.split("-").slice(-2).join("-")), " ", item.text)))) : null,
      h("div", { class: "row" },
        inspect ? h("button", { type: "button", onclick: () => openInspector(inspect.runId, inspect.step) }, finding.rule.startsWith("ai.") ? "Show the turn" : "Show the operations") : null,
        copyFinding(finding))));
}

function copyFinding(finding) {
  const status = h("span", { class: "muted small", role: "status" });
  return h("span", { class: "row" }, h("button", {
    type: "button",
    onclick: async () => {
      const text = findingMarkdown(finding, { auditId: currentAuditId, redact: makeRedactor({ root: context.getState().root || null }) });
      status.textContent = (await copyText(text)) ? "Copied this finding as Markdown." : "Couldn't reach the clipboard.";
    },
  }, "Copy this finding"), status);
}

// ---- operations inside each action (an Outliner for one run) -------------------------

let inspector = null;

function openInspector(runId, step) {
  inspector?.show(runId, step);
  document.getElementById("inspector")?.scrollIntoView({ behavior: "smooth", block: "start" });
}

const operationsCache = new Map();
async function loadOperations(runId) {
  if (operationsCache.has(runId)) return operationsCache.get(runId);
  if (!runDetails(runId).artifacts.includes("operations")) return null;
  const response = await fetch(artifactUrl(runId, "operations"), { cache: "no-store" });
  if (!response.ok) throw new Error(`ui-operations.json: HTTP ${response.status}`);
  const value = await response.json();
  operationsCache.set(runId, value);
  return value;
}

async function inspectorSection(uiRuns) {
  const section = h("section", { class: "card", id: "inspector" });
  const runSelect = h("select", { "aria-label": "Run" }, uiRuns.map((child) => h("option", { value: child.runId }, `${child.scenario}${child.round ? `, round ${child.round}` : ""} · ${child.status}`)));
  const phaseSelect = h("select", { "aria-label": "Phase" }, ["measured", "warmup", "setup", "all"].map((value) => h("option", { value }, value === "all" ? "every phase" : value)));
  const strip = h("div", { class: "timeline-strip" });
  const actions = h("div");
  const tree = h("div", { class: "op-tree-wrap" });
  let data = null;
  let selected = null;
  let wantStep = null;

  const showAction = (action) => {
    selected = action?.index ?? null;
    for (const node of strip.children) node.classList.toggle("on", Number(node.dataset.index) === selected);
    for (const row of actions.querySelectorAll("tr[data-index]")) row.classList.toggle("on", Number(row.dataset.index) === selected);
    if (!action) {
      tree.replaceChildren(h("p", { class: "muted small" }, "Pick an action to see what ran inside it."));
      return;
    }
    const turns = (data.trace?.ai?.turns ?? []).filter((turn) => turn.action === action.index);
    tree.replaceChildren(
      h("div", { class: "row-between" }, h("strong", {}, `#${action.index} ${action.label ?? action.op} (${action.phase}${action.iteration ? `, iteration ${action.iteration}` : ""})`), h("span", { class: "muted small" }, `${ms(action.durationMs)} · settled ${ms(action.settledMs)}`)),
      ...turns.map((turn) => turnTimeline(turn)),
      turns.length ? h("h4", {}, "Every operation inside the action, by time") : null,
      opTree(action),
      kv([
        ["Unattributed", action.window
          ? `${ms(action.unattributedMs)} of the ${ms(action.window.ms)} response window (${action.window.basis}): time inside no instrumented operation`
          : `${ms(action.unattributedMs)} of ${ms(action.durationMs)}: time inside no instrumented operation (older run: measured against the whole action)`],
        ["Timings", action.timings ? [
          `dispatch ${ms(action.timings.dispatchMs)}`, `state ${ms(action.timings.stateMs)}`, `last frame ${ms(action.timings.lastFrameMs)}`,
          `settled ${ms(action.timings.settledMs)}`, `quiet wait ${ms(action.timings.quietWaitMs)}`, `elapsed ${ms(action.timings.elapsedMs)}`,
        ].join(" · ") : null],
        ["After the window", action.afterWindowMs ? `${ms(action.afterWindowMs)} of span time ran after the response window` : null],
        ["Kind", action.kind === "observation" ? "observation (watches the app; not a user interaction)" : null],
        ["Stalls", action.stalls?.length ? action.stalls.map((stall) => `${ms(stall.lateMs)} late at +${ms(stall.atMs)}`).join(", ") : "none"],
        ["Markers", action.markers?.length ? action.markers.map((marker) => `${marker.name} +${ms(marker.atMs)}`).join(", ") : null],
        ["Still running after", action.outlived?.length ? action.outlived.map((op) => `${op.name} (+${ms(op.endsAfterMs)})`).join(", ") : null],
      ]));
  };

  const render = () => {
    if (!data) return;
    const phase = phaseSelect.value;
    const shown = (data.actions ?? []).filter((action) => phase === "all" || action.phase === phase);
    // The strip: every shown action as a block, its width its duration.
    const total = shown.reduce((sum, action) => sum + Math.max(action.durationMs ?? 0, 1), 0) || 1;
    strip.replaceChildren(...shown.map((action) => {
      const block = h("button", { type: "button", class: `tl-block ph-${action.phase}${action.stalls?.length ? " stalled" : ""}`, "data-index": action.index, title: `#${action.index} ${action.label ?? action.op}: ${ms(action.durationMs)}${action.stalls?.length ? `, ${action.stalls.length} stall(s)` : ""}`, onclick: () => showAction(action) });
      block.style.flexGrow = String(Math.max(action.durationMs ?? 0, 1) / total * 100);
      const covered = h("span", { class: "tl-covered" });
      const windowMs = action.window?.ms ?? action.durationMs;
      covered.style.width = `${action.durationMs ? Math.min(100, Math.round(((action.coveredMs ?? 0) / action.durationMs) * 100)) : 0}%`;
      covered.title = `${ms(action.coveredMs)} of a ${ms(windowMs)} response window in operations`;
      block.append(covered);
      return block;
    }));
    actions.replaceChildren(shown.length ? table(["#", "Step", { label: "Duration", num: true }, { label: "Window", num: true }, { label: "In operations", num: true }, { label: "Unattributed", num: true }, { label: "Stalls", num: true }, "Slowest inside"],
      shown.slice(0, 300).map((action) => ({
        source: action,
        cells: [action.index, action.label ?? action.op, ms(action.durationMs), ms(action.window?.ms ?? action.durationMs), ms(action.coveredMs), ms(action.unattributedMs), action.stalls?.length || "", slowestName(action.operations)],
      })), { onRow: showAction }) : empty("No actions in this phase."));
    for (const row of actions.querySelectorAll("tbody tr")) {
      const index = Number(row.firstChild.textContent);
      row.dataset.index = String(index);
    }
    const pick = wantStep != null ? shown.find((action) => action.step === wantStep) : shown.find((action) => action.index === selected);
    wantStep = null;
    showAction(pick ?? [...shown].sort((a, b) => (b.durationMs ?? 0) - (a.durationMs ?? 0))[0] ?? null);
  };

  const load = async () => {
    data = null;
    strip.replaceChildren();
    tree.replaceChildren();
    try {
      data = await loadOperations(runSelect.value);
    } catch (error) {
      actions.replaceChildren(empty(error.message));
      return;
    }
    if (!data) {
      actions.replaceChildren(empty("This run has no operations.", "The app that ran it didn't report LiveEvents to the UI Lab; rebuild it from this branch."));
      return;
    }
    render();
  };
  runSelect.addEventListener("change", load);
  phaseSelect.addEventListener("change", render);
  inspector = {
    show: async (runId, step) => {
      if (runSelect.value !== runId) {
        runSelect.value = runId;
        wantStep = step ?? null;
        phaseSelect.value = "measured";
        await load();
      } else {
        wantStep = step ?? null;
        render();
      }
    },
  };
  section.append(
    h("div", { class: "page-head" }, h("h3", {}, "Operations inside each action"), h("span", { class: "muted small" }, "nested by time: inside, not necessarily called by")),
    h("div", { class: "filters" }, h("label", {}, "Run", runSelect), h("label", {}, "Phase", phaseSelect)),
    h("div", { class: "timeline-legend muted small" }, "Each block is an action, as wide as it took; the darker part is time inside instrumented operations, red edges are stalls."),
    strip, h("div", { class: "inspector-grid" }, h("div", { class: "inspector-list" }, actions), h("div", { class: "inspector-tree" }, tree)));
  await load();
  return section;
}

// ---- one AI turn as tracks, one lane per thread (by cause, not by time) ----------------

const LANE_ORDER = ["main", "ArchivistAI", "ArchivistCoreWriter"];
const LANE_TEXT = { main: "UI thread", ArchivistAI: "AI thread", ArchivistCoreWriter: "Database writer" };
const MARK_TEXT = {
  "ai.first-byte": "first byte", "ai.first-delta": "first model text",
  "chat.ai-first-delta-received": "text reached the chat", "chat.ai-first-text-shown": "first text shown",
};

function turnBars(turn) {
  const bars = [];
  const marks = [];
  const walk = (node, depthByThread, parentThread) => {
    const thread = node.thread ?? parentThread ?? "unknown";
    const depth = depthByThread[thread] ?? 0;
    bars.push({ ...node, thread, depth });
    for (const marker of node.markers ?? []) {
      // A first-byte marker on a refused attempt is not the answer's.
      if (marker.name === "ai.first-byte" && node.ok === false) continue;
      marks.push({ ...marker, thread });
    }
    for (const child of node.children ?? []) walk(child, { ...depthByThread, [thread]: depth + 1 }, thread);
  };
  walk(turn.call ?? turn.tree, {}, turn.thread);
  if (turn.presentationSpan) bars.push({ ...turn.presentationSpan, depth: 0, children: [], fields: {} });
  for (const marker of turn.presentationMarks ?? []) marks.push(marker);
  return { bars, marks };
}

function turnTimeline(turn) {
  const { bars, marks } = turnBars(turn);
  const starts = bars.map((bar) => bar.startMs ?? 0);
  const ends = bars.map((bar) => (bar.startMs ?? 0) + (bar.durationMs ?? 0));
  const origin = Math.min(0, ...starts);
  const span = Math.max(1, Math.max(...ends) - origin);
  const at = (value) => `${Math.max(0, Math.min(100, ((value - origin) / span) * 100))}%`;
  const threads = [...new Set(bars.map((bar) => bar.thread))].sort((a, b) => (LANE_ORDER.indexOf(a) + 1 || 99) - (LANE_ORDER.indexOf(b) + 1 || 99));

  const ruler = h("div", { class: "tt-ruler" });
  const step = [1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000].find((value) => span / value <= 8) ?? 20000;
  for (let tick = Math.ceil(origin / step) * step; tick <= origin + span; tick += step) {
    const label = h("span", { class: "tt-tick" }, ms(tick));
    label.style.left = at(tick);
    ruler.append(label);
  }
  const lanes = threads.map((thread) => {
    const mine = bars.filter((bar) => bar.thread === thread);
    const rows = Math.max(...mine.map((bar) => bar.depth)) + 1;
    const track = h("div", { class: "tt-track" });
    track.style.height = `${rows * 18 + 2}px`;
    for (const bar of mine) {
      const category = bar.name.split(".")[0];
      const fields = Object.entries(bar.fields ?? {}).filter(([key]) => key !== "runId").map(([key, value]) => `${key} ${value}`).join(", ");
      const title = `${bar.name}: ${ms(bar.durationMs)}${bar.selfMs != null && bar.children?.length ? `, self ${ms(bar.selfMs)}` : ""} at +${ms((bar.startMs ?? 0))}${fields ? `\n${fields}` : ""}`;
      const node = h("div", { class: `tt-bar cat-${category}${bar.ok === false ? " failed" : ""}${bar.name.endsWith("wait") ? " waiting" : ""}`, title, role: "img", "aria-label": title },
        h("span", {}, bar.name.replace(/^ai\./, "")));
      node.style.left = at(bar.startMs ?? 0);
      node.style.width = `${Math.max(0.25, ((bar.durationMs ?? 0) / span) * 100)}%`;
      node.style.top = `${bar.depth * 18}px`;
      track.append(node);
    }
    for (const mark of marks.filter((entry) => entry.thread === thread)) {
      const line = h("div", { class: "tt-mark", title: `${MARK_TEXT[mark.name] ?? mark.name} at +${ms(mark.atMs)}` });
      line.style.left = at(mark.atMs);
      track.append(line);
    }
    return h("div", { class: "tt-lane" }, h("div", { class: "tt-label small" }, LANE_TEXT[thread] ?? thread), track);
  });
  const firsts = [
    ["Queued", turn.queueWaitMs], ["Prepared", turn.prepare?.durationMs], ["First byte", turn.firstNetworkByteMs],
    ["First model text", turn.firstModelDeltaMs], ["First text shown", turn.firstVisibleTextMs], ["Saved", turn.persistMs],
  ];
  return h("div", { class: "turn-timeline" },
    h("div", { class: "row-between" },
      h("strong", {}, "AI turn ", h("span", { class: `pill ${turn.outcome === "completed" ? "good" : turn.outcome === "cancelled" ? "" : "bad"}` }, turn.outcome ?? "?")),
      h("span", { class: "muted small" }, `${ms(turn.durationMs)} · ${turn.rounds.length} provider round${turn.rounds.length === 1 ? "" : "s"} · ${turn.tools.length} tool call${turn.tools.length === 1 ? "" : "s"}${turn.tokens?.input != null ? ` · ${turn.tokens.input} in / ${turn.tokens.output} out tokens` : ""}`)),
    h("div", { class: "tt-firsts small" }, firsts.map(([label, value]) => h("span", {}, h("span", { class: "muted" }, `${label} `), value == null ? "not measured" : ms(value)))),
    h("div", { class: "tt-grid" }, h("div", { class: "tt-lane" }, h("div", { class: "tt-label" }), ruler), lanes),
    h("p", { class: "muted small" }, "Lanes are threads; a bar sits under the span that caused it. Times are from the start of the run; ticks mark the first byte, the first model text and when the chat showed it. ",
      turn.unattributedMs ? `${ms(turn.unattributedMs)} of the run is inside no stage. ` : "",
      turn.missing?.length ? `Not measured: ${turn.missing.join("; ")}.` : ""));
}

function slowestName(nodes = []) {
  let best = null;
  const walk = (list) => list.forEach((node) => {
    if (!best || node.durationMs > best.durationMs) best = node;
    if (node.children) walk(node.children);
  });
  walk(nodes);
  return best ? `${best.name} ${ms(best.durationMs)}` : "—";
}

function opTree(action) {
  const total = Math.max(action.durationMs ?? 0, 1);
  const row = (node, depth) => {
    const bar = h("span", { class: "op-bar" }, h("span", { class: "op-span" }));
    const span = bar.firstChild;
    span.style.left = `${Math.max(0, Math.min(100, (node.startMs / total) * 100))}%`;
    span.style.width = `${Math.max(0.5, Math.min(100, (node.durationMs / total) * 100))}%`;
    const fields = node.fields ? Object.entries(node.fields).map(([key, value]) => `${key} ${value}`).join(", ") : "";
    const line = h("div", { class: `op-row${node.ok === false ? " failed" : ""}` },
      h("span", { class: "op-name" }, node.name, fields ? h("span", { class: "muted small" }, ` ${fields}`) : null),
      bar,
      h("span", { class: "op-num" }, ms(node.durationMs)),
      h("span", { class: "op-num muted" }, node.children ? `self ${ms(node.selfMs)}` : ""));
    line.firstChild.style.paddingLeft = `${depth * 14}px`;
    if (!node.children) return line;
    return h("details", { class: "op-node", open: true }, h("summary", {}, line), node.children.map((child) => row(child, depth + 1)));
  };
  if (!action.operations?.length) return h("p", { class: "muted small" }, "No instrumented operation ran in this action.");
  return h("div", { class: "op-tree", role: "tree" }, h("div", { class: "op-row op-head muted small" }, h("span", {}, "Operation"), h("span", {}, "When, within the action"), h("span", { class: "op-num" }, "Took"), h("span", { class: "op-num" }, "")),
    action.operations.map((node) => row(node, 0)));
}

// ---- build diagnostics -----------------------------------------------------------------

function buildSection(runId) {
  const { summary } = runDetails(runId);
  const section = h("section", { class: "card", id: "build" }, h("div", { class: "page-head" }, h("h3", {}, "Build"), h("a", { href: `#/run/${runId}`, class: "small" }, "The build run")));
  const evidence = summary?.evidence;
  if (!evidence?.timing) {
    section.append(empty(summary ? `No job evidence (${summary.status}).` : "The build run can't be read."));
    return section;
  }
  const t = evidence.timing;
  section.append(h("div", { class: "tiles" },
    tile("Jobs", evidence.jobs, `${evidence.byCategory?.compile?.jobs ?? 0} compiles`),
    tile("Wall time", ms(t.wallMs), "first job start to last job end"),
    tile("Summed", ms(t.summedJobMs), `peak ${t.peakParallelJobs} at once, average ${t.averageParallelJobs ?? "?"}`),
    evidence.criticalPath?.available ? tile("Longest chain", ms(evidence.criticalPath.lengthMs), `${evidence.criticalPath.jobs.length} jobs (approximation)`) : null,
    evidence.compilerCache?.configured ? tile(evidence.compilerCache.tool, evidence.compilerCache.observed ? `${Math.round(evidence.compilerCache.observed.hitRate * 100)}%` : "—", "cache hit rate") : null));
  const causes = evidence.rebuild?.causes ?? [];
  section.append(h("h4", {}, "Why it rebuilt"), causes.length
    ? table(["Changed input", "Kind", { label: "Outputs", num: true }, { label: "Compiles", num: true }, { label: "Included by", num: true }],
      causes.slice(0, 15).map((cause) => ({ cells: [h("span", { class: "mono path" }, cause.input), cause.inputKind, cause.outputs, cause.compiles, cause.includedBy?.objects ?? "—"] })))
    : h("p", { class: "muted small" }, evidence.rebuild ? `Nothing changed on disk explains it${evidence.rebuild.commandLineChanged?.count ? `; ${evidence.rebuild.commandLineChanged.count} command lines changed` : ""}.` : "Rebuild causes weren't recorded."));
  if (evidence.slowestCompiles?.length) {
    section.append(h("h4", {}, "Slowest compiles"), table(["File", { label: "Took", num: true }], evidence.slowestCompiles.slice(0, 10).map((job) => ({ cells: [h("span", { class: "mono path" }, job.source ?? job.output), ms(job.durationMs)] }))));
  }
  if (evidence.criticalPath?.available) {
    section.append(h("details", { class: "fold plain" }, h("summary", {}, "The longest dependency chain"),
      h("p", { class: "muted small" }, evidence.criticalPath.basis),
      h("ol", { class: "chain small" }, evidence.criticalPath.jobs.map((job) => h("li", {}, h("span", { class: "mono path" }, job.source ?? job.output), ` ${job.category} · ${ms(job.durationMs)}`)))));
  }
  if (evidence.headers?.mostIncluded?.length) {
    section.append(h("details", { class: "fold plain" }, h("summary", {}, "Most included headers"),
      table(["Header", { label: "Objects", num: true }], evidence.headers.mostIncluded.slice(0, 15).map((entry) => ({ cells: [h("span", { class: "mono path" }, entry.header), entry.objects] })))));
  }
  if (summary.timeTrace) {
    section.append(h("details", { class: "fold plain" }, h("summary", {}, "Compiler time trace"),
      h("p", { class: "muted small" }, summary.timeTrace.basis),
      table(["Header", { label: "Total", num: true }, { label: "Units", num: true }], summary.timeTrace.headers.map((entry) => ({ cells: [h("span", { class: "mono path" }, entry.name), ms(entry.totalMs), entry.units] })))));
  } else {
    section.append(h("p", { class: "muted small" }, "Which headers and templates cost the most: ", h("code", { class: "cmd" }, "npm run observatory -- build --time-trace"), " (a separate build folder; Clang only)."));
  }
  if (evidence.notes?.length) section.append(h("ul", { class: "muted small" }, evidence.notes.map((note) => h("li", {}, note))));
  return section;
}

function rulesFold() {
  return h("details", { class: "fold plain" }, h("summary", {}, `The rules (${RULES.length})`),
    h("p", { class: "muted small" }, "Every finding comes from one of these. Thresholds are rules of thumb, written down so you can disagree with them."),
    table(["Rule", "Subsystem", "Severity", { label: "When", wrap: true }, { label: "Measured with", wrap: true }],
      RULES.map((rule) => ({ cells: [h("code", {}, rule.id), rule.subsystem, rule.severity, rule.when, rule.basis] }))));
}

// ---- Compare: audit against audit --------------------------------------------------------

export async function auditComparison(base, candidate) {
  const files = await loadBaselines().catch(() => null);
  if (!files?.budgets) return [h("div", { class: "notice bad" }, "The budgets can't be read, so the audits can't be compared.")];
  const result = compareAudits({ audit: candidate, baseline: base, runs: context.getState().runs, budgets: files.budgets });
  if (result.status === "incompatible") {
    return [h("div", { class: "notice bad" }, h("strong", {}, "These audits can't be compared"), h("ul", {}, result.blockers.map((line) => h("li", {}, line))))];
  }
  const parts = [h("section", { class: "card" },
    h("div", { class: "row-between" }, h("h3", {}, "By scenario and build"), verdictPill(result.verdict)),
    table(["", "Verdict", { label: "Why", wrap: true }, { label: "Notable", wrap: true }], result.categories.map((category) => ({
      cells: [category.label, verdictPill(category.verdict), category.reasons?.join("; ") ?? "",
        [...category.metrics, ...category.steps].filter((metric) => metric.verdict === "regression" || metric.verdict === "improvement" || metric.overBudget)
          .slice(0, 4).map((metric) => h("div", { class: "small" }, `${metric.label}: ${ms(metric.baseline.median)} → ${ms(metric.candidate.median)} (${metric.deltaPercent > 0 ? "+" : ""}${metric.deltaPercent}%)${metric.verdict === "regression" || metric.verdict === "improvement" ? "" : ", over budget but unconfirmed"}`))],
    }))),
    h("p", { class: "muted small" }, "Per scenario: the median of each audit's runs. A regression needs at least three runs on each side, a change past the budget and no overlap between the runs; otherwise it is reported as not enough to tell."))];
  const { appeared, resolved, persisting } = result.findings;
  const list = (title, findings, after = false) => h("div", { class: "find-col" }, h("h4", {}, `${title} `, h("span", { class: "muted small" }, findings.length)),
    findings.length ? h("ul", { class: "small" }, findings.map((finding) => h("li", {}, severityDot(finding.severity), " ", finding.summary, after && finding.before ? h("span", { class: "muted" }, ` (was ${finding.before.text})`) : null))) : h("p", { class: "muted small" }, "None."));
  parts.push(h("section", { class: "card" }, h("h3", {}, "Findings"),
    h("p", { class: "muted small" }, "New and gone mean a rule's threshold was or wasn't met in each audit; a finding close to its threshold can flip on noise."),
    h("div", { class: "find-cols" }, list("New", appeared), list("Gone", resolved), list("Still there", persisting, true))));
  return parts;
}

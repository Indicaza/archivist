// The Live page: record what the app does while you use it (Start, Stop,
// Add marker), then read the recording as a timeline. The controls call the
// same live operations an agent calls; the analysis is live.mjs, the same
// code the CLI and the Copy for AI report use.
import { artifactUrl, hasControlKey, live, liveArtifacts, liveIsInApp, runDetails } from "./data.js";
import { empty, h, kindLabel, kv, legend, ms, statusPill, table, when } from "./dom.js";
import { copyForAiButton, exportButton } from "./lab.js";
import {
  at, bytes, compareLive, eventName, LONG_GAP_MS, liveModel, operationSummary, rangeDetail, spikesWithContext, spikeTypeLabel, summarySentence, windowLabel,
} from "./live.mjs";

const NS = "http://www.w3.org/2000/svg";
const DURATIONS = [15, 30, 60, 120];
const CAP_MS = 250;

let context = { getState: () => ({ runs: [] }), refresh: async () => {}, go: (hash) => { location.hash = hash; } };
let generation = 0;

export function configureLive(value) {
  context = { ...context, ...value };
}

function svg(tag, attrs = {}, text) {
  const node = document.createElementNS(NS, tag);
  for (const [name, value] of Object.entries(attrs)) if (value != null) node.setAttribute(name, String(value));
  if (text !== undefined) node.textContent = text;
  return node;
}

function clock(msValue) {
  const seconds = Math.max(0, Math.floor((msValue ?? 0) / 1000));
  return `${Math.floor(seconds / 60)}:${String(seconds % 60).padStart(2, "0")}`;
}

// ---- the recorder card ---------------------------------------------------------

function readOnlyNotice() {
  return h("div", { class: "notice warn" }, h("strong", {}, "Recording needs controls."),
    h("p", {}, "Inside Archivist, open the Observatory panel and go to Live. From a terminal (with \"Let agents record\" on in the app): ",
      h("code", { class: "cmd" }, "npm run observatory -- live record --duration 30")),
    h("p", { class: "muted small" }, "Saved recordings are listed below either way."));
}

function recorderCard() {
  const card = h("section", { class: "card live-recorder", "aria-live": "polite" });
  if (!hasControlKey()) {
    card.append(h("h3", {}, "Recorder"), readOnlyNotice());
    return card;
  }
  const mine = ++generation;
  const onPage = () => mine === generation && location.hash.startsWith("#/live");
  let timer = null;
  let previous = null;
  let note = "";

  const duration = h("select", { "aria-label": "Recording length" }, DURATIONS.map((seconds) => h("option", { value: seconds, selected: seconds === 30 }, `${seconds} s`)));
  const label = h("input", { type: "text", maxlength: 64, placeholder: "What are you about to do? (optional)", "aria-label": "Recording name", class: "wide" });
  const message = h("p", { class: "small", role: "status" });

  const act = async (operation, input) => {
    message.textContent = "";
    const response = await live(operation, input);
    if (!response.ok) message.replaceChildren(h("span", { class: "verdict-worse" }, response.error.message));
    await update();
    return response;
  };

  const idleView = (status) => [
    h("div", { class: "filters" },
      h("label", {}, "Length", duration),
      h("label", { class: "grow" }, "Name", label),
      h("button", { class: "primary", type: "button", onclick: () => act("start", { durationSeconds: Number(duration.value), ...(label.value.trim() ? { label: label.value.trim() } : {}) }) }, "Start recording")),
    h("p", { class: "muted small" }, "Records frame swaps of every Archivist window, how late the UI thread answers, CPU and memory, and the sidebar, workspace and chat operations it instruments. No content, typing or clipboard. It stops by itself at the length you pick."),
    status.lastRun?.runId ? h("p", { class: "small" }, "Last recording: ", h("a", { href: `#/live/${status.lastRun.runId}`, class: "mono" }, status.lastRun.runId), " ", statusPill(status.lastRun.status ?? "completed")) : null,
  ];

  const noteInput = h("input", { type: "text", maxlength: 64, placeholder: "Note (e.g. felt slow here)", "aria-label": "Marker note", class: "wide" });
  noteInput.addEventListener("input", () => { note = noteInput.value; });
  const recordingView = (status) => {
    const total = (status.durationSeconds ?? 30) * 1000;
    const bar = h("div", { class: "bar" }, h("span"));
    bar.firstChild.style.width = `${Math.min(100, Math.round(((status.elapsedMs ?? 0) / total) * 100))}%`;
    noteInput.value = note;
    return [
      h("div", { class: "live-indicator" }, h("span", { class: "dot", "aria-hidden": "true" }), h("strong", {}, "Recording"),
        h("span", { class: "mono" }, `${clock(status.elapsedMs)} / ${clock(total)}`),
        status.label ? h("span", { class: "muted" }, `"${status.label}"`) : null,
        h("span", { class: "muted small" }, `started by ${status.startedBy ?? "?"}`)),
      bar,
      h("div", { class: "filters" },
        h("button", { class: "primary", type: "button", onclick: () => act("stop") }, "Stop"),
        h("label", { class: "grow" }, "Marker", noteInput),
        h("button", { type: "button", onclick: async () => { const response = await act("mark", note.trim() ? { label: note.trim() } : {}); if (response.ok) { note = ""; message.textContent = `Marker added at ${at(response.result.markedAtMs)}.`; } } }, "Add marker")),
      h("p", { class: "muted small" }, `${status.frames ?? 0} frames · ${status.events ?? 0} events · ${status.windows ?? 0} window${status.windows === 1 ? "" : "s"} so far`),
    ];
  };

  const agentToggle = (status) => {
    if (!liveIsInApp()) return null;
    const box = h("input", { type: "checkbox", checked: status.agentAccess === true });
    box.addEventListener("change", async () => {
      box.disabled = true;
      await act("agent-access", { enabled: box.checked });
    });
    return h("div", { class: "agent-access" },
      h("label", {}, box, " Let agents record"),
      h("p", { class: "muted small" }, status.agentAccess
        ? "On: an agent on this Mac can start, stop and mark recordings (npm run observatory -- live …). It can't do anything else, and this turns off when Archivist quits."
        : "Off: only this panel can record. Turn on to let an agent (Claude Code, the CLI) start, stop and mark recordings on this Mac."));
  };

  const update = async () => {
    clearTimeout(timer);
    if (!onPage()) return;
    const response = await live("status");
    if (!onPage()) return;
    if (!response.ok) {
      card.replaceChildren(h("h3", {}, "Recorder"), h("div", { class: "notice bad" }, response.error.message), message);
      return;
    }
    const status = response.result;
    if (status.available === false) {
      card.replaceChildren(h("h3", {}, "Recorder"), h("div", { class: "notice warn" }, h("strong", {}, "This dashboard can't reach the app's recorder."),
        h("p", {}, status.message), h("p", { class: "muted small" }, "Recording is easiest from the Observatory panel inside Archivist.")), message);
      timer = setTimeout(update, 5000);
      return;
    }
    const busy = status.recording || status.state === "finalizing";
    if (!busy && previous && (previous.recording || previous.state === "finalizing")) {
      // Just finished: show it.
      const runId = previous.runId ?? status.lastRun?.runId;
      previous = status;
      await context.refresh();
      if (runId) context.go(`#/live/${runId}`);
      return;
    }
    previous = status;
    const focused = document.activeElement;
    const keepFocus = focused === noteInput || focused === label;
    card.replaceChildren(
      h("div", { class: "page-head" }, h("h3", {}, "Recorder"), statusPill(status.state === "idle" ? "idle" : status.state)),
      ...(status.recording ? recordingView(status) : status.state === "finalizing" ? [h("p", {}, "Saving the recording…")] : idleView(status)),
      ...[message, agentToggle(status)].filter(Boolean));
    if (keepFocus) focused.focus();
    // Once a second while recording: this page redraws too, and those frames are recorded.
    timer = setTimeout(update, busy ? 1000 : 3000);
  };
  card.append(h("h3", {}, "Recorder"), h("p", { class: "muted" }, "Reading the recorder…"));
  update();
  return card;
}

// ---- the timeline ---------------------------------------------------------------

const LABEL_W = 150;
const PLOT_W = 840;
const WIDTH = LABEL_W + PLOT_W + 10;

function niceStep(span) {
  for (const step of [100, 250, 500, 1000, 2000, 5000, 10000, 15000, 30000]) if (span / step <= 10) return step;
  return 60000;
}

// Lanes share one time axis; each has its own scale and says what it is.
export function timeline(model, { onSpike, onRange } = {}) {
  const span = Math.max(model.durationMs ?? 0, ...model.events.map((event) => event.endMs ?? event.atMs), 1000);
  const x = (value) => LABEL_W + (Math.max(0, Math.min(span, value)) / span) * PLOT_W;
  const toMs = (px) => Math.max(0, Math.min(span, ((px - LABEL_W) / PLOT_W) * span));
  const lanes = [];
  const bandLanes = [];
  let top = 8;
  const lane = (name, height, sub) => {
    const g = svg("g", { class: "lane" });
    g.append(svg("text", { x: 0, y: top + 12, class: "lane-name" }, name));
    if (sub) g.append(svg("text", { x: 0, y: top + 26, class: "lane-sub" }, sub));
    g.append(svg("line", { x1: LABEL_W, x2: LABEL_W + PLOT_W, y1: top + height, y2: top + height, class: "axis" }));
    const entry = { g, top, height };
    lanes.push(entry);
    top += height + 14;
    return entry;
  };
  // Bars from the lane's floor: values capped at CAP_MS, long ones stand out.
  const barsLane = (name, sub, points) => {
    const l = lane(name, 64, sub);
    bandLanes.push(l);
    const y = (value) => l.top + l.height - (Math.min(value, CAP_MS) / CAP_MS) * l.height;
    for (const mark of [LONG_GAP_MS, CAP_MS]) {
      l.g.append(svg("line", { x1: LABEL_W, x2: LABEL_W + PLOT_W, y1: y(mark), y2: y(mark), class: "gridline" }));
      l.g.append(svg("text", { x: LABEL_W - 4, y: y(mark) + 3, "text-anchor": "end", class: "tick" }, `${mark} ms`));
    }
    let normal = "";
    let long = "";
    for (const [atMs, value] of points) {
      const segment = `M${x(atMs).toFixed(1)} ${l.top + l.height}V${y(value).toFixed(1)}`;
      if (value >= LONG_GAP_MS) long += segment; else normal += segment;
    }
    l.g.append(svg("path", { d: normal, class: "bars" }), svg("path", { d: long, class: "bars long" }));
    if (points.some(([, value]) => value > CAP_MS)) l.g.append(svg("text", { x: LABEL_W + PLOT_W, y: l.top - 2, "text-anchor": "end", class: "tick" }, `taller bars cut at ${CAP_MS} ms`));
  };
  const lineLane = (name, sub, points, format) => {
    const l = lane(name, 44, sub);
    const values = points.map(([, value]) => value);
    if (!values.length) {
      l.g.append(svg("text", { x: LABEL_W + 6, y: l.top + 26, class: "tick" }, "not available"));
      return;
    }
    const lo = Math.min(...values);
    const hi = Math.max(...values);
    const y = (value) => l.top + l.height - (hi === lo ? 0.5 : (value - lo) / (hi - lo)) * (l.height - 6) - 3;
    l.g.append(svg("polyline", { points: points.map(([atMs, value]) => `${x(atMs).toFixed(1)},${y(value).toFixed(1)}`).join(" "), class: "line" }));
    l.g.append(svg("text", { x: LABEL_W - 4, y: l.top + 9, "text-anchor": "end", class: "tick" }, format(hi)));
    l.g.append(svg("text", { x: LABEL_W - 4, y: l.top + l.height, "text-anchor": "end", class: "tick" }, format(lo)));
  };

  for (const window of model.windows) {
    const frames = model.frames[window.id] ?? [];
    barsLane(`Frames · window ${window.id}`, `${window.className ?? "?"} · interval`, frames.filter((frame) => frame.intervalMs != null).map((frame) => [frame.atMs, frame.intervalMs]));
  }
  barsLane("UI thread", "how late a 50 ms timer fired", model.probes.map((probe) => [probe.atMs, probe.delayMs]));
  lineLane("CPU", "% of one core", model.samples.filter((sample) => sample.cpuPercent != null).map((sample) => [sample.atMs, sample.cpuPercent]), (value) => `${Math.round(value)}%`);
  lineLane("Memory", "resident", model.samples.filter((sample) => sample.rssBytes != null).map((sample) => [sample.atMs, sample.rssBytes]), bytes);

  // Operations: one row per category; spans as bars, markers as ticks, notes as flags.
  const categories = [...new Set(model.events.filter((event) => event.category !== "recording" && event.kind !== "annotation").map((event) => event.category))];
  const opLane = lane("Operations", Math.max(24, categories.length * 16 + 6), categories.length ? null : "none recorded");
  categories.forEach((category, row) => {
    const y = opLane.top + 4 + row * 16;
    opLane.g.append(svg("text", { x: LABEL_W - 4, y: y + 10, "text-anchor": "end", class: "tick" }, category));
    for (const event of model.events.filter((entry) => entry.category === category && entry.kind !== "annotation")) {
      const node = event.kind === "span" && event.endMs != null
        ? svg("rect", { x: x(event.atMs), y, width: Math.max(2, x(event.endMs) - x(event.atMs)), height: 12, rx: 2, class: `op${event.ok === false ? " failed" : ""}` })
        : svg("rect", { x: x(event.atMs) - 1, y, width: 2, height: 12, class: "op marker" });
      node.append(svg("title", {}, `${eventName(event)} at ${at(event.atMs)}${event.durationMs != null ? `, ${ms(event.durationMs)}` : ""}`));
      opLane.g.append(node);
    }
  });

  const height = top + 18;
  const root = svg("svg", { viewBox: `0 0 ${WIDTH} ${height}`, class: "chart timeline", role: "img", "aria-label": "Timeline of the recording" });
  // Time axis.
  const step = niceStep(span);
  for (let t = 0; t <= span; t += step) {
    root.append(svg("line", { x1: x(t), x2: x(t), y1: 4, y2: top - 10, class: "gridline faint" }));
    root.append(svg("text", { x: x(t), y: height - 4, "text-anchor": "middle", class: "tick" }, at(t)));
  }
  // Moments, behind the lanes.
  const bands = svg("g");
  // Window lanes come first, in model.windows order; the UI-thread lane after them.
  const laneOf = (spike) => (spike.window == null ? bandLanes[model.windows.length] : bandLanes[model.windows.findIndex((window) => window.id === spike.window)]) ?? { top: 2, height: top - 12 };
  for (const spike of model.spikes) {
    if (spike.atMs == null) continue;
    const where = laneOf(spike);
    const left = x(spike.atMs);
    const width = Math.max(4, x(spike.endMs ?? spike.atMs) - left);
    bands.append(svg("rect", { x: left, y: 2, width, height: top - 10, class: "spike-guide" }));
    const band = svg("rect", { x: left, y: where.top, width, height: where.height, class: `spike ${spike.type}`, tabindex: 0, "data-spike": spike.id });
    band.append(svg("title", {}, `#${spike.id} ${spikeTypeLabel(spike)} of ${ms(spike.durationMs)} at ${at(spike.atMs)} (${windowLabel(model, spike.window)})`));
    band.addEventListener("click", (event) => { event.stopPropagation(); onSpike?.(spike); });
    band.addEventListener("keydown", (event) => { if (event.key === "Enter" || event.key === " ") { event.preventDefault(); onSpike?.(spike); } });
    bands.append(band);
    root.append(svg("text", { x: left + width + 3, y: where.top + 10, class: "spike-id" }, `#${spike.id}`));
  }
  root.prepend(bands);
  // Notes across all lanes.
  for (const event of model.events.filter((entry) => entry.kind === "annotation")) {
    const flag = svg("g", { class: "note" });
    flag.append(svg("line", { x1: x(event.atMs), x2: x(event.atMs), y1: 2, y2: top - 10 }), svg("title", {}, `Note at ${at(event.atMs)}: ${event.name}`));
    root.append(flag);
  }
  for (const entry of lanes) root.append(entry.g);

  // Hover readout and drag-to-select.
  const cursor = svg("line", { y1: 2, y2: top - 10, class: "cursor", visibility: "hidden" });
  const selection = svg("rect", { y: 2, height: top - 10, class: "selection", visibility: "hidden" });
  root.append(selection, cursor);
  const readout = h("div", { class: "readout small mono" }, "Point at the timeline to read values; drag across it to select a time range.");
  const point = (event) => {
    const box = root.getBoundingClientRect();
    return ((event.clientX - box.left) / box.width) * WIDTH;
  };
  let dragFrom = null;
  root.addEventListener("pointerdown", (event) => {
    if (event.target.closest?.("[data-spike]")) return;
    const px = point(event);
    if (px < LABEL_W) return;
    dragFrom = px;
    root.setPointerCapture?.(event.pointerId);
  });
  root.addEventListener("pointermove", (event) => {
    const px = point(event);
    if (px < LABEL_W || px > LABEL_W + PLOT_W) {
      cursor.setAttribute("visibility", "hidden");
      return;
    }
    cursor.setAttribute("x1", px);
    cursor.setAttribute("x2", px);
    cursor.setAttribute("visibility", "visible");
    const t = toMs(px);
    if (dragFrom != null) {
      selection.setAttribute("x", Math.min(dragFrom, px));
      selection.setAttribute("width", Math.abs(px - dragFrom));
      selection.setAttribute("visibility", "visible");
      readout.textContent = `${at(toMs(Math.min(dragFrom, px)))} – ${at(toMs(Math.max(dragFrom, px)))}`;
      return;
    }
    const near = (list) => list.reduce((best, entry) => (best == null || Math.abs(entry.atMs - t) < Math.abs(best.atMs - t) ? entry : best), null);
    const probe = near(model.probes);
    const sample = near(model.samples);
    const parts = [`t ${at(t)}`];
    for (const window of model.windows) {
      const frame = (model.frames[window.id] ?? []).find((entry) => entry.atMs >= t);
      if (frame?.intervalMs != null) parts.push(`w${window.id} frame interval ${ms(frame.intervalMs)}`);
    }
    if (probe) parts.push(`UI thread late ${ms(probe.delayMs)}`);
    if (sample?.cpuPercent != null) parts.push(`CPU ${sample.cpuPercent}%`);
    if (sample?.rssBytes != null) parts.push(`RSS ${bytes(sample.rssBytes)}`);
    readout.textContent = parts.join(" · ");
  });
  const finish = (event) => {
    if (dragFrom == null) return;
    const px = point(event);
    const from = toMs(Math.min(dragFrom, px));
    const to = toMs(Math.max(dragFrom, px));
    dragFrom = null;
    if (Math.abs(to - from) < span / 200) {
      selection.setAttribute("visibility", "hidden");
      return;
    }
    onRange?.(from, to);
  };
  root.addEventListener("pointerup", finish);
  root.addEventListener("pointerleave", () => cursor.setAttribute("visibility", "hidden"));

  return h("div", { class: "timeline-wrap" }, root, readout,
    legend([["Frame interval / UI-thread delay", "var(--series-1)"], [`≥ ${LONG_GAP_MS} ms`, "var(--warn)"], ["Notable moment", "var(--bad)"], ["Operation", "var(--series-2)"], ["Your note", "var(--series-5)"]]));
}

// ---- detail of a moment or a range ----------------------------------------------

function eventLine(event) {
  const offset = event.offsetMs != null ? ` (${event.offsetMs < 0 ? `${ms(-event.offsetMs)} after` : `${ms(event.offsetMs)} before`})` : "";
  return `${at(event.atMs)} ${event.kind === "annotation" ? `note "${event.name}"` : eventName(event)}${event.durationMs != null ? `, ${ms(event.durationMs)}` : ""}${event.ok === false ? " (failed)" : ""}${offset}`;
}

function rangeBlock(model, from, to, title) {
  const detail = rangeDetail(model, from, to);
  return [
    h("h3", {}, title),
    h("div", { class: "grid cards" },
      h("div", { class: "card" }, h("h3", {}, "Frames"), kv(detail.windows.map((window) => [window.label, `${window.frames} frames, longest interval ${ms(window.maxIntervalMs)}${window.gaps.length ? `, ${window.gaps.length} ≥ ${LONG_GAP_MS} ms` : ""}`]))),
      h("div", { class: "card" }, h("h3", {}, "UI thread"), kv([["Probes", detail.eventLoop.probes], ["Longest delay", ms(detail.eventLoop.maxDelayMs)], ["Stalls", detail.eventLoop.stalls.map((stall) => `${ms(stall.delayMs)} from ${at(stall.fromMs)}`).join(", ") || "none"]])),
      h("div", { class: "card" }, h("h3", {}, "Resources"), kv([["CPU", detail.cpu.samples ? `mean ${detail.cpu.meanPercent}%, max ${detail.cpu.maxPercent}%` : "no samples"], ["Resident memory", detail.memory.samples ? `${bytes(detail.memory.firstRssBytes)} → ${bytes(detail.memory.lastRssBytes)}` : "no samples"]]))),
    detail.events.length ? h("ul", { class: "events" }, detail.events.map((event) => h("li", {}, eventLine(event)))) : h("p", { class: "muted" }, "No instrumented operation in this range."),
    detail.eventsOmitted ? h("p", { class: "muted small" }, `${detail.eventsOmitted} more not shown.`) : null,
  ];
}

function spikeBlock(model, spike) {
  const withContext = spikesWithContext(model, { limit: model.spikes.length }).find((entry) => entry.id === spike.id);
  return [
    h("h3", {}, `#${spike.id}: ${spikeTypeLabel(spike)} of ${ms(spike.durationMs)} at ${at(spike.atMs)}`),
    h("p", { class: "muted small" }, `${withContext.windowLabel} · rule: ${spike.rule ?? "?"}`),
    withContext.observedTogether.length
      ? h("div", { class: "notice" }, h("strong", {}, "Observed together (correlation, not a cause)"), h("ul", {}, withContext.observedTogether.map((line) => h("li", {}, line))))
      : h("p", { class: "muted" }, "No instrumented operation was running at or near this moment."),
    ...rangeBlock(model, Math.max(0, spike.atMs - 250), (spike.endMs ?? spike.atMs) + 250, "250 ms either side").slice(1),
  ];
}

// ---- pages ----------------------------------------------------------------------

function recordingsTable(runs) {
  return table(["Recording", "Status", { label: "Length", num: true }, { label: "Frame gaps", num: true }, { label: "Stalls", num: true }, { label: "Worst", num: true }, "Stopped by", "When"],
    runs.map((run) => ({
      source: run,
      cells: [h("a", { href: `#/live/${run.runId}` }, run.label ?? run.runId), statusPill(run.status), ms(run.live?.durationMs ?? null),
        run.live?.frameGaps ?? "—", run.live?.stalls ?? "—", ms(run.live?.worstMs ?? null), run.live?.stopReason ?? "—", when(run.createdAt)],
    })), { onRow: (run) => context.go(`#/live/${run.runId}`) });
}

export async function livePage(runId) {
  const runs = context.getState().runs.filter((run) => run.kind === "live-session");
  const parts = [h("div", { class: "page-head" }, h("h2", {}, "Live diagnostics"), h("span", { class: "muted small" }, "Record while you use Archivist, then read what happened."))];
  parts.push(recorderCard());
  if (runId) {
    parts.push(await recordingView(runId, runs));
    return parts;
  }
  parts.push(h("section", {}, h("h3", {}, "Recordings"), runs.length ? recordingsTable(runs) : empty("No recordings yet.", "Press Start recording, use the app, and the recording opens here when it stops.")));
  return parts;
}

async function recordingView(runId, runs) {
  const { run, summary } = runDetails(runId);
  if (!summary) return empty(`Recording ${runId} can't be read.`);
  const { metrics, events } = await liveArtifacts(runId);
  const model = liveModel({ runId, summary, metrics, events });
  const stats = model.stats ?? {};
  const section = h("section", { class: "recording" });
  const parts = [h("div", { class: "page-head" }, h("h2", {}, kindLabel(run), " ", statusPill(run.status)), h("span", { class: "mono muted" }, runId))];
  if (summary.status === "running") parts.push(h("div", { class: "notice warn" }, "Still recording. It opens here when it stops; press Refresh if it doesn't."));
  if (model.partial) parts.push(h("div", { class: "notice warn" }, "The app stopped while this was recording. Everything up to the last saved second is here; operations still open then have no end."));
  if (!model.hasMetrics && summary.status !== "running") parts.push(h("div", { class: "notice warn" }, "metrics.json is missing, so there is no timeline; the figures below come from summary.json."));
  parts.push(h("p", { class: "lead" }, summarySentence(model)));
  const busiest = (stats.frames?.windows ?? []).reduce((best, window) => (!best || window.frames > best.frames ? window : best), null);
  parts.push(h("div", { class: "grid cards" },
    h("div", { class: "card" }, h("h3", {}, "Recording"), kv([["Length", `${ms(model.durationMs)} of ${model.requestedDurationS ?? "?"} s`], ["Stopped by", model.stopReason], ["Started from", model.startedBy], ["When", when(summary.createdAt)]])),
    h("div", { class: "card" }, h("h3", {}, "Frames (busiest window)"), busiest
      ? kv([["Frames", busiest.frames], ["Median interval", ms(busiest.whileDrawing?.p50Ms ?? null)], ["p95", busiest.whileDrawing?.p95Ms == null ? "not enough frames" : ms(busiest.whileDrawing.p95Ms)], ["Longest", ms(busiest.maxIntervalMs ?? null)]])
      : h("p", { class: "muted" }, "No frames recorded.")),
    h("div", { class: "card" }, h("h3", {}, "UI thread"), kv([["Longest delay", ms(stats.eventLoop?.maxDelayMs ?? null)], ["Stalls ≥ 50 ms", stats.eventLoop?.stalls ?? "—"], ["Median delay", ms(stats.eventLoop?.p50DelayMs ?? null)]])),
    h("div", { class: "card" }, h("h3", {}, "CPU and memory"), kv([
      ["CPU", stats.cpu?.available ? `mean ${stats.cpu.meanPercent}%, max ${stats.cpu.maxPercent}%` : "not available"],
      ["Resident", stats.memory?.available ? `${bytes(stats.memory.startRssBytes)} → ${bytes(stats.memory.endRssBytes)}` : "not available"],
      ["Footprint", stats.memory?.footprintAvailable ? `${bytes(stats.memory.startFootprintBytes)} → ${bytes(stats.memory.endFootprintBytes)}` : "not available here"],
      ["Threads", stats.threads?.available ? `${stats.threads.min}–${stats.threads.max}` : "not available"]]))));
  parts.push(h("div", { class: "card actions-card" }, copyForAiButton(runId), exportButton(runId)));

  const detail = h("div", { class: "card detail", id: "live-detail", tabindex: -1 }, h("p", { class: "muted" }, "Click a highlighted moment, or drag across the timeline, to see what happened then."));
  const show = (nodes) => {
    detail.replaceChildren(...nodes.flat().filter(Boolean));
    detail.scrollIntoView({ block: "nearest", behavior: "smooth" });
  };
  const showSpike = (spike) => show(spikeBlock(model, spike));
  if (model.hasMetrics) {
    parts.push(h("section", {}, h("h3", {}, "Timeline"),
      h("div", { class: "card" }, timeline(model, { onSpike: showSpike, onRange: (from, to) => show(rangeBlock(model, from, to, `${at(from)} – ${at(to)}`)) })), detail));
  }

  const worst = spikesWithContext(model, { limit: 50 });
  parts.push(h("section", {}, h("h3", {}, `Notable moments (${model.spikes.length})`),
    worst.length
      ? table(["#", { label: "At", num: true }, "What", { label: "How long", num: true }, "Where", { label: "Observed together", wrap: true }],
        worst.map((spike) => ({ source: spike, cells: [spike.id, at(spike.atMs), spikeTypeLabel(spike), ms(spike.durationMs), spike.windowLabel, spike.observedTogether.slice(0, 2).join("; ") || "—"] })),
        { onRow: model.hasMetrics ? showSpike : null })
      : h("p", { class: "muted" }, `No frame gap between steady frames and no UI-thread stall reached ${LONG_GAP_MS} ms.`)));

  const operations = operationSummary(model);
  if (operations.length) {
    parts.push(h("section", {}, h("h3", {}, "Operations recorded"), table(["Operation", { label: "Count", num: true }, { label: "Mean", num: true }, { label: "Longest", num: true }, { label: "Failed", num: true }],
      operations.map((op) => ({ cells: [op.operation.replace(".", " "), op.count, op.meanMs == null ? "marker" : ms(op.meanMs), op.maxMs == null ? "—" : ms(op.maxMs), op.failed || ""] })))));
  }

  const others = runs.filter((entry) => entry.runId !== runId && entry.status !== "running");
  if (others.length) {
    const pick = h("select", { "aria-label": "Recording to compare with" }, h("option", { value: "" }, "Compare with…"), others.map((entry) => h("option", { value: entry.runId }, `${entry.label ?? entry.runId} (${when(entry.createdAt)})`)));
    const result = h("div");
    pick.addEventListener("change", async () => {
      if (!pick.value) return result.replaceChildren();
      const other = runDetails(pick.value);
      const base = liveModel({ runId: pick.value, summary: other.summary, ...(await liveArtifacts(pick.value)) });
      const comparison = compareLive(base, model);
      result.replaceChildren(comparison.compatible
        ? table(["Measure", { label: "Other", num: true }, { label: "This", num: true }, { label: "Change", num: true }], comparison.metrics.map((metric) => ({ cells: [metric.label, metric.base ?? "—", metric.candidate ?? "—", metric.delta == null ? "—" : `${metric.delta > 0 ? "+" : ""}${metric.delta}`] })))
        : h("div", { class: "notice bad" }, h("strong", {}, "Not comparable"), h("ul", {}, comparison.blockers.map((line) => h("li", {}, line)))),
      h("ul", { class: "muted small" }, comparison.notes.map((note) => h("li", {}, note))));
    });
    parts.push(h("section", {}, h("h3", {}, "Compare"), pick, result));
  }

  const files = Object.entries(summary.artifacts ?? {}).filter(([, file]) => typeof file === "string");
  parts.push(h("section", {}, h("h3", {}, "Files"), h("p", { class: "small" }, files.map(([name, file], index) => [index ? " · " : "", h("a", { href: artifactUrl(runId, name), target: "_blank", rel: "noopener" }, file)])),
    h("p", { class: "muted small" }, "For an agent: ", h("code", { class: "cmd" }, `npm run observatory -- live get ${runId} --json`))));
  section.append(...parts);
  return section;
}

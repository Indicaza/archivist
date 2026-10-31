// Live recordings (kind "live-session", written by the app's flight
// recorder): one model, the moments worth a look, what happened around them,
// any time window in detail, and comparisons. Pure (no I/O), shared by the
// page and the CLI, so people and agents get the same answers.
//
// Wording rule: things measured close together are "observed together" or
// "occurred during"; nothing here says one thing caused another.

export const LIVE_ANALYSIS_SCHEMA = "archivist.observatory.live-analysis";
export const LIVE_ANALYSIS_SCHEMA_VERSION = 1;
// Events within this distance of a moment count as "near" it.
export const NEAR_MS = 250;
export const LONG_GAP_MS = 50;

const num = (value) => (typeof value === "number" && Number.isFinite(value) ? value : null);
const plural = (count, word) => `${count} ${word}${count === 1 ? "" : "s"}`;
const round = (value, digits = 2) => (value == null ? null : Math.round(value * 10 ** digits) / 10 ** digits);

export function ms(value) {
  if (value == null) return "n/a";
  if (Math.abs(value) >= 1000) return `${(value / 1000).toFixed(2)} s`;
  return `${value.toFixed(Math.abs(value) >= 100 ? 0 : 1)} ms`;
}

export function at(value) {
  return value == null ? "?" : `${(value / 1000).toFixed(2)} s`;
}

export function bytes(value) {
  if (value == null) return "n/a";
  const abs = Math.abs(value);
  if (abs >= 2 ** 30) return `${(value / 2 ** 30).toFixed(2)} GB`;
  if (abs >= 2 ** 20) return `${(value / 2 ** 20).toFixed(1)} MB`;
  if (abs >= 2 ** 10) return `${(value / 2 ** 10).toFixed(0)} KB`;
  return `${value} B`;
}

export function eventName(event) {
  return `${event.category} ${event.name}`.replace(/-/g, " ");
}

// ---- the model ----------------------------------------------------------------

// summary: the run's summary.json; metrics, events: metrics.json and
// events.json (either may be null: a recording still running, or one whose
// files are missing).
export function liveModel({ runId = null, summary = {}, metrics = null, events = null } = {}) {
  const live = summary?.live ?? {};
  const windows = (metrics?.windows ?? live.windows ?? []).map((window) => ({
    id: window.id,
    role: window.role ?? null,
    className: window.className ?? null,
    width: num(window.width),
    height: num(window.height),
    attachedMs: num(window.attachedMs),
    detachedMs: num(window.detachedMs),
  }));
  const frames = {};
  for (const [id, swaps] of Object.entries(metrics?.frameSwaps ?? {})) {
    const list = [];
    let previous = null;
    for (const value of Array.isArray(swaps) ? swaps : []) {
      const atMs = num(value);
      if (atMs == null) continue;
      list.push({ atMs, intervalMs: previous == null ? null : round(atMs - previous) });
      previous = atMs;
    }
    frames[id] = list;
  }
  const probes = (metrics?.eventLoopProbes ?? [])
    .filter((pair) => Array.isArray(pair) && num(pair[0]) != null && num(pair[1]) != null)
    .map(([atMs, delayMs]) => ({ atMs, delayMs }));
  const samples = (metrics?.samples ?? []).map((sample) => ({
    atMs: num(sample.atMs),
    cpuPercent: num(sample.cpuPercent),
    rssBytes: num(sample.rssBytes),
    footprintBytes: num(sample.footprintBytes),
    threads: num(sample.threads),
  })).filter((sample) => sample.atMs != null);
  const list = (events?.events ?? []).map((event) => ({
    kind: event.kind ?? "marker",
    category: String(event.category ?? ""),
    name: String(event.name ?? ""),
    atMs: num(event.atMs),
    endMs: num(event.endMs),
    durationMs: num(event.durationMs),
    ok: typeof event.ok === "boolean" ? event.ok : null,
    fields: event.fields && typeof event.fields === "object" ? event.fields : {},
  })).filter((event) => event.atMs != null).sort((a, b) => a.atMs - b.atMs);
  const spikes = (Array.isArray(live.spikes) ? live.spikes : []).map((spike, index) => ({
    id: index + 1,
    type: spike.type,
    atMs: num(spike.atMs),
    durationMs: num(spike.durationMs),
    endMs: num(spike.atMs) != null && num(spike.durationMs) != null ? round(spike.atMs + spike.durationMs) : null,
    window: num(spike.window),
    rule: spike.rule ?? null,
  }));
  return {
    runId: runId ?? summary?.runId ?? null,
    status: summary?.status ?? "unknown",
    measurementVersion: num(live.measurementVersion),
    durationMs: num(live.durationMs),
    requestedDurationS: num(live.requestedDurationS),
    startedBy: live.startedBy ?? null,
    stopReason: live.stopReason ?? null,
    partial: live.partial === true,
    probeIntervalMs: num(live.probeIntervalMs) ?? 50,
    sampleIntervalMs: num(live.sampleIntervalMs) ?? 500,
    stats: live.stats ?? null,
    windows,
    frames,
    probes,
    samples,
    events: list,
    spikes,
    hasMetrics: Boolean(metrics),
    hasEvents: Boolean(events),
    identity: identityOf(summary),
  };
}

// What a comparison needs to know about where a recording was made.
function identityOf(summary) {
  const app = summary?.app ?? {};
  const environment = summary?.environment ?? {};
  const text = (value) => (typeof value === "string" && value ? value : null);
  return {
    appVersion: text(app.version),
    buildType: text(app.buildType),
    qtRuntimeVersion: text(app.qtRuntimeVersion),
    binary: app.binaryBytes != null || app.binaryModifiedAt ? `${app.binaryBytes ?? "?"} bytes, built ${app.binaryModifiedAt ?? "?"}` : null,
    commit: text(summary?.source?.commit),
    dirty: summary?.source?.dirty === true,
    platform: text(environment.platform),
    osVersion: text(environment.osVersion),
    architecture: text(environment.architecture),
    logicalCpus: num(environment.logicalCpus),
    cpuModel: text(environment.cpuModel),
  };
}

export function windowLabel(model, id) {
  if (id == null) return "UI thread";
  const window = model.windows.find((entry) => entry.id === id);
  return window ? `window ${id} (${window.className ?? window.role ?? "?"})` : `window ${id}`;
}

// ---- around a moment ------------------------------------------------------------

function overlaps(event, fromMs, toMs) {
  const end = event.endMs ?? event.atMs;
  return event.atMs <= toMs && end >= fromMs;
}

// What was going on during [fromMs, toMs], and just before or after it.
export function context(model, fromMs, toMs, { nearMs = NEAR_MS } = {}) {
  const during = [];
  const near = [];
  for (const event of model.events) {
    if (event.category === "recording") continue;
    if (event.kind === "span" && event.endMs != null && overlaps(event, fromMs, toMs)) {
      during.push(event);
    } else if (event.kind !== "span" && event.atMs >= fromMs && event.atMs <= toMs) {
      during.push(event);
    } else if (overlaps({ atMs: event.atMs, endMs: event.endMs ?? event.atMs }, fromMs - nearMs, toMs + nearMs)) {
      const edge = (event.endMs ?? event.atMs) < fromMs ? (event.endMs ?? event.atMs) - fromMs : event.atMs - toMs;
      near.push({ ...event, offsetMs: round(edge, 1) });
    }
  }
  return { during, near };
}

function describeEvent(event) {
  const parts = [eventName(event)];
  if (event.kind === "span" && event.durationMs != null) parts.push(`took ${ms(event.durationMs)}`);
  if (event.kind === "annotation") parts[0] = `your note "${event.name}"`;
  if (event.ok === false) parts.push("(failed)");
  const fields = Object.entries(event.fields ?? {}).filter(([key]) => key !== "unfinished");
  if (event.category === "workspace" && event.name === "publish" && fields.length) {
    const [stage, value] = fields.sort((a, b) => Number(b[1]) - Number(a[1]))[0];
    parts.push(`(longest stage: ${stage} ${ms(Number(value))})`);
  } else if (fields.length) {
    parts.push(`(${fields.slice(0, 3).map(([key, value]) => `${key} ${value}`).join(", ")})`);
  }
  if (event.fields?.unfinished) parts.push("(still running when the recording stopped)");
  return parts.join(" ");
}

// Plain sentences for one moment; correlation only.
export function correlationLines(model, spike, ctx = context(model, spike.atMs, spike.endMs ?? spike.atMs)) {
  const lines = [];
  for (const event of ctx.during) {
    lines.push(event.kind === "span" ? `Occurred during ${describeEvent(event)}` : `At the same time: ${describeEvent(event)}`);
  }
  for (const event of ctx.near.slice(0, 4)) {
    const when = event.offsetMs < 0 ? `${ms(-event.offsetMs)} after` : `${ms(event.offsetMs)} before`;
    lines.push(`${when} ${describeEvent(event)}`);
  }
  for (const other of model.spikes) {
    if (other.id !== spike.id && other.atMs != null && other.atMs <= (spike.endMs ?? spike.atMs) && (other.endMs ?? other.atMs) >= spike.atMs && other.type !== spike.type) {
      const label = spikeTypeLabel(other).toLowerCase();
      lines.push(`Overlaps ${/^[aeiou]/.test(label) ? "an" : "a"} ${label} of ${ms(other.durationMs)}`);
      break;
    }
  }
  return lines;
}

export function spikeTypeLabel(spike) {
  return spike.type === "event-loop-stall" ? "Event-loop stall" : "Frame gap";
}

// The notable moments, worst first, each with what happened around it.
export function spikesWithContext(model, { limit = 10, nearMs = NEAR_MS } = {}) {
  return model.spikes.slice(0, limit).map((spike) => {
    const ctx = context(model, spike.atMs, spike.endMs ?? spike.atMs, { nearMs });
    return {
      ...spike,
      windowLabel: windowLabel(model, spike.window),
      during: ctx.during.map(slimEvent),
      near: ctx.near.map(slimEvent),
      observedTogether: correlationLines(model, spike, ctx),
    };
  });
}

function slimEvent(event) {
  const out = { kind: event.kind, category: event.category, name: event.name, atMs: event.atMs, endMs: event.endMs, durationMs: event.durationMs, ok: event.ok };
  if (event.offsetMs != null) out.offsetMs = event.offsetMs;
  if (Object.keys(event.fields ?? {}).length) out.fields = event.fields;
  return out;
}

// ---- a time window in detail ------------------------------------------------------

export function rangeDetail(model, fromMs, toMs, { maxEvents = 200 } = {}) {
  const windows = Object.entries(model.frames).map(([id, list]) => {
    const inside = list.filter((frame) => frame.atMs >= fromMs && frame.atMs <= toMs);
    const intervals = inside.map((frame) => frame.intervalMs).filter((value) => value != null);
    return {
      window: Number(id),
      label: windowLabel(model, Number(id)),
      frames: inside.length,
      maxIntervalMs: intervals.length ? Math.max(...intervals) : null,
      gaps: inside.filter((frame) => frame.intervalMs != null && frame.intervalMs >= LONG_GAP_MS)
        .map((frame) => ({ endedAtMs: frame.atMs, intervalMs: frame.intervalMs })).slice(0, 50),
    };
  });
  const probes = model.probes.filter((probe) => probe.atMs >= fromMs && probe.atMs <= toMs + model.probeIntervalMs);
  const samples = model.samples.filter((sample) => sample.atMs >= fromMs - model.sampleIntervalMs && sample.atMs <= toMs + model.sampleIntervalMs);
  const cpu = samples.map((sample) => sample.cpuPercent).filter((value) => value != null);
  const rss = samples.map((sample) => sample.rssBytes).filter((value) => value != null);
  const events = model.events.filter((event) => overlaps(event, fromMs, toMs));
  return {
    fromMs: round(fromMs),
    toMs: round(toMs),
    windows,
    eventLoop: {
      probes: probes.length,
      maxDelayMs: probes.length ? Math.max(...probes.map((probe) => probe.delayMs)) : null,
      stalls: probes.filter((probe) => probe.delayMs >= LONG_GAP_MS).map((probe) => ({ fromMs: round(probe.atMs - probe.delayMs), delayMs: probe.delayMs })),
    },
    cpu: { samples: cpu.length, maxPercent: cpu.length ? Math.max(...cpu) : null, meanPercent: cpu.length ? round(cpu.reduce((a, b) => a + b, 0) / cpu.length, 1) : null },
    memory: { samples: rss.length, firstRssBytes: rss[0] ?? null, lastRssBytes: rss.at(-1) ?? null, maxRssBytes: rss.length ? Math.max(...rss) : null },
    spikes: model.spikes.filter((spike) => spike.atMs != null && spike.atMs <= toMs && (spike.endMs ?? spike.atMs) >= fromMs),
    events: events.slice(0, maxEvents).map(slimEvent),
    eventsOmitted: Math.max(0, events.length - maxEvents),
  };
}

// ---- headline figures ----------------------------------------------------------------

function busiestWindow(stats) {
  const windows = stats?.frames?.windows ?? [];
  return windows.reduce((best, window) => (!best || (window.frames ?? 0) > (best.frames ?? 0) ? window : best), null);
}

export function operationSummary(model) {
  const groups = new Map();
  for (const event of model.events) {
    if (event.category === "recording" || event.kind === "annotation") continue;
    const key = `${event.category}.${event.name}`;
    const group = groups.get(key) ?? { operation: key, kind: event.kind, count: 0, failed: 0, durations: [] };
    group.count += 1;
    if (event.ok === false) group.failed += 1;
    if (event.durationMs != null) group.durations.push(event.durationMs);
    groups.set(key, group);
  }
  return [...groups.values()].map((group) => ({
    operation: group.operation,
    kind: group.kind,
    count: group.count,
    failed: group.failed,
    meanMs: group.durations.length ? round(group.durations.reduce((a, b) => a + b, 0) / group.durations.length) : null,
    maxMs: group.durations.length ? Math.max(...group.durations) : null,
  })).sort((a, b) => (b.maxMs ?? -1) - (a.maxMs ?? -1) || b.count - a.count);
}

// One evidence-only paragraph, e.g. "During the 30.0 s recording, 5 notable
// moments were measured: ...". Counts what coincided; claims no cause.
export function summarySentence(model) {
  const duration = model.durationMs != null ? `${(model.durationMs / 1000).toFixed(1)} s` : "this";
  const gaps = model.spikes.filter((spike) => spike.type === "frame-gap");
  const stalls = model.spikes.filter((spike) => spike.type === "event-loop-stall");
  if (!model.spikes.length) {
    return `During the ${duration} recording, no frame gap while drawing and no event-loop stall reached ${LONG_GAP_MS} ms.`;
  }
  const parts = [];
  if (gaps.length) parts.push(`${gaps.length} frame gap${gaps.length === 1 ? "" : "s"} (largest ${ms(Math.max(...gaps.map((spike) => spike.durationMs)))})`);
  if (stalls.length) parts.push(`${stalls.length} event-loop stall${stalls.length === 1 ? "" : "s"} (largest ${ms(Math.max(...stalls.map((spike) => spike.durationMs)))})`);
  let sentence = `During the ${duration} recording, ${model.spikes.length} notable moment${model.spikes.length === 1 ? " was" : "s were"} measured: ${parts.join(" and ")}.`;
  const coincided = new Map();
  for (const spike of model.spikes) {
    const { during } = context(model, spike.atMs, spike.endMs ?? spike.atMs);
    for (const name of new Set(during.filter((event) => event.kind === "span" && !(event.category === "workspace" && event.name === "publish")).map(eventName))) {
      coincided.set(name, (coincided.get(name) ?? 0) + 1);
    }
  }
  const top = [...coincided.entries()].sort((a, b) => b[1] - a[1])[0];
  if (top) sentence += ` ${top[1]} of them occurred during ${top[0]}.`;
  const publish = operationSummary(model).find((entry) => entry.operation === "workspace.publish");
  if (publish?.maxMs != null && publish.maxMs >= 16) sentence += ` Workspace publishing ran ${publish.count}× (longest ${ms(publish.maxMs)}).`;
  return sentence;
}

// ---- comparing two recordings ---------------------------------------------------------

export function headline(model) {
  const stats = model.stats ?? {};
  const minutes = model.durationMs ? model.durationMs / 60000 : null;
  const window = busiestWindow(stats);
  const perMinute = (count) => (minutes ? round(count / minutes, 1) : null);
  return {
    durationMs: model.durationMs,
    frames: stats.frames?.total ?? null,
    busiestWindowP50Ms: window?.whileDrawing?.p50Ms ?? null,
    busiestWindowP95Ms: window?.whileDrawing?.p95Ms ?? null,
    frameGapsPerMinute: perMinute(model.spikes.filter((spike) => spike.type === "frame-gap").length),
    stallsPerMinute: perMinute(stats.eventLoop?.stalls ?? 0),
    maxEventLoopDelayMs: stats.eventLoop?.maxDelayMs ?? null,
    cpuMeanPercent: stats.cpu?.meanPercent ?? null,
    rssGrowthBytesPerMinute: minutes && stats.memory?.growthRssBytes != null ? Math.round(stats.memory.growthRssBytes / minutes) : null,
  };
}

const HEADLINE_LABELS = {
  durationMs: "Recording length",
  frames: "Frames swapped",
  busiestWindowP50Ms: "Frame interval while drawing, median (busiest window)",
  busiestWindowP95Ms: "Frame interval while drawing, p95 (busiest window)",
  frameGapsPerMinute: "Frame gaps per minute",
  stallsPerMinute: "Event-loop stalls per minute",
  maxEventLoopDelayMs: "Longest event-loop delay",
  cpuMeanPercent: "Mean CPU (% of one core)",
  rssGrowthBytesPerMinute: "Resident memory growth per minute",
};

// Differences that make two recordings' numbers less comparable. Blockers
// change what a number means; warnings are for the reader (and an agent) to
// weigh. Neither is a judgement of the code.
function compatibility(base, candidate) {
  const blockers = [];
  const warnings = [];
  if (base.measurementVersion !== candidate.measurementVersion) {
    blockers.push(`Measurement versions differ (${base.measurementVersion} vs ${candidate.measurementVersion}).`);
  }
  if (base.probeIntervalMs !== candidate.probeIntervalMs || base.sampleIntervalMs !== candidate.sampleIntervalMs) {
    blockers.push(`Sampling differs (probe ${base.probeIntervalMs} vs ${candidate.probeIntervalMs} ms, resources ${base.sampleIntervalMs} vs ${candidate.sampleIntervalMs} ms).`);
  }
  for (const model of [base, candidate]) {
    if (!model.stats) blockers.push(`${model.runId} has no statistics (status ${model.status}).`);
  }
  const a = base.identity ?? {};
  const b = candidate.identity ?? {};
  const differs = (key) => a[key] != null && b[key] != null && a[key] !== b[key];
  const unknown = (key) => (a[key] == null) !== (b[key] == null);
  if (differs("buildType")) warnings.push(`One is a ${a.buildType} build and the other ${b.buildType}: debug builds are much slower, so the difference says little about the code.`);
  else if (unknown("buildType")) warnings.push("Only one recording says whether it was a debug or release build.");
  if (["platform", "osVersion", "architecture", "logicalCpus", "cpuModel"].some(differs)) {
    warnings.push(`Made on different machines or systems (${[a.platform, a.osVersion, a.architecture, a.logicalCpus && `${a.logicalCpus} CPUs`].filter(Boolean).join(" ")} vs ${[b.platform, b.osVersion, b.architecture, b.logicalCpus && `${b.logicalCpus} CPUs`].filter(Boolean).join(" ")}).`);
  }
  if (differs("qtRuntimeVersion")) warnings.push(`Different Qt versions (${a.qtRuntimeVersion} vs ${b.qtRuntimeVersion}).`);
  if (differs("appVersion")) warnings.push(`Different app versions (${a.appVersion} vs ${b.appVersion}).`);
  if (a.commit && a.commit === b.commit && !a.dirty && !b.dirty && a.binary && a.binary === b.binary) {
    warnings.push("Both recordings are of the same build: any difference comes from what was done during them, or from the machine.");
  } else if (a.dirty || b.dirty) {
    warnings.push(`${[a.dirty && base.runId, b.dirty && candidate.runId].filter(Boolean).join(" and ")} had uncommitted changes, so its commit doesn't fully identify the code.`);
  }
  for (const model of [base, candidate]) {
    if (model.partial || (model.status && model.status !== "completed")) warnings.push(`${model.runId} is ${model.partial ? "partial" : model.status} (stopped by ${model.stopReason ?? "?"}): it may not cover the same activity.`);
    const dropped = Object.entries(model.stats?.dropped ?? {}).filter(([, count]) => count > 0);
    if (dropped.length) warnings.push(`${model.runId} dropped data at the recorder's limit (${dropped.map(([kind, count]) => `${count} ${kind}`).join(", ")}).`);
    if (model.events.some((event) => event.category === "app" && event.name === "state" && event.fields?.state && event.fields.state !== "active")) {
      warnings.push(`${model.runId}: the app left the foreground during the recording, which can inflate event-loop delays.`);
    }
  }
  if (base.durationMs && candidate.durationMs) {
    const ratio = Math.max(base.durationMs, candidate.durationMs) / Math.min(base.durationMs, candidate.durationMs);
    if (ratio > 1.5) warnings.push(`Their lengths differ a lot (${ms(base.durationMs)} vs ${ms(candidate.durationMs)}); per-minute figures are comparable, totals are not.`);
  }
  const windowSet = (model) => model.windows.map((window) => `${window.className ?? "?"} ${window.width ?? "?"}x${window.height ?? "?"}`).sort().join(", ");
  if (windowSet(base) !== windowSet(candidate)) {
    warnings.push(`Different windows were recorded (${windowSet(base) || "none"} vs ${windowSet(candidate) || "none"}); frame figures follow the busiest window.`);
  }
  return { blockers, warnings };
}

export function compareLive(base, candidate) {
  const { blockers, warnings } = compatibility(base, candidate);
  const a = headline(base);
  const b = headline(candidate);
  const metrics = Object.keys(HEADLINE_LABELS).map((key) => ({
    metric: key,
    label: HEADLINE_LABELS[key],
    base: a[key],
    candidate: b[key],
    delta: a[key] != null && b[key] != null ? round(b[key] - a[key]) : null,
  }));
  return {
    compatible: blockers.length === 0,
    blockers,
    warnings,
    baseRunId: base.runId,
    runId: candidate.runId,
    metrics: blockers.length ? [] : metrics,
    notes: [
      "One recording each: what you did during each recording drives these numbers as much as the code does.",
      "Not a significance test. Repeat the same interaction several times before trusting a difference.",
    ],
  };
}

// ---- the report (Copy for AI) --------------------------------------------------------

// Facts for diagnostic.mjs: what a live recording adds to the shared report.
export function liveFacts(summary, { metrics = null, events = null } = {}, redact = (text) => text) {
  const model = liveModel({ summary, metrics, events });
  const stats = model.stats ?? {};
  const notes = model.events.filter((event) => event.kind === "annotation").map((event) => ({ atMs: event.atMs, label: redact(event.name) }));
  const appStates = model.events.filter((event) => event.category === "app" && event.name === "state").map((event) => event.fields?.state);
  return {
    experiment: {
      requestedDurationS: model.requestedDurationS,
      durationMs: model.durationMs,
      startedBy: model.startedBy,
      stopReason: model.stopReason,
      partial: model.partial,
      measurementVersion: model.measurementVersion,
      probeIntervalMs: model.probeIntervalMs,
      sampleIntervalMs: model.sampleIntervalMs,
      windows: model.windows.map((window) => ({ id: window.id, className: window.className, size: window.width && window.height ? `${window.width}x${window.height}` : null, closedDuringRecording: window.detachedMs != null && model.durationMs != null && window.detachedMs < model.durationMs - 5 })),
      app: summary?.app ? { version: summary.app.version ?? null, qtRuntimeVersion: summary.app.qtRuntimeVersion ?? null, buildType: summary.app.buildType ?? null, binaryModifiedAt: summary.app.binaryModifiedAt ?? null } : null,
    },
    results: {
      status: model.status,
      error: summary?.error ? redact(summary.error) : null,
      summary: summarySentence(model),
      frames: (stats.frames?.windows ?? []).map((window) => ({ window: window.window, label: windowLabel(model, window.window), frames: window.frames, whileDrawing: window.whileDrawing ?? null, maxIntervalMs: window.maxIntervalMs ?? null, gaps: window.gaps ?? null })),
      eventLoop: stats.eventLoop ?? null,
      cpu: stats.cpu ?? null,
      memory: stats.memory ?? null,
      threads: stats.threads ?? null,
      dropped: stats.dropped ?? null,
    },
    evidence: {
      spikes: spikesWithContext(model, { limit: 8 }),
      operations: operationSummary(model).slice(0, 12),
      notes,
      appStates,
      liveCorrelations: true,
    },
    liveMissing: liveMissing(model, stats, appStates),
  };
}

function liveMissing(model, stats, appStates) {
  const lines = [];
  if (!model.hasMetrics) lines.push("metrics.json is missing, so frame, stall and resource figures come only from summary.json.");
  if (model.partial) lines.push("The app stopped during the recording; data after the last saved second is lost and open operations have no end.");
  if (!stats.cpu?.available) lines.push("CPU use was not available on this platform.");
  if (!stats.memory?.available) lines.push("Memory use was not available on this platform.");
  if (stats.memory?.available && !stats.memory?.footprintAvailable) lines.push("Memory footprint (macOS phys_footprint) is not available here; resident memory is used.");
  if (!stats.threads?.available) lines.push("Thread count was not available on this platform.");
  const dropped = stats.dropped ?? {};
  for (const [kind, count] of Object.entries(dropped)) if (count) lines.push(`${count} ${kind} were not kept (the recorder's memory limit).`);
  if (appStates.some((state) => state && state !== "active")) lines.push("The app left the foreground during the recording; macOS may throttle timers then, which inflates event-loop delays.");
  lines.push(
    "Frame figures are QQuickWindow frame-swap times. They are not GPU time, compositor latency or when a frame reached the screen.",
    "Qt draws only when something changes, so long frame intervals while nothing moves are normal; only gaps between steady frames, or overlapping a stall, are listed.",
    "Event-loop delays are how late a 50 ms UI-thread timer fired. They show the UI thread was busy, not what it was doing.",
    "No profiler or stack samples: nothing here says which code ran during a moment.",
    "Operations are only the instrumented ones (sidebar, workspace publish, wells, magnet drags, layouts, tabs, chat switch). Anything else shows up only as time.",
  );
  return lines;
}

export function liveMarkdown(facts, out) {
  const e = facts.experiment;
  const r = facts.results;
  const ev = facts.evidence;
  out.push("", "## Recording",
    `- Length: ${ms(e.durationMs)} of ${e.requestedDurationS ?? "?"} s requested; stopped by ${e.stopReason ?? "?"}; started from ${e.startedBy ?? "?"}${e.partial ? "; PARTIAL (the app stopped while recording)" : ""}`,
    `- Windows recorded: ${e.windows.length ? e.windows.map((window) => `${window.id} ${window.className ?? "?"}${window.size ? ` ${window.size}` : ""}${window.closedDuringRecording ? " (closed during)" : ""}`).join(", ") : "none"}`,
    `- Sampling: UI-thread probe every ${e.probeIntervalMs} ms, resources every ${e.sampleIntervalMs} ms; measurement version ${e.measurementVersion ?? "?"}`);
  if (e.app) out.push(`- App: ${[e.app.version ? `version ${e.app.version}` : null, e.app.qtRuntimeVersion ? `Qt ${e.app.qtRuntimeVersion}` : null, e.app.buildType, e.app.binaryModifiedAt ? `binary built ${e.app.binaryModifiedAt}` : null].filter(Boolean).join(", ")}`);

  out.push("", "## Result", `- Status: ${r.status}${r.error ? `: ${r.error}` : ""}`, `- ${r.summary}`);
  out.push("", "## Measurements");
  for (const window of r.frames) {
    const d = window.whileDrawing ?? {};
    out.push(`- ${window.label}: ${window.frames} frames; interval while drawing median ${ms(d.p50Ms)}, p95 ${d.p95Ms == null ? "not computed (<20)" : ms(d.p95Ms)}, max ${ms(d.maxMs)}; ${plural(window.gaps ?? 0, "interval")} ≥ ${LONG_GAP_MS} ms (idle pauses included)`);
  }
  if (!r.frames.length) out.push("- No frames were recorded.");
  const loop = r.eventLoop ?? {};
  out.push(`- Event loop: ${loop.probes ?? 0} probes, median delay ${ms(loop.p50DelayMs)}, p95 ${loop.p95DelayMs == null ? "n/a" : ms(loop.p95DelayMs)}, max ${ms(loop.maxDelayMs)}; ${plural(loop.stalls ?? 0, "stall")} ≥ ${LONG_GAP_MS} ms`);
  out.push(r.cpu?.available ? `- CPU: mean ${r.cpu.meanPercent}%, max ${r.cpu.maxPercent}% of one core (${r.cpu.samples} samples)` : "- CPU: not available");
  if (r.memory?.available) {
    out.push(`- Resident memory: ${bytes(r.memory.startRssBytes)} → ${bytes(r.memory.endRssBytes)} (${r.memory.growthRssBytes >= 0 ? "+" : ""}${bytes(r.memory.growthRssBytes)}), max ${bytes(r.memory.maxRssBytes)}`);
    if (r.memory.footprintAvailable) out.push(`- Memory footprint: ${bytes(r.memory.startFootprintBytes)} → ${bytes(r.memory.endFootprintBytes)} (${r.memory.growthFootprintBytes >= 0 ? "+" : ""}${bytes(r.memory.growthFootprintBytes)})`);
  } else {
    out.push("- Memory: not available");
  }
  if (r.threads?.available) out.push(`- Threads: ${r.threads.min}–${r.threads.max}`);

  out.push("", "## Worst moments (measured)");
  if (ev.spikes.length) {
    for (const spike of ev.spikes) out.push(`- #${spike.id} at ${at(spike.atMs)}: ${spikeTypeLabel(spike).toLowerCase()} of ${ms(spike.durationMs)} (${spike.windowLabel}; ${spike.rule})`);
  } else {
    out.push(`- None: no frame gap between steady frames and no event-loop stall reached ${LONG_GAP_MS} ms.`);
  }
  if (ev.spikes.some((spike) => spike.observedTogether.length)) {
    out.push("", "## Observed together (correlation, not a cause)");
    for (const spike of ev.spikes) {
      if (!spike.observedTogether.length) continue;
      out.push(`- #${spike.id} (${ms(spike.durationMs)} at ${at(spike.atMs)}): ${spike.observedTogether.slice(0, 4).join("; ")}`);
    }
  }
  if (ev.operations.length) {
    out.push("", "## Operations recorded");
    for (const op of ev.operations) {
      out.push(`- ${op.operation.replace(".", " ")}: ${op.count}×${op.meanMs != null ? `, mean ${ms(op.meanMs)}, max ${ms(op.maxMs)}` : " (markers, no duration)"}${op.failed ? `, ${op.failed} failed` : ""}`);
    }
  }
  if (ev.notes.length) {
    out.push("", "## Notes added while recording", ...ev.notes.map((note) => `- ${at(note.atMs)}: "${note.label}"`));
  }
}

export function liveCommands(runId) {
  return [
    `npm run observatory -- live get ${runId} --json`,
    `npm run observatory -- live spikes ${runId} --json`,
    `npm run observatory -- live window ${runId} --from <ms> --to <ms> --json`,
    `npm run observatory -- export ${runId}`,
    "npm run observatory -- live record --duration 30 --json   # a new recording (needs agent access on)",
  ];
}

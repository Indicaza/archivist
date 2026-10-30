// Extracts the archivist.wells.timing measurements from a runtime log.
// Only these exact message shapes are recognized; everything else stays in
// the copied original log. View titles are not stored.
//
// Emitted by frontend/src/App/Domains/Workspace/workspace_store*.cpp:
//   archivist.wells.timing: publish: geometry 0.10, views 0.20, ... ms
//   archivist.wells.timing: minimize “Title” model 0.01 ms, publish 2.3 ms
//   archivist.wells.timing: restore “Title” original model 0.01 ms, publish 2.1 ms, after 0.4 ms

export const CATEGORY = "archivist.wells.timing";

const NUMBER = String.raw`([-+]?\d+(?:\.\d+)?(?:e[-+]?\d+)?)`;
const PREFIX = `${CATEGORY}: `;
const MARKER = PREFIX.replace(/\./g, "\\.");
const PUBLISH = new RegExp(`${MARKER}publish: (.+) ms\\s*$`);
const STAGE = new RegExp(`^([a-z]+) ${NUMBER}$`, "i");
const MINIMIZE = new RegExp(`${MARKER}minimize .+ model ${NUMBER} ms, publish ${NUMBER} ms\\s*$`, "i");
const RESTORE = new RegExp(
  `${MARKER}restore .+ (original|fallback|returned|revealed|opened) model ${NUMBER} ms, publish ${NUMBER} ms, after ${NUMBER} ms\\s*$`,
  "i",
);

function parsePublishStages(text) {
  const stages = {};
  for (const part of text.split(/,\s*/)) {
    const match = part.trim().match(STAGE);
    if (!match) {
      return null;
    }
    stages[match[1]] = Number(match[2]);
  }
  return stages;
}

// Returns the recognized events plus the line numbers of category lines
// that did not match a known shape (malformed or newer formats).
export function parseRuntimeLog(text) {
  const events = [];
  const unrecognized = [];
  let categoryLines = 0;
  const lines = text.split(/\r?\n/);

  lines.forEach((line, index) => {
    if (!line.includes(PREFIX)) {
      return;
    }
    categoryLines += 1;
    const lineNumber = index + 1;
    let match = line.match(PUBLISH);
    if (match) {
      const stagesMs = parsePublishStages(match[1]);
      if (stagesMs) {
        const totalMs = Object.values(stagesMs).reduce((sum, value) => sum + value, 0);
        events.push({ operation: "publish", lineNumber, stagesMs, totalMs: Math.round(totalMs * 1000) / 1000 });
        return;
      }
    }
    match = line.match(MINIMIZE);
    if (match) {
      events.push({ operation: "minimize", lineNumber, modelMs: Number(match[1]), publishMs: Number(match[2]) });
      return;
    }
    match = line.match(RESTORE);
    if (match) {
      events.push({
        operation: "restore",
        lineNumber,
        how: match[1],
        modelMs: Number(match[2]),
        publishMs: Number(match[3]),
        afterMs: Number(match[4]),
      });
      return;
    }
    unrecognized.push(lineNumber);
  });

  return { totalLines: lines.length, categoryLines, events, unrecognizedLines: unrecognized };
}

// Count, mean and max of one field per operation.
export function summarizeEvents(events) {
  const fields = { publish: "totalMs", minimize: "publishMs", restore: "publishMs" };
  const summary = {};
  for (const [operation, field] of Object.entries(fields)) {
    const values = events.filter((event) => event.operation === operation).map((event) => event[field]);
    if (values.length) {
      summary[operation] = {
        field,
        count: values.length,
        meanMs: Math.round((values.reduce((a, b) => a + b, 0) / values.length) * 1000) / 1000,
        maxMs: Math.max(...values),
      };
    }
  }
  return summary;
}

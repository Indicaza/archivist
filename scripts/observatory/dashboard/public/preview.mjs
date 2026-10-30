// A window of a text artifact's lines, or the lines matching a search.
// Pure, so the page can page through a log without asking the server again.
export const MAX_PREVIEW_LINES = 2000;

export function previewText(text, { offset = 0, limit = 400, q = "" } = {}) {
  const lines = String(text).split(/\r?\n/);
  if (lines.at(-1) === "") {
    lines.pop();
  }
  const size = Math.min(MAX_PREVIEW_LINES, Math.max(1, Number(limit) || 400));
  let numbered = lines.map((line, index) => ({ n: index + 1, text: line }));
  if (q) {
    const needle = String(q).toLowerCase();
    numbered = numbered.filter((line) => line.text.toLowerCase().includes(needle));
  }
  const start = Math.max(0, Number(offset) || 0);
  return {
    totalLines: lines.length,
    matchingLines: numbered.length,
    offset: start,
    lines: numbered.slice(start, start + size),
    truncated: start + size < numbered.length,
  };
}

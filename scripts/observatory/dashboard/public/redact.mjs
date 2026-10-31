// Conservative redaction for text that leaves the machine through Copy for
// AI or an evidence export. Pure, so the page and the CLI share it.
//
// It removes: the Observatory folder and home folder paths, user names in
// /Users/<name> and /home/<name>, e-mail addresses, values of secret-looking
// settings (token=, password:, Authorization: ...), well-known API key
// formats, and long opaque strings. Git commits and SHA-256 hashes (plain
// hex) are kept: they identify the build and are not secrets.

export const REDACTION_RULES = [
  "Observatory folder path -> <observatory>",
  "the Archivist checkout -> <repo>/ (paths inside it are kept)",
  "any other path in a home folder -> ~/<path> or /Users/<user>/<path>",
  "e-mail addresses -> <email>",
  "values after token, secret, password, api key, authorization, cookie -> <redacted>",
  "known API key formats (sk-, ghp_, xox, AKIA, AIza, Bearer) -> <redacted>",
  "opaque strings of 32+ mixed-case letters and digits -> <redacted>",
];

// Redaction is pattern matching. It catches the usual paths and secret
// formats, not every secret: a password in an unusual form, or private text
// that isn't a path or a key, gets through. Say so wherever it is applied.
export const REDACTION_LIMITS = "Pattern-based redaction: it catches common paths and secret formats, not everything. Read it before sharing.";

// Mixed case and digits: a key or token, not a word, a path part or a hash.
const OPAQUE = /^(?=.*[0-9])(?=.*[a-z])(?=.*[A-Z])/;
const escape = (text) => text.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");

// root: the Observatory folder; repo: the Archivist checkout (its paths are
// kept, relative, because they help); home: the user's home folder. Any
// other path under a home folder is hidden whole: it can name private files.
export function makeRedactor({ root = null, repo = null, home = null } = {}) {
  const exact = [];
  if (root) exact.push([new RegExp(escape(root), "g"), "<observatory>"]);
  if (repo) exact.push([new RegExp(`${escape(repo)}/?`, "g"), "<repo>/"]);
  if (home && home.length > 1) exact.push([new RegExp(escape(home), "g"), "~"]);
  return (value) => {
    if (value == null) return value;
    let text = String(value);
    for (const [pattern, replacement] of exact) text = text.replace(pattern, replacement);
    return text
      // A path runs to the end of the line, a quote or bracket, or ": " (file
      // names can contain spaces, so a space doesn't end it).
      .replace(/(?<![\w.])~\/[^"'`<>()\n]+?(?=$|["'`<>()\n]|: )/gm, "~/<path>")
      .replace(/\/(Users|home)\/[^/\s"'`<>]+(\/[^"'`<>()\n]+?(?=$|["'`<>()\n]|: ))?/gm, (match, base, rest) => `/${base}/<user>${rest ? "/<path>" : ""}`)
      .replace(/[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}/g, "<email>")
      .replace(/\b(Bearer|Basic)\s+[A-Za-z0-9._~+/=-]{8,}/gi, "$1 <redacted>")
      .replace(/((?:api[_-]?key|access[_-]?key|secret|token|password|passwd|authorization|cookie|session[_-]?id)["']?\s*[:=]\s*["']?)[^\s"',;]+/gi, "$1<redacted>")
      .replace(/\b(sk-[A-Za-z0-9_-]{16,}|gh[pousr]_[A-Za-z0-9]{20,}|xox[abpr]-[A-Za-z0-9-]{10,}|AKIA[0-9A-Z]{16}|AIza[0-9A-Za-z_-]{30,})\b/g, "<redacted>")
      .replace(/[A-Za-z0-9+_=-]{32,}/g, (match) => (OPAQUE.test(match) && !/^[0-9a-f]+$/i.test(match) ? "<redacted>" : match));
  };
}

// Redacts every string inside a JSON value (keys are kept).
export function redactJson(value, redact) {
  if (typeof value === "string") return redact(value);
  if (Array.isArray(value)) return value.map((entry) => redactJson(entry, redact));
  if (value && typeof value === "object") {
    return Object.fromEntries(Object.entries(value).map(([key, entry]) => [key, redactJson(entry, redact)]));
  }
  return value;
}

// Lines worth showing from a log: errors and warnings only, short, redacted.
const NOTABLE = /\b(error|fatal|warning|failed|failure|refus\w*|timed out|timeout|crash\w*|abort\w*|exception|assert\w*)\b/i;

export function logExcerpt(text, redact, { maxLines = 8, maxLength = 240 } = {}) {
  const lines = String(text ?? "").split(/\r?\n/);
  const picked = [];
  for (const line of lines) {
    if (!NOTABLE.test(line)) continue;
    const clean = redact(line.trim());
    if (!clean || picked.includes(clean)) continue;
    picked.push(clean.length > maxLength ? `${clean.slice(0, maxLength)}…` : clean);
    if (picked.length >= maxLines) break;
  }
  return { lines: picked, totalLines: lines.length - (lines.at(-1) === "" ? 1 : 0) };
}

// Who may use the control operations over HTTP. Reading records stays open
// to the page as before; anything that starts, stops or writes needs all of:
//   - POST with Content-Type: application/json (a plain form can't send it)
//   - X-Observatory-Token equal to this session's control key, which only the
//     page opened from the dashboard's own start-up link (or Archivist's own
//     panel) knows; a custom header also forces a CORS preflight, which is
//     never granted, so another site's page can't even send the request
//   - an Origin header naming this server (browsers always send one on POST)
//   - Sec-Fetch-Site, when present, of same-origin
// The Host check and loopback binding in front of this stay as they were.
import { randomBytes, timingSafeEqual } from "node:crypto";

export const TOKEN_HEADER = "x-observatory-token";
export const MAX_BODY_BYTES = 64 * 1024;

export function newControlToken() {
  return randomBytes(24).toString("base64url");
}

function sameToken(given, expected) {
  const a = Buffer.from(String(given ?? ""));
  const b = Buffer.from(String(expected));
  return a.length === b.length && timingSafeEqual(a, b);
}

// null when allowed, else { code, message } (codes from operations.mjs).
export function checkControlRequest(request, { token, origins }) {
  if (!token) return { code: "controls-disabled", message: "Controls are off. Start the dashboard with: npm run observatory:dashboard -- --control" };
  if (request.method !== "POST") return { code: "invalid-input", message: "Control operations are POST requests." };
  const origin = request.headers.origin;
  if (!origin || !origins.has(origin)) return { code: "forbidden-origin", message: "Control requests must come from the Observatory page itself." };
  const site = request.headers["sec-fetch-site"];
  if (site && site !== "same-origin") return { code: "forbidden-origin", message: "Control requests must come from the Observatory page itself." };
  if (!String(request.headers["content-type"] ?? "").toLowerCase().startsWith("application/json")) {
    return { code: "invalid-input", message: "Send the input as application/json." };
  }
  if (!sameToken(request.headers[TOKEN_HEADER], token)) {
    return { code: "unauthorized", message: "This page doesn't have the control key. Open the link the dashboard printed when it started." };
  }
  return null;
}

export function readBody(request, limit = MAX_BODY_BYTES) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    let size = 0;
    request.on("data", (chunk) => {
      size += chunk.length;
      if (size > limit) {
        reject(Object.assign(new Error(`The request body is over ${limit} bytes.`), { code: "invalid-input" }));
        request.destroy();
        return;
      }
      chunks.push(chunk);
    });
    request.on("end", () => resolve(Buffer.concat(chunks).toString("utf8")));
    request.on("error", reject);
  });
}

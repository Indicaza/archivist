import type { RequestHandler } from "express";

// The API only exists for the Archivist desktop app on this machine. Binding
// to 127.0.0.1 keeps other machines out, but a web page open in the user's
// browser can still reach localhost. These checks close that gap until the
// Node API is removed:
//
// - Host must be a loopback name. A DNS-rebinding page arrives with its own
//   domain in Host, so it is refused even though it resolved to 127.0.0.1.
// - A request that carries an Origin must come from the app's own pages.
//   Browsers always send Origin on cross-site requests; the Qt app's network
//   requests send none.
export function createLocalOnlyGuard(port: number): RequestHandler {
  const allowedHosts = new Set([
    `127.0.0.1:${port}`,
    `localhost:${port}`,
    `[::1]:${port}`,
  ]);

  const allowedOrigins = new Set([
    `http://127.0.0.1:${port}`,
    `http://localhost:${port}`,
    "http://127.0.0.1:5173",
  ]);

  return (request, response, next) => {
    const host = String(request.headers.host ?? "").toLowerCase();
    const origin = request.headers.origin;

    if (!allowedHosts.has(host)) {
      response.status(403).json({
        ok: false,
        error: { message: "Archivist only accepts requests from this computer." },
      });
      return;
    }

    if (origin !== undefined && !allowedOrigins.has(origin)) {
      response.status(403).json({
        ok: false,
        error: { message: "Archivist does not accept requests from web pages." },
      });
      return;
    }

    next();
  };
}

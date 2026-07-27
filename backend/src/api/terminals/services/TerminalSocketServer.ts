import type { Server } from "node:http";
import type { Duplex } from "node:stream";
import {
  WebSocket,
  WebSocketServer,
  type RawData,
} from "ws";
import { AppError } from "../../../errors/app-error.js";
import {
  terminalClientMessageSchema,
  terminalSocketQuerySchema,
} from "../schemas/TerminalSchemas.js";
import type { TerminalSocketContext } from "../types/TerminalTypes.js";
import { terminalSessionManager } from "./TerminalSessionManager.js";
import type { TerminalSession } from "./TerminalSession.js";

const terminalSocketPath = "/api/terminals/socket";
const acceptedOrigins = new Set([
  "http://127.0.0.1:3333",
  "http://localhost:3333",
]);

function isLoopbackAddress(address: string | undefined): boolean {
  return (
    address === "127.0.0.1"
    || address === "::1"
    || address === "::ffff:127.0.0.1"
  );
}

function rejectUpgrade(
  socket: Duplex,
  statusCode: number,
  message: string,
): void {
  const body = `${message}\n`;

  socket.write(
    `HTTP/1.1 ${statusCode} ${message}\r\n`
      + "Connection: close\r\n"
      + "Content-Type: text/plain; charset=utf-8\r\n"
      + `Content-Length: ${Buffer.byteLength(body)}\r\n`
      + "\r\n"
      + body,
  );
  socket.destroy();
}

function parseContext(requestUrl: string): TerminalSocketContext {
  const url = new URL(requestUrl, "http://127.0.0.1:3333");
  const parsed = terminalSocketQuerySchema.safeParse({
    sessionId: url.searchParams.get("sessionId"),
    collectionId: url.searchParams.get("collectionId"),
    libraryId: url.searchParams.get("libraryId"),
    cols: url.searchParams.get("cols") ?? undefined,
    rows: url.searchParams.get("rows") ?? undefined,
  });

  if (!parsed.success) {
    throw new AppError(
      400,
      "Invalid terminal connection.",
      parsed.error.flatten(),
    );
  }

  return parsed.data;
}

function parseClientMessage(data: RawData): unknown {
  return JSON.parse(data.toString("utf8"));
}

export function installTerminalSocketServer(
  server: Server,
): {
  close(): void;
} {
  const socketServer = new WebSocketServer({
    noServer: true,
    perMessageDeflate: false,
    maxPayload: 128 * 1024,
  });

  server.on("upgrade", (request, socket, head) => {
    const url = new URL(
      request.url ?? "/",
      "http://127.0.0.1:3333",
    );

    if (url.pathname !== terminalSocketPath) {
      rejectUpgrade(socket, 404, "Not Found");
      return;
    }

    if (!isLoopbackAddress(request.socket.remoteAddress)) {
      rejectUpgrade(socket, 403, "Forbidden");
      return;
    }

    const origin = request.headers.origin;

    if (!origin || !acceptedOrigins.has(origin)) {
      rejectUpgrade(socket, 403, "Forbidden");
      return;
    }

    let context: TerminalSocketContext;

    try {
      context = parseContext(request.url ?? "");
    } catch (error) {
      rejectUpgrade(
        socket,
        error instanceof AppError ? error.statusCode : 400,
        "Bad Request",
      );
      return;
    }

    let session: TerminalSession;

    try {
      session = terminalSessionManager.getOrCreate(context);
    } catch (error) {
      rejectUpgrade(
        socket,
        error instanceof AppError ? error.statusCode : 500,
        error instanceof AppError
          ? error.message
          : "Terminal unavailable",
      );
      return;
    }

    socketServer.handleUpgrade(
      request,
      socket,
      head,
      (webSocket) => {
        socketServer.emit(
          "connection",
          webSocket,
          request,
        );
        session.attach(webSocket);

        webSocket.on("message", (rawData, isBinary) => {
          if (isBinary) {
            webSocket.close(
              1003,
              "Binary terminal messages are not supported.",
            );
            return;
          }

          let message: unknown;

          try {
            message = parseClientMessage(rawData);
          } catch {
            webSocket.close(1007, "Invalid terminal message.");
            return;
          }

          const parsed = terminalClientMessageSchema.safeParse(
            message,
          );

          if (!parsed.success) {
            webSocket.close(1008, "Invalid terminal message.");
            return;
          }

          switch (parsed.data.type) {
            case "input":
              session.write(parsed.data.data);
              break;
            case "resize":
              session.resize(
                parsed.data.cols,
                parsed.data.rows,
              );
              break;
            case "kill":
              session.kill();
              break;
            case "ping":
              if (webSocket.readyState === WebSocket.OPEN) {
                webSocket.send(
                  JSON.stringify({
                    type: "pong",
                    sentAt: parsed.data.sentAt,
                  }),
                );
              }
              break;
          }
        });

        webSocket.on("close", () => {
          session.detach(webSocket);
        });

        webSocket.on("error", () => {
          session.detach(webSocket);
        });
      },
    );
  });

  return {
    close(): void {
      terminalSessionManager.shutdown();
      socketServer.close();
    },
  };
}

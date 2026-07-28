import { randomBytes, randomUUID } from "node:crypto";
import { spawn, type ChildProcess } from "node:child_process";
import {
  createServer,
  type IncomingMessage,
  type Server,
} from "node:http";
import type { AddressInfo } from "node:net";
import type { Duplex } from "node:stream";
import {
  type IWebSocket,
  WebSocketMessageReader,
  WebSocketMessageWriter,
} from "vscode-ws-jsonrpc";
import {
  createConnection,
  createProcessStreamConnection,
  forward,
} from "vscode-ws-jsonrpc/server";
import {
  WebSocket,
  WebSocketServer,
} from "ws";
import { AppError } from "../../../errors/app-error.js";
import {
  getLanguageServerDefinition,
} from "../registry/LanguageServerRegistry.js";
import {
  languageSupportSocketQuerySchema,
} from "../schemas/LanguageSupportSchemas.js";
import type {
  LanguageServerDefinition,
  LanguageServerSessionDescriptor,
  LanguageServerSessionSummary,
  ResolvedLanguageWorkspace,
} from "../types/LanguageSupportTypes.js";
import { resolveLanguageServerExecutable } from "./LanguageServerExecutableResolver.js";
import {
  resolveLanguageServerLaunch,
  type LanguageServerLaunchConfiguration,
} from "./LanguageServerLaunchResolver.js";
import { resolveLanguageWorkspace } from "./LanguageWorkspaceResolver.js";

const socketPath = "/language-support";
const pendingLifetimeMilliseconds = 60_000;
const maximumPendingSessions = 32;
const stderrTailLineLimit = 40;
const verboseServerStderr =
  process.env.ARCHIVIST_LANGUAGE_SERVER_TRACE === "1";

function isRoutineServerStderr(
  serverId: string,
  message: string,
): boolean {
  if (serverId === "clangd" && /^I\[/.test(message)) {
    return true;
  }

  if (serverId === "markdown" && /\bINF\b/.test(message)) {
    return true;
  }

  if (serverId === "qml") {
    return (
      message.includes("Did Setup")
      || message.includes("Using build directories passed by")
      || message.includes("Using import directories passed by")
      || message.includes("Disabling CMake calls")
    );
  }

  return (
    serverId === "rust"
    && message.includes("No path was found")
    && message.includes("rust-analyzer.toml")
  );
}

function configuredMaximumActiveSessions(): number {
  const configured = Number(
    process.env.ARCHIVIST_MAX_LANGUAGE_SERVERS ?? 24,
  );

  if (!Number.isFinite(configured)) {
    return 24;
  }

  return Math.max(4, Math.min(64, Math.floor(configured)));
}

const maximumActiveSessions =
  configuredMaximumActiveSessions();

interface PendingSession {
  descriptor: LanguageServerSessionDescriptor;
  token: string;
  definition: LanguageServerDefinition;
  workspace: ResolvedLanguageWorkspace;
  executablePath: string;
  launch: LanguageServerLaunchConfiguration;
  expiresAtMilliseconds: number;
}

interface ActiveSession {
  summary: LanguageServerSessionSummary;
  key: string;
  process: ChildProcess;
  socket: WebSocket;
  startedAtMilliseconds: number;
  stderrBuffer: string;
  stderrTail: string[];
}

function isLoopbackAddress(
  address: string | undefined,
): boolean {
  return (
    address === "127.0.0.1"
    || address === "::1"
    || address === "::ffff:127.0.0.1"
  );
}

function acceptedOrigin(origin: string | undefined): boolean {
  if (!origin) {
    return false;
  }

  const backendPort = Number(process.env.PORT ?? 3333);

  return (
    origin === `http://127.0.0.1:${backendPort}`
    || origin === `http://localhost:${backendPort}`
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

function sessionKey(
  definition: LanguageServerDefinition,
  workspace: ResolvedLanguageWorkspace,
): string {
  return [
    definition.id,
    workspace.workspaceRoot,
  ].join(":");
}

function minimalEnvironment(): NodeJS.ProcessEnv {
  const environment: NodeJS.ProcessEnv = {};
  const allowedKeys = [
    "PATH",
    "HOME",
    "USER",
    "LOGNAME",
    "SHELL",
    "TMPDIR",
    "TMP",
    "TEMP",
    "LANG",
    "LC_ALL",
    "PATHEXT",
    "SystemRoot",
    "WINDIR",
    "ComSpec",
  ];

  for (const key of allowedKeys) {
    const value = process.env[key];

    if (value) {
      environment[key] = value;
    }
  }

  environment.ARCHIVIST_LANGUAGE_SUPPORT = "1";
  environment.NO_COLOR = "1";

  return environment;
}

function webSocketAdapter(
  webSocket: WebSocket,
): IWebSocket {
  return {
    send: (content) => {
      webSocket.send(content, (error) => {
        if (error) {
          webSocket.close(
            1011,
            "Language-support transport failed.",
          );
        }
      });
    },
    onMessage: (callback) => {
      webSocket.on("message", (data) => {
        callback(data);
      });
    },
    onError: (callback) => {
      webSocket.on("error", callback);
    },
    onClose: (callback) => {
      webSocket.on("close", callback);
    },
    dispose: () => {
      if (
        webSocket.readyState === WebSocket.OPEN
        || webSocket.readyState === WebSocket.CONNECTING
      ) {
        webSocket.close();
      }
    },
  };
}

export class LanguageSupportSocketServer {
  private readonly httpServer: Server;
  private readonly webSocketServer: WebSocketServer;
  private readonly pendingSessions = new Map<
    string,
    PendingSession
  >();
  private readonly activeSessions = new Map<
    string,
    ActiveSession
  >();
  private readonly activeSessionIdByKey = new Map<
    string,
    string
  >();
  private port = 0;
  private started = false;
  private cleanupTimer: NodeJS.Timeout | null = null;

  constructor() {
    this.httpServer = createServer(
      (_request, response) => {
        response.writeHead(404, {
          "Content-Type": "text/plain; charset=utf-8",
        });
        response.end("Not Found\n");
      },
    );
    this.webSocketServer = new WebSocketServer({
      noServer: true,
      perMessageDeflate: false,
      maxPayload: 4 * 1024 * 1024,
    });

    this.httpServer.on(
      "upgrade",
      (request, socket, head) => {
        this.handleUpgrade(request, socket, head);
      },
    );
  }

  async start(): Promise<void> {
    if (this.started) {
      return;
    }

    await new Promise<void>((resolve, reject) => {
      const handleError = (error: Error) => {
        reject(error);
      };

      this.httpServer.once("error", handleError);
      this.httpServer.listen(
        0,
        "127.0.0.1",
        () => {
          this.httpServer.off("error", handleError);
          resolve();
        },
      );
    });

    const address = this.httpServer.address();

    if (
      !address
      || typeof address === "string"
    ) {
      throw new Error(
        "Language Support could not resolve its loopback port.",
      );
    }

    this.port = (address as AddressInfo).port;
    this.started = true;
    this.cleanupTimer = setInterval(() => {
      this.removeExpiredPendingSessions();
    }, 15_000);
    this.cleanupTimer.unref();

    console.log(
      `Language Support listening at ws://127.0.0.1:${this.port}${socketPath}`
      + ` · max active ${maximumActiveSessions}`,
    );
  }

  createSession(input: {
    libraryId: string;
    serverId: string;
    workspaceRoot?: string;
    filePath?: string;
  }): LanguageServerSessionDescriptor {
    if (!this.started || this.port <= 0) {
      throw new AppError(
        503,
        "Language Support is still starting.",
      );
    }

    this.removeExpiredPendingSessions();

    if (
      this.pendingSessions.size
      >= maximumPendingSessions
    ) {
      throw new AppError(
        429,
        "Too many language-support sessions are pending.",
      );
    }

    const definition = getLanguageServerDefinition(
      input.serverId,
    );

    if (!definition) {
      throw new AppError(
        404,
        "Language server is not registered.",
      );
    }

    if (!definition.enabledByDefault) {
      throw new AppError(
        409,
        "This language server is disabled.",
      );
    }

    const workspace = resolveLanguageWorkspace(
      definition,
      {
        libraryId: input.libraryId,
        workspaceRoot: input.workspaceRoot,
        filePath: input.filePath,
      },
    );
    const executablePath =
      resolveLanguageServerExecutable(
        definition,
        workspace.workspaceRoot,
      );

    if (!executablePath) {
      throw new AppError(
        409,
        `${definition.displayName} is not installed.`,
      );
    }

    const launch = resolveLanguageServerLaunch(
      definition,
      workspace,
    );
    const sessionId = randomUUID();
    const token = randomBytes(32).toString("hex");
    const expiresAtMilliseconds =
      Date.now() + pendingLifetimeMilliseconds;
    const expiresAt = new Date(
      expiresAtMilliseconds,
    ).toISOString();
    const socketUrl = new URL(
      `ws://127.0.0.1:${this.port}${socketPath}`,
    );

    socketUrl.searchParams.set(
      "sessionId",
      sessionId,
    );
    socketUrl.searchParams.set("token", token);

    const descriptor: LanguageServerSessionDescriptor = {
      sessionId,
      serverId: definition.id,
      displayName: definition.displayName,
      languageIds: definition.languageIds,
      state: "pending",
      workspaceRoot: workspace.workspaceRoot,
      filePath: workspace.filePath,
      socketUrl: socketUrl.toString(),
      expiresAt,
    };

    this.pendingSessions.set(sessionId, {
      descriptor,
      token,
      definition,
      workspace,
      executablePath,
      launch,
      expiresAtMilliseconds,
    });

    return descriptor;
  }

  listSessions(): LanguageServerSessionSummary[] {
    this.removeExpiredPendingSessions();

    const pending: LanguageServerSessionSummary[] = [
      ...this.pendingSessions.values(),
    ].map((session) => ({
      sessionId: session.descriptor.sessionId,
      serverId: session.descriptor.serverId,
      displayName: session.descriptor.displayName,
      state: "pending",
      workspaceRoot:
        session.descriptor.workspaceRoot,
      filePath: session.descriptor.filePath,
      processId: null,
      connectedAt: null,
      expiresAt: session.descriptor.expiresAt,
    }));
    const active = [...this.activeSessions.values()].map(
      (session) => session.summary,
    );

    return [...pending, ...active];
  }

  close(): void {
    if (this.cleanupTimer) {
      clearInterval(this.cleanupTimer);
      this.cleanupTimer = null;
    }

    for (const session of this.activeSessions.values()) {
      this.stopActiveSession(session);
    }

    this.activeSessions.clear();
    this.activeSessionIdByKey.clear();
    this.pendingSessions.clear();
    this.webSocketServer.close();
    this.httpServer.close();
    this.started = false;
    this.port = 0;
  }

  private removeExpiredPendingSessions(): void {
    const now = Date.now();

    for (
      const [sessionId, session]
      of this.pendingSessions
    ) {
      if (session.expiresAtMilliseconds <= now) {
        this.pendingSessions.delete(sessionId);
      }
    }
  }

  private consumePendingSession(
    requestUrl: string,
  ): PendingSession {
    const url = new URL(
      requestUrl,
      `http://127.0.0.1:${this.port}`,
    );
    const parsed =
      languageSupportSocketQuerySchema.safeParse({
        sessionId: url.searchParams.get("sessionId"),
        token: url.searchParams.get("token"),
      });

    if (!parsed.success) {
      throw new AppError(
        400,
        "Invalid language-support connection.",
      );
    }

    const pending = this.pendingSessions.get(
      parsed.data.sessionId,
    );

    if (
      !pending
      || pending.token !== parsed.data.token
      || pending.expiresAtMilliseconds <= Date.now()
    ) {
      this.pendingSessions.delete(
        parsed.data.sessionId,
      );
      throw new AppError(
        403,
        "The language-support session expired or is invalid.",
      );
    }

    this.pendingSessions.delete(parsed.data.sessionId);
    return pending;
  }

  private handleUpgrade(
    request: IncomingMessage,
    socket: Duplex,
    head: Buffer,
  ): void {
    const url = new URL(
      request.url ?? "/",
      `http://127.0.0.1:${this.port}`,
    );

    if (url.pathname !== socketPath) {
      rejectUpgrade(socket, 404, "Not Found");
      return;
    }

    if (!isLoopbackAddress(request.socket.remoteAddress)) {
      rejectUpgrade(socket, 403, "Forbidden");
      return;
    }

    if (!acceptedOrigin(request.headers.origin)) {
      rejectUpgrade(socket, 403, "Forbidden");
      return;
    }

    let pending: PendingSession;

    try {
      pending = this.consumePendingSession(
        request.url ?? "",
      );
    } catch (error) {
      rejectUpgrade(
        socket,
        error instanceof AppError
          ? error.statusCode
          : 400,
        error instanceof AppError
          ? error.message
          : "Bad Request",
      );
      return;
    }

    this.webSocketServer.handleUpgrade(
      request,
      socket,
      head,
      (webSocket) => {
        this.launchSession(pending, webSocket);
      },
    );
  }

  private launchSession(
    pending: PendingSession,
    webSocket: WebSocket,
  ): void {
    const key = sessionKey(
      pending.definition,
      pending.workspace,
    );
    const existingSessionId =
      this.activeSessionIdByKey.get(key);

    if (existingSessionId) {
      const existing = this.activeSessions.get(
        existingSessionId,
      );

      if (existing) {
        this.finishActiveSession(
          existing,
          "replaced by a new workspace session",
        );
      }
    }

    if (
      this.activeSessions.size
      >= maximumActiveSessions
    ) {
      const active = [...this.activeSessions.values()]
        .map((session) =>
          `${session.summary.serverId}@${session.summary.workspaceRoot}`
        )
        .join(" | ");
      console.warn(
        `[Language Support] Refused ${pending.definition.id}`
        + ` workspace=${pending.workspace.workspaceRoot}`
        + ` active=${this.activeSessions.size}/${maximumActiveSessions}`
        + ` sessions=${active || "none"}`,
      );
      webSocket.close(
        1013,
        "Too many language servers are active.",
      );
      return;
    }

    if (pending.launch.buildDirectory) {
      console.log(
        `[Language Support:${pending.definition.id}] Using build directory ${pending.launch.buildDirectory}`,
      );
    }

    if (pending.launch.importDirectories.length > 0) {
      console.log(
        `[Language Support:${pending.definition.id}] Using import paths ${pending.launch.importDirectories.join(", ")}`,
      );
    }

    console.log(
      `[Language Support:${pending.definition.id}] launching`
      + ` session=${pending.descriptor.sessionId}`
      + ` active=${this.activeSessions.size + 1}/${maximumActiveSessions}`
      + ` cwd=${pending.workspace.workspaceRoot}`
      + ` executable=${pending.executablePath}`
      + ` args=${JSON.stringify(pending.launch.args)}`,
    );

    const child = spawn(
      pending.executablePath,
      [...pending.launch.args],
      {
        cwd: pending.workspace.workspaceRoot,
        env: minimalEnvironment(),
        shell: false,
        stdio: ["pipe", "pipe", "pipe"],
        windowsHide: true,
      },
    );
    const processConnection =
      createProcessStreamConnection(child);

    if (!processConnection) {
      child.kill();
      webSocket.close(
        1011,
        "Language server streams are unavailable.",
      );
      return;
    }

    const connectedAt = new Date().toISOString();
    const summary: LanguageServerSessionSummary = {
      sessionId: pending.descriptor.sessionId,
      serverId: pending.definition.id,
      displayName: pending.definition.displayName,
      state: "connected",
      workspaceRoot: pending.workspace.workspaceRoot,
      filePath: pending.workspace.filePath,
      processId: child.pid ?? null,
      connectedAt,
      expiresAt: pending.descriptor.expiresAt,
    };
    const active: ActiveSession = {
      summary,
      key,
      process: child,
      socket: webSocket,
      startedAtMilliseconds: Date.now(),
      stderrBuffer: "",
      stderrTail: [],
    };

    this.activeSessions.set(summary.sessionId, active);
    this.activeSessionIdByKey.set(key, summary.sessionId);

    const socket = webSocketAdapter(webSocket);
    const socketConnection = createConnection(
      new WebSocketMessageReader(socket),
      new WebSocketMessageWriter(socket),
      () => socket.dispose(),
    );

    forward(socketConnection, processConnection);

    child.stderr?.on("data", (data: Buffer) => {
      this.recordServerStderr(
        active,
        pending.definition.id,
        data,
      );
    });

    child.on("error", (error) => {
      console.error(
        `[Language Support:${pending.definition.id}] process error`
        + ` session=${summary.sessionId}`
        + ` message=${error.message}`,
      );
      this.finishActiveSession(active, "process error");
    });

    child.on("exit", (code, signal) => {
      this.flushServerStderr(
        active,
        pending.definition.id,
      );
      const durationMilliseconds =
        Date.now() - active.startedAtMilliseconds;
      const detail = active.stderrTail.length > 0
        ? ` stderrTail=${JSON.stringify(active.stderrTail)}`
        : "";

      console.warn(
        `[Language Support:${pending.definition.id}] process exited`
        + ` session=${summary.sessionId}`
        + ` code=${code === null ? "null" : code}`
        + ` signal=${signal || "none"}`
        + ` durationMs=${durationMilliseconds}`
        + detail,
      );

      this.finishActiveSession(active, "process exit");
    });

    webSocket.on("close", (code, reason) => {
      this.finishActiveSession(
        active,
        `socket close ${code} ${reason.toString()}`.trim(),
      );
    });
    webSocket.on("error", (error) => {
      console.warn(
        `[Language Support:${pending.definition.id}] socket error`
        + ` session=${summary.sessionId}`
        + ` message=${error.message}`,
      );
      this.finishActiveSession(active, "socket error");
    });

    console.log(
      `[Language Support:${pending.definition.id}] transport connected`
      + ` session=${summary.sessionId}`
      + ` pid=${summary.processId ?? "unknown"}`
      + ` workspace=${pending.workspace.workspaceRoot}`,
    );
  }

  private recordServerStderr(
    session: ActiveSession,
    serverId: string,
    data: Buffer,
  ): void {
    session.stderrBuffer += data.toString("utf8");
    const lines = session.stderrBuffer.split(/\r?\n/);
    session.stderrBuffer = lines.pop() ?? "";

    for (const line of lines) {
      const message = line.trim();

      if (!message) {
        continue;
      }

      if (
        !verboseServerStderr
        && isRoutineServerStderr(serverId, message)
      ) {
        continue;
      }

      session.stderrTail.push(message);

      if (session.stderrTail.length > stderrTailLineLimit) {
        session.stderrTail.shift();
      }

      console.warn(
        `[Language Support:${serverId}] stderr ${message}`,
      );
    }
  }

  private flushServerStderr(
    session: ActiveSession,
    serverId: string,
  ): void {
    const message = session.stderrBuffer.trim();
    session.stderrBuffer = "";

    if (!message) {
      return;
    }

    if (
      !verboseServerStderr
      && isRoutineServerStderr(serverId, message)
    ) {
      return;
    }

    session.stderrTail.push(message);

    if (session.stderrTail.length > stderrTailLineLimit) {
      session.stderrTail.shift();
    }

    console.warn(
      `[Language Support:${serverId}] stderr ${message}`,
    );
  }

  private finishActiveSession(
    session: ActiveSession,
    reason: string,
  ): void {
    if (
      this.activeSessions.get(
        session.summary.sessionId,
      ) !== session
    ) {
      return;
    }

    this.activeSessions.delete(
      session.summary.sessionId,
    );

    if (
      this.activeSessionIdByKey.get(session.key)
      === session.summary.sessionId
    ) {
      this.activeSessionIdByKey.delete(session.key);
    }

    console.log(
      `[Language Support:${session.summary.serverId}] session closed`
      + ` session=${session.summary.sessionId}`
      + ` reason=${reason}`
      + ` active=${this.activeSessions.size}/${maximumActiveSessions}`
      + ` workspace=${session.summary.workspaceRoot}`,
    );
    this.stopActiveSession(session);
  }

  private stopActiveSession(
    session: ActiveSession,
  ): void {
    if (
      session.socket.readyState === WebSocket.OPEN
      || session.socket.readyState === WebSocket.CONNECTING
    ) {
      session.socket.close();
    }

    if (
      session.process.exitCode === null
      && session.process.signalCode === null
    ) {
      session.process.kill();
    }
  }
}

export const languageSupportSocketServer =
  new LanguageSupportSocketServer();

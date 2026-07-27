import {
  isJsonRpcRequest,
  isJsonRpcResponse,
  type JsonRpcErrorShape,
  type JsonRpcId,
  type JsonRpcMessage,
} from "./LspTypes.js";

interface PendingRequest {
  method: string;
  resolve(value: unknown): void;
  reject(error: Error): void;
  timeoutId: number;
}

export interface LspConnectionHandlers {
  handleNotification(
    method: string,
    params: unknown,
  ): void;
  handleRequest(
    method: string,
    params: unknown,
  ): Promise<unknown> | unknown;
  handleClose(message: string): void;
}

export class LspResponseError extends Error {
  constructor(
    readonly code: number,
    message: string,
    readonly data?: unknown,
  ) {
    super(message);
    this.name = "LspResponseError";
  }
}

export class LspConnection {
  private readonly pending = new Map<
    JsonRpcId,
    PendingRequest
  >();

  private nextRequestId = 1;
  private closed = false;

  private constructor(
    private readonly socket: WebSocket,
    private readonly handlers: LspConnectionHandlers,
  ) {
    socket.addEventListener("message", (event) => {
      void this.handleRawMessage(event.data);
    });
    socket.addEventListener("close", (event) => {
      this.finish(
        event.reason
          || `Language-support socket closed (${event.code}).`,
      );
    });
    socket.addEventListener("error", () => {
      this.finish("Language-support socket failed.");
    });
  }

  static open(
    socketUrl: string,
    handlers: LspConnectionHandlers,
  ): Promise<LspConnection> {
    return new Promise((resolve, reject) => {
      const socket = new WebSocket(socketUrl);
      const timeoutId = window.setTimeout(() => {
        socket.close();
        reject(
          new Error(
            "Language-support WebSocket connection timed out.",
          ),
        );
      }, 10_000);

      const handleOpen = (): void => {
        window.clearTimeout(timeoutId);
        socket.removeEventListener("error", handleInitialError);
        resolve(new LspConnection(socket, handlers));
      };
      const handleInitialError = (): void => {
        window.clearTimeout(timeoutId);
        socket.removeEventListener("open", handleOpen);
        reject(
          new Error(
            "Language-support WebSocket could not connect.",
          ),
        );
      };

      socket.addEventListener("open", handleOpen, {
        once: true,
      });
      socket.addEventListener("error", handleInitialError, {
        once: true,
      });
    });
  }

  request<Result>(
    method: string,
    params?: unknown,
  ): Promise<Result> {
    if (this.closed) {
      return Promise.reject(
        new Error("Language-support connection is closed."),
      );
    }

    const id = this.nextRequestId;
    this.nextRequestId += 1;

    return new Promise<Result>((resolve, reject) => {
      const timeoutId = window.setTimeout(() => {
        this.pending.delete(id);
        reject(
          new Error(
            `Language-server request timed out: ${method}`,
          ),
        );
      }, 20_000);

      this.pending.set(id, {
        method,
        resolve: (value) => resolve(value as Result),
        reject,
        timeoutId,
      });

      this.send({
        jsonrpc: "2.0",
        id,
        method,
        params,
      });
    });
  }

  notify(method: string, params?: unknown): void {
    if (this.closed) {
      return;
    }

    this.send({
      jsonrpc: "2.0",
      method,
      params,
    });
  }

  dispose(): void {
    if (this.closed) {
      return;
    }

    this.closed = true;
    this.rejectPending(
      new Error("Language-support connection was disposed."),
    );
    this.socket.close(1000, "Archivist closed the session.");
  }

  private send(message: JsonRpcMessage): void {
    if (
      this.closed
      || this.socket.readyState !== WebSocket.OPEN
    ) {
      return;
    }

    this.socket.send(JSON.stringify(message));
  }

  private async handleRawMessage(data: unknown): Promise<void> {
    let text: string;

    if (typeof data === "string") {
      text = data;
    } else if (data instanceof Blob) {
      text = await data.text();
    } else if (data instanceof ArrayBuffer) {
      text = new TextDecoder().decode(data);
    } else {
      return;
    }

    let message: JsonRpcMessage;

    try {
      message = JSON.parse(text) as JsonRpcMessage;
    } catch {
      return;
    }

    if (isJsonRpcResponse(message)) {
      this.handleResponse(message.id, message.result, message.error);
      return;
    }

    if (isJsonRpcRequest(message)) {
      await this.handleServerRequest(
        message.id,
        message.method,
        message.params,
      );
      return;
    }

    this.handlers.handleNotification(
      message.method,
      message.params,
    );
  }

  private handleResponse(
    id: JsonRpcId,
    result: unknown,
    error: JsonRpcErrorShape | undefined,
  ): void {
    const pending = this.pending.get(id);

    if (!pending) {
      return;
    }

    this.pending.delete(id);
    window.clearTimeout(pending.timeoutId);

    if (error) {
      pending.reject(
        new LspResponseError(
          error.code,
          `${pending.method}: ${error.message}`,
          error.data,
        ),
      );
      return;
    }

    pending.resolve(result);
  }

  private async handleServerRequest(
    id: JsonRpcId,
    method: string,
    params: unknown,
  ): Promise<void> {
    try {
      const result = await this.handlers.handleRequest(
        method,
        params,
      );

      this.send({
        jsonrpc: "2.0",
        id,
        result,
      });
    } catch (error) {
      const responseError = error instanceof LspResponseError
        ? error
        : new LspResponseError(
            -32603,
            error instanceof Error
              ? error.message
              : "Language-client request failed.",
          );

      this.send({
        jsonrpc: "2.0",
        id,
        error: {
          code: responseError.code,
          message: responseError.message,
          data: responseError.data,
        },
      });
    }
  }

  private finish(message: string): void {
    if (this.closed) {
      return;
    }

    this.closed = true;
    this.rejectPending(new Error(message));
    this.handlers.handleClose(message);
  }

  private rejectPending(error: Error): void {
    for (const pending of this.pending.values()) {
      window.clearTimeout(pending.timeoutId);
      pending.reject(error);
    }

    this.pending.clear();
  }
}

import type * as Monaco from "monaco-editor/editor/editor.api";
import {
  LspConnection,
  LspResponseError,
} from "./LspConnection.js";
import {
  diagnosticMarker,
  fileName,
} from "./LspMonacoConversions.js";
import {
  isRecord,
  type LanguageServerSessionDescriptor,
  type LspPublishDiagnosticsParams,
} from "./LspTypes.js";

type MonacoApi = typeof Monaco;

type Disposable = {
  dispose(): void;
};

export interface LanguageSupportCallbacks {
  reportStatus(message: string): void;
}

interface ManagedDocument {
  model: Monaco.editor.ITextModel;
  languageId: string;
  version: number;
  changeDisposable: Disposable;
  disposeDisposable: Disposable;
}

export class WorkspaceLanguageClient {
  private connection: LspConnection | null = null;
  private readonly documents = new Map<
    string,
    ManagedDocument
  >();
  private disposed = false;

  constructor(
    private readonly monaco: MonacoApi,
    readonly descriptor: LanguageServerSessionDescriptor,
    readonly libraryId: string,
    private readonly callbacks: LanguageSupportCallbacks,
    private readonly onClosed: (
      client: WorkspaceLanguageClient,
    ) => void,
  ) {}

  get key(): string {
    return [
      this.descriptor.serverId,
      this.descriptor.workspaceRoot,
    ].join(":");
  }

  get workspaceRoot(): string {
    return this.descriptor.workspaceRoot;
  }

  async connect(): Promise<void> {
    this.connection = await LspConnection.open(
      this.descriptor.socketUrl,
      {
        handleNotification: (method, params) => {
          this.handleNotification(method, params);
        },
        handleRequest: (method, params) =>
          this.handleServerRequest(method, params),
        handleClose: (message) => {
          this.handleClose(message);
        },
      },
    );

    const workspaceUri = this.monaco.Uri.file(
      this.descriptor.workspaceRoot,
    ).toString();
    await this.connection.request<unknown>(
      "initialize",
      {
        processId: null,
        clientInfo: {
          name: "Archivist",
          version: "0.1.0",
        },
        locale: navigator.language,
        rootPath: this.descriptor.workspaceRoot,
        rootUri: workspaceUri,
        capabilities: {
          general: {
            positionEncodings: ["utf-16"],
          },
          workspace: {
            applyEdit: false,
            configuration: false,
            workspaceFolders: true,
          },
          window: {
            workDoneProgress: true,
          },
          textDocument: {
            synchronization: {
              dynamicRegistration: false,
              willSave: false,
              willSaveWaitUntil: false,
              didSave: false,
            },
            completion: {
              dynamicRegistration: false,
              contextSupport: true,
              completionItem: {
                snippetSupport: true,
                commitCharactersSupport: true,
                documentationFormat: [
                  "markdown",
                  "plaintext",
                ],
                deprecatedSupport: true,
                preselectSupport: true,
                insertReplaceSupport: true,
                labelDetailsSupport: true,
              },
            },
            hover: {
              dynamicRegistration: false,
              contentFormat: ["markdown", "plaintext"],
            },
            signatureHelp: {
              dynamicRegistration: false,
              contextSupport: true,
              signatureInformation: {
                documentationFormat: [
                  "markdown",
                  "plaintext",
                ],
                parameterInformation: {
                  labelOffsetSupport: true,
                },
                activeParameterSupport: true,
              },
            },
            definition: {
              dynamicRegistration: false,
              linkSupport: true,
            },
            references: {
              dynamicRegistration: false,
            },
            publishDiagnostics: {
              relatedInformation: true,
              tagSupport: {
                valueSet: [1, 2],
              },
              versionSupport: true,
            },
          },
        },
        workspaceFolders: [
          {
            uri: workspaceUri,
            name: fileName(this.descriptor.workspaceRoot),
          },
        ],
        trace: "off",
      },
    );

    this.connection.notify("initialized", {});
    console.info(
      `[Language Support] ${this.descriptor.displayName} initialized for ${this.descriptor.workspaceRoot}`,
    );
  }

  attachDocument(
    model: Monaco.editor.ITextModel,
    languageId: string,
  ): void {
    if (this.disposed || !this.connection || model.isDisposed()) {
      return;
    }

    const uri = model.uri.toString();

    if (this.documents.has(uri)) {
      return;
    }

    const managed: ManagedDocument = {
      model,
      languageId,
      version: model.getVersionId(),
      changeDisposable: model.onDidChangeContent(() => {
        const current = this.documents.get(uri);

        if (!current || !this.connection) {
          return;
        }

        current.version = model.getVersionId();
        this.connection.notify("textDocument/didChange", {
          textDocument: {
            uri,
            version: current.version,
          },
          contentChanges: [
            {
              text: model.getValue(),
            },
          ],
        });
      }),
      disposeDisposable: model.onWillDispose(() => {
        this.detachDocument(uri);
      }),
    };

    this.documents.set(uri, managed);
    this.connection.notify("textDocument/didOpen", {
      textDocument: {
        uri,
        languageId,
        version: managed.version,
        text: model.getValue(),
      },
    });
  }

  reopenDocument(
    model: Monaco.editor.ITextModel,
  ): boolean {
    if (this.disposed || !this.connection || model.isDisposed()) {
      return false;
    }

    const uri = model.uri.toString();
    const document = this.documents.get(uri);

    if (!document) {
      return false;
    }

    document.version = model.getVersionId();
    this.connection.notify("textDocument/didOpen", {
      textDocument: {
        uri,
        languageId: document.languageId,
        version: document.version,
        text: model.getValue(),
      },
    });

    console.debug(
      `[Language Support] Reopened ${uri} after a missing-model response.`,
    );
    return true;
  }

  request<Result>(
    method: string,
    params: unknown,
  ): Promise<Result> {
    if (!this.connection) {
      return Promise.reject(
        new Error("Language-support connection is not ready."),
      );
    }

    return this.connection.request<Result>(method, params);
  }

  dispose(): void {
    if (this.disposed) {
      return;
    }

    this.disposed = true;

    for (const uri of [...this.documents.keys()]) {
      this.detachDocument(uri);
    }

    this.connection?.dispose();
    this.connection = null;
  }

  private detachDocument(uri: string): void {
    const document = this.documents.get(uri);

    if (!document) {
      return;
    }

    this.documents.delete(uri);
    document.changeDisposable.dispose();
    document.disposeDisposable.dispose();
    this.connection?.notify("textDocument/didClose", {
      textDocument: { uri },
    });
    this.monaco.editor.setModelMarkers(
      document.model,
      `archivist-lsp:${this.descriptor.serverId}`,
      [],
    );
  }

  private handleNotification(
    method: string,
    params: unknown,
  ): void {
    switch (method) {
      case "textDocument/publishDiagnostics":
        this.publishDiagnostics(params);
        break;
      case "window/showMessage":
      case "window/logMessage":
        if (isRecord(params) && typeof params.message === "string") {
          console.info(
            `[Language Support:${this.descriptor.serverId}] ${params.message}`,
          );
        }
        break;
      case "language/status":
        if (isRecord(params) && typeof params.message === "string") {
          this.callbacks.reportStatus(params.message);
        }
        break;
    }
  }

  private handleServerRequest(
    method: string,
    params: unknown,
  ): unknown {
    switch (method) {
      case "workspace/configuration": {
        const items = isRecord(params)
          && Array.isArray(params.items)
            ? params.items
            : [];
        return items.map(() => null);
      }
      case "workspace/workspaceFolders": {
        const uri = this.monaco.Uri.file(
          this.descriptor.workspaceRoot,
        ).toString();
        return [
          {
            uri,
            name: fileName(this.descriptor.workspaceRoot),
          },
        ];
      }
      case "client/registerCapability":
      case "client/unregisterCapability":
      case "window/workDoneProgress/create":
      case "workspace/diagnostic/refresh":
      case "workspace/semanticTokens/refresh":
      case "workspace/inlayHint/refresh":
        return null;
      case "workspace/applyEdit":
        return {
          applied: false,
          failureReason:
            "Archivist does not apply language-server edits yet.",
        };
      case "window/showMessageRequest":
        return null;
      case "window/showDocument":
        return { success: false };
      default:
        throw new LspResponseError(
          -32601,
          `Unsupported language-server request: ${method}`,
        );
    }
  }

  private publishDiagnostics(params: unknown): void {
    if (
      !isRecord(params)
      || typeof params.uri !== "string"
      || !Array.isArray(params.diagnostics)
    ) {
      return;
    }

    const typed = params as unknown as LspPublishDiagnosticsParams;
    const model = this.monaco.editor.getModel(
      this.monaco.Uri.parse(typed.uri),
    );

    if (!model) {
      return;
    }

    this.monaco.editor.setModelMarkers(
      model,
      `archivist-lsp:${this.descriptor.serverId}`,
      typed.diagnostics.map((diagnostic) =>
        diagnosticMarker(this.monaco, diagnostic),
      ),
    );
  }

  private handleClose(message: string): void {
    if (this.disposed) {
      return;
    }

    console.warn(
      `[Language Support:${this.descriptor.serverId}] ${message}`,
    );
    this.callbacks.reportStatus(
      `${this.descriptor.displayName} disconnected`,
    );
    this.dispose();
    this.onClosed(this);
  }
}

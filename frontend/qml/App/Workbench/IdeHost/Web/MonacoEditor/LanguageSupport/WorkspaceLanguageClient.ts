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
import {
  recordLanguageSupportTelemetry,
} from "./LanguageSupportTelemetry.js";
import {
  languageServerConfiguration,
  languageServerInitializationOptions,
  languageServerSettings,
} from "./LanguageServerSettings.js";

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

function diagnosticSeverityName(
  severity: number | undefined,
): string {
  switch (severity) {
    case 2:
      return "warning";
    case 3:
      return "info";
    case 4:
      return "hint";
    default:
      return "error";
  }
}

function initializeCapabilityNames(result: unknown): string[] {
  if (!isRecord(result) || !isRecord(result.capabilities)) {
    return [];
  }

  return Object.entries(result.capabilities)
    .filter(([, value]) => value !== false && value !== null)
    .map(([name]) => name)
    .sort();
}

export class WorkspaceLanguageClient {
  private connection: LspConnection | null = null;
  private readonly documents = new Map<
    string,
    ManagedDocument
  >();
  private readonly diagnosticFingerprintByUri = new Map<
    string,
    string
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
    const initializeResult = await this.connection.request<unknown>(
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
            configuration: true,
            didChangeConfiguration: {
              dynamicRegistration: false,
            },
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
            formatting: {
              dynamicRegistration: false,
            },
            rangeFormatting: {
              dynamicRegistration: false,
            },
            rename: {
              dynamicRegistration: false,
              prepareSupport: true,
            },
            codeAction: {
              dynamicRegistration: false,
              dataSupport: true,
              disabledSupport: true,
              isPreferredSupport: true,
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
        initializationOptions:
          languageServerInitializationOptions(
            this.descriptor.serverId,
          ),
        trace: "off",
      },
    );

    this.connection.notify("initialized", {});
    this.connection.notify(
      "workspace/didChangeConfiguration",
      {
        settings: languageServerSettings(
          this.descriptor.serverId,
        ),
      },
    );
    const capabilities = initializeCapabilityNames(
      initializeResult,
    );
    console.info(
      `[Language Support:${this.descriptor.serverId}] initialized`
      + ` workspace=${this.descriptor.workspaceRoot}`
      + ` capabilities=${capabilities.join(",") || "none"}`,
    );
    recordLanguageSupportTelemetry({
      kind: "initialized",
      provider: "lsp",
      serverId: this.descriptor.serverId,
      workspaceRoot: this.descriptor.workspaceRoot,
      capabilities,
    });
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
    console.info(
      `[Language Support:${this.descriptor.serverId}] didOpen`
      + ` lspLanguage=${languageId}`
      + ` monacoLanguage=${model.getLanguageId()}`
      + ` version=${managed.version}`
      + ` uri=${uri}`,
    );
    recordLanguageSupportTelemetry({
      kind: "document-open",
      provider: "lsp",
      serverId: this.descriptor.serverId,
      workspaceRoot: this.descriptor.workspaceRoot,
      uri,
      monacoLanguageId: model.getLanguageId(),
      lspLanguageId: languageId,
      version: managed.version,
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
    this.diagnosticFingerprintByUri.delete(uri);
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
        return items.map((item) => {
          const section = isRecord(item)
            && typeof item.section === "string"
              ? item.section
              : "";

          return languageServerConfiguration(
            this.descriptor.serverId,
            section,
          );
        });
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

    const fingerprint = JSON.stringify(
      typed.diagnostics.map((diagnostic) => [
        diagnostic.severity ?? 1,
        diagnostic.range.start.line,
        diagnostic.range.start.character,
        diagnostic.message,
      ]),
    );

    if (
      this.diagnosticFingerprintByUri.get(typed.uri)
      === fingerprint
    ) {
      return;
    }

    this.diagnosticFingerprintByUri.set(
      typed.uri,
      fingerprint,
    );

    const counts = {
      error: 0,
      warning: 0,
      info: 0,
      hint: 0,
    };

    for (const diagnostic of typed.diagnostics) {
      counts[diagnosticSeverityName(
        diagnostic.severity,
      ) as keyof typeof counts] += 1;
    }

    console.info(
      `[Language Support:${this.descriptor.serverId}] diagnostics`
      + ` total=${typed.diagnostics.length}`
      + ` errors=${counts.error}`
      + ` warnings=${counts.warning}`
      + ` info=${counts.info}`
      + ` hints=${counts.hint}`
      + ` uri=${typed.uri}`,
    );

    for (const diagnostic of typed.diagnostics.slice(0, 8)) {
      console.info(
        `[Language Support:${this.descriptor.serverId}] diagnostic`
        + ` severity=${diagnosticSeverityName(diagnostic.severity)}`
        + ` line=${diagnostic.range.start.line + 1}`
        + ` column=${diagnostic.range.start.character + 1}`
        + ` message=${diagnostic.message.replace(/\s+/g, " ").trim()}`,
      );
    }

    if (typed.diagnostics.length > 8) {
      console.info(
        `[Language Support:${this.descriptor.serverId}] diagnostic`
        + ` omitted=${typed.diagnostics.length - 8}`
        + ` uri=${typed.uri}`,
      );
    }

    recordLanguageSupportTelemetry({
      kind: "diagnostics",
      provider: "lsp",
      serverId: this.descriptor.serverId,
      workspaceRoot: this.descriptor.workspaceRoot,
      uri: typed.uri,
      diagnostics: {
        total: typed.diagnostics.length,
        errors: counts.error,
        warnings: counts.warning,
        info: counts.info,
        hints: counts.hint,
        details: typed.diagnostics.slice(0, 8).map(
          (diagnostic) => ({
            severity: diagnosticSeverityName(
              diagnostic.severity,
            ) as "error" | "warning" | "info" | "hint",
            line: diagnostic.range.start.line + 1,
            column: diagnostic.range.start.character + 1,
            message: diagnostic.message
              .replace(/\s+/g, " ")
              .trim(),
          }),
        ),
      },
    });
  }

  private handleClose(message: string): void {
    if (this.disposed) {
      return;
    }

    console.warn(
      `[Language Support:${this.descriptor.serverId}] disconnected`
      + ` documents=${this.documents.size}`
      + ` workspace=${this.descriptor.workspaceRoot}`
      + ` reason=${message}`,
    );
    recordLanguageSupportTelemetry({
      kind: "disconnected",
      provider: "lsp",
      serverId: this.descriptor.serverId,
      workspaceRoot: this.descriptor.workspaceRoot,
      message,
    });
    this.callbacks.reportStatus(
      `${this.descriptor.displayName} disconnected`,
    );
    this.dispose();
    this.onClosed(this);
  }
}

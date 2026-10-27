import type * as Monaco from "monaco-editor/editor/editor.api";
import type {
  ArchivistDocument,
} from "../../IdeHost.types.js";
import {
  completionResult,
  describeOtherFiles,
  diagnosticMarker,
  documentHighlightsResult,
  documentSymbolsResult,
  hoverContents,
  locationResult,
  lspLanguageId,
  markdownValue,
  parseSessionResponse,
  pathWithin,
  rangesIntersect,
  singleDocumentEdit,
  textEditsResult,
  toLspPosition,
  toLspRange,
  toMonacoRange,
} from "./LspMonacoConversions.js";
import type {
  LspCodeAction,
  LspCommand,
  LspCompletionItem,
  LspCompletionList,
  LspDocumentHighlight,
  LspDocumentSymbol,
  LspHover,
  LspLocation,
  LspLocationLink,
  LspPrepareRenameResult,
  LspSignatureHelp,
  LspSymbolInformation,
  LspTextEdit,
  LspWorkspaceEdit,
} from "./LspTypes.js";
import {
  LibraryModelResolver,
} from "./LibraryModelResolver.js";
import {
  type LanguageSupportCallbacks,
  WorkspaceLanguageClient,
} from "./WorkspaceLanguageClient.js";
import {
  recordLanguageSupportTelemetry,
} from "./LanguageSupportTelemetry.js";
import {
  languageSupportDefinition,
  languageSupportDefinitions,
  type LanguageSupportDefinition,
} from "./LanguageSupportRegistry.js";

type MonacoApi = typeof Monaco;

type Disposable = {
  dispose(): void;
};

export type DefinitionLocation = Monaco.languages.Location;

/** Monaco command that runs a language-server command for a code action. */
const executeServerCommandId = "archivist.languageSupport.executeCommand";

/**
 * Languages whose Monaco built-in formatter already matches the language
 * server's formatter, so the built-in one stays in charge.
 */
const builtInFormattingLanguages = new Set([
  "typescript",
  "javascript",
]);

const codeActionKinds = [
  "quickfix",
  "refactor",
  "refactor.extract",
  "refactor.inline",
  "refactor.rewrite",
  "source",
  "source.organizeImports",
  "source.fixAll",
];

export class LanguageSupportClient {
  private readonly clientsByKey = new Map<
    string,
    WorkspaceLanguageClient
  >();
  private readonly clientKeyByModelUri = new Map<
    string,
    string
  >();
  private readonly disposables: Disposable[] = [];
  private readonly documentByModelUri = new Map<
    string,
    ArchivistDocument
  >();
  private readonly modelResolver: LibraryModelResolver;
  private readonly unavailableServerRequests = new Map<
    string,
    string
  >();
  private readonly markerFingerprintByUri = new Map<
    string,
    string
  >();
  private openQueue: Promise<void> = Promise.resolve();
  private disposed = false;

  constructor(
    private readonly monaco: MonacoApi,
    private readonly callbacks: LanguageSupportCallbacks,
  ) {
    this.modelResolver = new LibraryModelResolver(monaco);
    this.registerProviders();
    this.disposables.push(
      this.monaco.editor.registerCommand(
        executeServerCommandId,
        (_accessor, clientKey: unknown, command: unknown) => {
          void this.executeServerCommand(
            String(clientKey || ""),
            command as LspCommand,
          );
        },
      ),
    );
    this.disposables.push(
      this.monaco.editor.onDidChangeMarkers((resources) => {
        this.recordMarkerTelemetry(resources);
      }),
    );
    window.addEventListener("beforeunload", () => {
      this.dispose();
    });
  }

  openDocument(
    document: ArchivistDocument,
    model: Monaco.editor.ITextModel,
    monacoLanguageId: string,
  ): void {
    const definition = languageSupportDefinition(
      monacoLanguageId,
    );
    const resolvedLspLanguageId = definition
      ? lspLanguageId(document, monacoLanguageId)
      : undefined;

    recordLanguageSupportTelemetry({
      kind: "document-classified",
      provider: definition ? "lsp" : "monaco",
      serverId: definition?.serverId,
      workspaceRoot: document.workspaceRoot || undefined,
      filePath: document.filePath || undefined,
      uri: model.uri.toString(),
      monacoLanguageId,
      lspLanguageId: resolvedLspLanguageId,
      version: model.getVersionId(),
    });
    this.documentByModelUri.set(
      model.uri.toString(),
      document,
    );

    if (
      this.disposed
      || !definition
      || !document.libraryId
      || !document.workspaceRoot
      || !document.filePath
    ) {
      return;
    }

    this.openQueue = this.openQueue
      .then(() => this.attachDocument(
        document,
        model,
        monacoLanguageId,
        definition,
      ))
      .catch((error) => {
        const message = error instanceof Error
          ? error.message
          : "Language support could not start.";
        console.error(`[Language Support] ${message}`);
        recordLanguageSupportTelemetry({
          kind: "client-error",
          provider: "lsp",
          serverId: definition.serverId,
          workspaceRoot: document.workspaceRoot || undefined,
          filePath: document.filePath || undefined,
          uri: model.uri.toString(),
          monacoLanguageId,
          lspLanguageId: resolvedLspLanguageId,
          message,
        });
        this.callbacks.reportStatus(message);
      });
  }

  async definitionsAt(
    model: Monaco.editor.ITextModel,
    position: Monaco.Position,
  ): Promise<readonly DefinitionLocation[]> {
    await this.openQueue;

    if (this.disposed || model.isDisposed()) {
      return [];
    }

    const client = this.clientForModel(model);

    if (!client) {
      return [];
    }

    const result = await this.requestForModel<
      | LspLocation
      | LspLocation[]
      | LspLocationLink[]
      | null
    >(
      client,
      model,
      "textDocument/definition",
      {
        textDocument: {
          uri: model.uri.toString(),
        },
        position: toLspPosition(position),
      },
    );

    if (!result || model.isDisposed()) {
      return [];
    }

    const values = Array.isArray(result)
      ? result
      : [result];
    const document = this.documentByModelUri.get(
      model.uri.toString(),
    );

    if (document) {
      await this.modelResolver.ensureLocationModels(
        document,
        values,
      );
    }

    if (model.isDisposed()) {
      return [];
    }

    return values.map((value) => {
      if ("targetUri" in value) {
        return {
          uri: this.monaco.Uri.parse(value.targetUri),
          range: toMonacoRange(
            this.monaco,
            value.targetSelectionRange,
          ),
        };
      }

      return {
        uri: this.monaco.Uri.parse(value.uri),
        range: toMonacoRange(this.monaco, value.range),
      };
    });
  }

  dispose(): void {
    if (this.disposed) {
      return;
    }

    this.disposed = true;

    for (const disposable of this.disposables) {
      disposable.dispose();
    }

    this.disposables.length = 0;

    for (const client of this.clientsByKey.values()) {
      client.dispose();
    }

    this.clientsByKey.clear();
    this.clientKeyByModelUri.clear();
    this.documentByModelUri.clear();
    this.unavailableServerRequests.clear();
    this.markerFingerprintByUri.clear();
  }

  private recordMarkerTelemetry(
    resources: readonly Monaco.Uri[],
  ): void {
    for (const resource of resources) {
      const uri = resource.toString();
      const model = this.monaco.editor.getModel(resource);

      if (!model || model.isDisposed()) {
        continue;
      }

      const markers = this.monaco.editor.getModelMarkers({
        resource,
      });
      const fingerprint = JSON.stringify(
        markers.map((entry) => [
          entry.owner,
          entry.severity,
          entry.startLineNumber,
          entry.startColumn,
          entry.message,
        ]),
      );

      if (this.markerFingerprintByUri.get(uri) === fingerprint) {
        continue;
      }

      this.markerFingerprintByUri.set(uri, fingerprint);
      const counts = {
        errors: 0,
        warnings: 0,
        info: 0,
        hints: 0,
      };

      for (const entry of markers) {
        if (entry.severity === this.monaco.MarkerSeverity.Error) {
          counts.errors += 1;
        } else if (entry.severity === this.monaco.MarkerSeverity.Warning) {
          counts.warnings += 1;
        } else if (entry.severity === this.monaco.MarkerSeverity.Info) {
          counts.info += 1;
        } else {
          counts.hints += 1;
        }
      }

      const document = this.documentByModelUri.get(uri);
      recordLanguageSupportTelemetry({
        kind: "markers",
        provider: "monaco",
        workspaceRoot: document?.workspaceRoot || undefined,
        filePath: document?.filePath || undefined,
        uri,
        monacoLanguageId: model.getLanguageId(),
        version: model.getVersionId(),
        diagnostics: {
          total: markers.length,
          ...counts,
          details: markers.slice(0, 8).map((entry) => ({
            owner: entry.owner,
            severity:
              entry.severity === this.monaco.MarkerSeverity.Error
                ? "error"
                : entry.severity === this.monaco.MarkerSeverity.Warning
                  ? "warning"
                  : entry.severity === this.monaco.MarkerSeverity.Info
                    ? "info"
                    : "hint",
            line: entry.startLineNumber,
            column: entry.startColumn,
            message: entry.message.replace(/\s+/g, " ").trim(),
          })),
        },
      });
    }
  }

  private async attachDocument(
    document: ArchivistDocument,
    model: Monaco.editor.ITextModel,
    monacoLanguageId: string,
    definition: LanguageSupportDefinition,
  ): Promise<void> {
    if (this.disposed || model.isDisposed()) {
      return;
    }

    const existing = [...this.clientsByKey.values()].find(
      (client) =>
        client.descriptor.serverId === definition.serverId
        && client.libraryId === document.libraryId
        && pathWithin(client.workspaceRoot, document.filePath),
    );
    const client = existing ?? await this.startClient(
      document,
      definition,
    );

    if (!client || this.disposed || model.isDisposed()) {
      return;
    }

    client.attachDocument(
      model,
      lspLanguageId(document, monacoLanguageId),
    );
    this.clientKeyByModelUri.set(
      model.uri.toString(),
      client.key,
    );
  }

  private async startClient(
    document: ArchivistDocument,
    definition: LanguageSupportDefinition,
  ): Promise<WorkspaceLanguageClient | null> {
    const requestKey = [
      definition.serverId,
      document.workspaceRoot,
    ].join(":");
    if (this.unavailableServerRequests.has(requestKey)) {
      return null;
    }

    this.callbacks.reportStatus(
      `Starting ${definition.displayName} language support…`,
    );

    const controller = new AbortController();
    const timeoutId = window.setTimeout(() => {
      controller.abort();
    }, 10_000);

    const response = await fetch(
      "/api/language-support/sessions",
      {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
        },
        body: JSON.stringify({
          libraryId: document.libraryId,
          serverId: definition.serverId,
          workspaceRoot: document.workspaceRoot,
          filePath: document.filePath,
        }),
        signal: controller.signal,
      },
    ).finally(() => {
      window.clearTimeout(timeoutId);
    });

    const payload: unknown = await response.json();

    if (!response.ok) {
      let message =
        `Language-support request failed (${response.status}).`;

      try {
        parseSessionResponse(payload);
      } catch (error) {
        message = error instanceof Error
          ? error.message
          : message;
      }

      if (
        response.status === 409
        && /not installed|disabled/i.test(message)
      ) {
        this.unavailableServerRequests.set(
          requestKey,
          message,
        );
        console.info(
          `[Language Support:${definition.serverId}] ${message}`,
        );
        this.callbacks.reportStatus(
          `${definition.displayName} language support unavailable`,
        );
        return null;
      }

      throw new Error(message);
    }

    const descriptor = parseSessionResponse(payload);
    const key = [
      descriptor.serverId,
      descriptor.workspaceRoot,
    ].join(":");
    const existing = this.clientsByKey.get(key);

    if (existing) {
      return existing;
    }

    const client = new WorkspaceLanguageClient(
      this.monaco,
      descriptor,
      document.libraryId,
      this.callbacks,
      (closedClient) => {
        if (
          this.clientsByKey.get(closedClient.key)
          === closedClient
        ) {
          this.clientsByKey.delete(closedClient.key);
        }

        for (
          const [uri, clientKey]
          of this.clientKeyByModelUri
        ) {
          if (clientKey === closedClient.key) {
            this.clientKeyByModelUri.delete(uri);
          }
        }
      },
    );

    this.clientsByKey.set(key, client);

    try {
      await client.connect();
    } catch (error) {
      this.clientsByKey.delete(key);
      client.dispose();
      throw error;
    }

    this.unavailableServerRequests.delete(requestKey);
    this.callbacks.reportStatus(
      `${descriptor.displayName} connected`,
    );
    return client;
  }

  private clientForModel(
    model: Monaco.editor.ITextModel,
  ): WorkspaceLanguageClient | null {
    const key = this.clientKeyByModelUri.get(
      model.uri.toString(),
    );

    return key ? this.clientsByKey.get(key) ?? null : null;
  }

  private async requestForModel<Result>(
    client: WorkspaceLanguageClient,
    model: Monaco.editor.ITextModel,
    method: string,
    params: unknown,
  ): Promise<Result | null> {
    try {
      return await client.request<Result>(method, params);
    } catch (error) {
      if (
        this.isMissingModelError(error)
        && client.reopenDocument(model)
      ) {
        try {
          return await client.request<Result>(method, params);
        } catch (retryError) {
          this.logRequestError(
            method,
            model,
            retryError,
            true,
          );
          return null;
        }
      }

      this.logRequestError(method, model, error, false);
      return null;
    }
  }

  private isMissingModelError(error: unknown): boolean {
    return error instanceof Error
      && /model not found/i.test(error.message);
  }

  private logRequestError(
    method: string,
    model: Monaco.editor.ITextModel,
    error: unknown,
    retried: boolean,
  ): void {
    const message = error instanceof Error
      ? error.message
      : String(error || "Unknown language-server error.");
    const retryText = retried ? " after document reopen" : "";

    console.warn(
      `[Language Support] ${method} failed${retryText} for ${model.uri.toString()}: ${message}`,
    );
  }

  private registerProviders(): void {
    for (const definition of languageSupportDefinitions) {
      for (const languageId of definition.monacoLanguageIds) {
        this.registerCommonProviders(
          languageId,
          definition,
        );
        this.registerEditingProviders(languageId);

        if (!builtInFormattingLanguages.has(languageId)) {
          this.registerFormattingProviders(languageId);
        }

        if (definition.signatureHelp) {
          this.registerSignatureHelpProvider(languageId);
        }
      }
    }
  }

  private registerCommonProviders(
    languageId: string,
    definition: LanguageSupportDefinition,
  ): void {
    this.disposables.push(
      this.monaco.languages.registerHoverProvider(
        languageId,
        {
          provideHover: async (model, position, token) => {
            const client = this.clientForModel(model);

            if (!client) {
              return null;
            }

            const result = await this.requestForModel<LspHover | null>(
              client,
              model,
              "textDocument/hover",
              {
                textDocument: {
                  uri: model.uri.toString(),
                },
                position: toLspPosition(position),
              },
            );

            if (!result || token.isCancellationRequested) {
              return null;
            }

            return {
              contents: hoverContents(result.contents),
              range: result.range
                ? toMonacoRange(this.monaco, result.range)
                : undefined,
            };
          },
        },
      ),
    );

    this.disposables.push(
      this.monaco.languages.registerCompletionItemProvider(
        languageId,
        {
          triggerCharacters: [...definition.completionTriggers],
          provideCompletionItems: async (
            model,
            position,
            context,
            token,
          ) => {
            const client = this.clientForModel(model);

            if (!client) {
              return { suggestions: [] };
            }

            const result = await this.requestForModel<
              LspCompletionItem[] | LspCompletionList | null
            >(
              client,
              model,
              "textDocument/completion",
              {
                textDocument: {
                  uri: model.uri.toString(),
                },
                position: toLspPosition(position),
                context: {
                  triggerKind: context.triggerKind + 1,
                  triggerCharacter: context.triggerCharacter,
                },
              },
            );

            return token.isCancellationRequested
              ? { suggestions: [] }
              : completionResult(
                  this.monaco,
                  model,
                  position,
                  result,
                );
          },
        },
      ),
    );

    this.disposables.push(
      this.monaco.languages.registerDefinitionProvider(
        languageId,
        {
          provideDefinition: async (model, position, token) => {
            const definitions = await this.definitionsAt(
              model,
              position,
            );

            return definitions.length === 0
              || token.isCancellationRequested
              ? null
              : [...definitions];
          },
        },
      ),
    );

    this.disposables.push(
      this.monaco.languages.registerReferenceProvider(
        languageId,
        {
          provideReferences: async (model, position, context, token) => {
            const client = this.clientForModel(model);

            if (!client) {
              return null;
            }

            const result = await this.requestForModel<
              LspLocation[] | null
            >(
              client,
              model,
              "textDocument/references",
              {
                textDocument: {
                  uri: model.uri.toString(),
                },
                position: toLspPosition(position),
                context: {
                  includeDeclaration: context.includeDeclaration,
                },
              },
            );

            if (!result || token.isCancellationRequested) {
              return null;
            }

            const document = this.documentByModelUri.get(
              model.uri.toString(),
            );

            if (document) {
              await this.modelResolver.ensureLocationModels(
                document,
                result,
              );
            }

            if (token.isCancellationRequested) {
              return null;
            }

            return result.map((value) =>
              locationResult(this.monaco, value)
            );
          },
        },
      ),
    );
  }

  private registerSignatureHelpProvider(
    languageId: string,
  ): void {
    this.disposables.push(
      this.monaco.languages.registerSignatureHelpProvider(
        languageId,
        {
          signatureHelpTriggerCharacters: ["(", ","],
          signatureHelpRetriggerCharacters: [","],
          provideSignatureHelp: async (
            model,
            position,
            token,
            context,
          ) => {
            const client = this.clientForModel(model);

            if (!client) {
              return null;
            }

            const result = await this.requestForModel<
              LspSignatureHelp | null
            >(
              client,
              model,
              "textDocument/signatureHelp",
              {
                textDocument: {
                  uri: model.uri.toString(),
                },
                position: toLspPosition(position),
                context: {
                  isRetrigger: context.isRetrigger,
                  triggerCharacter: context.triggerCharacter,
                  triggerKind: context.triggerKind + 1,
                },
              },
            );

            if (!result || token.isCancellationRequested) {
              return null;
            }

            return {
              value: {
                activeSignature: result.activeSignature ?? 0,
                activeParameter: result.activeParameter ?? 0,
                signatures: result.signatures.map(
                  (signature) => ({
                    label: signature.label,
                    documentation: markdownValue(
                      signature.documentation,
                    ),
                    activeParameter:
                      signature.activeParameter,
                    parameters: (signature.parameters ?? []).map(
                      (parameter) => ({
                        label: parameter.label,
                        documentation: markdownValue(
                          parameter.documentation,
                        ),
                      }),
                    ),
                  }),
                ),
              },
              dispose(): void {},
            };
          },
        },
      ),
    );
  }

  private async executeServerCommand(
    clientKey: string,
    command: LspCommand,
  ): Promise<void> {
    const client = this.clientsByKey.get(clientKey);

    if (!client || !command || typeof command.command !== "string") {
      this.callbacks.reportStatus(
        "The language server for this action is no longer running.",
      );
      return;
    }

    try {
      await client.request("workspace/executeCommand", {
        command: command.command,
        arguments: command.arguments ?? [],
      });
    } catch (error) {
      const message = error instanceof Error
        ? error.message
        : String(error || "Unknown language-server error.");

      console.warn(
        `[Language Support] ${command.command} failed: ${message}`,
      );
      this.callbacks.reportStatus(
        `${command.title || "Action"} failed: ${message}`,
      );
    }
  }

  private monacoWorkspaceEdit(
    model: Monaco.editor.ITextModel,
    versionId: number,
    edits: readonly LspTextEdit[],
  ): Monaco.languages.WorkspaceEdit {
    return {
      edits: textEditsResult(this.monaco, edits).map((textEdit) => ({
        resource: model.uri,
        textEdit,
        versionId,
      })),
    };
  }

  private codeActionResult(
    client: WorkspaceLanguageClient,
    model: Monaco.editor.ITextModel,
    versionId: number,
    value: LspCodeAction | LspCommand,
  ): Monaco.languages.CodeAction | null {
    if (typeof value.command === "string") {
      const command = value as LspCommand;

      return client.supportsCommand(command.command)
        ? {
            title: command.title,
            command: {
              id: executeServerCommandId,
              title: command.title,
              arguments: [client.key, command],
            },
          }
        : null;
    }

    const action = value as LspCodeAction;

    // Move refactors always produce new files, and some servers create those
    // files on disk before asking for the edit. Hide them rather than leave
    // empty files behind when the edit is refused.
    if (action.kind?.startsWith("refactor.move")) {
      return null;
    }

    const result: Monaco.languages.CodeAction = {
      title: action.title,
      kind: action.kind,
      isPreferred: action.isPreferred,
      disabled: action.disabled?.reason,
      diagnostics: action.diagnostics?.map((diagnostic) =>
        diagnosticMarker(this.monaco, diagnostic)
      ),
    };
    const command = action.command
      && client.supportsCommand(action.command.command)
        ? action.command
        : null;

    if (action.edit) {
      const split = singleDocumentEdit(
        this.monaco,
        action.edit,
        model.uri.toString(),
      );

      if (split.otherUris.length > 0 || split.resourceOperations > 0) {
        result.disabled = split.otherUris.length > 0
          ? `Also changes ${describeOtherFiles(this.monaco, split.otherUris)}`
          : "Creates, renames, or deletes files";
      } else {
        result.edit = this.monacoWorkspaceEdit(
          model,
          versionId,
          split.edits,
        );
      }
    } else if (!command && !action.disabled) {
      // Needs codeAction/resolve or an unsupported command.
      return null;
    }

    if (command) {
      result.command = {
        id: executeServerCommandId,
        title: command.title,
        arguments: [client.key, command],
      };
    }

    return result;
  }

  private registerEditingProviders(
    languageId: string,
  ): void {
    this.disposables.push(
      this.monaco.languages.registerRenameProvider(
        languageId,
        {
          resolveRenameLocation: async (model, position, token) => {
            const client = this.clientForModel(model);

            if (!client || !client.supportsPrepareRename()) {
              return null;
            }

            const result = await this.requestForModel<
              LspPrepareRenameResult | null
            >(
              client,
              model,
              "textDocument/prepareRename",
              {
                textDocument: {
                  uri: model.uri.toString(),
                },
                position: toLspPosition(position),
              },
            );

            if (token.isCancellationRequested) {
              return null;
            }

            if (!result) {
              return {
                range: new this.monaco.Range(
                  position.lineNumber,
                  position.column,
                  position.lineNumber,
                  position.column,
                ),
                text: "",
                rejectReason: "This symbol can't be renamed.",
              };
            }

            if ("defaultBehavior" in result) {
              return null;
            }

            const lspRange = "range" in result ? result.range : result;
            const range = toMonacoRange(this.monaco, lspRange);

            return {
              range,
              text: "placeholder" in result
                ? result.placeholder
                : model.getValueInRange(range),
            };
          },
          provideRenameEdits: async (model, position, newName, token) => {
            const client = this.clientForModel(model);

            if (!client || !client.supports("renameProvider")) {
              return null;
            }

            const versionId = model.getVersionId();
            const result = await this.requestForModel<
              LspWorkspaceEdit | null
            >(
              client,
              model,
              "textDocument/rename",
              {
                textDocument: {
                  uri: model.uri.toString(),
                },
                position: toLspPosition(position),
                newName,
              },
            );

            if (!result || token.isCancellationRequested) {
              return null;
            }

            const split = singleDocumentEdit(
              this.monaco,
              result,
              model.uri.toString(),
            );

            if (split.otherUris.length > 0 || split.resourceOperations > 0) {
              const reason = split.otherUris.length > 0
                ? "This rename would also change "
                  + describeOtherFiles(this.monaco, split.otherUris)
                  + ". Renaming across files isn't supported yet."
                : "This rename would create, move, or delete files,"
                  + " which isn't supported yet.";

              this.callbacks.reportStatus(reason);
              return {
                edits: [],
                rejectReason: reason,
              };
            }

            return this.monacoWorkspaceEdit(
              model,
              versionId,
              split.edits,
            );
          },
        },
      ),
    );

    this.disposables.push(
      this.monaco.languages.registerCodeActionProvider(
        languageId,
        {
          provideCodeActions: async (model, range, context, token) => {
            const client = this.clientForModel(model);
            const empty = {
              actions: [],
              dispose(): void {},
            };

            if (!client || !client.supports("codeActionProvider")) {
              return empty;
            }

            const lspRange = toLspRange(range);
            const versionId = model.getVersionId();
            const diagnostics = client
              .diagnosticsFor(model.uri.toString())
              .filter((diagnostic) =>
                rangesIntersect(diagnostic.range, lspRange)
              );
            const result = await this.requestForModel<
              (LspCodeAction | LspCommand)[] | null
            >(
              client,
              model,
              "textDocument/codeAction",
              {
                textDocument: {
                  uri: model.uri.toString(),
                },
                range: lspRange,
                context: {
                  diagnostics,
                  only: context.only ? [context.only] : undefined,
                  triggerKind: context.trigger,
                },
              },
            );

            if (
              !result
              || token.isCancellationRequested
              || model.isDisposed()
            ) {
              return empty;
            }

            return {
              actions: result.flatMap((value) => {
                const action = this.codeActionResult(
                  client,
                  model,
                  versionId,
                  value,
                );

                return action ? [action] : [];
              }),
              dispose(): void {},
            };
          },
        },
        {
          providedCodeActionKinds: codeActionKinds,
        },
      ),
    );

    this.disposables.push(
      this.monaco.languages.registerDocumentSymbolProvider(
        languageId,
        {
          displayName: "Archivist language support",
          provideDocumentSymbols: async (model, token) => {
            const client = this.clientForModel(model);

            if (!client || !client.supports("documentSymbolProvider")) {
              return null;
            }

            const result = await this.requestForModel<
              (LspDocumentSymbol | LspSymbolInformation)[] | null
            >(
              client,
              model,
              "textDocument/documentSymbol",
              {
                textDocument: {
                  uri: model.uri.toString(),
                },
              },
            );

            return !result || token.isCancellationRequested
              ? null
              : documentSymbolsResult(
                  this.monaco,
                  result,
                  model.uri.toString(),
                );
          },
        },
      ),
    );

    this.disposables.push(
      this.monaco.languages.registerDocumentHighlightProvider(
        languageId,
        {
          provideDocumentHighlights: async (model, position, token) => {
            const client = this.clientForModel(model);

            if (!client || !client.supports("documentHighlightProvider")) {
              return null;
            }

            const result = await this.requestForModel<
              LspDocumentHighlight[] | null
            >(
              client,
              model,
              "textDocument/documentHighlight",
              {
                textDocument: {
                  uri: model.uri.toString(),
                },
                position: toLspPosition(position),
              },
            );

            return !result || token.isCancellationRequested
              ? null
              : documentHighlightsResult(this.monaco, result);
          },
        },
      ),
    );
  }

  private registerFormattingProviders(
    languageId: string,
  ): void {
    this.disposables.push(
      this.monaco.languages.registerDocumentFormattingEditProvider(
        languageId,
        {
          displayName: "Archivist language support",
          provideDocumentFormattingEdits: async (model, options, token) => {
            const client = this.clientForModel(model);

            if (!client || !client.supports("documentFormattingProvider")) {
              return null;
            }

            const result = await this.requestForModel<
              LspTextEdit[] | null
            >(
              client,
              model,
              "textDocument/formatting",
              {
                textDocument: {
                  uri: model.uri.toString(),
                },
                options: {
                  tabSize: options.tabSize,
                  insertSpaces: options.insertSpaces,
                },
              },
            );

            return token.isCancellationRequested
              ? null
              : textEditsResult(this.monaco, result);
          },
        },
      ),
    );

    this.disposables.push(
      this.monaco.languages.registerDocumentRangeFormattingEditProvider(
        languageId,
        {
          displayName: "Archivist language support",
          provideDocumentRangeFormattingEdits: async (
            model,
            range,
            options,
            token,
          ) => {
            const client = this.clientForModel(model);

            if (
              !client
              || !client.supports("documentRangeFormattingProvider")
            ) {
              return null;
            }

            const result = await this.requestForModel<
              LspTextEdit[] | null
            >(
              client,
              model,
              "textDocument/rangeFormatting",
              {
                textDocument: {
                  uri: model.uri.toString(),
                },
                range: toLspRange(range),
                options: {
                  tabSize: options.tabSize,
                  insertSpaces: options.insertSpaces,
                },
              },
            );

            return token.isCancellationRequested
              ? null
              : textEditsResult(this.monaco, result);
          },
        },
      ),
    );
  }
}

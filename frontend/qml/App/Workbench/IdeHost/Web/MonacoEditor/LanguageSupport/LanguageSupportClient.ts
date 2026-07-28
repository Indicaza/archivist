import type * as Monaco from "monaco-editor/editor/editor.api";
import type {
  ArchivistDocument,
} from "../../IdeHost.types.js";
import {
  completionResult,
  hoverContents,
  locationResult,
  lspLanguageId,
  markdownValue,
  parseSessionResponse,
  pathWithin,
  toLspPosition,
  toMonacoRange,
} from "./LspMonacoConversions.js";
import type {
  LspCompletionItem,
  LspCompletionList,
  LspHover,
  LspLocation,
  LspLocationLink,
  LspSignatureHelp,
} from "./LspTypes.js";
import {
  LibraryModelResolver,
} from "./LibraryModelResolver.js";
import {
  type LanguageSupportCallbacks,
  WorkspaceLanguageClient,
} from "./WorkspaceLanguageClient.js";

type MonacoApi = typeof Monaco;

type Disposable = {
  dispose(): void;
};

interface LanguageSupportDefinition {
  serverId: string;
  displayName: string;
  monacoLanguageIds: readonly string[];
  completionTriggers: readonly string[];
  signatureHelp: boolean;
}

const languageSupportDefinitions: readonly LanguageSupportDefinition[] = [
  {
    serverId: "typescript",
    displayName: "TypeScript",
    monacoLanguageIds: ["typescript", "javascript"],
    completionTriggers: [
      ".",
      '"',
      "'",
      "`",
      "/",
      "@",
      "<",
      "#",
    ],
    signatureHelp: true,
  },
  {
    serverId: "qml",
    displayName: "QML",
    monacoLanguageIds: ["qml"],
    completionTriggers: [".", ":", '"', "'"],
    signatureHelp: false,
  },
];

function languageSupportDefinition(
  monacoLanguageId: string,
): LanguageSupportDefinition | null {
  return languageSupportDefinitions.find((definition) =>
    definition.monacoLanguageIds.includes(monacoLanguageId)
  ) ?? null;
}

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
  private openQueue: Promise<void> = Promise.resolve();
  private disposed = false;

  constructor(
    private readonly monaco: MonacoApi,
    private readonly callbacks: LanguageSupportCallbacks,
  ) {
    this.modelResolver = new LibraryModelResolver(monaco);
    this.registerProviders();
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

    if (
      this.disposed
      || !definition
      || !document.libraryId
      || !document.workspaceRoot
      || !document.filePath
    ) {
      return;
    }

    this.documentByModelUri.set(
      model.uri.toString(),
      document,
    );

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
        this.callbacks.reportStatus(message);
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

    if (this.disposed || model.isDisposed()) {
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
  ): Promise<WorkspaceLanguageClient> {
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
      parseSessionResponse(payload);
      throw new Error(
        `Language-support request failed (${response.status}).`,
      );
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
            const client = this.clientForModel(model);

            if (!client) {
              return null;
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

            if (!result || token.isCancellationRequested) {
              return null;
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

            if (token.isCancellationRequested) {
              return null;
            }

            return values.map((value) =>
              locationResult(this.monaco, value)
            );
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
}

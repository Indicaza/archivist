import { getAgentById, requireActiveAgent } from "../../agents/models/Agent.js";
import { buildAgentInstructions } from "../../agents/services/AgentInstructionBuilder.js";
import type { Agent } from "../../agents/types/AgentTypes.js";
import { createContextRun } from "../../cognition/contextRuns/models/ContextRun.js";
import { aiProviderRegistry } from "../../../core/ai/AIProviderRegistry.js";
import { modelCatalog } from "../../../core/ai/ModelCatalog.js";
import { modelRegistry } from "../../../core/ai/ModelRegistry.js";
import { contextCompilerRegistry } from "../../../core/cognition/conscious/context/ContextCompilerRegistry.js";
import type {
  ContextCompilerConfig,
  ContextManifest,
  ContextSourceMessage,
} from "../../../core/cognition/conscious/context/ContextCompilerTypes.js";
import { estimateTokens } from "../../../core/cognition/conscious/context/utilities/estimateTokens.js";
import {
  createMessage,
  getChatById,
  getMessagesByChatId,
  updateMessage,
} from "../models/Chat.js";
import type { ChatAttachmentSource, ChatMessage } from "../types/ChatTypes.js";
import { buildChatAttachmentEvidence } from "./ChatAttachmentEvidence.js";
import { buildChatLibraryRetrievalEvidence } from "./ChatLibraryRetrievalEvidence.js";

export type ChatTurnExecutionEvent = {
  type:
    | "retrieval.started"
    | "retrieval.completed"
    | "context.started"
    | "context.completed"
    | "model.started"
    | "model.delta"
    | "model.completed";
  payload?: Record<string, unknown>;
};

export type ChatTurnSession = {
  chatId: string;
  libraryId: string | null;
  agent: Agent;
  userMessage: ChatMessage;
  assistantMessage: ChatMessage;
};

type CompleteChatTurnOptions = {
  signal?: AbortSignal;
  onEvent?: (event: ChatTurnExecutionEvent) => void;
};

type CompleteChatTurnResult = {
  userMessage: ChatMessage;
  assistantMessage: ChatMessage;
  provider: string;
  model: string;
  agentId: string;
  contextManifest: ContextManifest;
  contextWarnings: string[];
  attachmentSources: ChatAttachmentSource[];
  contextRunId: string | null;
};

function toContextSourceMessage(message: ChatMessage): ContextSourceMessage {
  return {
    id: message.id,
    role: message.role,
    content: message.content,
    createdAt: message.createdAt,
  };
}

function logCompiledContext(
  chatId: string,
  agentId: string,
  config: ContextCompilerConfig,
  manifest: ContextManifest,
  warnings: string[],
): void {
  if (process.env.NODE_ENV === "production") {
    return;
  }

  console.debug("[ContextCompiler]", {
    chatId,
    agentId,
    config,
    manifest,
    warnings,
  });
}

function abortError(): Error {
  const error = new Error("AI Run cancelled.");
  error.name = "AbortError";
  return error;
}

function throwIfAborted(signal?: AbortSignal): void {
  if (signal?.aborted) {
    throw abortError();
  }
}

function emitEvent(
  options: CompleteChatTurnOptions,
  event: ChatTurnExecutionEvent,
): void {
  throwIfAborted(options.signal);
  options.onEvent?.(event);
}

export function beginChatTurn(
  chatId: string,
  content: string,
): ChatTurnSession {
  const chat = getChatById(chatId);

  if (!chat) {
    throw new Error(`Chat ${chatId} could not be loaded.`);
  }

  if (chat.archivedAt) {
    throw new Error(`Chat ${chatId} is archived and cannot respond.`);
  }

  const agent = getAgentById(chat.agentId) ?? requireActiveAgent(chat.agentId);

  if (agent.archivedAt) {
    throw new Error(`Agent ${agent.id} is archived and cannot respond.`);
  }

  const userMessage = createMessage(chatId, {
    role: "user",
    content,
    status: "complete",
  });

  const assistantMessage = createMessage(chatId, {
    role: "assistant",
    content: "",
    status: "streaming",
  });

  return {
    chatId,
    libraryId: chat.libraryId,
    agent,
    userMessage,
    assistantMessage,
  };
}

export async function completeChatTurnSession(
  session: ChatTurnSession,
  options: CompleteChatTurnOptions = {},
): Promise<CompleteChatTurnResult> {
  const {
    chatId,
    libraryId,
    agent,
    userMessage,
    assistantMessage: pendingAssistantMessage,
  } = session;
  let streamedText = "";

  try {
    throwIfAborted(options.signal);
    await modelCatalog.initialize();

    if (!modelRegistry.has(agent.generation.provider, agent.generation.model)) {
      try {
        await modelCatalog.refreshModels({
          force: true,
        });
      } catch {
        throwIfAborted(options.signal);
      }
    }

    modelRegistry.getDefinition(
      agent.generation.provider,
      agent.generation.model,
    );

    const provider = aiProviderRegistry.require(agent.generation.provider);
    const storedMessages = getMessagesByChatId(chatId);

    emitEvent(options, {
      type: "retrieval.started",
      payload: {
        libraryId,
      },
    });

    const attachmentEvidence = await buildChatAttachmentEvidence(
      chatId,
      userMessage,
    );
    throwIfAborted(options.signal);

    const attachedFileIds = new Set(
      attachmentEvidence.outcomes
        .map((outcome) => outcome.metadata.fileId)
        .filter((fileId): fileId is string => typeof fileId === "string"),
    );
    const retrievalEvidence = buildChatLibraryRetrievalEvidence(
      chatId,
      libraryId,
      userMessage,
      attachedFileIds,
    );

    emitEvent(options, {
      type: "retrieval.completed",
      payload: {
        attachedSourceCount: attachmentEvidence.sources.length,
        retrievedSourceCount: retrievalEvidence.manifestSources.length,
        warningCount:
          attachmentEvidence.warnings.length + retrievalEvidence.warnings.length,
      },
    });

    const sourceMessages = storedMessages
      .filter((message) => message.status === "complete")
      .map(toContextSourceMessage);

    const evidenceMessages = [
      retrievalEvidence.contextMessage,
      attachmentEvidence.contextMessage,
    ].filter((message): message is ContextSourceMessage => message !== null);

    if (evidenceMessages.length > 0) {
      const currentMessageIndex = sourceMessages.findIndex(
        (message) => message.id === userMessage.id,
      );

      sourceMessages.splice(
        currentMessageIndex < 0 ? sourceMessages.length : currentMessageIndex,
        0,
        ...evidenceMessages,
      );
    }

    const definition = contextCompilerRegistry.getDefinition(
      agent.context.compiler,
    );

    const validatedConfig = contextCompilerRegistry.parseConfig(
      agent.context.compiler,
      agent.context.config,
    );

    emitEvent(options, {
      type: "context.started",
      payload: {
        compiler: agent.context.compiler,
      },
    });

    const compiledContext = definition.compiler.compile({
      chatId,
      currentMessageId: userMessage.id,
      messages: sourceMessages,
      config: validatedConfig,
    });

    const contextWarnings = [
      ...compiledContext.warnings,
      ...attachmentEvidence.warnings,
      ...retrievalEvidence.warnings,
    ];

    const retrievalContextIncluded =
      retrievalEvidence.contextMessage !== null &&
      compiledContext.manifest.includedMessageIds.includes(
        retrievalEvidence.contextMessage.id,
      );

    if (retrievalEvidence.contextMessage !== null) {
      if (!retrievalContextIncluded) {
        contextWarnings.push(
          "The selected Context Compiler omitted automatically retrieved Library evidence.",
        );
      }

      if (process.env.NODE_ENV !== "production") {
        console.info("[LibraryRetrieval] Context Compiler decision", {
          chatId,
          included: retrievalContextIncluded,
          sourceCount: retrievalEvidence.manifestSources.length,
        });
      }
    }

    const attachmentContextIncluded =
      attachmentEvidence.contextMessage !== null &&
      compiledContext.manifest.includedMessageIds.includes(
        attachmentEvidence.contextMessage.id,
      );

    const attachmentSources = attachmentEvidence.contextMessage
      ? attachmentEvidence.sources
      : [];

    let providerMessages = compiledContext.providerMessages;
    let includedMessageIds = compiledContext.manifest.includedMessageIds;
    let omittedMessageIds = compiledContext.manifest.omittedMessageIds;
    let estimatedInputTokens = compiledContext.manifest.estimatedInputTokens;

    if (
      attachmentEvidence.contextMessage !== null &&
      !attachmentContextIncluded
    ) {
      providerMessages = [
        {
          role: "system",
          content: attachmentEvidence.contextMessage.content,
        },
        ...providerMessages,
      ];
      includedMessageIds = [
        attachmentEvidence.contextMessage.id,
        ...includedMessageIds,
      ];
      omittedMessageIds = omittedMessageIds.filter(
        (messageId) => messageId !== attachmentEvidence.contextMessage?.id,
      );
      estimatedInputTokens += estimateTokens(
        attachmentEvidence.contextMessage.content,
      );
      contextWarnings.push(
        "The selected Context Compiler omitted explicit attached-file evidence, so Archivist restored that bounded evidence ahead of the compiled conversation.",
      );
    }

    const contextManifest: ContextManifest = {
      ...compiledContext.manifest,
      includedMessageIds,
      omittedMessageIds,
      includedMessageCount: includedMessageIds.length,
      omittedMessageCount: omittedMessageIds.length,
      estimatedInputTokens,
      includedSources: [
        ...attachmentSources.map((source) => ({
          id: source.attachmentId,
          source: "library-file" as const,
          label: `${source.libraryName}/${source.relativePath}`,
          estimatedTokens: source.estimatedTokens,
          truncated: source.truncated,
          metadata: {
            attachmentId: source.attachmentId,
            retrievalMode: "attached",
            libraryId: source.libraryId,
            libraryName: source.libraryName,
            fileId: source.fileId,
            fileName: source.fileName,
            relativePath: source.relativePath,
          },
        })),
        ...(retrievalContextIncluded ? retrievalEvidence.manifestSources : []),
      ],
    };

    if (
      contextManifest.estimatedInputTokens >
      contextManifest.totalBudget - contextManifest.responseTokenReserve
    ) {
      contextWarnings.push(
        "Explicit attached-file evidence caused the estimated provider input to exceed the selected Context Compiler budget.",
      );
    }

    logCompiledContext(
      chatId,
      agent.id,
      validatedConfig,
      contextManifest,
      contextWarnings,
    );

    emitEvent(options, {
      type: "context.completed",
      payload: {
        compiler: contextManifest.compiler,
        includedMessageCount: contextManifest.includedMessageCount,
        omittedMessageCount: contextManifest.omittedMessageCount,
        estimatedInputTokens: contextManifest.estimatedInputTokens,
        sourceCount: contextManifest.includedSources?.length ?? 0,
        warningCount: contextWarnings.length,
      },
    });

    emitEvent(options, {
      type: "model.started",
      payload: {
        provider: agent.generation.provider,
        model: agent.generation.model,
      },
    });

    const result = await provider.streamText(
      {
        instructions: buildAgentInstructions(agent),
        messages: providerMessages,
        generation: agent.generation,
      },
      {
        signal: options.signal,
        onDelta(delta) {
          streamedText += delta;
          emitEvent(options, {
            type: "model.delta",
            payload: {
              delta,
            },
          });
        },
      },
    );

    emitEvent(options, {
      type: "model.completed",
      payload: {
        provider: result.provider,
        model: result.model,
        characterCount: result.text.length,
      },
    });

    const assistantMessage = updateMessage(pendingAssistantMessage.id, {
      content: result.text,
      status: "complete",
    });

    const retrievalOutcomes = retrievalEvidence.outcomes.map((outcome) => {
      if (
        retrievalContextIncluded ||
        (outcome.status !== "included" && outcome.status !== "truncated")
      ) {
        return outcome;
      }

      return {
        ...outcome,
        status: "omitted" as const,
        includedTokens: 0,
        truncated: false,
        reason:
          "The selected Context Compiler omitted automatically retrieved Library evidence.",
      };
    });

    let contextRunId: string | null = null;

    try {
      contextRunId = createContextRun({
        chatId,
        userMessageId: userMessage.id,
        assistantMessageId: assistantMessage.id,
        provider: result.provider,
        model: result.model,
        agentId: agent.id,
        compiler: contextManifest.compiler,
        manifest: contextManifest,
        warnings: contextWarnings,
        sources: [...attachmentEvidence.outcomes, ...retrievalOutcomes],
      }).id;
    } catch (error) {
      console.error("[ContextRun] Failed to persist context inspection data.", {
        chatId,
        assistantMessageId: assistantMessage.id,
        error,
      });
      contextWarnings.push(
        "Archivist completed the response but could not persist its context inspection record.",
      );
    }

    return {
      userMessage,
      assistantMessage,
      provider: result.provider,
      model: result.model,
      agentId: agent.id,
      contextManifest,
      contextWarnings,
      attachmentSources,
      contextRunId,
    };
  } catch (error) {
    updateMessage(pendingAssistantMessage.id, {
      content: streamedText,
      status: options.signal?.aborted ? "cancelled" : "failed",
    });

    throw error;
  }
}

export async function completeChatTurn(
  chatId: string,
  content: string,
): Promise<CompleteChatTurnResult> {
  return completeChatTurnSession(beginChatTurn(chatId, content));
}

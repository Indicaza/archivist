import { getAgentById, requireActiveAgent } from "../../agents/models/Agent.js";
import { buildAgentInstructions } from "../../agents/services/AgentInstructionBuilder.js";
import { aiProviderRegistry } from "../../../core/ai/AIProviderRegistry.js";
import { modelCatalog } from "../../../core/ai/ModelCatalog.js";
import { modelRegistry } from "../../../core/ai/ModelRegistry.js";
import { contextCompilerRegistry } from "../../../core/cognition/conscious/context/ContextCompilerRegistry.js";
import type {
  ContextCompilerConfig,
  ContextManifest,
  ContextSourceMessage,
} from "../../../core/cognition/conscious/context/ContextCompilerTypes.js";
import {
  createMessage,
  getChatById,
  getMessagesByChatId,
} from "../models/Chat.js";
import type { ChatMessage } from "../types/ChatTypes.js";

type CompleteChatTurnResult = {
  userMessage: ChatMessage;
  assistantMessage: ChatMessage;
  provider: string;
  model: string;
  agentId: string;
  contextManifest: ContextManifest;
  contextWarnings: string[];
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

export async function completeChatTurn(
  chatId: string,
  content: string,
): Promise<CompleteChatTurnResult> {
  const chat = getChatById(chatId);

  if (!chat) {
    throw new Error(`Chat ${chatId} could not be loaded.`);
  }

  const agent = getAgentById(chat.agentId) ?? requireActiveAgent(chat.agentId);

  if (agent.archivedAt) {
    throw new Error(`Agent ${agent.id} is archived and cannot respond.`);
  }

  await modelCatalog.initialize();

  if (!modelRegistry.has(agent.generation.provider, agent.generation.model)) {
    try {
      await modelCatalog.refreshModels({
        force: true,
      });
    } catch {
      // Validation below returns the clearer error.
    }
  }

  modelRegistry.getDefinition(
    agent.generation.provider,
    agent.generation.model,
  );

  const provider = aiProviderRegistry.require(agent.generation.provider);

  const userMessage = createMessage(chatId, {
    role: "user",
    content,
    status: "complete",
  });

  const storedMessages = getMessagesByChatId(chatId);

  const definition = contextCompilerRegistry.getDefinition(
    agent.context.compiler,
  );

  const validatedConfig = contextCompilerRegistry.parseConfig(
    agent.context.compiler,
    agent.context.config,
  );

  const compiledContext = definition.compiler.compile({
    chatId,
    currentMessageId: userMessage.id,

    messages: storedMessages
      .filter((message) => message.status === "complete")
      .map(toContextSourceMessage),

    config: validatedConfig,
  });

  logCompiledContext(
    chatId,
    agent.id,
    validatedConfig,
    compiledContext.manifest,
    compiledContext.warnings,
  );

  const result = await provider.generateText({
    instructions: buildAgentInstructions(agent),

    messages: compiledContext.providerMessages,

    generation: agent.generation,
  });

  const assistantMessage = createMessage(chatId, {
    role: "assistant",
    content: result.text,
    status: "complete",
  });

  return {
    userMessage,
    assistantMessage,
    provider: result.provider,
    model: result.model,
    agentId: agent.id,
    contextManifest: compiledContext.manifest,
    contextWarnings: compiledContext.warnings,
  };
}

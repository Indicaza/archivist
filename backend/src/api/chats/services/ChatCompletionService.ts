import { openAIProvider } from "../../../core/ai/providers/OpenAIProvider.js";
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
  config: ContextCompilerConfig,
  manifest: ContextManifest,
  warnings: string[],
): void {
  if (process.env.NODE_ENV === "production") {
    return;
  }

  console.debug("[ContextCompiler]", {
    chatId,
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

  const userMessage = createMessage(chatId, {
    role: "user",
    content,
    status: "complete",
  });

  const storedMessages = getMessagesByChatId(chatId);

  const definition = contextCompilerRegistry.getDefinition(
    chat.context.compiler,
  );

  const validatedConfig = contextCompilerRegistry.parseConfig(
    chat.context.compiler,
    chat.context.config,
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
    validatedConfig,
    compiledContext.manifest,
    compiledContext.warnings,
  );

  const result = await openAIProvider.generateText({
    messages: compiledContext.providerMessages,
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
    contextManifest: compiledContext.manifest,
    contextWarnings: compiledContext.warnings,
  };
}

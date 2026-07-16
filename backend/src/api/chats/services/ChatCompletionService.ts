import { openAIProvider } from "../../../core/ai/providers/OpenAIProvider.js";
import { createMessage, getMessagesByChatId } from "../models/Chat.js";
import type { ChatMessage } from "../types/ChatTypes.js";

type CompleteChatTurnResult = {
  userMessage: ChatMessage;
  assistantMessage: ChatMessage;
  provider: string;
  model: string;
};

export async function completeChatTurn(
  chatId: string,
  content: string,
): Promise<CompleteChatTurnResult> {
  const userMessage = createMessage(chatId, {
    role: "user",
    content,
    status: "complete",
  });

  const history = getMessagesByChatId(chatId);

  const result = await openAIProvider.generateText({
    messages: history
      .filter((message) => message.status === "complete")
      .map((message) => ({
        role: message.role,
        content: message.content,
      })),
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
  };
}

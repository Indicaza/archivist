import { AppError } from "../../../../errors/app-error.js";
import {
  getMessageById,
  updateMessage,
} from "../../../chats/models/Chat.js";
import { getChatFileAttachments } from "../../../chats/models/ChatAttachment.js";
import {
  beginChatTurn,
  completeChatTurnSession,
} from "../../../chats/services/ChatCompletionService.js";
import type { ChatMessage } from "../../../chats/types/ChatTypes.js";
import {
  cancelAIRun,
  completeAIRun,
  createAIRun,
  failAIRun,
  getActiveAIRunByChatId,
  isAIRunTerminal,
  requireAIRun,
} from "../models/AIRun.js";
import type { AIRun } from "../types/AIRunTypes.js";
import { aiRunEventBroker } from "./AIRunEventBroker.js";

const activeRunControllers = new Map<string, AbortController>();
const activeRunBuffers = new Map<string, string>();

function errorMessage(error: unknown): string {
  if (error instanceof Error && error.message.trim()) {
    return error.message;
  }

  return "The AI Run failed unexpectedly.";
}

function errorCode(error: unknown): string {
  if (error instanceof Error && error.name === "AbortError") {
    return "cancelled";
  }

  return "generation_failed";
}

async function executeChatRun(
  runId: string,
  session: ReturnType<typeof beginChatTurn>,
  controller: AbortController,
): Promise<void> {
  try {
    const result = await completeChatTurnSession(session, {
      signal: controller.signal,
      onEvent(event) {
        if (
          event.type === "model.delta" &&
          typeof event.payload?.delta === "string"
        ) {
          activeRunBuffers.set(
            runId,
            `${activeRunBuffers.get(runId) ?? ""}${event.payload.delta}`,
          );
        }

        aiRunEventBroker.append(runId, event.type, event.payload);
      },
    });

    const currentRun = requireAIRun(runId);

    if (currentRun.status !== "running") {
      return;
    }

    completeAIRun(
      runId,
      result.assistantMessage.content,
      result.contextRunId,
    );
    aiRunEventBroker.append(runId, "run.completed", {
      assistantMessageId: result.assistantMessage.id,
      contextRunId: result.contextRunId,
      characterCount: result.assistantMessage.content.length,
    });
  } catch (error) {
    const currentRun = requireAIRun(runId);
    const assistantMessage = getMessageById(currentRun.assistantMessageId);

    if (currentRun.status === "cancelled") {
      cancelAIRun(runId, assistantMessage?.content ?? "");
      return;
    }

    if (isAIRunTerminal(currentRun.status)) {
      return;
    }

    const finalResponse = assistantMessage?.content ?? "";

    if (controller.signal.aborted || errorCode(error) === "cancelled") {
      updateMessage(currentRun.assistantMessageId, {
        content: finalResponse,
        status: "cancelled",
      });
      cancelAIRun(runId, finalResponse);
      aiRunEventBroker.append(runId, "run.cancelled", {
        assistantMessageId: currentRun.assistantMessageId,
        characterCount: finalResponse.length,
      });
      return;
    }

    const message = errorMessage(error);

    updateMessage(currentRun.assistantMessageId, {
      content: finalResponse,
      status: "failed",
    });
    failAIRun(runId, errorCode(error), message, finalResponse);
    aiRunEventBroker.append(runId, "run.failed", {
      errorCode: errorCode(error),
      message,
      assistantMessageId: currentRun.assistantMessageId,
      characterCount: finalResponse.length,
    });
  } finally {
    activeRunControllers.delete(runId);
    activeRunBuffers.delete(runId);
  }
}

export function startChatRun(
  chatId: string,
  content: string,
): {
  run: AIRun;
  userMessage: ChatMessage;
  assistantMessage: ChatMessage;
} {
  const activeRun = getActiveAIRunByChatId(chatId);

  if (activeRun) {
    throw new AppError(409, "This Chat already has an active AI Run.", {
      runId: activeRun.id,
    });
  }

  const session = beginChatTurn(chatId, content);
  const attachedFiles = getChatFileAttachments(chatId).map((attachment) => ({
    attachmentId: attachment.id,
    libraryId: attachment.libraryId,
    libraryName: attachment.libraryName,
    fileId: attachment.fileId,
    fileName: attachment.fileName,
    relativePath: attachment.relativePath,
    fileStatus: attachment.fileStatus,
  }));
  const run = createAIRun({
    chatId,
    libraryId: session.libraryId,
    agentId: session.agent.id,
    userMessageId: session.userMessage.id,
    assistantMessageId: session.assistantMessage.id,
    contextCompiler: session.agent.context.compiler,
    provider: session.agent.generation.provider,
    model: session.agent.generation.model,
  });
  const controller = new AbortController();

  activeRunControllers.set(run.id, controller);
  activeRunBuffers.set(run.id, "");
  aiRunEventBroker.append(run.id, "run.started", {
    chatId,
    libraryId: session.libraryId,
    agentId: session.agent.id,
    userMessageId: session.userMessage.id,
    assistantMessageId: session.assistantMessage.id,
    contextCompiler: session.agent.context.compiler,
    provider: session.agent.generation.provider,
    model: session.agent.generation.model,
    attachedFiles,
  });

  queueMicrotask(() => {
    void executeChatRun(run.id, session, controller);
  });

  return {
    run: requireAIRun(run.id),
    userMessage: session.userMessage,
    assistantMessage: session.assistantMessage,
  };
}

export function cancelChatRun(runId: string): AIRun {
  const run = requireAIRun(runId);

  if (isAIRunTerminal(run.status)) {
    return run;
  }

  const controller = activeRunControllers.get(runId);
  controller?.abort();

  const assistantMessage = getMessageById(run.assistantMessageId);
  const finalResponse =
    activeRunBuffers.get(runId) ?? assistantMessage?.content ?? "";

  updateMessage(run.assistantMessageId, {
    content: finalResponse,
    status: "cancelled",
  });
  cancelAIRun(runId, finalResponse);
  aiRunEventBroker.append(runId, "run.cancelled", {
    assistantMessageId: run.assistantMessageId,
    characterCount: finalResponse.length,
  });

  return requireAIRun(runId);
}

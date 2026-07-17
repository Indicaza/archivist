import { z } from "zod";
import type { AIMessage } from "../../../../ai/AIProvider.js";
import type { ContextCompiler } from "../ContextCompiler.js";
import type { ContextCompilerDefinition } from "../ContextCompilerDefinition.js";
import type {
  CompileContextInput,
  CompiledContext,
  ContextSourceMessage,
} from "../ContextCompilerTypes.js";
import { estimateTokens } from "../utilities/estimateTokens.js";

const recentHistoryConfigSchema = z
  .object({
    totalTokens: z.number().int().min(1_000).max(200_000),
    responseTokenReserve: z.number().int().min(256).max(32_000),
  })
  .refine((config) => config.responseTokenReserve < config.totalTokens, {
    message: "Response token reserve must be smaller than total tokens.",
    path: ["responseTokenReserve"],
  });

export type RecentHistoryConfig = z.infer<typeof recentHistoryConfigSchema>;

const DESCRIPTOR = {
  id: "recent-history",
  version: 1,
  name: "Recent History",
} as const;

const DEFAULT_CONFIG: RecentHistoryConfig = {
  totalTokens: 32_000,
  responseTokenReserve: 4_000,
};

function toProviderMessage(message: ContextSourceMessage): AIMessage {
  return {
    role: message.role,
    content: message.content,
  };
}

export class RecentHistoryCompilerV1 implements ContextCompiler<RecentHistoryConfig> {
  readonly descriptor = DESCRIPTOR;

  compile(input: CompileContextInput<RecentHistoryConfig>): CompiledContext {
    const startedAt = performance.now();
    const warnings: string[] = [];

    const availableInputTokens =
      input.config.totalTokens - input.config.responseTokenReserve;

    const eligibleMessages = input.messages.filter(
      (message) => message.content.trim().length > 0,
    );

    const currentMessage = eligibleMessages.find(
      (message) => message.id === input.currentMessageId,
    );

    if (!currentMessage) {
      throw new Error(
        `Current message ${input.currentMessageId} was not supplied to the Context Compiler.`,
      );
    }

    const currentMessageTokens = estimateTokens(currentMessage.content);

    if (currentMessageTokens > availableInputTokens) {
      warnings.push(
        "The current user message exceeds the provider input budget and was preserved without truncation.",
      );
    }

    const selectedMessages: ContextSourceMessage[] = [currentMessage];
    const selectedMessageIds = new Set([currentMessage.id]);

    let estimatedInputTokens = currentMessageTokens;

    for (let index = eligibleMessages.length - 1; index >= 0; index -= 1) {
      const message = eligibleMessages[index];

      if (selectedMessageIds.has(message.id)) {
        continue;
      }

      const messageTokens = estimateTokens(message.content);

      if (estimatedInputTokens + messageTokens > availableInputTokens) {
        continue;
      }

      selectedMessages.push(message);
      selectedMessageIds.add(message.id);
      estimatedInputTokens += messageTokens;
    }

    selectedMessages.sort((left, right) => {
      const createdAtDifference =
        new Date(left.createdAt).getTime() -
        new Date(right.createdAt).getTime();

      if (createdAtDifference !== 0) {
        return createdAtDifference;
      }

      return eligibleMessages.indexOf(left) - eligibleMessages.indexOf(right);
    });

    const omittedMessages = eligibleMessages.filter(
      (message) => !selectedMessageIds.has(message.id),
    );

    return {
      providerMessages: selectedMessages.map(toProviderMessage),

      manifest: {
        compiler: this.descriptor,
        includedMessageIds: selectedMessages.map((message) => message.id),
        omittedMessageIds: omittedMessages.map((message) => message.id),
        includedMessageCount: selectedMessages.length,
        omittedMessageCount: omittedMessages.length,
        estimatedInputTokens,
        responseTokenReserve: input.config.responseTokenReserve,
        totalBudget: input.config.totalTokens,
        compilationTimeMs: Number((performance.now() - startedAt).toFixed(3)),
      },

      warnings,
    };
  }
}

export const recentHistoryCompilerV1 = new RecentHistoryCompilerV1();

export const recentHistoryCompilerV1Definition: ContextCompilerDefinition<RecentHistoryConfig> =
  {
    descriptor: DESCRIPTOR,

    description:
      "Includes the newest complete messages that fit within the configured token budget while always preserving the current user message.",

    configSchema: recentHistoryConfigSchema,

    defaultConfig: DEFAULT_CONFIG,

    configFields: [
      {
        type: "integer",
        key: "totalTokens",
        label: "Total token budget",
        description:
          "Maximum combined budget available for provider input and response output.",
        minimum: 1_000,
        maximum: 200_000,
        step: 1_000,
      },
      {
        type: "integer",
        key: "responseTokenReserve",
        label: "Response token reserve",
        description:
          "Tokens reserved for the assistant response before selecting conversation history.",
        minimum: 256,
        maximum: 32_000,
        step: 256,
      },
    ],

    compiler: recentHistoryCompilerV1,
  };

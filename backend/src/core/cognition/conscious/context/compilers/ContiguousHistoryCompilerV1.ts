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

const contiguousHistoryConfigSchema = z
  .object({
    totalTokens: z.number().int().min(1_000).max(200_000),
    responseTokenReserve: z.number().int().min(256).max(32_000),
  })
  .refine((config) => config.responseTokenReserve < config.totalTokens, {
    message: "Response token reserve must be smaller than total tokens.",
    path: ["responseTokenReserve"],
  });

export type ContiguousHistoryConfig = z.infer<
  typeof contiguousHistoryConfigSchema
>;

const DESCRIPTOR = {
  id: "contiguous-history",
  version: 1,
  name: "Contiguous History",
} as const;

const DEFAULT_CONFIG: ContiguousHistoryConfig = {
  totalTokens: 32_000,
  responseTokenReserve: 4_000,
};

function toProviderMessage(message: ContextSourceMessage): AIMessage {
  return {
    role: message.role,
    content: message.content,
  };
}

export class ContiguousHistoryCompilerV1 implements ContextCompiler<ContiguousHistoryConfig> {
  readonly descriptor = DESCRIPTOR;

  compile(
    input: CompileContextInput<ContiguousHistoryConfig>,
  ): CompiledContext {
    const startedAt = performance.now();
    const warnings: string[] = [];

    const availableInputTokens =
      input.config.totalTokens - input.config.responseTokenReserve;

    const eligibleMessages = input.messages.filter(
      (message) => message.content.trim().length > 0,
    );

    const currentMessageIndex = eligibleMessages.findIndex(
      (message) => message.id === input.currentMessageId,
    );

    if (currentMessageIndex < 0) {
      throw new Error(
        `Current message ${input.currentMessageId} was not supplied to the Context Compiler.`,
      );
    }

    const currentMessage = eligibleMessages[currentMessageIndex];
    const currentMessageTokens = estimateTokens(currentMessage.content);

    if (currentMessageTokens > availableInputTokens) {
      warnings.push(
        "The current user message exceeds the provider input budget and was preserved without truncation.",
      );
    }

    const selectedMessages: ContextSourceMessage[] = [currentMessage];
    let estimatedInputTokens = currentMessageTokens;

    for (let index = currentMessageIndex - 1; index >= 0; index -= 1) {
      const message = eligibleMessages[index];
      const messageTokens = estimateTokens(message.content);

      if (estimatedInputTokens + messageTokens > availableInputTokens) {
        break;
      }

      selectedMessages.unshift(message);
      estimatedInputTokens += messageTokens;
    }

    const selectedMessageIds = new Set(
      selectedMessages.map((message) => message.id),
    );

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

export const contiguousHistoryCompilerV1 = new ContiguousHistoryCompilerV1();

export const contiguousHistoryCompilerV1Definition: ContextCompilerDefinition<ContiguousHistoryConfig> =
  {
    descriptor: DESCRIPTOR,

    description:
      "Includes one uninterrupted window of recent conversation history and stops when the next older message does not fit.",

    configSchema: contiguousHistoryConfigSchema,

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
        step: 1,
      },
      {
        type: "integer",
        key: "responseTokenReserve",
        label: "Response token reserve",
        description:
          "Tokens reserved for the assistant response before selecting conversation history.",
        minimum: 256,
        maximum: 32_000,
        step: 1,
      },
    ],

    compiler: contiguousHistoryCompilerV1,
  };

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

const turnPairsConfigSchema = z
  .object({
    totalTokens: z.number().int().min(1_000).max(200_000),
    responseTokenReserve: z.number().int().min(256).max(32_000),
  })
  .refine((config) => config.responseTokenReserve < config.totalTokens, {
    message: "Response token reserve must be smaller than total tokens.",
    path: ["responseTokenReserve"],
  });

export type TurnPairsConfig = z.infer<typeof turnPairsConfigSchema>;

const DESCRIPTOR = {
  id: "turn-pairs",
  version: 1,
  name: "Turn Pairs",
} as const;

const DEFAULT_CONFIG: TurnPairsConfig = {
  totalTokens: 32_000,
  responseTokenReserve: 4_000,
};

function toProviderMessage(message: ContextSourceMessage): AIMessage {
  return {
    role: message.role,
    content: message.content,
  };
}

function estimateMessageGroupTokens(messages: ContextSourceMessage[]): number {
  return messages.reduce(
    (total, message) => total + estimateTokens(message.content),
    0,
  );
}

function groupMessagesIntoTurns(
  messages: ContextSourceMessage[],
): ContextSourceMessage[][] {
  const groups: ContextSourceMessage[][] = [];
  let activeGroup: ContextSourceMessage[] = [];

  for (const message of messages) {
    if (message.role === "user") {
      if (activeGroup.length > 0) {
        groups.push(activeGroup);
      }

      activeGroup = [message];
      continue;
    }

    if (activeGroup.length === 0) {
      groups.push([message]);
      continue;
    }

    activeGroup.push(message);
  }

  if (activeGroup.length > 0) {
    groups.push(activeGroup);
  }

  return groups;
}

export class TurnPairsCompilerV1 implements ContextCompiler<TurnPairsConfig> {
  readonly descriptor = DESCRIPTOR;

  compile(input: CompileContextInput<TurnPairsConfig>): CompiledContext {
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

    const historicalMessages = eligibleMessages.slice(0, currentMessageIndex);

    const historicalTurns = groupMessagesIntoTurns(historicalMessages);

    const selectedTurns: ContextSourceMessage[][] = [];
    let estimatedInputTokens = currentMessageTokens;

    for (let index = historicalTurns.length - 1; index >= 0; index -= 1) {
      const turn = historicalTurns[index];
      const turnTokens = estimateMessageGroupTokens(turn);

      if (estimatedInputTokens + turnTokens > availableInputTokens) {
        break;
      }

      selectedTurns.unshift(turn);
      estimatedInputTokens += turnTokens;
    }

    const selectedMessages = [...selectedTurns.flat(), currentMessage];

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

export const turnPairsCompilerV1 = new TurnPairsCompilerV1();

export const turnPairsCompilerV1Definition: ContextCompilerDefinition<TurnPairsConfig> =
  {
    descriptor: DESCRIPTOR,

    description:
      "Selects recent user and assistant turns as complete groups so questions and answers are not separated by token budgeting.",

    configSchema: turnPairsConfigSchema,

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
          "Tokens reserved for the assistant response before selecting conversation turns.",
        minimum: 256,
        maximum: 32_000,
        step: 1,
      },
    ],

    compiler: turnPairsCompilerV1,
  };

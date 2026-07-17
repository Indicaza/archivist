import { z } from "zod";
import type { AIMessage, AIMessageRole } from "../../../../ai/AIProvider.js";
import { chatMessageFtsTool } from "../../../retrieval/tools/ChatMessageFtsTool.js";
import type { ContextCandidate } from "../../../retrieval/ContextRetrievalTypes.js";
import type { ContextCompiler } from "../ContextCompiler.js";
import type { ContextCompilerDefinition } from "../ContextCompilerDefinition.js";
import type {
  CompileContextInput,
  CompiledContext,
  ContextSourceMessage,
} from "../ContextCompilerTypes.js";
import { estimateTokens } from "../utilities/estimateTokens.js";

const autoBalancedConfigSchema = z
  .object({
    totalTokens: z.number().int().min(1_000).max(200_000),

    responseTokenReserve: z.number().int().min(256).max(32_000),

    recentHistoryTokens: z.number().int().min(256).max(160_000),

    retrievalTokens: z.number().int().min(0).max(160_000),

    retrievalResultLimit: z.number().int().min(1).max(50),

    searchScope: z.enum(["current-chat", "all-chats"]),

    minimumSearchTermLength: z.number().int().min(2).max(20),
  })
  .superRefine((config, context) => {
    const availableInputTokens =
      config.totalTokens - config.responseTokenReserve;

    if (config.responseTokenReserve >= config.totalTokens) {
      context.addIssue({
        code: z.ZodIssueCode.custom,
        path: ["responseTokenReserve"],
        message:
          "Response token reserve must be smaller than the total token budget.",
      });
    }

    if (
      config.recentHistoryTokens + config.retrievalTokens >
      availableInputTokens
    ) {
      context.addIssue({
        code: z.ZodIssueCode.custom,
        path: ["retrievalTokens"],
        message:
          "Recent-history and retrieval budgets cannot exceed the available provider input budget.",
      });
    }
  });

export type AutoBalancedConfig = z.infer<typeof autoBalancedConfigSchema>;

const DESCRIPTOR = {
  id: "auto-balanced",
  version: 1,
  name: "Auto Balanced",
} as const;

const DEFAULT_CONFIG: AutoBalancedConfig = {
  totalTokens: 32_000,
  responseTokenReserve: 4_000,
  recentHistoryTokens: 18_000,
  retrievalTokens: 10_000,
  retrievalResultLimit: 16,
  searchScope: "current-chat",
  minimumSearchTermLength: 3,
};

function toProviderMessage(message: ContextSourceMessage): AIMessage {
  return {
    role: message.role,
    content: message.content,
  };
}

function compareMessages(
  left: ContextSourceMessage,
  right: ContextSourceMessage,
): number {
  const createdAtDifference =
    new Date(left.createdAt).getTime() - new Date(right.createdAt).getTime();

  if (createdAtDifference !== 0) {
    return createdAtDifference;
  }

  return left.id.localeCompare(right.id);
}

function getSearchTerms(content: string, minimumLength: number): string[] {
  return Array.from(
    new Set(
      (content.toLocaleLowerCase().match(/[\p{L}\p{N}_']+/gu) ?? []).filter(
        (term) => term.length >= minimumLength,
      ),
    ),
  ).slice(0, 24);
}

function isMessageRole(value: unknown): value is AIMessageRole {
  return value === "user" || value === "assistant" || value === "system";
}

function candidateToSourceMessage(
  candidate: ContextCandidate,
): ContextSourceMessage | null {
  const messageId = candidate.metadata.messageId;
  const role = candidate.metadata.role;
  const createdAt = candidate.metadata.createdAt;

  if (
    typeof messageId !== "string" ||
    !isMessageRole(role) ||
    typeof createdAt !== "string"
  ) {
    return null;
  }

  return {
    id: messageId,
    role,
    content: candidate.content,
    createdAt,
  };
}

export class AutoBalancedCompilerV1 implements ContextCompiler<AutoBalancedConfig> {
  readonly descriptor = DESCRIPTOR;

  compile(input: CompileContextInput<AutoBalancedConfig>): CompiledContext {
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

    const selectedMessages = new Map<string, ContextSourceMessage>();

    selectedMessages.set(currentMessage.id, currentMessage);

    let recentTokens = currentMessageTokens;
    let retrievalTokens = 0;
    let estimatedInputTokens = currentMessageTokens;

    /*
     * Phase 1:
     * Preserve a contiguous block of recent conversation history.
     *
     * We stop at the first message that does not fit so the recent
     * conversation does not develop unexplained holes.
     */
    for (let index = eligibleMessages.length - 1; index >= 0; index -= 1) {
      const message = eligibleMessages[index];

      if (selectedMessages.has(message.id)) {
        continue;
      }

      const messageTokens = estimateTokens(message.content);

      if (
        recentTokens + messageTokens > input.config.recentHistoryTokens ||
        estimatedInputTokens + messageTokens > availableInputTokens
      ) {
        break;
      }

      selectedMessages.set(message.id, message);
      recentTokens += messageTokens;
      estimatedInputTokens += messageTokens;
    }

    /*
     * Phase 2:
     * Use the current message as the deterministic FTS query.
     */
    const searchTerms = getSearchTerms(
      currentMessage.content,
      input.config.minimumSearchTermLength,
    );

    if (input.config.retrievalTokens > 0 && searchTerms.length > 0) {
      const retrievalResult = chatMessageFtsTool.search({
        query: searchTerms.join(" "),
        chatId:
          input.config.searchScope === "current-chat"
            ? input.chatId
            : undefined,
        limit: input.config.retrievalResultLimit,
      });

      warnings.push(...retrievalResult.warnings);

      for (const candidate of retrievalResult.candidates) {
        if (selectedMessages.has(candidate.id)) {
          continue;
        }

        const message = candidateToSourceMessage(candidate);

        if (!message) {
          warnings.push(
            `Retrieved candidate ${candidate.id} was omitted because its message metadata was incomplete.`,
          );

          continue;
        }

        const messageTokens = candidate.estimatedTokens;

        if (retrievalTokens + messageTokens > input.config.retrievalTokens) {
          continue;
        }

        if (estimatedInputTokens + messageTokens > availableInputTokens) {
          continue;
        }

        selectedMessages.set(message.id, message);
        retrievalTokens += messageTokens;
        estimatedInputTokens += messageTokens;
      }
    } else if (input.config.retrievalTokens > 0) {
      warnings.push(
        "The current message did not contain enough searchable terms for FTS retrieval.",
      );
    }

    const orderedMessages = Array.from(selectedMessages.values()).sort(
      compareMessages,
    );

    const includedMessageIds = new Set(
      orderedMessages.map((message) => message.id),
    );

    const omittedMessages = eligibleMessages.filter(
      (message) => !includedMessageIds.has(message.id),
    );

    return {
      providerMessages: orderedMessages.map(toProviderMessage),

      manifest: {
        compiler: this.descriptor,
        includedMessageIds: orderedMessages.map((message) => message.id),
        omittedMessageIds: omittedMessages.map((message) => message.id),
        includedMessageCount: orderedMessages.length,
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

export const autoBalancedCompilerV1 = new AutoBalancedCompilerV1();

export const autoBalancedCompilerV1Definition: ContextCompilerDefinition<AutoBalancedConfig> =
  {
    descriptor: DESCRIPTOR,

    description:
      "Preserves a contiguous block of recent conversation history, then searches indexed Chat messages for older relevant context and merges both within separate token budgets.",

    configSchema: autoBalancedConfigSchema,

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
          "Tokens reserved for the assistant response before context selection.",
        minimum: 256,
        maximum: 32_000,
        step: 256,
      },
      {
        type: "integer",
        key: "recentHistoryTokens",
        label: "Recent-history budget",
        description:
          "Maximum tokens reserved for a contiguous block of recent Chat history, including the current message.",
        minimum: 256,
        maximum: 160_000,
        step: 256,
      },
      {
        type: "integer",
        key: "retrievalTokens",
        label: "Retrieval budget",
        description:
          "Maximum tokens available for older messages returned by indexed search.",
        minimum: 0,
        maximum: 160_000,
        step: 256,
      },
      {
        type: "integer",
        key: "retrievalResultLimit",
        label: "Retrieval candidate limit",
        description:
          "Maximum number of ranked FTS candidates considered before token budgeting.",
        minimum: 1,
        maximum: 50,
        step: 1,
      },
      {
        type: "select",
        key: "searchScope",
        label: "Search scope",
        description:
          "Search only the current Chat or search every Chat stored by Archivist.",
        options: [
          {
            value: "current-chat",
            label: "Current Chat",
          },
          {
            value: "all-chats",
            label: "All Chats",
          },
        ],
      },
      {
        type: "integer",
        key: "minimumSearchTermLength",
        label: "Minimum search-term length",
        description:
          "Ignores shorter words when constructing the deterministic FTS query.",
        minimum: 2,
        maximum: 20,
        step: 1,
      },
    ],

    compiler: autoBalancedCompilerV1,
  };

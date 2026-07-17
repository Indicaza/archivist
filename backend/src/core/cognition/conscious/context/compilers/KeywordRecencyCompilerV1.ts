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

const keywordRecencyConfigSchema = z
  .object({
    totalTokens: z.number().int().min(1_000).max(200_000),
    responseTokenReserve: z.number().int().min(256).max(32_000),
    recentMessageFloor: z.number().int().min(0).max(50),
    candidateLimit: z.number().int().min(1).max(500),
    keywordWeight: z.number().int().min(0).max(100),
    recencyWeight: z.number().int().min(0).max(100),
  })
  .refine((config) => config.responseTokenReserve < config.totalTokens, {
    message: "Response token reserve must be smaller than total tokens.",
    path: ["responseTokenReserve"],
  })
  .refine((config) => config.keywordWeight > 0 || config.recencyWeight > 0, {
    message: "Keyword weight and recency weight cannot both be zero.",
    path: ["keywordWeight"],
  });

export type KeywordRecencyConfig = z.infer<typeof keywordRecencyConfigSchema>;

const DESCRIPTOR = {
  id: "keyword-recency",
  version: 1,
  name: "Keyword Recency",
} as const;

const DEFAULT_CONFIG: KeywordRecencyConfig = {
  totalTokens: 32_000,
  responseTokenReserve: 4_000,
  recentMessageFloor: 4,
  candidateLimit: 40,
  keywordWeight: 70,
  recencyWeight: 30,
};

const STOP_WORDS = new Set([
  "about",
  "after",
  "again",
  "also",
  "because",
  "before",
  "being",
  "between",
  "could",
  "does",
  "from",
  "have",
  "into",
  "just",
  "more",
  "most",
  "other",
  "over",
  "said",
  "should",
  "some",
  "than",
  "that",
  "their",
  "them",
  "then",
  "there",
  "these",
  "they",
  "this",
  "those",
  "through",
  "very",
  "want",
  "were",
  "what",
  "when",
  "where",
  "which",
  "while",
  "with",
  "would",
  "your",
]);

type ScoredMessage = {
  message: ContextSourceMessage;
  originalIndex: number;
  score: number;
};

function toProviderMessage(message: ContextSourceMessage): AIMessage {
  return {
    role: message.role,
    content: message.content,
  };
}

function extractKeywords(content: string): Set<string> {
  const words = content.toLowerCase().match(/[a-z0-9][a-z0-9'-]*/g);

  if (!words) {
    return new Set();
  }

  return new Set(
    words.filter((word) => word.length >= 3 && !STOP_WORDS.has(word)),
  );
}

function countKeywordOverlap(
  queryKeywords: Set<string>,
  content: string,
): number {
  if (queryKeywords.size === 0) {
    return 0;
  }

  const messageKeywords = extractKeywords(content);
  let overlap = 0;

  for (const keyword of queryKeywords) {
    if (messageKeywords.has(keyword)) {
      overlap += 1;
    }
  }

  return overlap;
}

export class KeywordRecencyCompilerV1 implements ContextCompiler<KeywordRecencyConfig> {
  readonly descriptor = DESCRIPTOR;

  compile(input: CompileContextInput<KeywordRecencyConfig>): CompiledContext {
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

    const selectedMessages: ContextSourceMessage[] = [currentMessage];

    const selectedMessageIds = new Set<string>([currentMessage.id]);

    let estimatedInputTokens = currentMessageTokens;

    const recentFloorStart = Math.max(
      0,
      historicalMessages.length - input.config.recentMessageFloor,
    );

    for (
      let index = recentFloorStart;
      index < historicalMessages.length;
      index += 1
    ) {
      const message = historicalMessages[index];
      const messageTokens = estimateTokens(message.content);

      if (estimatedInputTokens + messageTokens > availableInputTokens) {
        continue;
      }

      selectedMessages.push(message);
      selectedMessageIds.add(message.id);
      estimatedInputTokens += messageTokens;
    }

    const queryKeywords = extractKeywords(currentMessage.content);

    const scoredCandidates: ScoredMessage[] = historicalMessages
      .map((message, originalIndex) => {
        const overlap = countKeywordOverlap(queryKeywords, message.content);

        const recency =
          historicalMessages.length > 0
            ? (originalIndex + 1) / historicalMessages.length
            : 0;

        return {
          message,
          originalIndex,
          score:
            overlap * input.config.keywordWeight +
            recency * input.config.recencyWeight,
        };
      })
      .filter((candidate) => !selectedMessageIds.has(candidate.message.id))
      .sort((left, right) => {
        if (right.score !== left.score) {
          return right.score - left.score;
        }

        return right.originalIndex - left.originalIndex;
      })
      .slice(0, input.config.candidateLimit);

    for (const candidate of scoredCandidates) {
      const messageTokens = estimateTokens(candidate.message.content);

      if (estimatedInputTokens + messageTokens > availableInputTokens) {
        continue;
      }

      selectedMessages.push(candidate.message);
      selectedMessageIds.add(candidate.message.id);
      estimatedInputTokens += messageTokens;
    }

    selectedMessages.sort((left, right) => {
      const leftIndex = eligibleMessages.findIndex(
        (message) => message.id === left.id,
      );

      const rightIndex = eligibleMessages.findIndex(
        (message) => message.id === right.id,
      );

      return leftIndex - rightIndex;
    });

    const omittedMessages = eligibleMessages.filter(
      (message) => !selectedMessageIds.has(message.id),
    );

    if (queryKeywords.size === 0) {
      warnings.push(
        "The current message produced no usable keywords, so selection relied primarily on recency.",
      );
    }

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

export const keywordRecencyCompilerV1 = new KeywordRecencyCompilerV1();

export const keywordRecencyCompilerV1Definition: ContextCompilerDefinition<KeywordRecencyConfig> =
  {
    descriptor: DESCRIPTOR,

    description:
      "Combines a guaranteed recent-message floor with deterministic keyword-overlap and recency scoring for older messages.",

    configSchema: keywordRecencyConfigSchema,

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
          "Tokens reserved for the assistant response before selecting context.",
        minimum: 256,
        maximum: 32_000,
        step: 1,
      },
      {
        type: "integer",
        key: "recentMessageFloor",
        label: "Recent message floor",
        description:
          "Number of newest historical messages considered before keyword-ranked retrieval.",
        minimum: 0,
        maximum: 50,
        step: 1,
      },
      {
        type: "integer",
        key: "candidateLimit",
        label: "Candidate limit",
        description:
          "Maximum number of older ranked messages considered for inclusion.",
        minimum: 1,
        maximum: 500,
        step: 1,
      },
      {
        type: "integer",
        key: "keywordWeight",
        label: "Keyword weight",
        description:
          "How strongly exact query-term overlap affects message ranking.",
        minimum: 0,
        maximum: 100,
        step: 1,
      },
      {
        type: "integer",
        key: "recencyWeight",
        label: "Recency weight",
        description:
          "How strongly newer historical messages are favored during ranking.",
        minimum: 0,
        maximum: 100,
        step: 1,
      },
    ],

    compiler: keywordRecencyCompilerV1,
  };

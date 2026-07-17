import type { AIMessage, AIMessageRole } from "../../../ai/AIProvider.js";

export type ContextCompilerDescriptor = {
  id: string;
  version: number;
  name: string;
};

export type ContextCompilerReference = {
  id: string;
  version: number;
};

export type ContextCompilerConfig = Record<string, unknown>;

export type ContextSourceMessage = {
  id: string;
  role: AIMessageRole;
  content: string;
  createdAt: string;
};

export type CompileContextInput<
  TConfig extends ContextCompilerConfig = ContextCompilerConfig,
> = {
  chatId: string;
  currentMessageId: string;
  messages: ContextSourceMessage[];
  config: TConfig;
};

export type ContextManifest = {
  compiler: ContextCompilerDescriptor;
  includedMessageIds: string[];
  omittedMessageIds: string[];
  includedMessageCount: number;
  omittedMessageCount: number;
  estimatedInputTokens: number;
  responseTokenReserve: number;
  totalBudget: number;
  compilationTimeMs: number;
};

export type CompiledContext = {
  providerMessages: AIMessage[];
  manifest: ContextManifest;
  warnings: string[];
};

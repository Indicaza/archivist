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

export type ContextManifestSource = {
  id: string;
  source: "library-file";
  label: string;
  estimatedTokens: number;
  truncated: boolean;
  metadata: Record<string, string | number | boolean | null>;
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
  includedSources?: ContextManifestSource[];
};

export type CompiledContext = {
  providerMessages: AIMessage[];
  manifest: ContextManifest;
  warnings: string[];
};

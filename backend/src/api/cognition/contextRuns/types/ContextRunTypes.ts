import type {
  ContextCompilerDescriptor,
  ContextManifest,
} from "../../../../core/cognition/conscious/context/ContextCompilerTypes.js";

export type ContextSourceOutcomeStatus =
  | "included"
  | "truncated"
  | "omitted"
  | "unavailable"
  | "failed";

export type ContextSourceOutcome = {
  id: string;
  source: "library-file";
  label: string;
  status: ContextSourceOutcomeStatus;
  estimatedTokens: number;
  includedTokens: number;
  truncated: boolean;
  reason: string | null;
  metadata: Record<string, string | number | boolean | null>;
};

export type ContextRun = {
  id: string;
  chatId: string;
  userMessageId: string;
  assistantMessageId: string;
  provider: string;
  model: string;
  agentId: string;
  compiler: ContextCompilerDescriptor;
  manifest: ContextManifest;
  warnings: string[];
  sources: ContextSourceOutcome[];
  createdAt: string;
};

export type CreateContextRunInput = Omit<
  ContextRun,
  "id" | "createdAt"
>;

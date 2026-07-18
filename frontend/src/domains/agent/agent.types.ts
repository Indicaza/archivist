import type {
  ContextCompilerConfig,
  ContextCompilerReference,
} from "../cognition/contextCompiler.types";

export type AgentIdentityConfig = {
  personality: string | null;
  temperament: string | null;
  voice: string | null;
  backstory: string | null;
};

export type AgentProfessionConfig = {
  jobTitle: string | null;
  mission: string | null;
  expertise: string[];
  responsibilities: string[];
  successCriteria: string[];
  limitations: string[];
};

export type AgentVerbosity = "concise" | "balanced" | "detailed";

export type AgentOutputContract = {
  responseStyle: string | null;
  verbosity: AgentVerbosity;
  formattingRules: string[];
  codeOutputPreferences: string[];
  citationRequirements: string | null;
  followUpBehavior: string | null;
};

export type GenerationConfig = {
  provider: "openai";
  model: string;
  temperature: number | null;
  maxOutputTokens: number | null;
  topP: number | null;
  frequencyPenalty: number | null;
  presencePenalty: number | null;
};

export type AgentContextSettings = {
  compiler: ContextCompilerReference;
  config: ContextCompilerConfig;
};

export type Agent = {
  id: string;
  name: string;
  description: string | null;
  identity: AgentIdentityConfig;
  profession: AgentProfessionConfig;
  doctrine: string | null;
  outputContract: AgentOutputContract;
  systemInstructions: string | null;
  generation: GenerationConfig;
  context: AgentContextSettings;
  isBuiltIn: boolean;
  archivedAt: string | null;
  createdAt: string;
  updatedAt: string;
};

export type CreateAgentInput = {
  name?: string;
  description?: string | null;
  identity?: Partial<AgentIdentityConfig>;
  profession?: Partial<AgentProfessionConfig>;
  doctrine?: string | null;
  outputContract?: Partial<AgentOutputContract>;
  systemInstructions?: string | null;
  generation?: Partial<GenerationConfig>;
  context?: AgentContextSettings;
};

export type UpdateAgentInput = CreateAgentInput;

export type DuplicateAgentInput = {
  name?: string;
};

export type DeleteAgentResult = {
  reassignedChatCount: number;
};

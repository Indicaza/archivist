export type AIMessageRole = "user" | "assistant" | "system";

export type AIMessage = {
  role: AIMessageRole;
  content: string;
};

export type AIGenerationConfig = {
  provider: string;
  model: string;
  temperature: number | null;
  maxOutputTokens: number | null;
  topP: number | null;
  frequencyPenalty: number | null;
  presencePenalty: number | null;
};

export type GenerateTextInput = {
  messages: AIMessage[];
  instructions?: string;
  generation: AIGenerationConfig;
};

export type GenerateTextResult = {
  text: string;
  provider: string;
  model: string;
  toolCallCount?: number;
  toolRoundCount?: number;
};

export type StreamTextOptions = {
  signal?: AbortSignal;
  onDelta?: (delta: string) => void | Promise<void>;
};

export type AIProviderToolDefinition = {
  name: string;
  description: string;
  parameters: Record<string, unknown>;
};

export type AIProviderToolCall = {
  callId: string;
  name: string;
  arguments: unknown;
  round: number;
};

export type AIProviderToolResult = {
  output: unknown;
};

export type StreamTextWithToolsOptions = StreamTextOptions & {
  tools: AIProviderToolDefinition[];
  maxToolRounds?: number;
  onToolCall: (
    call: AIProviderToolCall,
  ) => Promise<AIProviderToolResult>;
};

export type DiscoveredAIModel = {
  provider: string;
  modelId: string;
  createdAt: string | null;
  ownedBy: string | null;
};

export type ProviderHealthStatus =
  | "connected"
  | "unavailable"
  | "misconfigured";

export type ProviderHealth = {
  provider: string;
  status: ProviderHealthStatus;
  checkedAt: string;
  message: string | null;
};

export interface AIProvider {
  readonly providerId: string;
  readonly displayName: string;

  generateText(input: GenerateTextInput): Promise<GenerateTextResult>;

  streamText(
    input: GenerateTextInput,
    options?: StreamTextOptions,
  ): Promise<GenerateTextResult>;

  streamTextWithTools?(
    input: GenerateTextInput,
    options: StreamTextWithToolsOptions,
  ): Promise<GenerateTextResult>;

  discoverModels(): Promise<DiscoveredAIModel[]>;

  checkHealth(): Promise<ProviderHealth>;
}

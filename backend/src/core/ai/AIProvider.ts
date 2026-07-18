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

  discoverModels(): Promise<DiscoveredAIModel[]>;

  checkHealth(): Promise<ProviderHealth>;
}

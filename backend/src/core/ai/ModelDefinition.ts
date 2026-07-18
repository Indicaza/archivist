export type GenerationControl =
  | "temperature"
  | "maxOutputTokens"
  | "topP"
  | "frequencyPenalty"
  | "presencePenalty";

export type ModelAvailabilityStatus =
  | "verified"
  | "discovered"
  | "unavailable"
  | "deprecated";

export type ModelGenerationDefaults = {
  temperature: number | null;
  maxOutputTokens: number | null;
  topP: number | null;
  frequencyPenalty: number | null;
  presencePenalty: number | null;
};

export type ModelCapabilities = {
  text: boolean;
  vision: boolean;
  tools: boolean;
  streaming: boolean;
  reasoning: boolean;
  embeddings: boolean;
};

export type ModelDefinition = {
  provider: string;
  modelId: string;
  displayName: string;
  description: string;

  status: ModelAvailabilityStatus;

  discoveredAt: string | null;
  providerCreatedAt: string | null;
  ownedBy: string | null;

  contextWindowTokens: number | null;
  maximumOutputTokens: number | null;

  capabilities: ModelCapabilities;
  supportedControls: GenerationControl[];
  defaults: ModelGenerationDefaults;
};

export type PublicModelDefinition = ModelDefinition;

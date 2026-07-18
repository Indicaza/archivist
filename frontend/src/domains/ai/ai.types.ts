export type GenerationControl =
  | "temperature"
  | "maxOutputTokens"
  | "topP"
  | "frequencyPenalty"
  | "presencePenalty";

export type ModelGenerationDefaults = {
  temperature: number | null;
  maxOutputTokens: number | null;
  topP: number | null;
  frequencyPenalty: number | null;
  presencePenalty: number | null;
};

export type ModelDefinition = {
  provider: "openai";
  modelId: string;
  displayName: string;
  description: string;
  contextWindowTokens: number | null;
  maximumOutputTokens: number | null;
  supportedControls: GenerationControl[];
  defaults: ModelGenerationDefaults;
};

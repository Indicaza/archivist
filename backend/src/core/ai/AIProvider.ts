export type AIMessageRole = "user" | "assistant" | "system";

export type AIMessage = {
  role: AIMessageRole;
  content: string;
};

export type GenerateTextInput = {
  messages: AIMessage[];
  instructions?: string;
};

export type GenerateTextResult = {
  text: string;
  provider: string;
  model: string;
};

export interface AIProvider {
  generateText(input: GenerateTextInput): Promise<GenerateTextResult>;
}

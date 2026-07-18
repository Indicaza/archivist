import OpenAI from "openai";
import { env } from "../../../config/env.js";
import type {
  AIMessage,
  AIProvider,
  DiscoveredAIModel,
  GenerateTextInput,
  GenerateTextResult,
  ProviderHealth,
} from "../AIProvider.js";

const openAIClient = new OpenAI({
  apiKey: env.OPENAI_API_KEY,
});

function buildInput(messages: AIMessage[]) {
  return messages.map((message) => ({
    role: message.role,
    content: message.content,
  }));
}

function unixSecondsToIso(value: number | null | undefined): string | null {
  if (typeof value !== "number" || !Number.isFinite(value)) {
    return null;
  }

  return new Date(value * 1_000).toISOString();
}

export class OpenAIProvider implements AIProvider {
  readonly providerId = "openai";
  readonly displayName = "OpenAI";

  async generateText(input: GenerateTextInput): Promise<GenerateTextResult> {
    const response = await openAIClient.responses.create({
      model: input.generation.model,

      instructions: input.instructions,

      input: buildInput(input.messages),

      max_output_tokens: input.generation.maxOutputTokens ?? undefined,

      temperature: input.generation.temperature ?? undefined,

      top_p: input.generation.topP ?? undefined,
    });

    const text = response.output_text.trim();

    if (!text) {
      throw new Error("OpenAI returned an empty response.");
    }

    return {
      text,
      provider: this.providerId,
      model: input.generation.model,
    };
  }

  async discoverModels(): Promise<DiscoveredAIModel[]> {
    const page = await openAIClient.models.list();

    return page.data.map((model) => ({
      provider: this.providerId,
      modelId: model.id,
      createdAt: unixSecondsToIso(model.created),
      ownedBy: model.owned_by ?? null,
    }));
  }

  async checkHealth(): Promise<ProviderHealth> {
    const checkedAt = new Date().toISOString();

    try {
      await openAIClient.models.list();

      return {
        provider: this.providerId,
        status: "connected",
        checkedAt,
        message: null,
      };
    } catch (error) {
      return {
        provider: this.providerId,
        status: "unavailable",
        checkedAt,
        message:
          error instanceof Error
            ? error.message
            : "OpenAI could not be reached.",
      };
    }
  }
}

export const openAIProvider = new OpenAIProvider();

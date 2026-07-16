import OpenAI from "openai";
import { env } from "../../../config/env.js";
import type {
  AIMessage,
  AIProvider,
  GenerateTextInput,
  GenerateTextResult,
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

export class OpenAIProvider implements AIProvider {
  async generateText(input: GenerateTextInput): Promise<GenerateTextResult> {
    const response = await openAIClient.responses.create({
      model: env.OPENAI_MODEL,

      instructions:
        input.instructions ??
        [
          "You are Archivist, a thoughtful local-first AI assistant.",
          "Give clear, useful answers.",
          "Do not claim to have inspected files or context that was not supplied.",
          "Prefer concise answers unless the user asks for depth.",
        ].join(" "),

      input: buildInput(input.messages),
    });

    const text = response.output_text.trim();

    if (!text) {
      throw new Error("OpenAI returned an empty response.");
    }

    return {
      text,
      provider: "openai",
      model: env.OPENAI_MODEL,
    };
  }
}

export const openAIProvider = new OpenAIProvider();

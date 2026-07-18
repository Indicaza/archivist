import { openAIProvider } from "../core/ai/providers/OpenAIProvider.js";

const result = await openAIProvider.generateText({
  messages: [
    {
      role: "user",
      content:
        "Reply with a short confirmation that Archivist OpenAI is working.",
    },
  ],
  generation: {
    provider: "openai",
    model: "gpt-5-mini",
    temperature: null,
    maxOutputTokens: 200,
    topP: null,
    frequencyPenalty: null,
    presencePenalty: null,
  },
});

console.log(result);

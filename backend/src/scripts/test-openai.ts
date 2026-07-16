import { openAIProvider } from "../core/ai/providers/OpenAIProvider.js";

async function main() {
  const result = await openAIProvider.generateText({
    messages: [
      {
        role: "user",
        content: "Reply with exactly: Archivist online.",
      },
    ],
  });

  console.log({
    provider: result.provider,
    model: result.model,
    text: result.text,
  });
}

main().catch((error: unknown) => {
  console.error(error instanceof Error ? error.message : error);

  process.exitCode = 1;
});

import { aiToolRegistry, type AIToolRegistry } from "./AIToolRegistry.js";
import { libraryReadTools } from "./builtins/LibraryReadTools.js";

export function registerBuiltInAITools(
  registry: AIToolRegistry = aiToolRegistry,
): void {
  const registeredToolIds: string[] = [];

  for (const tool of libraryReadTools) {
    if (!registry.get(tool.id)) {
      registry.register(tool);
      registeredToolIds.push(tool.id);
    }
  }

  if (
    registeredToolIds.length > 0 &&
    process.env.ARCHIVIST_AI_TOOL_LOGS !== "0"
  ) {
    console.info("[AIToolRegistry]", {
      registeredCount: registeredToolIds.length,
      toolIds: registeredToolIds,
    });
  }
}

import type { AIProviderToolDefinition } from "../ai/AIProvider.js";
import { aiToolRegistry } from "./AIToolRegistry.js";

const discoveryToolIds = new Set([
  "list_directory",
  "search_filenames",
  "search_library",
]);

const verificationToolIds = new Set(["read_file_ranges"]);

export type ModelAIToolAvailability = {
  includeDiscoveryTools?: boolean;
  includeFullFileRead?: boolean;
};

export function listModelAvailableAITools(
  availability: ModelAIToolAvailability = {},
): AIProviderToolDefinition[] {
  const includeDiscoveryTools =
    availability.includeDiscoveryTools ?? true;
  const includeFullFileRead =
    availability.includeFullFileRead ?? true;
  const verificationOnly = !includeDiscoveryTools && !includeFullFileRead;

  return aiToolRegistry
    .list()
    .filter(
      (tool) =>
        tool.permission === "read-only" &&
        tool.inputJsonSchema !== undefined &&
        (includeDiscoveryTools || !discoveryToolIds.has(tool.id)) &&
        (includeFullFileRead || tool.id !== "read_file") &&
        (!verificationOnly || verificationToolIds.has(tool.id)),
    )
    .map((tool) => ({
      name: tool.id,
      description: tool.description,
      parameters: tool.inputJsonSchema ?? {
        type: "object",
        properties: {},
        additionalProperties: false,
      },
    }));
}

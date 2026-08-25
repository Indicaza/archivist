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
  includeVerificationTools?: boolean;
  includeMutationProposalTools?: boolean;
};

export function listModelAvailableAITools(
  availability: ModelAIToolAvailability = {},
): AIProviderToolDefinition[] {
  const includeDiscoveryTools =
    availability.includeDiscoveryTools ?? true;
  const includeFullFileRead =
    availability.includeFullFileRead ?? true;
  const includeVerificationTools =
    availability.includeVerificationTools ?? true;
  const includeMutationProposalTools =
    availability.includeMutationProposalTools ?? false;
  const verificationOnly = !includeDiscoveryTools && !includeFullFileRead;

  return aiToolRegistry
    .list()
    .filter((tool) => {
      const mutationProposal =
        tool.permission === "safe-local-mutation"
        && tool.executionMode === "proposal";

      if (mutationProposal) {
        return (
          includeMutationProposalTools
          && tool.inputJsonSchema !== undefined
        );
      }

      if (
        tool.permission !== "read-only"
        || tool.inputJsonSchema === undefined
      ) {
        return false;
      }

      if (verificationOnly) {
        return (
          includeVerificationTools
          && verificationToolIds.has(tool.id)
        );
      }

      return (
        (includeDiscoveryTools || !discoveryToolIds.has(tool.id))
        && (includeFullFileRead || tool.id !== "read_file")
      );
    })
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

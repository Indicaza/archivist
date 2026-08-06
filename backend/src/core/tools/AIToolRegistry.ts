import { AIToolError } from "./AIToolTypes.js";
import type { AnyAIToolDefinition } from "./AIToolTypes.js";

export class AIToolRegistry {
  private readonly tools = new Map<string, AnyAIToolDefinition>();

  register(tool: AnyAIToolDefinition): void {
    if (!tool.id.trim()) {
      throw new Error("AI tool IDs may not be empty.");
    }

    if (!Number.isFinite(tool.timeoutMs) || tool.timeoutMs <= 0) {
      throw new Error(`AI tool "${tool.id}" must define a positive timeout.`);
    }

    if (this.tools.has(tool.id)) {
      throw new Error(`AI tool "${tool.id}" is already registered.`);
    }

    this.tools.set(tool.id, tool);
  }

  get(toolId: string): AnyAIToolDefinition | null {
    return this.tools.get(toolId) ?? null;
  }

  require(toolId: string): AnyAIToolDefinition {
    const tool = this.get(toolId);

    if (!tool) {
      throw new AIToolError(
        "tool_not_found",
        `AI tool "${toolId}" is not registered.`,
      );
    }

    return tool;
  }

  list(): AnyAIToolDefinition[] {
    return Array.from(this.tools.values()).sort((first, second) =>
      first.id.localeCompare(second.id),
    );
  }
}

export const aiToolRegistry = new AIToolRegistry();

import { AppError } from "../../errors/app-error.js";
import type {
  ModelDefinition,
  PublicModelDefinition,
} from "./ModelDefinition.js";

function createRegistryKey(provider: string, modelId: string): string {
  return `${provider}:${modelId}`;
}

function cloneDefinition(definition: ModelDefinition): PublicModelDefinition {
  return {
    ...definition,

    capabilities: {
      ...definition.capabilities,
    },

    supportedControls: [...definition.supportedControls],

    defaults: {
      ...definition.defaults,
    },
  };
}

export class ModelRegistry {
  private readonly definitions = new Map<string, ModelDefinition>();

  replaceProviderModels(
    provider: string,
    definitions: ModelDefinition[],
  ): void {
    for (const [key, definition] of this.definitions) {
      if (definition.provider === provider) {
        this.definitions.delete(key);
      }
    }

    for (const definition of definitions) {
      this.set(definition);
    }
  }

  set(definition: ModelDefinition): void {
    const key = createRegistryKey(definition.provider, definition.modelId);

    this.definitions.set(key, definition);
  }

  list(provider?: string): PublicModelDefinition[] {
    return Array.from(this.definitions.values())
      .filter((definition) => !provider || definition.provider === provider)
      .sort((left, right) => {
        const statusDifference =
          getStatusPriority(left.status) - getStatusPriority(right.status);

        if (statusDifference !== 0) {
          return statusDifference;
        }

        return left.displayName.localeCompare(right.displayName, undefined, {
          numeric: true,
          sensitivity: "base",
        });
      })
      .map(cloneDefinition);
  }

  getDefinition(provider: string, modelId: string): ModelDefinition {
    const key = createRegistryKey(provider, modelId);

    const definition = this.definitions.get(key);

    if (!definition) {
      throw new AppError(
        400,
        `Model "${modelId}" is not registered for provider "${provider}".`,
      );
    }

    return definition;
  }

  has(provider: string, modelId: string): boolean {
    return this.definitions.has(createRegistryKey(provider, modelId));
  }
}

function getStatusPriority(status: ModelDefinition["status"]): number {
  switch (status) {
    case "verified":
      return 0;

    case "discovered":
      return 1;

    case "deprecated":
      return 2;

    case "unavailable":
      return 3;
  }
}

export const modelRegistry = new ModelRegistry();

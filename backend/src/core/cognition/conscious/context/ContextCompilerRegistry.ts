import type { ContextCompilerDefinition } from "./ContextCompilerDefinition.js";
import type {
  ContextCompilerConfig,
  ContextCompilerReference,
} from "./ContextCompilerTypes.js";
import { contiguousHistoryCompilerV1Definition } from "./compilers/ContiguousHistoryCompilerV1.js";
import { keywordRecencyCompilerV1Definition } from "./compilers/KeywordRecencyCompilerV1.js";
import { recentHistoryCompilerV1Definition } from "./compilers/RecentHistoryCompilerV1.js";
import { turnPairsCompilerV1Definition } from "./compilers/TurnPairsCompilerV1.js";

type RegisteredContextCompilerDefinition = ContextCompilerDefinition<any>;

export type PublicContextCompilerDefinition = {
  descriptor: {
    id: string;
    version: number;
    name: string;
  };
  description: string;
  defaultConfig: ContextCompilerConfig;
  configFields: RegisteredContextCompilerDefinition["configFields"];
};

function createRegistryKey(reference: ContextCompilerReference): string {
  return `${reference.id}:v${reference.version}`;
}

function toPublicDefinition(
  definition: RegisteredContextCompilerDefinition,
): PublicContextCompilerDefinition {
  return {
    descriptor: definition.descriptor,
    description: definition.description,
    defaultConfig: definition.defaultConfig,
    configFields: definition.configFields,
  };
}

export class ContextCompilerRegistry {
  private readonly definitions = new Map<
    string,
    RegisteredContextCompilerDefinition
  >();

  register(definition: RegisteredContextCompilerDefinition): void {
    const key = createRegistryKey(definition.descriptor);

    if (this.definitions.has(key)) {
      throw new Error(`Context Compiler "${key}" is already registered.`);
    }

    this.definitions.set(key, definition);
  }

  getDefinition(
    reference: ContextCompilerReference,
  ): RegisteredContextCompilerDefinition {
    const key = createRegistryKey(reference);
    const definition = this.definitions.get(key);

    if (!definition) {
      throw new Error(`Context Compiler "${key}" is not registered.`);
    }

    return definition;
  }

  getPublicDefinition(
    reference: ContextCompilerReference,
  ): PublicContextCompilerDefinition {
    return toPublicDefinition(this.getDefinition(reference));
  }

  list(): PublicContextCompilerDefinition[] {
    return Array.from(this.definitions.values())
      .map(toPublicDefinition)
      .sort((left, right) => {
        const nameDifference = left.descriptor.name.localeCompare(
          right.descriptor.name,
        );

        if (nameDifference !== 0) {
          return nameDifference;
        }

        return right.descriptor.version - left.descriptor.version;
      });
  }

  has(reference: ContextCompilerReference): boolean {
    return this.definitions.has(createRegistryKey(reference));
  }

  parseConfig(
    reference: ContextCompilerReference,
    config: ContextCompilerConfig,
  ): ContextCompilerConfig {
    const definition = this.getDefinition(reference);
    const result = definition.configSchema.safeParse(config);

    if (!result.success) {
      throw new Error(
        [
          `Invalid configuration for Context Compiler "${createRegistryKey(
            reference,
          )}".`,
          result.error.issues
            .map((issue) => {
              const path = issue.path.join(".") || "config";

              return `${path}: ${issue.message}`;
            })
            .join(" "),
        ].join(" "),
      );
    }

    return result.data;
  }

  getDefaultConfig(reference: ContextCompilerReference): ContextCompilerConfig {
    const definition = this.getDefinition(reference);

    return {
      ...definition.defaultConfig,
    };
  }
}

export const contextCompilerRegistry = new ContextCompilerRegistry();

contextCompilerRegistry.register(recentHistoryCompilerV1Definition);

contextCompilerRegistry.register(contiguousHistoryCompilerV1Definition);

contextCompilerRegistry.register(turnPairsCompilerV1Definition);

contextCompilerRegistry.register(keywordRecencyCompilerV1Definition);

export const defaultContextCompilerReference = {
  id: "recent-history",
  version: 1,
} as const satisfies ContextCompilerReference;

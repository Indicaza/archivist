import { mkdir, readFile, rename, writeFile } from "node:fs/promises";
import path from "node:path";
import type {
  AIProvider,
  DiscoveredAIModel,
  ProviderHealth,
} from "./AIProvider.js";
import { aiProviderRegistry } from "./AIProviderRegistry.js";
import type {
  GenerationControl,
  ModelCapabilities,
  ModelDefinition,
  ModelGenerationDefaults,
} from "./ModelDefinition.js";
import { modelRegistry } from "./ModelRegistry.js";
import { openAIProvider } from "./providers/OpenAIProvider.js";

const CACHE_VERSION = 1;
const DEFAULT_REFRESH_INTERVAL_MS = 24 * 60 * 60 * 1_000;

const cacheFilePath = path.resolve(
  process.cwd(),
  "data",
  "ai-model-cache.json",
);

type ProviderCacheEntry = {
  provider: string;
  refreshedAt: string;
  health: ProviderHealth;
  models: ModelDefinition[];
};

type ModelCacheFile = {
  version: number;
  providers: Record<string, ProviderCacheEntry>;
};

type RefreshModelsOptions = {
  force?: boolean;
};

const verifiedOpenAIModels = new Map<string, Partial<ModelDefinition>>([
  [
    "gpt-5-mini",
    {
      displayName: "GPT-5 Mini",

      description:
        "Fast general-purpose OpenAI text model for everyday Archivist conversations.",

      status: "verified",

      capabilities: {
        text: true,
        vision: false,
        tools: true,
        streaming: true,
        reasoning: true,
        embeddings: false,
      },

      supportedControls: ["maxOutputTokens"],

      defaults: {
        temperature: null,
        maxOutputTokens: 4_000,
        topP: null,
        frequencyPenalty: null,
        presencePenalty: null,
      },

      contextWindowTokens: null,
      maximumOutputTokens: null,
    },
  ],
]);

function emptyCache(): ModelCacheFile {
  return {
    version: CACHE_VERSION,
    providers: {},
  };
}

function defaultCapabilities(reasoning: boolean): ModelCapabilities {
  return {
    text: true,
    vision: false,
    tools: false,
    streaming: false,
    reasoning,
    embeddings: false,
  };
}

function defaultGenerationConfig(): ModelGenerationDefaults {
  return {
    temperature: null,
    maxOutputTokens: null,
    topP: null,
    frequencyPenalty: null,
    presencePenalty: null,
  };
}

function formatModelName(modelId: string): string {
  return modelId
    .split("-")
    .map((part) => {
      if (/^gpt$/i.test(part)) {
        return "GPT";
      }

      if (/^o\d/i.test(part)) {
        return part.toUpperCase();
      }

      if (/^\d/.test(part)) {
        return part;
      }

      return part.charAt(0).toUpperCase() + part.slice(1);
    })
    .join(" ");
}

function isReasoningModel(modelId: string): boolean {
  return /^(o1|o3|o4)(-|$)/i.test(modelId);
}

function isLikelyResponsesTextModel(modelId: string): boolean {
  const normalized = modelId.toLowerCase();

  const supportedFamily =
    normalized.startsWith("gpt-") || /^(o1|o3|o4)(-|$)/.test(normalized);

  if (!supportedFamily) {
    return false;
  }

  const excludedFragments = [
    "audio",
    "realtime",
    "transcribe",
    "tts",
    "embedding",
    "moderation",
    "image",
    "search",
    "instruct",
  ];

  return !excludedFragments.some((fragment) => normalized.includes(fragment));
}

function createDiscoveredDefinition(model: DiscoveredAIModel): ModelDefinition {
  const verified =
    model.provider === "openai"
      ? verifiedOpenAIModels.get(model.modelId)
      : undefined;

  const reasoning = isReasoningModel(model.modelId);

  const conservativeControls: GenerationControl[] = ["maxOutputTokens"];

  return {
    provider: model.provider,
    modelId: model.modelId,

    displayName: verified?.displayName ?? formatModelName(model.modelId),

    description:
      verified?.description ??
      "Discovered from the provider. Archivist uses conservative generation settings until this model is verified.",

    status: verified ? "verified" : "discovered",

    discoveredAt: new Date().toISOString(),
    providerCreatedAt: model.createdAt,
    ownedBy: model.ownedBy,

    contextWindowTokens: verified?.contextWindowTokens ?? null,

    maximumOutputTokens: verified?.maximumOutputTokens ?? null,

    capabilities: verified?.capabilities ?? defaultCapabilities(reasoning),

    supportedControls: verified?.supportedControls ?? conservativeControls,

    defaults: verified?.defaults ?? defaultGenerationConfig(),
  };
}

async function readCache(): Promise<ModelCacheFile> {
  try {
    const contents = await readFile(cacheFilePath, "utf8");

    const parsed = JSON.parse(contents) as Partial<ModelCacheFile>;

    if (
      parsed.version !== CACHE_VERSION ||
      typeof parsed.providers !== "object" ||
      parsed.providers === null
    ) {
      return emptyCache();
    }

    return {
      version: CACHE_VERSION,
      providers: parsed.providers,
    };
  } catch {
    return emptyCache();
  }
}

async function writeCache(cache: ModelCacheFile): Promise<void> {
  await mkdir(path.dirname(cacheFilePath), {
    recursive: true,
  });

  const temporaryPath = `${cacheFilePath}.tmp`;

  await writeFile(temporaryPath, JSON.stringify(cache, null, 2), "utf8");

  await rename(temporaryPath, cacheFilePath);
}

function isFresh(refreshedAt: string | undefined): boolean {
  if (!refreshedAt) {
    return false;
  }

  const refreshedTime = new Date(refreshedAt).getTime();

  if (!Number.isFinite(refreshedTime)) {
    return false;
  }

  return Date.now() - refreshedTime < DEFAULT_REFRESH_INTERVAL_MS;
}

function applyCachedModels(cache: ModelCacheFile): void {
  for (const entry of Object.values(cache.providers)) {
    modelRegistry.replaceProviderModels(entry.provider, entry.models);
  }
}

async function refreshProvider(
  provider: AIProvider,
  cache: ModelCacheFile,
): Promise<ProviderCacheEntry> {
  const discovered = await provider.discoverModels();

  const models = discovered
    .filter((model) => {
      if (provider.providerId === "openai") {
        return isLikelyResponsesTextModel(model.modelId);
      }

      return true;
    })
    .map(createDiscoveredDefinition);

  const health: ProviderHealth = {
    provider: provider.providerId,
    status: "connected",
    checkedAt: new Date().toISOString(),
    message: null,
  };

  const entry: ProviderCacheEntry = {
    provider: provider.providerId,
    refreshedAt: new Date().toISOString(),
    health,
    models,
  };

  cache.providers[provider.providerId] = entry;

  modelRegistry.replaceProviderModels(provider.providerId, models);

  return entry;
}

export class ModelCatalog {
  private initialized = false;
  private initializationPromise: Promise<void> | null = null;

  private refreshPromise: Promise<ModelDefinition[]> | null = null;

  async initialize(): Promise<void> {
    if (this.initialized) {
      return;
    }

    if (this.initializationPromise) {
      return this.initializationPromise;
    }

    this.initializationPromise = this.initializeInternal();

    try {
      await this.initializationPromise;
    } finally {
      this.initializationPromise = null;
    }
  }

  private async initializeInternal(): Promise<void> {
    if (!aiProviderRegistry.get("openai")) {
      aiProviderRegistry.register(openAIProvider);
    }

    const cache = await readCache();

    applyCachedModels(cache);

    this.initialized = true;
  }

  async listModels(): Promise<ModelDefinition[]> {
    await this.initialize();

    const cache = await readCache();

    const openAIEntry = cache.providers.openai;

    if (!openAIEntry || !isFresh(openAIEntry.refreshedAt)) {
      try {
        await this.refreshModels();
      } catch (error) {
        console.warn(
          "[AIModelCatalog] Model refresh failed. Using cached models.",
          error,
        );
      }
    }

    return modelRegistry.list();
  }

  async refreshModels(
    options: RefreshModelsOptions = {},
  ): Promise<ModelDefinition[]> {
    await this.initialize();

    if (this.refreshPromise && !options.force) {
      return this.refreshPromise;
    }

    this.refreshPromise = this.refreshModelsInternal();

    try {
      return await this.refreshPromise;
    } finally {
      this.refreshPromise = null;
    }
  }

  private async refreshModelsInternal(): Promise<ModelDefinition[]> {
    const cache = await readCache();

    for (const provider of aiProviderRegistry.list()) {
      try {
        await refreshProvider(provider, cache);
      } catch (error) {
        const previous = cache.providers[provider.providerId];

        cache.providers[provider.providerId] = {
          provider: provider.providerId,

          refreshedAt: previous?.refreshedAt ?? new Date(0).toISOString(),

          health: {
            provider: provider.providerId,
            status: "unavailable",
            checkedAt: new Date().toISOString(),

            message:
              error instanceof Error
                ? error.message
                : "Provider model discovery failed.",
          },

          models: previous?.models ?? [],
        };

        if (previous?.models) {
          modelRegistry.replaceProviderModels(
            provider.providerId,
            previous.models,
          );
        }
      }
    }

    await writeCache(cache);

    return modelRegistry.list();
  }

  async getProviderHealth(): Promise<ProviderHealth[]> {
    await this.initialize();

    const cache = await readCache();

    return Object.values(cache.providers).map((entry) => entry.health);
  }
}

export const modelCatalog = new ModelCatalog();

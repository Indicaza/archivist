import { AppError } from "../../errors/app-error.js";
import type { AIProvider } from "./AIProvider.js";

export class AIProviderRegistry {
  private readonly providers = new Map<string, AIProvider>();

  register(provider: AIProvider): void {
    if (this.providers.has(provider.providerId)) {
      throw new Error(
        `AI provider "${provider.providerId}" is already registered.`,
      );
    }

    this.providers.set(provider.providerId, provider);
  }

  get(providerId: string): AIProvider | null {
    return this.providers.get(providerId) ?? null;
  }

  require(providerId: string): AIProvider {
    const provider = this.get(providerId);

    if (!provider) {
      throw new AppError(400, `AI provider "${providerId}" is not registered.`);
    }

    return provider;
  }

  list(): AIProvider[] {
    return Array.from(this.providers.values());
  }
}

export const aiProviderRegistry = new AIProviderRegistry();

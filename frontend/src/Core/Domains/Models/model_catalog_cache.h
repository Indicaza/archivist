#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <optional>

// Reads the model definitions the AI runtime caches in ai-model-cache.json
// (next to archivist.db). Model discovery itself still lives in the AI
// runtime; the core only needs the definitions to validate Agent settings.
namespace ModelCatalogCache
{
[[nodiscard]] QString cachePathForDatabase(const QString &databasePath);

// The definition for provider/model from the cache, falling back to the
// models Archivist ships as verified. nullopt when the model is unknown.
[[nodiscard]] std::optional<QVariantMap> definition(
    const QString &cachePath,
    const QString &provider,
    const QString &modelId
);

// refreshProvider in ModelCatalog.ts: `discovered` is [{modelId, createdAt,
// ownedBy}] from the provider's model list. Writes the provider's entry.
void applyDiscovery(const QString &cachePath, const QString &provider, const QVariantList &discovered);
// Keeps the previous models and marks the provider unavailable.
void recordDiscoveryFailure(const QString &cachePath, const QString &provider, const QString &message);

// Every cached model for the provider (plus the shipped verified ones), for
// the model picker. Each entry is a definition map.
[[nodiscard]] QVariantList models(const QString &cachePath, const QString &provider);
// {provider, status, checkedAt, message} from the last discovery, or empty.
[[nodiscard]] QVariantMap health(const QString &cachePath, const QString &provider);
}

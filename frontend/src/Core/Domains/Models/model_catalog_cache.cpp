#include "model_catalog_cache.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

namespace
{
struct CacheSnapshot
{
    QString path;
    QDateTime modified;
    qint64 size = -1;
    QHash<QString, QVariantMap> definitions;
};

QString key(const QString &provider, const QString &modelId)
{
    return provider + QLatin1Char(':') + modelId;
}

// Mirrors verifiedOpenAIModels in backend/src/core/ai/ModelCatalog.ts.
QHash<QString, QVariantMap> verifiedDefinitions()
{
    const QVariant null = QVariant::fromValue(nullptr);

    return {
        {key(QStringLiteral("openai"), QStringLiteral("gpt-5-mini")),
         QVariantMap{
             {QStringLiteral("provider"), QStringLiteral("openai")},
             {QStringLiteral("modelId"), QStringLiteral("gpt-5-mini")},
             {QStringLiteral("displayName"), QStringLiteral("GPT-5 Mini")},
             {QStringLiteral("status"), QStringLiteral("verified")},
             {QStringLiteral("supportedControls"), QVariantList{QStringLiteral("maxOutputTokens")}},
             {QStringLiteral("maximumOutputTokens"), null},
             {QStringLiteral("contextWindowTokens"), null},
         }},
    };
}

QHash<QString, QVariantMap> readCache(const QString &path)
{
    QHash<QString, QVariantMap> definitions;
    QFile file(path);

    if (!file.open(QIODevice::ReadOnly)) {
        return definitions;
    }

    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();

    if (root.value(QStringLiteral("version")).toInt() != 1) {
        return definitions;
    }

    const QJsonObject providers = root.value(QStringLiteral("providers")).toObject();

    for (auto provider = providers.begin(); provider != providers.end(); ++provider) {
        const QJsonArray models = provider.value().toObject().value(QStringLiteral("models")).toArray();

        for (const QJsonValue &model : models) {
            const QVariantMap definition = model.toObject().toVariantMap();
            const QString providerId = definition.value(QStringLiteral("provider")).toString();
            const QString modelId = definition.value(QStringLiteral("modelId")).toString();

            if (!providerId.isEmpty() && !modelId.isEmpty()) {
                definitions.insert(key(providerId, modelId), definition);
            }
        }
    }

    return definitions;
}
}

namespace ModelCatalogCache
{
QString cachePathForDatabase(const QString &databasePath)
{
    return QFileInfo(databasePath).dir().filePath(QStringLiteral("ai-model-cache.json"));
}

std::optional<QVariantMap> definition(const QString &cachePath, const QString &provider, const QString &modelId)
{
    // Reads happen on the UI and writer threads; the cache file is re-read
    // only when it changes on disk.
    static QMutex mutex;
    static CacheSnapshot snapshot;

    QMutexLocker locker(&mutex);
    const QFileInfo info(cachePath);
    const QDateTime modified = info.exists() ? info.lastModified() : QDateTime();
    const qint64 size = info.exists() ? info.size() : -1;

    if (snapshot.path != cachePath || snapshot.modified != modified || snapshot.size != size) {
        snapshot = {cachePath, modified, size, readCache(cachePath)};
    }

    const QString lookup = key(provider, modelId);
    const auto cached = snapshot.definitions.constFind(lookup);

    if (cached != snapshot.definitions.constEnd()) {
        return *cached;
    }

    static const QHash<QString, QVariantMap> verified = verifiedDefinitions();
    const auto seed = verified.constFind(lookup);

    if (seed != verified.constEnd()) {
        return *seed;
    }

    return std::nullopt;
}
}

namespace
{
QString isoNow()
{
    return QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss.zzz'Z'"));
}

QJsonObject readRoot(const QString &path)
{
    QFile file(path);

    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    return root.value(QStringLiteral("version")).toInt() == 1 && root.value(QStringLiteral("providers")).isObject() ? root : QJsonObject();
}

void writeRoot(const QString &path, QJsonObject providers)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);

    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }

    file.write(QJsonDocument(QJsonObject{{QStringLiteral("version"), 1}, {QStringLiteral("providers"), providers}}).toJson(QJsonDocument::Indented));
    file.commit();
}

QString formatModelName(const QString &modelId)
{
    static const QRegularExpression gpt(QStringLiteral("^gpt$"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression oSeries(QStringLiteral("^o\\d"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression digit(QStringLiteral("^\\d"));
    QStringList parts;

    for (const QString &part : modelId.split(QLatin1Char('-'))) {
        if (gpt.match(part).hasMatch()) {
            parts.append(QStringLiteral("GPT"));
        } else if (oSeries.match(part).hasMatch()) {
            parts.append(part.toUpper());
        } else if (digit.match(part).hasMatch() || part.isEmpty()) {
            parts.append(part);
        } else {
            parts.append(part.left(1).toUpper() + part.mid(1));
        }
    }

    return parts.join(QLatin1Char(' '));
}

bool isLikelyResponsesTextModel(const QString &modelId)
{
    static const QRegularExpression family(QStringLiteral("^(o1|o3|o4)(-|$)"));
    const QString normalized = modelId.toLower();

    if (!normalized.startsWith(QStringLiteral("gpt-")) && !family.match(normalized).hasMatch()) {
        return false;
    }

    for (const char *fragment : {"audio", "realtime", "transcribe", "tts", "embedding", "moderation", "image", "search", "instruct"}) {
        if (normalized.contains(QLatin1String(fragment))) {
            return false;
        }
    }

    return true;
}

QJsonObject discoveredDefinition(const QString &provider, const QVariantMap &model)
{
    static const QRegularExpression reasoningFamily(QStringLiteral("^(o1|o3|o4)(-|$)"), QRegularExpression::CaseInsensitiveOption);
    const QString modelId = model.value(QStringLiteral("modelId")).toString();
    const bool verified = provider == QStringLiteral("openai") && modelId == QStringLiteral("gpt-5-mini");
    const bool reasoning = reasoningFamily.match(modelId).hasMatch();
    const auto nullable = [](const QVariant &value) {
        return value.isNull() || !value.isValid() ? QJsonValue(QJsonValue::Null) : QJsonValue::fromVariant(value);
    };
    const QJsonObject nullDefaults{
        {QStringLiteral("temperature"), QJsonValue::Null},
        {QStringLiteral("maxOutputTokens"), QJsonValue::Null},
        {QStringLiteral("topP"), QJsonValue::Null},
        {QStringLiteral("frequencyPenalty"), QJsonValue::Null},
        {QStringLiteral("presencePenalty"), QJsonValue::Null},
    };
    QJsonObject verifiedDefaults = nullDefaults;
    verifiedDefaults.insert(QStringLiteral("maxOutputTokens"), 4000);

    return {
        {QStringLiteral("provider"), provider},
        {QStringLiteral("modelId"), modelId},
        {QStringLiteral("displayName"), verified ? QStringLiteral("GPT-5 Mini") : formatModelName(modelId)},
        {QStringLiteral("description"),
         verified ? QStringLiteral("Fast general-purpose OpenAI text model for everyday Archivist conversations.")
                  : QStringLiteral("Discovered from the provider. Archivist uses conservative generation settings until this model is verified.")},
        {QStringLiteral("status"), verified ? QStringLiteral("verified") : QStringLiteral("discovered")},
        {QStringLiteral("discoveredAt"), isoNow()},
        {QStringLiteral("providerCreatedAt"), nullable(model.value(QStringLiteral("createdAt")))},
        {QStringLiteral("ownedBy"), nullable(model.value(QStringLiteral("ownedBy")))},
        {QStringLiteral("contextWindowTokens"), QJsonValue::Null},
        {QStringLiteral("maximumOutputTokens"), QJsonValue::Null},
        {QStringLiteral("capabilities"),
         QJsonObject{
             {QStringLiteral("text"), true},
             {QStringLiteral("vision"), false},
             {QStringLiteral("tools"), verified},
             {QStringLiteral("streaming"), verified},
             {QStringLiteral("reasoning"), verified || reasoning},
             {QStringLiteral("embeddings"), false},
         }},
        {QStringLiteral("supportedControls"), QJsonArray{QStringLiteral("maxOutputTokens")}},
        {QStringLiteral("defaults"), verified ? verifiedDefaults : nullDefaults},
    };
}
}

namespace ModelCatalogCache
{
void applyDiscovery(const QString &cachePath, const QString &provider, const QVariantList &discovered)
{
    QJsonArray models;

    for (const QVariant &value : discovered) {
        const QVariantMap model = value.toMap();

        if (provider != QStringLiteral("openai") || isLikelyResponsesTextModel(model.value(QStringLiteral("modelId")).toString())) {
            models.append(discoveredDefinition(provider, model));
        }
    }

    const QString now = isoNow();
    QJsonObject providers = readRoot(cachePath).value(QStringLiteral("providers")).toObject();
    providers.insert(provider, QJsonObject{
        {QStringLiteral("provider"), provider},
        {QStringLiteral("refreshedAt"), now},
        {QStringLiteral("health"),
         QJsonObject{
             {QStringLiteral("provider"), provider},
             {QStringLiteral("status"), QStringLiteral("connected")},
             {QStringLiteral("checkedAt"), now},
             {QStringLiteral("message"), QJsonValue::Null},
         }},
        {QStringLiteral("models"), models},
    });
    writeRoot(cachePath, providers);
}

void recordDiscoveryFailure(const QString &cachePath, const QString &provider, const QString &message)
{
    QJsonObject providers = readRoot(cachePath).value(QStringLiteral("providers")).toObject();
    const QJsonObject previous = providers.value(provider).toObject();
    providers.insert(provider, QJsonObject{
        {QStringLiteral("provider"), provider},
        {QStringLiteral("refreshedAt"),
         previous.contains(QStringLiteral("refreshedAt")) ? previous.value(QStringLiteral("refreshedAt"))
                                                          : QJsonValue(QStringLiteral("1970-01-01T00:00:00.000Z"))},
        {QStringLiteral("health"),
         QJsonObject{
             {QStringLiteral("provider"), provider},
             {QStringLiteral("status"), QStringLiteral("unavailable")},
             {QStringLiteral("checkedAt"), isoNow()},
             {QStringLiteral("message"), message},
         }},
        {QStringLiteral("models"), previous.value(QStringLiteral("models")).isArray() ? previous.value(QStringLiteral("models")) : QJsonArray()},
    });
    writeRoot(cachePath, providers);
}

QVariantList models(const QString &cachePath, const QString &provider)
{
    QVariantList result;
    QSet<QString> seen;
    const QJsonArray cached = readRoot(cachePath).value(QStringLiteral("providers")).toObject()
                                  .value(provider).toObject().value(QStringLiteral("models")).toArray();

    for (const QJsonValue &value : cached) {
        const QVariantMap definition = value.toObject().toVariantMap();
        const QString modelId = definition.value(QStringLiteral("modelId")).toString();

        if (!modelId.isEmpty() && !seen.contains(modelId)) {
            seen.insert(modelId);
            result.append(definition);
        }
    }

    for (const QVariantMap &definition : verifiedDefinitions()) {
        if (definition.value(QStringLiteral("provider")).toString() == provider
            && !seen.contains(definition.value(QStringLiteral("modelId")).toString())) {
            result.prepend(definition);
        }
    }

    return result;
}

QVariantMap health(const QString &cachePath, const QString &provider)
{
    return readRoot(cachePath).value(QStringLiteral("providers")).toObject()
        .value(provider).toObject().value(QStringLiteral("health")).toObject().toVariantMap();
}
}

#include "ai_settings.h"

#include "Core/Domains/AI/ai_provider.h"
#include "Core/Domains/Models/model_catalog_cache.h"
#include "Core/Support/secret_store.h"

#include <QNetworkAccessManager>
#include <QSettings>
#include <QUrl>

AISettings::AISettings(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
}

void AISettings::setDatabasePath(const QString &databasePath)
{
    m_databasePath = databasePath;
    reload();
}

void AISettings::reload()
{
    m_keySource = AIProvider::openAIKeySource(m_databasePath);
    ++m_modelRevision;
    emit changed();
    emit modelsChanged();
}

bool AISettings::openAIKeyConfigured() const
{
    return !m_keySource.isEmpty();
}

QString AISettings::openAIKeySource() const
{
    return m_keySource;
}

QString AISettings::keyLocation() const
{
    return SecretStore::locationName();
}

QString AISettings::localBaseUrl() const
{
    return AIProvider::localBaseUrl();
}

QString AISettings::defaultLocalBaseUrl() const
{
    return AIProvider::defaultLocalBaseUrl;
}

QVariantList AISettings::providers() const
{
    return {
        QVariantMap{{QStringLiteral("id"), AIProvider::openAIProvider}, {QStringLiteral("label"), QStringLiteral("OpenAI")}},
        QVariantMap{{QStringLiteral("id"), AIProvider::localProvider}, {QStringLiteral("label"), QStringLiteral("Local model server")}},
    };
}

bool AISettings::busy() const
{
    return m_busy;
}

QString AISettings::statusMessage() const
{
    return m_statusMessage;
}

QString AISettings::errorMessage() const
{
    return m_errorMessage;
}

int AISettings::modelRevision() const
{
    return m_modelRevision;
}

QString AISettings::cachePath() const
{
    return ModelCatalogCache::cachePathForDatabase(m_databasePath);
}

QVariantList AISettings::models(const QString &provider) const
{
    QVariantList result;

    if (m_databasePath.isEmpty()) {
        return result;
    }

    for (const QVariant &value : ModelCatalogCache::models(cachePath(), provider)) {
        const QVariantMap definition = value.toMap();
        result.append(QVariantMap{
            {QStringLiteral("modelId"), definition.value(QStringLiteral("modelId"))},
            {QStringLiteral("displayName"), definition.value(QStringLiteral("displayName"), definition.value(QStringLiteral("modelId")))},
            {QStringLiteral("status"), definition.value(QStringLiteral("status"))},
        });
    }

    return result;
}

QVariantMap AISettings::health(const QString &provider) const
{
    return m_databasePath.isEmpty() ? QVariantMap() : ModelCatalogCache::health(cachePath(), provider);
}

bool AISettings::saveOpenAIKey(const QString &key)
{
    const QString trimmed = key.trimmed();

    if (trimmed.isEmpty()) {
        setMessages({}, QStringLiteral("Paste an OpenAI API key first."));
        return false;
    }

    QString error;

    if (!SecretStore::write(AIProvider::openAIKeyAccount, trimmed, &error)) {
        setMessages({}, QStringLiteral("Could not save the key to the %1: %2").arg(keyLocation(), error));
        return false;
    }

    // The Keychain copy replaces the plain-text one older builds kept.
    QSettings().remove(QStringLiteral("ai/openaiApiKey"));
    reload();
    setMessages(QStringLiteral("Saved to the %1.").arg(keyLocation()), {});
    refreshModels(AIProvider::openAIProvider);
    return true;
}

void AISettings::removeOpenAIKey()
{
    SecretStore::remove(AIProvider::openAIKeyAccount);
    QSettings().remove(QStringLiteral("ai/openaiApiKey"));
    reload();

    if (m_keySource == QStringLiteral("environment")) {
        setMessages({}, QStringLiteral("Removed the saved key, but OPENAI_API_KEY is still set in the environment."));
    } else if (m_keySource == QStringLiteral("env-file")) {
        setMessages({}, QStringLiteral("Removed the saved key, but backend/.env still sets OPENAI_API_KEY."));
    } else {
        setMessages(QStringLiteral("Removed the key from the %1.").arg(keyLocation()), {});
    }
}

void AISettings::setLocalBaseUrl(const QString &url)
{
    const QString trimmed = url.trimmed();
    const QUrl parsed(trimmed);

    if (!trimmed.isEmpty() && (!parsed.isValid() || !parsed.scheme().startsWith(QStringLiteral("http")) || parsed.host().isEmpty())) {
        setMessages({}, QStringLiteral("Use an address like %1.").arg(AIProvider::defaultLocalBaseUrl));
        return;
    }

    AIProvider::setLocalBaseUrl(trimmed);
    emit changed();
    refreshModels(AIProvider::localProvider);
}

void AISettings::refreshModels(const QString &provider)
{
    if (!AIProvider::isKnownProvider(provider) || m_databasePath.isEmpty()) {
        return;
    }

    if (m_request) {
        m_request->abort();
        m_request->deleteLater();
    }

    m_keySource = AIProvider::openAIKeySource(m_databasePath);
    emit changed();
    const AIProvider::OpenAIConfig config = AIProvider::configFor(provider, m_databasePath);

    if (provider == AIProvider::openAIProvider && config.apiKey.isEmpty()) {
        setMessages({}, QStringLiteral("Add an OpenAI API key to load OpenAI models."));
        return;
    }

    const QString path = cachePath();
    const bool local = provider == AIProvider::localProvider;
    auto *request = new OpenAIModelList(m_network, config, this);
    m_request = request;
    setBusy(true);
    setMessages(QStringLiteral("Loading models..."), {});

    connect(request, &OpenAIModelList::finished, this, [this, request, provider, path, local](const QVariantList &models) {
        request->deleteLater();
        ModelCatalogCache::applyDiscovery(path, provider, models);
        ++m_modelRevision;
        emit modelsChanged();
        setBusy(false);
        const int count = ModelCatalogCache::models(path, provider).size();

        if (local && count == 0) {
            setMessages({}, QStringLiteral("The server is running but has no models. Pull one first, for example: ollama pull llama3.2"));
        } else {
            setMessages(count == 1 ? QStringLiteral("1 model available.") : QStringLiteral("%1 models available.").arg(count), {});
        }
    });
    connect(request, &OpenAIModelList::failed, this, [this, request, provider, path, local](const QString &message) {
        request->deleteLater();
        ModelCatalogCache::recordDiscoveryFailure(path, provider, message);
        ++m_modelRevision;
        emit modelsChanged();
        setBusy(false);

        if (local) {
            setMessages({}, QStringLiteral("No model server answered at %1. Start Ollama or LM Studio, or change the address.").arg(AIProvider::localBaseUrl()));
        } else {
            setMessages({}, message);
        }
    });
    request->start();
}

void AISettings::setMessages(const QString &status, const QString &error)
{
    if (m_statusMessage == status && m_errorMessage == error) {
        return;
    }

    m_statusMessage = status;
    m_errorMessage = error;
    emit messageChanged();
}

void AISettings::setBusy(bool busy)
{
    if (m_busy != busy) {
        m_busy = busy;
        emit busyChanged();
    }
}

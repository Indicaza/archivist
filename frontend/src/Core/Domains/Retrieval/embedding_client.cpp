#include "embedding_client.h"

#include "vector_codec.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <algorithm>

namespace Retrieval
{
OpenAIEmbeddingClient::OpenAIEmbeddingClient(
    QNetworkAccessManager *network,
    std::function<AIProvider::OpenAIConfig()> configSource,
    int timeoutMs,
    QObject *parent
)
    : QObject(parent)
    , m_network(network)
    , m_configSource(std::move(configSource))
    , m_timeoutMs(timeoutMs)
{
}

OpenAIEmbeddingClient::~OpenAIEmbeddingClient()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
    }
}

void OpenAIEmbeddingClient::deliver(const EmbeddingResult &result)
{
    Callback done = std::exchange(m_done, nullptr);

    if (done) {
        done(result);
    }
}

void OpenAIEmbeddingClient::embed(const QStringList &inputs, const EmbeddingConfig &config, Callback done)
{
    m_done = std::move(done);
    const AIProvider::OpenAIConfig provider = m_configSource ? m_configSource() : AIProvider::OpenAIConfig{};

    if (provider.apiKey.isEmpty()) {
        QTimer::singleShot(0, this, [this]() {
            EmbeddingResult result;
            result.error = EmbeddingResult::Error::NotConfigured;
            result.message = QStringLiteral("No OpenAI API key is configured, so semantic indexing is waiting.");
            deliver(result);
        });
        return;
    }

    QJsonArray input;

    for (const QString &text : inputs) {
        input.append(text.left(config.maxInputCharacters));
    }

    const QJsonObject body{
        {QStringLiteral("model"), config.model},
        {QStringLiteral("input"), input},
        {QStringLiteral("dimensions"), config.dimensions},
        {QStringLiteral("encoding_format"), QStringLiteral("float")},
    };
    const int expected = static_cast<int>(inputs.size());
    const int dimensions = config.dimensions;
    m_reply = m_network->post(AIProvider::jsonRequest(provider, QStringLiteral("embeddings"), m_timeoutMs), QJsonDocument(body).toJson(QJsonDocument::Compact));
    QNetworkReply *reply = m_reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, expected, dimensions]() {
        reply->deleteLater();

        if (reply != m_reply) {
            return;
        }

        m_reply = nullptr;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray payload = reply->readAll();
        EmbeddingResult result;
        result.status = status;

        if (status == 0) {
            result.error = EmbeddingResult::Error::Network;
            result.message = AIProvider::networkFailureFor(reply);
            deliver(result);
            return;
        }

        if (status < 200 || status >= 300) {
            result.message = AIProvider::errorMessageFor(status, payload);
            result.retryAfterMs = 0;

            if (status == 429) {
                result.error = EmbeddingResult::Error::RateLimited;
            } else if (status == 401 || status == 403) {
                result.error = EmbeddingResult::Error::Unauthorized;
            } else if (AIProvider::isRetryableStatus(status)) {
                result.error = EmbeddingResult::Error::Server;
            } else {
                result.error = EmbeddingResult::Error::InvalidRequest;
            }

            if (result.retryable() && (reply->hasRawHeader("retry-after-ms") || reply->hasRawHeader("retry-after"))) {
                result.retryAfterMs = AIProvider::retryDelayFor(reply, 0);
            }

            deliver(result);
            return;
        }

        result = parseResponse(payload, expected, dimensions);
        result.status = status;
        deliver(result);
    });
}

void OpenAIEmbeddingClient::cancel()
{
    if (m_reply) {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }

    if (m_done) {
        EmbeddingResult result;
        result.error = EmbeddingResult::Error::Cancelled;
        result.message = QStringLiteral("Cancelled.");
        deliver(result);
    }
}

EmbeddingResult OpenAIEmbeddingClient::parseResponse(const QByteArray &body, int expectedCount, int dimensions)
{
    EmbeddingResult result;
    const QJsonObject root = QJsonDocument::fromJson(body).object();
    const QJsonArray data = root.value(QStringLiteral("data")).toArray();
    const auto invalid = [&](const QString &message) {
        EmbeddingResult failure;
        failure.error = EmbeddingResult::Error::InvalidRequest;
        failure.message = message;
        return failure;
    };

    if (data.size() != expectedCount) {
        return invalid(QStringLiteral("OpenAI returned %1 embeddings for %2 inputs.").arg(data.size()).arg(expectedCount));
    }

    QList<QVector<float>> vectors(expectedCount);
    QList<bool> seen(expectedCount, false);

    for (const QJsonValue &value : data) {
        const QJsonObject item = value.toObject();
        const int index = item.value(QStringLiteral("index")).toInt(-1);
        const QJsonArray embedding = item.value(QStringLiteral("embedding")).toArray();

        if (index < 0 || index >= expectedCount || seen.at(index)) {
            return invalid(QStringLiteral("OpenAI returned an embedding with an unexpected index."));
        }

        if (embedding.size() != dimensions) {
            return invalid(QStringLiteral("OpenAI returned a %1-dimension embedding; %2 were requested.").arg(embedding.size()).arg(dimensions));
        }

        QVector<float> vector(dimensions);

        for (int position = 0; position < dimensions; ++position) {
            vector[position] = static_cast<float>(embedding.at(position).toDouble());
        }

        vectors[index] = VectorCodec::normalized(std::move(vector));
        seen[index] = true;
    }

    result.vectors = vectors;
    result.promptTokens = root.value(QStringLiteral("usage")).toObject().value(QStringLiteral("prompt_tokens")).toInt();
    return result;
}
}

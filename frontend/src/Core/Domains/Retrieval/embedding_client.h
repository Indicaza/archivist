#pragma once

#include "embedding_config.h"

#include "Core/Domains/AI/ai_provider.h"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

// Turns text into vectors. The indexer and chat-time query embedding only see
// this interface, so tests answer with canned vectors and no network.
namespace Retrieval
{
struct EmbeddingResult
{
    enum class Error
    {
        None,
        NotConfigured, // no OpenAI key: nothing was sent
        Network,       // no HTTP answer (offline, DNS, timeout); retry later
        RateLimited,   // 429; retry after retryAfterMs
        Server,        // 5xx or 408/409; retry later
        Unauthorized,  // 401/403: the key is wrong; retrying will not help
        InvalidRequest, // other 4xx or a malformed answer; retrying will not help
        Cancelled,
    };

    Error error = Error::None;
    QString message;
    int retryAfterMs = 0;   // a hint from the server, 0 when it gave none
    int status = 0;         // HTTP status, 0 without one
    int promptTokens = 0;   // as the provider reported them
    QList<QVector<float>> vectors; // one unit vector per input, in input order

    [[nodiscard]] bool ok() const { return error == Error::None; }
    [[nodiscard]] bool retryable() const
    {
        return error == Error::Network || error == Error::RateLimited || error == Error::Server;
    }
};

class EmbeddingClient
{
public:
    using Callback = std::function<void(const EmbeddingResult &)>;

    virtual ~EmbeddingClient() = default;

    // One request for every input, answered once through `done` on the
    // client's thread (never synchronously from inside embed()). One request
    // at a time per client.
    virtual void embed(const QStringList &inputs, const EmbeddingConfig &config, Callback done) = 0;
    // Drops the request in flight; its callback receives Cancelled.
    virtual void cancel() = 0;
    [[nodiscard]] virtual bool busy() const = 0;
};

// POST /v1/embeddings on the user's OpenAI connection. A single attempt: the
// indexer owns retries and backoff so they survive a restart.
class OpenAIEmbeddingClient final : public QObject, public EmbeddingClient
{
    Q_OBJECT

public:
    // `configSource` is read before every request so a key saved or removed
    // in the app takes effect without restarting.
    OpenAIEmbeddingClient(QNetworkAccessManager *network, std::function<AIProvider::OpenAIConfig()> configSource, int timeoutMs = 60000, QObject *parent = nullptr);
    ~OpenAIEmbeddingClient() override;

    void embed(const QStringList &inputs, const EmbeddingConfig &config, Callback done) override;
    void cancel() override;
    [[nodiscard]] bool busy() const override { return static_cast<bool>(m_done); }

    // Parses an /embeddings response body. Public for tests.
    [[nodiscard]] static EmbeddingResult parseResponse(const QByteArray &body, int expectedCount, int dimensions);

private:
    void deliver(const EmbeddingResult &result);

    QNetworkAccessManager *m_network;
    std::function<AIProvider::OpenAIConfig()> m_configSource;
    int m_timeoutMs;
    QPointer<QNetworkReply> m_reply;
    Callback m_done;
};
}

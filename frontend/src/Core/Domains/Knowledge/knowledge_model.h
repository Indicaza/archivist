#pragma once

#include "Core/Domains/AI/ai_provider.h"

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

// The model the Wiki librarians write with. The job runner only sees this
// interface, so tests answer with canned JSON and no network.
namespace Knowledge
{
struct ModelRequest
{
    QString model;          // e.g. "gpt-5-mini"
    QString instructions;   // system text
    QString input;          // the user message: task, evidence, existing page
    QString schemaName;
    QJsonObject schema;     // strict JSON schema for the reply
    int maxOutputTokens = 8000;
};

struct ModelResult
{
    enum class Error
    {
        None,
        NotConfigured, // no OpenAI key: nothing was sent
        Network,       // no HTTP answer (offline, DNS, timeout); retry later
        RateLimited,   // 429
        Server,        // 5xx
        Unauthorized,  // 401/403
        InvalidRequest, // other 4xx, refusal, or an answer that is not the requested JSON
        Cancelled,
    };

    Error error = Error::None;
    QString message;
    QString text;        // the JSON text the model produced
    QString model;       // as the provider reported it
    int status = 0;
    int retryAfterMs = 0;
    int inputTokens = 0;
    int outputTokens = 0;

    [[nodiscard]] bool ok() const { return error == Error::None; }
    [[nodiscard]] bool retryable() const { return error == Error::Network || error == Error::RateLimited || error == Error::Server; }
    // "not-configured", "network", ... for job error codes.
    [[nodiscard]] QString code() const;
};

class KnowledgeModel
{
public:
    using Callback = std::function<void(const ModelResult &)>;

    virtual ~KnowledgeModel() = default;
    // Answers once through `done`, never synchronously from inside generate().
    virtual void generate(const ModelRequest &request, Callback done) = 0;
    virtual void cancel() = 0;
    [[nodiscard]] virtual bool busy() const = 0;
};

// POST /v1/responses on the user's OpenAI connection with a strict JSON
// schema, not streamed. One attempt: the job runner owns retries.
class OpenAIKnowledgeModel final : public QObject, public KnowledgeModel
{
    Q_OBJECT

public:
    OpenAIKnowledgeModel(QNetworkAccessManager *network, std::function<AIProvider::OpenAIConfig()> configSource, int timeoutMs = 180000, QObject *parent = nullptr);
    ~OpenAIKnowledgeModel() override;

    void generate(const ModelRequest &request, Callback done) override;
    void cancel() override;
    [[nodiscard]] bool busy() const override { return static_cast<bool>(m_done); }

    // The request body for `request`, and the parser for a response body.
    // Public for tests.
    [[nodiscard]] static QJsonObject requestBody(const ModelRequest &request);
    [[nodiscard]] static ModelResult parseResponse(const QByteArray &body);

private:
    void deliver(const ModelResult &result);

    QNetworkAccessManager *m_network;
    std::function<AIProvider::OpenAIConfig()> m_configSource;
    int m_timeoutMs;
    QPointer<QNetworkReply> m_reply;
    Callback m_done;
};
}

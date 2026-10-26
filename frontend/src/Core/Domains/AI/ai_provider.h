#pragma once

#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

// Native port of backend/src/core/ai/providers/OpenAIProvider.ts on top of
// QNetworkAccessManager. Behaves like the official SDK the Node backend used:
// same request bodies, same error messages, two retries with backoff.
namespace AIProvider
{
// "openai" is the OpenAI API. "local" is any OpenAI-compatible server on this
// computer (Ollama, LM Studio, llama.cpp), reached through Chat Completions.
inline const QString openAIProvider = QStringLiteral("openai");
inline const QString localProvider = QStringLiteral("local");
inline const QString openAIKeyAccount = QStringLiteral("openai-api-key");
inline const QString defaultLocalBaseUrl = QStringLiteral("http://127.0.0.1:11434/v1");

struct OpenAIConfig
{
    QString apiKey;
    QUrl baseUrl; // ends with /v1
    // Chat Completions instead of the Responses API.
    bool chatCompletions = false;
};

// OPENAI_API_KEY / OPENAI_BASE_URL from the environment, then backend/.env
// next to the database's data folder, then the key saved in the app
// (SecretStore), then the legacy QSettings ai/openaiApiKey.
[[nodiscard]] OpenAIConfig openAIConfig(const QString &databasePath);
// Where the OpenAI key comes from: "environment", "env-file", "app" or "".
[[nodiscard]] QString openAIKeySource(const QString &databasePath);

// ARCHIVIST_LOCAL_AI_URL, then QSettings ai/localBaseUrl, then Ollama's
// default address. Local servers do not need a key.
[[nodiscard]] OpenAIConfig localConfig();
[[nodiscard]] QString localBaseUrl();
void setLocalBaseUrl(const QString &url);

[[nodiscard]] OpenAIConfig configFor(const QString &provider, const QString &databasePath);
[[nodiscard]] bool isKnownProvider(const QString &provider);
// "OpenAI" or "The local model server", for error messages.
[[nodiscard]] QString displayName(const QString &provider);

// "The SDK error message" for a failed HTTP response.
[[nodiscard]] QString errorMessageFor(int status, const QByteArray &body);
}

// One streamed POST /responses round (or /chat/completions for a local
// server), retried like the SDK until the first byte of the stream arrives.
class OpenAIResponseStream final : public QObject
{
    Q_OBJECT

public:
    OpenAIResponseStream(QNetworkAccessManager *network, AIProvider::OpenAIConfig config, QJsonObject body, QObject *parent = nullptr);
    ~OpenAIResponseStream() override;

    void start();
    void abort();

signals:
    void textDelta(const QString &delta);
    // {callId, name, arguments (raw JSON text)}
    void functionCall(const QVariantMap &call);
    void finished(const QString &responseId);
    void failed(const QString &message);

private:
    void send();
    void handleData();
    void handleFinished();
    void handleBlock(const QByteArray &block);
    void retryOrFail(const QString &message, int retryAfterMs);
    void handleChatChunk(const QJsonObject &chunk);
    void emitChatToolCalls();

    QNetworkAccessManager *m_network;
    AIProvider::OpenAIConfig m_config;
    QJsonObject m_body;
    QPointer<QNetworkReply> m_reply;
    QTimer *m_retryTimer = nullptr;
    QByteArray m_buffer;
    QByteArray m_errorBody;
    QString m_responseId;
    // Chat Completions streams each tool call in pieces, keyed by index.
    QMap<int, QVariantMap> m_chatToolCalls;
    int m_attempt = 0;
    bool m_streaming = false;
    bool m_done = false;
};

// GET /models, for model discovery.
class OpenAIModelList final : public QObject
{
    Q_OBJECT

public:
    OpenAIModelList(QNetworkAccessManager *network, AIProvider::OpenAIConfig config, QObject *parent = nullptr);
    void start();
    void abort();

signals:
    // [{modelId, createdAt, ownedBy}]
    void finished(const QVariantList &models);
    void failed(const QString &message);

private:
    QNetworkAccessManager *m_network;
    AIProvider::OpenAIConfig m_config;
    QPointer<QNetworkReply> m_reply;
};

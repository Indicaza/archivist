#include "knowledge_model.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>

namespace Knowledge
{
QString ModelResult::code() const
{
    switch (error) {
    case Error::None:
        return {};
    case Error::NotConfigured:
        return QStringLiteral("not-configured");
    case Error::Network:
        return QStringLiteral("network");
    case Error::RateLimited:
        return QStringLiteral("rate-limited");
    case Error::Server:
        return QStringLiteral("server");
    case Error::Unauthorized:
        return QStringLiteral("unauthorized");
    case Error::InvalidRequest:
        return QStringLiteral("invalid-request");
    case Error::Cancelled:
        return QStringLiteral("cancelled");
    }

    return QStringLiteral("unknown");
}

OpenAIKnowledgeModel::OpenAIKnowledgeModel(QNetworkAccessManager *network, std::function<AIProvider::OpenAIConfig()> configSource, int timeoutMs, QObject *parent)
    : QObject(parent)
    , m_network(network)
    , m_configSource(std::move(configSource))
    , m_timeoutMs(timeoutMs)
{
}

OpenAIKnowledgeModel::~OpenAIKnowledgeModel()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
    }
}

void OpenAIKnowledgeModel::deliver(const ModelResult &result)
{
    if (Callback done = std::exchange(m_done, nullptr)) {
        done(result);
    }
}

QJsonObject OpenAIKnowledgeModel::requestBody(const ModelRequest &request)
{
    QJsonObject body{
        {QStringLiteral("model"), request.model},
        {QStringLiteral("instructions"), request.instructions},
        {QStringLiteral("input"), QJsonArray{QJsonObject{
                                      {QStringLiteral("role"), QStringLiteral("user")},
                                      {QStringLiteral("content"), request.input},
                                  }}},
        {QStringLiteral("text"), QJsonObject{{QStringLiteral("format"), QJsonObject{
                                                 {QStringLiteral("type"), QStringLiteral("json_schema")},
                                                 {QStringLiteral("name"), request.schemaName},
                                                 {QStringLiteral("schema"), request.schema},
                                                 {QStringLiteral("strict"), true},
                                             }}}},
        {QStringLiteral("max_output_tokens"), request.maxOutputTokens},
        // Library text is not kept on OpenAI's side beyond the request.
        {QStringLiteral("store"), false},
    };
    // Reasoning models: a modest effort is enough to summarize given passages.
    static const QRegularExpression reasoningModel(QStringLiteral("^(gpt-5|o1|o3|o4)([.-]|$)"));

    if (reasoningModel.match(request.model).hasMatch() && !request.model.contains(QStringLiteral("-chat"))) {
        body.insert(QStringLiteral("reasoning"), QJsonObject{{QStringLiteral("effort"), QStringLiteral("low")}});
    }

    return body;
}

ModelResult OpenAIKnowledgeModel::parseResponse(const QByteArray &body)
{
    ModelResult result;
    const QJsonObject root = QJsonDocument::fromJson(body).object();
    result.model = root.value(QStringLiteral("model")).toString();
    const QJsonObject usage = root.value(QStringLiteral("usage")).toObject();
    result.inputTokens = usage.value(QStringLiteral("input_tokens")).toInt();
    result.outputTokens = usage.value(QStringLiteral("output_tokens")).toInt();

    if (root.value(QStringLiteral("status")).toString() == QStringLiteral("incomplete")) {
        result.error = ModelResult::Error::InvalidRequest;
        result.message = QStringLiteral("The model stopped before finishing (%1).")
                             .arg(root.value(QStringLiteral("incomplete_details")).toObject().value(QStringLiteral("reason")).toString(QStringLiteral("unknown reason")));
        return result;
    }

    QString text;

    for (const QJsonValue &item : root.value(QStringLiteral("output")).toArray()) {
        if (item.toObject().value(QStringLiteral("type")).toString() != QStringLiteral("message")) {
            continue;
        }

        for (const QJsonValue &part : item.toObject().value(QStringLiteral("content")).toArray()) {
            const QJsonObject content = part.toObject();
            const QString type = content.value(QStringLiteral("type")).toString();

            if (type == QStringLiteral("output_text")) {
                text += content.value(QStringLiteral("text")).toString();
            } else if (type == QStringLiteral("refusal")) {
                result.error = ModelResult::Error::InvalidRequest;
                result.message = QStringLiteral("The model declined: %1").arg(content.value(QStringLiteral("refusal")).toString());
                return result;
            }
        }
    }

    if (text.trimmed().isEmpty()) {
        result.error = ModelResult::Error::InvalidRequest;
        result.message = QStringLiteral("The model returned no text.");
        return result;
    }

    result.text = text;
    return result;
}

void OpenAIKnowledgeModel::generate(const ModelRequest &request, Callback done)
{
    m_done = std::move(done);
    const AIProvider::OpenAIConfig provider = m_configSource ? m_configSource() : AIProvider::OpenAIConfig{};

    if (provider.apiKey.isEmpty() || provider.chatCompletions) {
        QTimer::singleShot(0, this, [this]() {
            ModelResult result;
            result.error = ModelResult::Error::NotConfigured;
            result.message = QStringLiteral("Add an OpenAI API key (Agent editor → Model) to generate Wiki pages.");
            deliver(result);
        });
        return;
    }

    m_reply = m_network->post(AIProvider::jsonRequest(provider, QStringLiteral("responses"), m_timeoutMs),
                              QJsonDocument(requestBody(request)).toJson(QJsonDocument::Compact));
    QNetworkReply *reply = m_reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        if (reply != m_reply) {
            return;
        }

        m_reply = nullptr;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray payload = reply->readAll();
        ModelResult result;
        result.status = status;

        if (status == 0) {
            result.error = ModelResult::Error::Network;
            result.message = AIProvider::networkFailureFor(reply);
            deliver(result);
            return;
        }

        if (status < 200 || status >= 300) {
            result.message = AIProvider::errorMessageFor(status, payload);
            result.error = status == 429                         ? ModelResult::Error::RateLimited
                : status == 401 || status == 403                 ? ModelResult::Error::Unauthorized
                : AIProvider::isRetryableStatus(status)          ? ModelResult::Error::Server
                                                                 : ModelResult::Error::InvalidRequest;

            if (result.retryable() && (reply->hasRawHeader("retry-after-ms") || reply->hasRawHeader("retry-after"))) {
                result.retryAfterMs = AIProvider::retryDelayFor(reply, 0);
            }

            deliver(result);
            return;
        }

        result = parseResponse(payload);
        result.status = status;
        deliver(result);
    });
}

void OpenAIKnowledgeModel::cancel()
{
    if (m_reply) {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }

    if (m_done) {
        ModelResult result;
        result.error = ModelResult::Error::Cancelled;
        result.message = QStringLiteral("Cancelled.");
        deliver(result);
    }
}
}

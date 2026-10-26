#include "ai_provider.h"

#include "Core/Support/secret_store.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QSettings>
#include <QTimer>
#include <cmath>
#include <utility>

namespace
{
constexpr int maximumRetries = 2;
constexpr int requestTimeoutMs = 10 * 60 * 1000;

QString dotEnvValue(const QString &path, const QString &key)
{
    QFile file(path);

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    while (!file.atEnd()) {
        QString line = QString::fromUtf8(file.readLine()).trimmed();

        if (line.startsWith(QStringLiteral("export "))) {
            line = line.mid(7).trimmed();
        }

        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || !line.startsWith(key + QLatin1Char('='))) {
            continue;
        }

        QString value = line.mid(key.size() + 1).trimmed();

        if (value.size() >= 2 && (value.startsWith(QLatin1Char('"')) || value.startsWith(QLatin1Char('\'')))
            && value.endsWith(value.at(0))) {
            value = value.mid(1, value.size() - 2);
        } else {
            const qsizetype comment = value.indexOf(QStringLiteral(" #"));

            if (comment >= 0) {
                value = value.left(comment).trimmed();
            }
        }

        return value;
    }

    return {};
}

QNetworkRequest requestFor(const AIProvider::OpenAIConfig &config, const QString &path)
{
    QUrl url = config.baseUrl;
    url.setPath(url.path().endsWith(QLatin1Char('/')) ? url.path() + path : url.path() + QLatin1Char('/') + path);
    QNetworkRequest request(url);

    if (!config.apiKey.isEmpty()) {
        request.setRawHeader("Authorization", "Bearer " + config.apiKey.toUtf8());
    }

    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");
    request.setTransferTimeout(requestTimeoutMs);
    return request;
}

bool retryableStatus(int status)
{
    return status == 408 || status == 409 || status == 429 || status >= 500;
}

int retryDelayMs(QNetworkReply *reply, int attempt)
{
    if (reply) {
        bool ok = false;
        const double milliseconds = reply->rawHeader("retry-after-ms").toDouble(&ok);

        if (ok && milliseconds >= 0 && milliseconds < 60000) {
            return static_cast<int>(milliseconds);
        }

        const QByteArray retryAfter = reply->rawHeader("retry-after");
        const double seconds = retryAfter.toDouble(&ok);

        if (ok && seconds >= 0 && seconds < 60) {
            return static_cast<int>(seconds * 1000);
        }
    }

    const double base = std::min(0.5 * std::pow(2.0, attempt), 8.0);
    const double jitter = 1.0 - QRandomGenerator::global()->generateDouble() * 0.25;
    return static_cast<int>(base * jitter * 1000);
}

QString networkFailureMessage(QNetworkReply *reply)
{
    if (reply && (reply->error() == QNetworkReply::TimeoutError || reply->error() == QNetworkReply::OperationCanceledError)) {
        return QStringLiteral("Request timed out.");
    }

    return QStringLiteral("Connection error.");
}
}

namespace AIProvider
{
namespace
{
QString dotEnvPath(const QString &databasePath)
{
    // backend/data/archivist.db -> backend/.env
    return QFileInfo(databasePath).dir().absoluteFilePath(QStringLiteral("../.env"));
}
}

QString openAIKeySource(const QString &databasePath)
{
    if (!qEnvironmentVariable("OPENAI_API_KEY").trimmed().isEmpty()) {
        return QStringLiteral("environment");
    }

    if (!dotEnvValue(dotEnvPath(databasePath), QStringLiteral("OPENAI_API_KEY")).isEmpty()) {
        return QStringLiteral("env-file");
    }

    if (!SecretStore::read(openAIKeyAccount).trimmed().isEmpty()
        || !QSettings().value(QStringLiteral("ai/openaiApiKey")).toString().trimmed().isEmpty()) {
        return QStringLiteral("app");
    }

    return {};
}

OpenAIConfig openAIConfig(const QString &databasePath)
{
    const QString dotEnv = dotEnvPath(databasePath);
    OpenAIConfig config;
    config.apiKey = qEnvironmentVariable("OPENAI_API_KEY").trimmed();

    if (config.apiKey.isEmpty()) {
        config.apiKey = dotEnvValue(dotEnv, QStringLiteral("OPENAI_API_KEY"));
    }

    if (config.apiKey.isEmpty()) {
        config.apiKey = SecretStore::read(openAIKeyAccount).trimmed();
    }

    if (config.apiKey.isEmpty()) {
        config.apiKey = QSettings().value(QStringLiteral("ai/openaiApiKey")).toString().trimmed();
    }

    QString baseUrl = qEnvironmentVariable("OPENAI_BASE_URL").trimmed();

    if (baseUrl.isEmpty()) {
        baseUrl = dotEnvValue(dotEnv, QStringLiteral("OPENAI_BASE_URL"));
    }

    config.baseUrl = QUrl(baseUrl.isEmpty() ? QStringLiteral("https://api.openai.com/v1") : baseUrl);
    return config;
}

QString localBaseUrl()
{
    const QString configured = qEnvironmentVariable("ARCHIVIST_LOCAL_AI_URL").trimmed();

    if (!configured.isEmpty()) {
        return configured;
    }

    const QString saved = QSettings().value(QStringLiteral("ai/localBaseUrl")).toString().trimmed();
    return saved.isEmpty() ? defaultLocalBaseUrl : saved;
}

void setLocalBaseUrl(const QString &url)
{
    QSettings settings;
    const QString trimmed = url.trimmed();

    if (trimmed.isEmpty() || trimmed == defaultLocalBaseUrl) {
        settings.remove(QStringLiteral("ai/localBaseUrl"));
    } else {
        settings.setValue(QStringLiteral("ai/localBaseUrl"), trimmed);
    }
}

OpenAIConfig localConfig()
{
    OpenAIConfig config;
    config.baseUrl = QUrl(localBaseUrl());
    config.apiKey = QSettings().value(QStringLiteral("ai/localApiKey")).toString().trimmed();
    config.chatCompletions = true;
    return config;
}

OpenAIConfig configFor(const QString &provider, const QString &databasePath)
{
    return provider == localProvider ? localConfig() : openAIConfig(databasePath);
}

bool isKnownProvider(const QString &provider)
{
    return provider == openAIProvider || provider == localProvider;
}

QString displayName(const QString &provider)
{
    return provider == localProvider ? QStringLiteral("The local model server") : QStringLiteral("OpenAI");
}

QString errorMessageFor(int status, const QByteArray &body)
{
    QString message;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);

    if (parseError.error == QJsonParseError::NoError && document.isObject()) {
        const QJsonValue error = document.object().value(QStringLiteral("error"));

        if (error.isObject() && error.toObject().value(QStringLiteral("message")).isString()) {
            message = error.toObject().value(QStringLiteral("message")).toString();
        } else if (!error.isUndefined() && !error.isNull()) {
            message = QString::fromUtf8(QJsonDocument(QJsonArray{error}).toJson(QJsonDocument::Compact)).mid(1).chopped(1);
        } else {
            message = QString::fromUtf8(document.toJson(QJsonDocument::Compact));
        }
    } else if (!body.trimmed().isEmpty()) {
        message = QString::fromUtf8(body).trimmed();
    }

    if (status > 0 && !message.isEmpty()) {
        return QStringLiteral("%1 %2").arg(status).arg(message);
    }

    if (status > 0) {
        return QStringLiteral("%1 status code (no body)").arg(status);
    }

    return message.isEmpty() ? QStringLiteral("(no status code or body)") : message;
}
}

OpenAIResponseStream::OpenAIResponseStream(QNetworkAccessManager *network, AIProvider::OpenAIConfig config, QJsonObject body, QObject *parent)
    : QObject(parent)
    , m_network(network)
    , m_config(std::move(config))
    , m_body(std::move(body))
{
}

OpenAIResponseStream::~OpenAIResponseStream()
{
    m_done = true;

    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
    }
}

void OpenAIResponseStream::start()
{
    send();
}

void OpenAIResponseStream::abort()
{
    m_done = true;

    if (m_retryTimer) {
        m_retryTimer->stop();
    }

    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
    }
}

void OpenAIResponseStream::send()
{
    m_buffer.clear();
    m_errorBody.clear();
    m_streaming = false;
    m_chatToolCalls.clear();
    QNetworkRequest request = requestFor(m_config, m_config.chatCompletions ? QStringLiteral("chat/completions") : QStringLiteral("responses"));
    request.setRawHeader("Accept", "text/event-stream");
    m_reply = m_network->post(request, QJsonDocument(m_body).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::readyRead, this, &OpenAIResponseStream::handleData);
    connect(m_reply, &QNetworkReply::finished, this, &OpenAIResponseStream::handleFinished);
}

void OpenAIResponseStream::retryOrFail(const QString &message, int retryAfterMs)
{
    if (m_attempt >= maximumRetries || m_streaming) {
        m_done = true;
        emit failed(message);
        return;
    }

    ++m_attempt;

    if (!m_retryTimer) {
        m_retryTimer = new QTimer(this);
        m_retryTimer->setSingleShot(true);
        connect(m_retryTimer, &QTimer::timeout, this, &OpenAIResponseStream::send);
    }

    m_retryTimer->start(retryAfterMs);
}

void OpenAIResponseStream::handleData()
{
    if (!m_reply || m_done) {
        return;
    }

    const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray chunk = m_reply->readAll();

    if (status < 200 || status >= 300) {
        m_errorBody += chunk;
        return;
    }

    m_streaming = true;
    m_buffer += chunk;
    m_buffer.replace("\r\n", "\n");
    qsizetype boundary = m_buffer.indexOf("\n\n");

    while (boundary >= 0 && !m_done) {
        const QByteArray block = m_buffer.left(boundary);
        m_buffer.remove(0, boundary + 2);
        handleBlock(block);
        boundary = m_buffer.indexOf("\n\n");
    }
}

void OpenAIResponseStream::handleBlock(const QByteArray &block)
{
    QByteArray data;

    for (const QByteArray &line : block.split('\n')) {
        if (line.startsWith("data:")) {
            QByteArray value = line.mid(5);

            if (value.startsWith(' ')) {
                value.remove(0, 1);
            }

            data += data.isEmpty() ? value : "\n" + value;
        }
    }

    if (data.isEmpty() || data == "[DONE]") {
        return;
    }

    const QJsonObject event = QJsonDocument::fromJson(data).object();

    if (event.value(QStringLiteral("error")).isObject()) {
        // The SDK throws APIError(undefined, data.error) for these.
        const QJsonObject error = event.value(QStringLiteral("error")).toObject();
        m_done = true;
        emit failed(
            error.value(QStringLiteral("message")).isString()
                ? error.value(QStringLiteral("message")).toString()
                : QString::fromUtf8(QJsonDocument(error).toJson(QJsonDocument::Compact))
        );
        return;
    }

    if (m_config.chatCompletions) {
        handleChatChunk(event);
        return;
    }

    const QString responseId = event.value(QStringLiteral("response")).toObject().value(QStringLiteral("id")).toString();

    if (!responseId.isEmpty()) {
        m_responseId = responseId;
    }

    const QString type = event.value(QStringLiteral("type")).toString();

    if (type == QStringLiteral("response.output_text.delta") || type == QStringLiteral("response.refusal.delta")) {
        emit textDelta(event.value(QStringLiteral("delta")).toString());
        return;
    }

    if (type != QStringLiteral("response.output_item.done")) {
        return;
    }

    const QJsonObject item = event.value(QStringLiteral("item")).toObject();

    if (item.value(QStringLiteral("type")).toString() != QStringLiteral("function_call")
        || item.value(QStringLiteral("call_id")).toString().isEmpty() || item.value(QStringLiteral("name")).toString().isEmpty()) {
        return;
    }

    emit functionCall({
        {QStringLiteral("callId"), item.value(QStringLiteral("call_id")).toString()},
        {QStringLiteral("name"), item.value(QStringLiteral("name")).toString()},
        {QStringLiteral("arguments"),
         item.value(QStringLiteral("arguments")).isString() ? item.value(QStringLiteral("arguments")).toString() : QStringLiteral("{}")},
    });
}

void OpenAIResponseStream::handleFinished()
{
    QNetworkReply *reply = m_reply;

    if (!reply || m_done) {
        return;
    }

    handleData();
    reply->deleteLater();
    m_reply = nullptr;

    if (m_done) {
        return;
    }

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (status == 0) {
        retryOrFail(networkFailureMessage(reply), retryDelayMs(nullptr, m_attempt));
        return;
    }

    if (status < 200 || status >= 300) {
        m_errorBody += reply->readAll();
        const QString message = AIProvider::errorMessageFor(status, m_errorBody);
        const QByteArray shouldRetry = reply->rawHeader("x-should-retry");
        const bool retry = shouldRetry == "true" || (shouldRetry != "false" && retryableStatus(status));

        if (retry) {
            retryOrFail(message, retryDelayMs(reply, m_attempt));
        } else {
            m_done = true;
            emit failed(message);
        }

        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        m_done = true;
        emit failed(networkFailureMessage(reply));
        return;
    }

    if (!m_buffer.trimmed().isEmpty()) {
        handleBlock(m_buffer);
        m_buffer.clear();
    }

    if (!m_done) {
        m_done = true;

        if (m_config.chatCompletions) {
            emitChatToolCalls();

            if (m_responseId.isEmpty()) {
                m_responseId = QStringLiteral("chat-completion");
            }
        }

        emit finished(m_responseId);
    }
}

void OpenAIResponseStream::handleChatChunk(const QJsonObject &chunk)
{
    if (chunk.value(QStringLiteral("id")).isString() && !chunk.value(QStringLiteral("id")).toString().isEmpty()) {
        m_responseId = chunk.value(QStringLiteral("id")).toString();
    }

    const QJsonArray choices = chunk.value(QStringLiteral("choices")).toArray();

    if (choices.isEmpty()) {
        return;
    }

    const QJsonObject delta = choices.first().toObject().value(QStringLiteral("delta")).toObject();
    const QJsonValue content = delta.value(QStringLiteral("content"));

    if (content.isString() && !content.toString().isEmpty()) {
        emit textDelta(content.toString());
    }

    const QJsonArray toolCalls = delta.value(QStringLiteral("tool_calls")).toArray();

    for (qsizetype position = 0; position < toolCalls.size(); ++position) {
        const QJsonObject call = toolCalls.at(position).toObject();
        const int index = call.value(QStringLiteral("index")).isDouble() ? call.value(QStringLiteral("index")).toInt() : static_cast<int>(position);
        QVariantMap &pending = m_chatToolCalls[index];
        const QJsonObject function = call.value(QStringLiteral("function")).toObject();

        if (call.value(QStringLiteral("id")).isString() && !call.value(QStringLiteral("id")).toString().isEmpty()) {
            pending.insert(QStringLiteral("callId"), call.value(QStringLiteral("id")).toString());
        }

        if (function.value(QStringLiteral("name")).isString() && !function.value(QStringLiteral("name")).toString().isEmpty()) {
            pending.insert(QStringLiteral("name"), function.value(QStringLiteral("name")).toString());
        }

        const QJsonValue arguments = function.value(QStringLiteral("arguments"));

        if (arguments.isString()) {
            pending.insert(QStringLiteral("arguments"), pending.value(QStringLiteral("arguments")).toString() + arguments.toString());
        } else if (arguments.isObject()) {
            // Some servers send the arguments as an object in one piece.
            pending.insert(QStringLiteral("arguments"), QString::fromUtf8(QJsonDocument(arguments.toObject()).toJson(QJsonDocument::Compact)));
        }
    }
}

void OpenAIResponseStream::emitChatToolCalls()
{
    const QMap<int, QVariantMap> calls = std::exchange(m_chatToolCalls, {});

    for (auto iterator = calls.cbegin(); iterator != calls.cend(); ++iterator) {
        QVariantMap call = iterator.value();

        if (call.value(QStringLiteral("name")).toString().isEmpty()) {
            continue;
        }

        if (call.value(QStringLiteral("callId")).toString().isEmpty()) {
            call.insert(QStringLiteral("callId"), QStringLiteral("call_%1").arg(iterator.key()));
        }

        if (call.value(QStringLiteral("arguments")).toString().trimmed().isEmpty()) {
            call.insert(QStringLiteral("arguments"), QStringLiteral("{}"));
        }

        emit functionCall(call);
    }
}

OpenAIModelList::OpenAIModelList(QNetworkAccessManager *network, AIProvider::OpenAIConfig config, QObject *parent)
    : QObject(parent)
    , m_network(network)
    , m_config(std::move(config))
{
}

void OpenAIModelList::start()
{
    m_reply = m_network->get(requestFor(m_config, QStringLiteral("models")));
    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        QNetworkReply *reply = m_reply;

        if (!reply) {
            return;
        }

        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();

        if (status == 0) {
            emit failed(networkFailureMessage(reply));
            return;
        }

        if (status < 200 || status >= 300) {
            emit failed(AIProvider::errorMessageFor(status, body));
            return;
        }

        QVariantList models;

        for (const QJsonValue &value : QJsonDocument::fromJson(body).object().value(QStringLiteral("data")).toArray()) {
            const QJsonObject model = value.toObject();
            const QJsonValue created = model.value(QStringLiteral("created"));
            const QJsonValue ownedBy = model.value(QStringLiteral("owned_by"));
            models.append(QVariantMap{
                {QStringLiteral("modelId"), model.value(QStringLiteral("id")).toString()},
                {QStringLiteral("createdAt"),
                 created.isDouble()
                     ? QVariant(QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(created.toDouble() * 1000)).toUTC().toString(
                         QStringLiteral("yyyy-MM-dd'T'HH:mm:ss.zzz'Z'")
                     ))
                     : QVariant::fromValue(nullptr)},
                {QStringLiteral("ownedBy"), ownedBy.isString() ? QVariant(ownedBy.toString()) : QVariant::fromValue(nullptr)},
            });
        }

        emit finished(models);
    });
}

void OpenAIModelList::abort()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
    }
}

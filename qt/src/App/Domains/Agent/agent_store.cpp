#include "agent_store.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkReply>

namespace
{
struct JsonReplyResult
{
    bool ok = false;
    QJsonObject object;
    QString errorMessage;
};

QString responseErrorMessage(const QJsonObject &object, const QString &fallback)
{
    const QJsonObject error = object.value(QStringLiteral("error")).toObject();
    const QString message = error.value(QStringLiteral("message")).toString();
    return message.isEmpty() ? fallback : message;
}

JsonReplyResult consumeJsonReply(QNetworkReply *reply)
{
    const QByteArray payload = reply->readAll();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    const QJsonObject object = document.isObject() ? document.object() : QJsonObject{};

    if (reply->error() != QNetworkReply::NoError) {
        return {
            false,
            object,
            responseErrorMessage(object, reply->errorString()),
        };
    }

    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return {
            false,
            {},
            QStringLiteral("Archivist API returned an invalid JSON response."),
        };
    }

    if (!object.value(QStringLiteral("ok")).toBool(false)) {
        return {
            false,
            object,
            responseErrorMessage(object, QStringLiteral("Archivist API request failed.")),
        };
    }

    return {true, object, {}};
}
}

AgentStore::AgentStore(QObject *parent)
    : QObject(parent)
    , m_baseUrl(QStringLiteral("http://127.0.0.1:3333/api"))
{
}

QVariantList AgentStore::agents() const
{
    return m_agents;
}

bool AgentStore::loading() const
{
    return m_loading;
}

QString AgentStore::errorMessage() const
{
    return m_errorMessage;
}

QNetworkRequest AgentStore::requestFor(const QString &path) const
{
    QNetworkRequest request{QUrl(m_baseUrl.toString() + path)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    return request;
}

void AgentStore::refresh()
{
    if (m_loading) {
        return;
    }

    setErrorMessage({});
    setLoading(true);

    QNetworkReply *reply = m_network.get(requestFor(QStringLiteral("/agents")));

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const JsonReplyResult result = consumeJsonReply(reply);
        reply->deleteLater();
        setLoading(false);

        if (!result.ok) {
            setErrorMessage(result.errorMessage);
            return;
        }

        setAgents(result.object.value(QStringLiteral("agents")).toArray().toVariantList());
    });
}

QVariantMap AgentStore::agentById(const QString &agentId) const
{
    if (agentId.isEmpty()) {
        return {};
    }

    for (const QVariant &value : m_agents) {
        const QVariantMap agent = value.toMap();
        if (agent.value(QStringLiteral("id")).toString() == agentId) {
            return agent;
        }
    }

    return {};
}

void AgentStore::setAgents(const QVariantList &agents)
{
    if (m_agents == agents) {
        return;
    }

    m_agents = agents;
    emit agentsChanged();
}

void AgentStore::setLoading(bool loading)
{
    if (m_loading == loading) {
        return;
    }

    m_loading = loading;
    emit loadingChanged();
}

void AgentStore::setErrorMessage(const QString &message)
{
    if (m_errorMessage == message) {
        return;
    }

    m_errorMessage = message;
    emit errorMessageChanged();
}

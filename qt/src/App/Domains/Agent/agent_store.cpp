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

QString encodedPathSegment(const QString &value)
{
    return QString::fromUtf8(QUrl::toPercentEncoding(value));
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

bool AgentStore::mutating() const
{
    return m_mutating;
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

    clearError();
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

void AgentStore::createAgent(const QVariantMap &input)
{
    if (m_mutating) {
        return;
    }

    clearError();
    setMutating(true);

    QNetworkReply *reply = m_network.post(
        requestFor(QStringLiteral("/agents")),
        QJsonDocument(QJsonObject::fromVariantMap(input)).toJson(QJsonDocument::Compact)
    );

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const JsonReplyResult result = consumeJsonReply(reply);
        reply->deleteLater();
        setMutating(false);

        if (!result.ok) {
            setErrorMessage(result.errorMessage);
            return;
        }

        const QVariantMap agent = result.object.value(QStringLiteral("agent")).toObject().toVariantMap();
        upsertAgent(agent);
        emit agentCreated(agent);
    });
}

void AgentStore::updateAgent(const QString &agentId, const QVariantMap &input)
{
    if (m_mutating || agentId.isEmpty()) {
        return;
    }

    clearError();
    setMutating(true);

    const QString path = QStringLiteral("/agents/%1").arg(encodedPathSegment(agentId));
    QNetworkReply *reply = m_network.sendCustomRequest(
        requestFor(path),
        QByteArrayLiteral("PATCH"),
        QJsonDocument(QJsonObject::fromVariantMap(input)).toJson(QJsonDocument::Compact)
    );

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const JsonReplyResult result = consumeJsonReply(reply);
        reply->deleteLater();
        setMutating(false);

        if (!result.ok) {
            setErrorMessage(result.errorMessage);
            return;
        }

        const QVariantMap agent = result.object.value(QStringLiteral("agent")).toObject().toVariantMap();
        upsertAgent(agent);
        emit agentUpdated(agent);
    });
}

void AgentStore::clearError()
{
    setErrorMessage({});
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

void AgentStore::upsertAgent(const QVariantMap &agent)
{
    const QString agentId = agent.value(QStringLiteral("id")).toString();

    if (agentId.isEmpty()) {
        return;
    }

    QVariantList nextAgents = m_agents;

    for (qsizetype index = 0; index < nextAgents.size(); ++index) {
        if (nextAgents.at(index).toMap().value(QStringLiteral("id")).toString() == agentId) {
            nextAgents[index] = agent;
            setAgents(nextAgents);
            return;
        }
    }

    qsizetype insertIndex = 0;

    while (
        insertIndex < nextAgents.size()
        && nextAgents.at(insertIndex).toMap().value(QStringLiteral("isBuiltIn")).toBool()
    ) {
        ++insertIndex;
    }

    nextAgents.insert(insertIndex, agent);
    setAgents(nextAgents);
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

void AgentStore::setMutating(bool mutating)
{
    if (m_mutating == mutating) {
        return;
    }

    m_mutating = mutating;
    emit mutatingChanged();
}

void AgentStore::setErrorMessage(const QString &message)
{
    if (m_errorMessage == message) {
        return;
    }

    m_errorMessage = message;
    emit errorMessageChanged();
}

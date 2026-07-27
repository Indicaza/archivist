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

QVariantList withoutAgent(const QVariantList &agents, const QString &agentId)
{
    QVariantList filtered;
    filtered.reserve(agents.size());

    for (const QVariant &value : agents) {
        if (value.toMap().value(QStringLiteral("id")).toString() != agentId) {
            filtered.append(value);
        }
    }

    return filtered;
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

QVariantList AgentStore::archivedAgents() const
{
    return m_archivedAgents;
}

bool AgentStore::loading() const
{
    return m_loading;
}

bool AgentStore::loadingArchived() const
{
    return m_loadingArchived;
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

void AgentStore::refreshArchived()
{
    if (m_loadingArchived) {
        return;
    }

    clearError();
    setLoadingArchived(true);

    QNetworkReply *reply = m_network.get(requestFor(QStringLiteral("/agents/archived")));

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const JsonReplyResult result = consumeJsonReply(reply);
        reply->deleteLater();
        setLoadingArchived(false);

        if (!result.ok) {
            setErrorMessage(result.errorMessage);
            return;
        }

        setArchivedAgents(result.object.value(QStringLiteral("agents")).toArray().toVariantList());
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
        upsertActiveAgent(agent);
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

        if (agent.value(QStringLiteral("archivedAt")).toString().isEmpty()) {
            upsertActiveAgent(agent);
        } else {
            upsertArchivedAgent(agent);
        }

        emit agentUpdated(agent);
    });
}

void AgentStore::duplicateAgent(const QString &agentId)
{
    if (m_mutating || agentId.isEmpty()) {
        return;
    }

    clearError();
    setMutating(true);

    const QString path = QStringLiteral("/agents/%1/duplicate").arg(encodedPathSegment(agentId));
    QNetworkReply *reply = m_network.post(requestFor(path), QByteArrayLiteral("{}"));

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const JsonReplyResult result = consumeJsonReply(reply);
        reply->deleteLater();
        setMutating(false);

        if (!result.ok) {
            setErrorMessage(result.errorMessage);
            return;
        }

        const QVariantMap agent = result.object.value(QStringLiteral("agent")).toObject().toVariantMap();
        upsertActiveAgent(agent);
        emit agentDuplicated(agent);
    });
}

void AgentStore::archiveAgent(const QString &agentId)
{
    if (m_mutating || agentId.isEmpty()) {
        return;
    }

    clearError();
    setMutating(true);

    const QString path = QStringLiteral("/agents/%1/archive").arg(encodedPathSegment(agentId));
    QNetworkReply *reply = m_network.post(requestFor(path), QByteArray{});

    connect(reply, &QNetworkReply::finished, this, [this, reply, agentId]() {
        const JsonReplyResult result = consumeJsonReply(reply);
        reply->deleteLater();
        setMutating(false);

        if (!result.ok) {
            setErrorMessage(result.errorMessage);
            return;
        }

        const QVariantMap agent = result.object.value(QStringLiteral("agent")).toObject().toVariantMap();
        removeActiveAgent(agentId);
        upsertArchivedAgent(agent);
        emit agentArchived(agent);
    });
}

void AgentStore::restoreAgent(const QString &agentId)
{
    if (m_mutating || agentId.isEmpty()) {
        return;
    }

    clearError();
    setMutating(true);

    const QString path = QStringLiteral("/agents/%1/restore").arg(encodedPathSegment(agentId));
    QNetworkReply *reply = m_network.post(requestFor(path), QByteArray{});

    connect(reply, &QNetworkReply::finished, this, [this, reply, agentId]() {
        const JsonReplyResult result = consumeJsonReply(reply);
        reply->deleteLater();
        setMutating(false);

        if (!result.ok) {
            setErrorMessage(result.errorMessage);
            return;
        }

        const QVariantMap agent = result.object.value(QStringLiteral("agent")).toObject().toVariantMap();
        removeArchivedAgent(agentId);
        upsertActiveAgent(agent);
        emit agentRestored(agent);
    });
}

void AgentStore::deleteAgent(const QString &agentId)
{
    if (m_mutating || agentId.isEmpty()) {
        return;
    }

    clearError();
    setMutating(true);

    const QString path = QStringLiteral("/agents/%1").arg(encodedPathSegment(agentId));
    QNetworkReply *reply = m_network.sendCustomRequest(
        requestFor(path),
        QByteArrayLiteral("DELETE")
    );

    connect(reply, &QNetworkReply::finished, this, [this, reply, agentId]() {
        const JsonReplyResult result = consumeJsonReply(reply);
        reply->deleteLater();
        setMutating(false);

        if (!result.ok) {
            setErrorMessage(result.errorMessage);
            return;
        }

        removeActiveAgent(agentId);
        removeArchivedAgent(agentId);
        emit agentDeleted(
            agentId,
            result.object.value(QStringLiteral("reassignedChatCount")).toInt()
        );
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

    for (const QVariant &value : m_archivedAgents) {
        const QVariantMap agent = value.toMap();
        if (agent.value(QStringLiteral("id")).toString() == agentId) {
            return agent;
        }
    }

    return {};
}

void AgentStore::upsertActiveAgent(const QVariantMap &agent)
{
    const QString agentId = agent.value(QStringLiteral("id")).toString();

    if (agentId.isEmpty()) {
        return;
    }

    QVariantList nextAgents = withoutAgent(m_agents, agentId);
    qsizetype insertIndex = 0;

    while (
        insertIndex < nextAgents.size()
        && nextAgents.at(insertIndex).toMap().value(QStringLiteral("isBuiltIn")).toBool()
    ) {
        ++insertIndex;
    }

    if (agent.value(QStringLiteral("isBuiltIn")).toBool()) {
        nextAgents.prepend(agent);
    } else {
        nextAgents.insert(insertIndex, agent);
    }

    setAgents(nextAgents);
}

void AgentStore::upsertArchivedAgent(const QVariantMap &agent)
{
    const QString agentId = agent.value(QStringLiteral("id")).toString();

    if (agentId.isEmpty()) {
        return;
    }

    QVariantList nextAgents = withoutAgent(m_archivedAgents, agentId);
    nextAgents.prepend(agent);
    setArchivedAgents(nextAgents);
}

void AgentStore::removeActiveAgent(const QString &agentId)
{
    setAgents(withoutAgent(m_agents, agentId));
}

void AgentStore::removeArchivedAgent(const QString &agentId)
{
    setArchivedAgents(withoutAgent(m_archivedAgents, agentId));
}

void AgentStore::setAgents(const QVariantList &agents)
{
    if (m_agents == agents) {
        return;
    }

    m_agents = agents;
    emit agentsChanged();
}

void AgentStore::setArchivedAgents(const QVariantList &agents)
{
    if (m_archivedAgents == agents) {
        return;
    }

    m_archivedAgents = agents;
    emit archivedAgentsChanged();
}

void AgentStore::setLoading(bool loading)
{
    if (m_loading == loading) {
        return;
    }

    m_loading = loading;
    emit loadingChanged();
}

void AgentStore::setLoadingArchived(bool loading)
{
    if (m_loadingArchived == loading) {
        return;
    }

    m_loadingArchived = loading;
    emit loadingArchivedChanged();
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

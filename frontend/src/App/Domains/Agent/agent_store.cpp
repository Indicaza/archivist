#include "agent_store.h"

#include "Core/Domains/Agents/agent_repository.h"
#include "Core/core_runtime.h"

namespace
{
const QString coreUnavailableMessage = QStringLiteral(
    "Archivist's data layer is not running. Restart the app."
);

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

void AgentStore::refresh()
{
    if (m_loading) {
        return;
    }

    CoreRuntime *core = CoreRuntime::active();

    if (!core) {
        setErrorMessage(coreUnavailableMessage);
        return;
    }

    clearError();
    setLoading(true);

    core->read(
        [](CoreDatabase &database) { return QVariant(AgentRepository::activeAgents(database)); },
        this,
        [this](const CoreResult &result) {
            setLoading(false);

            if (!result.ok) {
                setErrorMessage(result.errorMessage);
                return;
            }

            setAgents(result.value.toList());
        }
    );
}

void AgentStore::refreshArchived()
{
    if (m_loadingArchived) {
        return;
    }

    CoreRuntime *core = CoreRuntime::active();

    if (!core) {
        setErrorMessage(coreUnavailableMessage);
        return;
    }

    clearError();
    setLoadingArchived(true);

    core->read(
        [](CoreDatabase &database) { return QVariant(AgentRepository::archivedAgents(database)); },
        this,
        [this](const CoreResult &result) {
            setLoadingArchived(false);

            if (!result.ok) {
                setErrorMessage(result.errorMessage);
                return;
            }

            setArchivedAgents(result.value.toList());
        }
    );
}

void AgentStore::mutate(CoreJob job, CoreDone done)
{
    CoreRuntime *core = CoreRuntime::active();

    if (!core) {
        setErrorMessage(coreUnavailableMessage);
        return;
    }

    clearError();
    setMutating(true);

    core->write(std::move(job), this, [this, done = std::move(done)](const CoreResult &result) {
        setMutating(false);

        if (!result.ok) {
            setErrorMessage(result.errorMessage);
            return;
        }

        done(result.value);
    });
}

void AgentStore::createAgent(const QVariantMap &input)
{
    if (m_mutating) {
        return;
    }

    mutate(
        [input](CoreDatabase &database) { return QVariant(AgentRepository::createAgent(database, input)); },
        [this](const QVariant &value) {
            const QVariantMap agent = value.toMap();
            upsertActiveAgent(agent);
            emit agentCreated(agent);
        }
    );
}

void AgentStore::updateAgent(const QString &agentId, const QVariantMap &input)
{
    if (m_mutating || agentId.isEmpty()) {
        return;
    }

    mutate(
        [agentId, input](CoreDatabase &database) {
            return QVariant(AgentRepository::updateAgent(database, agentId, input));
        },
        [this](const QVariant &value) {
            const QVariantMap agent = value.toMap();

            if (agent.value(QStringLiteral("archivedAt")).toString().isEmpty()) {
                upsertActiveAgent(agent);
            } else {
                upsertArchivedAgent(agent);
            }

            emit agentUpdated(agent);
        }
    );
}

void AgentStore::duplicateAgent(const QString &agentId)
{
    if (m_mutating || agentId.isEmpty()) {
        return;
    }

    mutate(
        [agentId](CoreDatabase &database) { return QVariant(AgentRepository::duplicateAgent(database, agentId)); },
        [this](const QVariant &value) {
            const QVariantMap agent = value.toMap();
            upsertActiveAgent(agent);
            emit agentDuplicated(agent);
        }
    );
}

void AgentStore::archiveAgent(const QString &agentId)
{
    if (m_mutating || agentId.isEmpty()) {
        return;
    }

    mutate(
        [agentId](CoreDatabase &database) { return QVariant(AgentRepository::archiveAgent(database, agentId)); },
        [this, agentId](const QVariant &value) {
            const QVariantMap agent = value.toMap();
            removeActiveAgent(agentId);
            upsertArchivedAgent(agent);
            emit agentArchived(agent);
        }
    );
}

void AgentStore::restoreAgent(const QString &agentId)
{
    if (m_mutating || agentId.isEmpty()) {
        return;
    }

    mutate(
        [agentId](CoreDatabase &database) { return QVariant(AgentRepository::restoreAgent(database, agentId)); },
        [this, agentId](const QVariant &value) {
            const QVariantMap agent = value.toMap();
            removeArchivedAgent(agentId);
            upsertActiveAgent(agent);
            emit agentRestored(agent);
        }
    );
}

void AgentStore::deleteAgent(const QString &agentId)
{
    if (m_mutating || agentId.isEmpty()) {
        return;
    }

    mutate(
        [agentId](CoreDatabase &database) { return QVariant(AgentRepository::deleteAgent(database, agentId)); },
        [this, agentId](const QVariant &value) {
            removeActiveAgent(agentId);
            removeArchivedAgent(agentId);
            emit agentDeleted(agentId, value.toInt());
        }
    );
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

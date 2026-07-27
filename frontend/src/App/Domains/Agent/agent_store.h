#pragma once

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

class AgentStore final : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariantList agents READ agents NOTIFY agentsChanged)
    Q_PROPERTY(QVariantList archivedAgents READ archivedAgents NOTIFY archivedAgentsChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool loadingArchived READ loadingArchived NOTIFY loadingArchivedChanged)
    Q_PROPERTY(bool mutating READ mutating NOTIFY mutatingChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)

public:
    explicit AgentStore(QObject *parent = nullptr);

    [[nodiscard]] QVariantList agents() const;
    [[nodiscard]] QVariantList archivedAgents() const;
    [[nodiscard]] bool loading() const;
    [[nodiscard]] bool loadingArchived() const;
    [[nodiscard]] bool mutating() const;
    [[nodiscard]] QString errorMessage() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void refreshArchived();
    Q_INVOKABLE void createAgent(const QVariantMap &input);
    Q_INVOKABLE void updateAgent(const QString &agentId, const QVariantMap &input);
    Q_INVOKABLE void duplicateAgent(const QString &agentId);
    Q_INVOKABLE void archiveAgent(const QString &agentId);
    Q_INVOKABLE void restoreAgent(const QString &agentId);
    Q_INVOKABLE void deleteAgent(const QString &agentId);
    Q_INVOKABLE void clearError();
    Q_INVOKABLE QVariantMap agentById(const QString &agentId) const;

signals:
    void agentsChanged();
    void archivedAgentsChanged();
    void loadingChanged();
    void loadingArchivedChanged();
    void mutatingChanged();
    void errorMessageChanged();
    void agentCreated(const QVariantMap &agent);
    void agentUpdated(const QVariantMap &agent);
    void agentDuplicated(const QVariantMap &agent);
    void agentArchived(const QVariantMap &agent);
    void agentRestored(const QVariantMap &agent);
    void agentDeleted(const QString &agentId, int reassignedChatCount);

private:
    [[nodiscard]] QNetworkRequest requestFor(const QString &path) const;
    void upsertActiveAgent(const QVariantMap &agent);
    void upsertArchivedAgent(const QVariantMap &agent);
    void removeActiveAgent(const QString &agentId);
    void removeArchivedAgent(const QString &agentId);
    void setAgents(const QVariantList &agents);
    void setArchivedAgents(const QVariantList &agents);
    void setLoading(bool loading);
    void setLoadingArchived(bool loading);
    void setMutating(bool mutating);
    void setErrorMessage(const QString &message);

    QNetworkAccessManager m_network;
    QUrl m_baseUrl;
    QVariantList m_agents;
    QVariantList m_archivedAgents;
    bool m_loading = false;
    bool m_loadingArchived = false;
    bool m_mutating = false;
    QString m_errorMessage;
};

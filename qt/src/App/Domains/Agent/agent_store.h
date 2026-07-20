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
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)

public:
    explicit AgentStore(QObject *parent = nullptr);

    [[nodiscard]] QVariantList agents() const;
    [[nodiscard]] bool loading() const;
    [[nodiscard]] QString errorMessage() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE QVariantMap agentById(const QString &agentId) const;

signals:
    void agentsChanged();
    void loadingChanged();
    void errorMessageChanged();

private:
    [[nodiscard]] QNetworkRequest requestFor(const QString &path) const;
    void setAgents(const QVariantList &agents);
    void setLoading(bool loading);
    void setErrorMessage(const QString &message);

    QNetworkAccessManager m_network;
    QUrl m_baseUrl;
    QVariantList m_agents;
    bool m_loading = false;
    QString m_errorMessage;
};

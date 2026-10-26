#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class QNetworkAccessManager;
class OpenAIModelList;

// Backs the Model section of the Agent editor (QML singleton "AISettings"):
// the OpenAI key kept in the Keychain, the local model server address, and
// the model lists discovered from each provider.
class AISettings final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool openAIKeyConfigured READ openAIKeyConfigured NOTIFY changed)
    // "environment", "env-file", "app" or "" (no key).
    Q_PROPERTY(QString openAIKeySource READ openAIKeySource NOTIFY changed)
    Q_PROPERTY(QString keyLocation READ keyLocation CONSTANT)
    Q_PROPERTY(QString localBaseUrl READ localBaseUrl NOTIFY changed)
    Q_PROPERTY(QString defaultLocalBaseUrl READ defaultLocalBaseUrl CONSTANT)
    Q_PROPERTY(QVariantList providers READ providers CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY messageChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY messageChanged)
    // Bumped whenever a model list changes, so QML bindings re-read models().
    Q_PROPERTY(int modelRevision READ modelRevision NOTIFY modelsChanged)

public:
    explicit AISettings(QObject *parent = nullptr);

    void setDatabasePath(const QString &databasePath);

    [[nodiscard]] bool openAIKeyConfigured() const;
    [[nodiscard]] QString openAIKeySource() const;
    [[nodiscard]] QString keyLocation() const;
    [[nodiscard]] QString localBaseUrl() const;
    [[nodiscard]] QString defaultLocalBaseUrl() const;
    [[nodiscard]] QVariantList providers() const;
    [[nodiscard]] bool busy() const;
    [[nodiscard]] QString statusMessage() const;
    [[nodiscard]] QString errorMessage() const;
    [[nodiscard]] int modelRevision() const;

    // [{modelId, displayName, status}] for the provider, newest discovery.
    Q_INVOKABLE QVariantList models(const QString &provider) const;
    // {status: "connected"|"unavailable", message, checkedAt} or empty.
    Q_INVOKABLE QVariantMap health(const QString &provider) const;

    Q_INVOKABLE bool saveOpenAIKey(const QString &key);
    Q_INVOKABLE void removeOpenAIKey();
    Q_INVOKABLE void setLocalBaseUrl(const QString &url);
    // Re-reads the key source and asks the provider for its models.
    Q_INVOKABLE void refreshModels(const QString &provider);
    Q_INVOKABLE void reload();

signals:
    void changed();
    void busyChanged();
    void messageChanged();
    void modelsChanged();

private:
    void setMessages(const QString &status, const QString &error);
    void setBusy(bool busy);
    [[nodiscard]] QString cachePath() const;

    QString m_databasePath;
    QString m_keySource;
    QString m_statusMessage;
    QString m_errorMessage;
    int m_modelRevision = 0;
    bool m_busy = false;
    QNetworkAccessManager *m_network = nullptr;
    QPointer<OpenAIModelList> m_request;
};

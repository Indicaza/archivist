#pragma once

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

class CollectionStore final : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariantList collections READ collections NOTIFY collectionsChanged)
    Q_PROPERTY(QVariantList archivedCollections READ archivedCollections NOTIFY archivedCollectionsChanged)
    Q_PROPERTY(QString selectedCollectionId READ selectedCollectionId NOTIFY selectedCollectionIdChanged)
    Q_PROPERTY(QVariantMap selectedCollection READ selectedCollection NOTIFY selectedCollectionChanged)
    Q_PROPERTY(QVariantMap scope READ scope NOTIFY scopeChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool loadingArchived READ loadingArchived NOTIFY loadingArchivedChanged)
    Q_PROPERTY(bool mutating READ mutating NOTIFY mutatingChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)

public:
    explicit CollectionStore(QObject *parent = nullptr);

    [[nodiscard]] QVariantList collections() const;
    [[nodiscard]] QVariantList archivedCollections() const;
    [[nodiscard]] QString selectedCollectionId() const;
    [[nodiscard]] QVariantMap selectedCollection() const;
    [[nodiscard]] QVariantMap scope() const;
    [[nodiscard]] bool loading() const;
    [[nodiscard]] bool loadingArchived() const;
    [[nodiscard]] bool mutating() const;
    [[nodiscard]] QString errorMessage() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void refreshArchived();
    Q_INVOKABLE void selectCollection(const QString &collectionId);
    Q_INVOKABLE void createCollection(
        const QString &name,
        const QString &parentCollectionId,
        const QVariantList &libraryIds,
        const QVariantList &chatIds,
        const QVariantList &agentIds,
        const QString &defaultAgentId
    );
    Q_INVOKABLE void updateCollection(
        const QString &collectionId,
        const QString &name,
        const QString &parentCollectionId,
        const QVariantList &libraryIds,
        const QVariantList &chatIds,
        const QVariantList &agentIds,
        const QString &defaultAgentId
    );
    Q_INVOKABLE void archiveCollection(const QString &collectionId);
    Q_INVOKABLE void restoreCollection(const QString &collectionId);
    Q_INVOKABLE bool includesLibrary(const QString &libraryId) const;
    Q_INVOKABLE bool includesChat(const QString &chatId) const;
    Q_INVOKABLE bool isRosterAgent(const QString &agentId) const;

signals:
    void collectionsChanged();
    void archivedCollectionsChanged();
    void selectedCollectionIdChanged();
    void selectedCollectionChanged();
    void scopeChanged();
    void loadingChanged();
    void loadingArchivedChanged();
    void mutatingChanged();
    void errorMessageChanged();
    void collectionCreated(const QVariantMap &collection);
    void collectionUpdated(const QVariantMap &collection);
    void collectionArchived(const QVariantMap &collection);
    void collectionRestored(const QVariantMap &collection);
    void workspaceScopeChanged();

private:
    [[nodiscard]] QNetworkRequest requestFor(const QString &path) const;
    void fetchAppState();
    void fetchSelectedScope();
    void setCollections(const QVariantList &collections);
    void setArchivedCollections(const QVariantList &collections);
    void setSelectedCollectionId(const QString &collectionId);
    void setScope(const QVariantMap &scope);
    void setLoading(bool loading);
    void setLoadingArchived(bool loading);
    void setMutating(bool mutating);
    void setErrorMessage(const QString &message);
    [[nodiscard]] bool containsCollection(const QString &collectionId) const;
    [[nodiscard]] bool scopeContains(const QString &key, const QString &id) const;
    void upsertCollection(const QVariantMap &collection);
    void removeCollection(const QString &collectionId);
    void upsertArchivedCollection(const QVariantMap &collection);
    void removeArchivedCollection(const QString &collectionId);

    QNetworkAccessManager m_network;
    QUrl m_baseUrl;
    QVariantList m_collections;
    QVariantList m_archivedCollections;
    QString m_selectedCollectionId;
    QVariantMap m_scope;
    bool m_loading = false;
    bool m_loadingArchived = false;
    bool m_mutating = false;
    QString m_errorMessage;
};

#pragma once

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

class ChatStore final : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariantList chats READ chats NOTIFY chatsChanged)
    Q_PROPERTY(QVariantList archivedChats READ archivedChats NOTIFY archivedChatsChanged)
    Q_PROPERTY(QString selectedChatId READ selectedChatId NOTIFY selectedChatIdChanged)
    Q_PROPERTY(QVariantMap selectedChat READ selectedChat NOTIFY selectedChatChanged)
    Q_PROPERTY(QVariantList messages READ messages NOTIFY messagesChanged)
    Q_PROPERTY(QVariantList attachments READ attachments NOTIFY attachmentsChanged)
    Q_PROPERTY(QVariantList lastSources READ lastSources NOTIFY completionMetadataChanged)
    Q_PROPERTY(QVariantMap inspectedContext READ inspectedContext NOTIFY inspectedContextChanged)
    Q_PROPERTY(QString inspectedMessageId READ inspectedMessageId NOTIFY inspectedContextChanged)
    Q_PROPERTY(bool loadingChats READ loadingChats NOTIFY loadingChatsChanged)
    Q_PROPERTY(bool loadingArchivedChats READ loadingArchivedChats NOTIFY loadingArchivedChatsChanged)
    Q_PROPERTY(bool loadingMessages READ loadingMessages NOTIFY loadingMessagesChanged)
    Q_PROPERTY(bool loadingAttachments READ loadingAttachments NOTIFY loadingAttachmentsChanged)
    Q_PROPERTY(bool loadingOlderMessages READ loadingOlderMessages NOTIFY loadingOlderMessagesChanged)
    Q_PROPERTY(bool hasOlderMessages READ hasOlderMessages NOTIFY hasOlderMessagesChanged)
    Q_PROPERTY(bool responding READ responding NOTIFY respondingChanged)
    Q_PROPERTY(bool assigningAgent READ assigningAgent NOTIFY assigningAgentChanged)
    Q_PROPERTY(bool mutating READ mutating NOTIFY mutatingChanged)
    Q_PROPERTY(bool mutatingAttachment READ mutatingAttachment NOTIFY mutatingAttachmentChanged)
    Q_PROPERTY(bool loadingContext READ loadingContext NOTIFY loadingContextChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(QString contextErrorMessage READ contextErrorMessage NOTIFY contextErrorMessageChanged)
    Q_PROPERTY(QString lastProvider READ lastProvider NOTIFY completionMetadataChanged)
    Q_PROPERTY(QString lastModel READ lastModel NOTIFY completionMetadataChanged)

public:
    explicit ChatStore(QObject *parent = nullptr);

    [[nodiscard]] QVariantList chats() const;
    [[nodiscard]] QVariantList archivedChats() const;
    [[nodiscard]] QString selectedChatId() const;
    [[nodiscard]] QVariantMap selectedChat() const;
    [[nodiscard]] QVariantList messages() const;
    [[nodiscard]] QVariantList attachments() const;
    [[nodiscard]] QVariantList lastSources() const;
    [[nodiscard]] QVariantMap inspectedContext() const;
    [[nodiscard]] QString inspectedMessageId() const;
    [[nodiscard]] bool loadingChats() const;
    [[nodiscard]] bool loadingArchivedChats() const;
    [[nodiscard]] bool loadingMessages() const;
    [[nodiscard]] bool loadingAttachments() const;
    [[nodiscard]] bool loadingOlderMessages() const;
    [[nodiscard]] bool hasOlderMessages() const;
    [[nodiscard]] bool responding() const;
    [[nodiscard]] bool assigningAgent() const;
    [[nodiscard]] bool mutating() const;
    [[nodiscard]] bool mutatingAttachment() const;
    [[nodiscard]] bool loadingContext() const;
    [[nodiscard]] QString errorMessage() const;
    [[nodiscard]] QString contextErrorMessage() const;
    [[nodiscard]] QString lastProvider() const;
    [[nodiscard]] QString lastModel() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void refreshArchived();
    Q_INVOKABLE void createChat(const QString &libraryId);
    Q_INVOKABLE void selectChat(const QString &chatId);
    Q_INVOKABLE void refreshSelectedMessages();
    Q_INVOKABLE void refreshSelectedAttachments();
    Q_INVOKABLE void loadOlderMessages();
    Q_INVOKABLE void sendMessage(const QString &content);
    Q_INVOKABLE void finishMessageReveal(const QString &messageId);
    Q_INVOKABLE void assignAgentToSelectedChat(const QString &agentId);
    Q_INVOKABLE void attachFile(const QString &libraryId, const QString &fileId);
    Q_INVOKABLE void removeAttachment(const QString &attachmentId);
    Q_INVOKABLE void loadMessageContext(const QString &messageId);
    Q_INVOKABLE void clearInspectedContext();
    Q_INVOKABLE void updateChat(const QString &chatId, const QVariantMap &input);
    Q_INVOKABLE void archiveChat(const QString &chatId);
    Q_INVOKABLE void restoreChat(const QString &chatId);
    Q_INVOKABLE void deleteChat(const QString &chatId);
    Q_INVOKABLE void clearError();

signals:
    void chatsChanged();
    void archivedChatsChanged();
    void selectedChatIdChanged();
    void selectedChatChanged();
    void messagesChanged();
    void attachmentsChanged();
    void olderMessagesWillPrepend(int count);
    void olderMessagesPrepended(int count);
    void loadingChatsChanged();
    void loadingArchivedChatsChanged();
    void loadingMessagesChanged();
    void loadingAttachmentsChanged();
    void loadingOlderMessagesChanged();
    void hasOlderMessagesChanged();
    void respondingChanged();
    void assigningAgentChanged();
    void mutatingChanged();
    void mutatingAttachmentChanged();
    void errorMessageChanged();
    void completionMetadataChanged();
    void inspectedContextChanged();
    void loadingContextChanged();
    void contextErrorMessageChanged();
    void attachmentAdded(const QVariantMap &attachment);
    void attachmentRemoved(const QString &attachmentId);
    void chatCreated(const QVariantMap &chat);
    void chatUpdated(const QVariantMap &chat);
    void chatArchived(const QVariantMap &chat);
    void chatRestored(const QVariantMap &chat);
    void chatDeleted(const QString &chatId);

private:
    [[nodiscard]] QNetworkRequest requestFor(const QString &path) const;
    void fetchAppState();
    void setChats(const QVariantList &chats);
    void setArchivedChats(const QVariantList &chats);
    void setSelectedChatId(const QString &chatId);
    void setMessages(const QVariantList &messages);
    void setAttachments(const QVariantList &attachments);
    void setLoadingChats(bool loading);
    void setLoadingArchivedChats(bool loading);
    void setLoadingMessages(bool loading);
    void setLoadingAttachments(bool loading);
    void setLoadingOlderMessages(bool loading);
    void setHasOlderMessages(bool hasOlderMessages);
    void setResponding(bool responding);
    void setAssigningAgent(bool assigning);
    void setMutating(bool mutating);
    void setMutatingAttachment(bool mutating);
    void setLoadingContext(bool loading);
    void setErrorMessage(const QString &message);
    void setContextErrorMessage(const QString &message);
    void setInspectedContext(const QString &messageId, const QVariantMap &context);
    void setCompletionMetadata(
        const QString &provider,
        const QString &model,
        const QVariantList &sources = {}
    );
    void setMessagePageState(bool hasMore, const QString &beforeMessageId);
    void resetMessagePageState();
    void promoteSelectedChat();
    void upsertActiveChat(const QVariantMap &chat);
    void upsertArchivedChat(const QVariantMap &chat);
    void removeActiveChat(const QString &chatId);
    void removeArchivedChat(const QString &chatId);
    void replaceChat(const QVariantMap &chat);
    void upsertAttachment(const QVariantMap &attachment);
    void removeAttachmentFromList(const QString &attachmentId);
    [[nodiscard]] bool containsChat(const QString &chatId) const;

    QNetworkAccessManager m_network;
    QUrl m_baseUrl;
    QVariantList m_chats;
    QVariantList m_archivedChats;
    QString m_selectedChatId;
    QVariantList m_messages;
    QVariantList m_attachments;
    QVariantList m_lastSources;
    QVariantMap m_inspectedContext;
    QString m_inspectedMessageId;
    QString m_beforeMessageId;
    bool m_loadingChats = false;
    bool m_loadingArchivedChats = false;
    bool m_loadingMessages = false;
    bool m_loadingAttachments = false;
    bool m_loadingOlderMessages = false;
    bool m_hasOlderMessages = false;
    bool m_responding = false;
    bool m_assigningAgent = false;
    bool m_mutating = false;
    bool m_mutatingAttachment = false;
    bool m_loadingContext = false;
    QString m_errorMessage;
    QString m_contextErrorMessage;
    QString m_lastProvider;
    QString m_lastModel;
};

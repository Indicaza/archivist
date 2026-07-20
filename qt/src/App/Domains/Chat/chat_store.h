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
    Q_PROPERTY(QString selectedChatId READ selectedChatId NOTIFY selectedChatIdChanged)
    Q_PROPERTY(QVariantMap selectedChat READ selectedChat NOTIFY selectedChatChanged)
    Q_PROPERTY(QVariantList messages READ messages NOTIFY messagesChanged)
    Q_PROPERTY(bool loadingChats READ loadingChats NOTIFY loadingChatsChanged)
    Q_PROPERTY(bool loadingMessages READ loadingMessages NOTIFY loadingMessagesChanged)
    Q_PROPERTY(bool responding READ responding NOTIFY respondingChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(QString lastProvider READ lastProvider NOTIFY completionMetadataChanged)
    Q_PROPERTY(QString lastModel READ lastModel NOTIFY completionMetadataChanged)

public:
    explicit ChatStore(QObject *parent = nullptr);

    [[nodiscard]] QVariantList chats() const;
    [[nodiscard]] QString selectedChatId() const;
    [[nodiscard]] QVariantMap selectedChat() const;
    [[nodiscard]] QVariantList messages() const;
    [[nodiscard]] bool loadingChats() const;
    [[nodiscard]] bool loadingMessages() const;
    [[nodiscard]] bool responding() const;
    [[nodiscard]] QString errorMessage() const;
    [[nodiscard]] QString lastProvider() const;
    [[nodiscard]] QString lastModel() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void selectChat(const QString &chatId);
    Q_INVOKABLE void refreshSelectedMessages();
    Q_INVOKABLE void sendMessage(const QString &content);

signals:
    void chatsChanged();
    void selectedChatIdChanged();
    void selectedChatChanged();
    void messagesChanged();
    void loadingChatsChanged();
    void loadingMessagesChanged();
    void respondingChanged();
    void errorMessageChanged();
    void completionMetadataChanged();

private:
    [[nodiscard]] QNetworkRequest requestFor(const QString &path) const;
    void fetchAppState();
    void setChats(const QVariantList &chats);
    void setSelectedChatId(const QString &chatId);
    void setMessages(const QVariantList &messages);
    void setLoadingChats(bool loading);
    void setLoadingMessages(bool loading);
    void setResponding(bool responding);
    void setErrorMessage(const QString &message);
    void setCompletionMetadata(const QString &provider, const QString &model);
    void promoteSelectedChat();
    [[nodiscard]] bool containsChat(const QString &chatId) const;

    QNetworkAccessManager m_network;
    QUrl m_baseUrl;
    QVariantList m_chats;
    QString m_selectedChatId;
    QVariantList m_messages;
    bool m_loadingChats = false;
    bool m_loadingMessages = false;
    bool m_responding = false;
    QString m_errorMessage;
    QString m_lastProvider;
    QString m_lastModel;
};

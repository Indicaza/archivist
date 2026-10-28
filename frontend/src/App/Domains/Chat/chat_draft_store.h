#pragma once

#include "Core/Domains/Chats/chat_drafts.h"

#include <QObject>
#include <QString>
#include <QTimer>

class QSettings;

// Unsent composer text, kept per conversation and across restarts.
//
// The composer reads the selected chat's draft when the selection changes
// and writes back as the person types. Saving is debounced; a pending save
// is flushed on destruction.
class ChatDraftStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY draftsChanged)

public:
    // The key for text typed while no conversation is selected.
    static inline const QString NoChatKey = QStringLiteral("none");

    explicit ChatDraftStore(QSettings *settings = nullptr, QObject *parent = nullptr);
    ~ChatDraftStore() override;

    [[nodiscard]] int count() const;

    Q_INVOKABLE QString draft(const QString &chatId) const;
    Q_INVOKABLE bool hasDraft(const QString &chatId) const;
    Q_INVOKABLE void setDraft(const QString &chatId, const QString &text);
    Q_INVOKABLE void clearDraft(const QString &chatId);
    // Moves the no-chat draft onto a chat that was just created for it.
    Q_INVOKABLE void adoptDraft(const QString &fromChatId, const QString &toChatId);

    void flush();

signals:
    void draftsChanged();
    void draftChanged(const QString &chatId);

private:
    static QString keyFor(const QString &chatId);
    void scheduleSave();
    void load();

    QSettings *m_settings = nullptr;
    bool m_ownsSettings = false;
    Chats::DraftBook m_book;
    QTimer m_saveTimer;
    bool m_dirty = false;
};

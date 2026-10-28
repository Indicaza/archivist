#include "App/Domains/Chat/chat_draft_store.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>

namespace
{
const QString SettingsKey = QStringLiteral("chat/drafts");
constexpr int SaveDelayMs = 400;
} // namespace

ChatDraftStore::ChatDraftStore(QSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    if (!m_settings) {
        m_settings = new QSettings();
        m_ownsSettings = true;
    }
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(SaveDelayMs);
    connect(&m_saveTimer, &QTimer::timeout, this, &ChatDraftStore::flush);
    load();
}

ChatDraftStore::~ChatDraftStore()
{
    flush();
    if (m_ownsSettings) {
        delete m_settings;
    }
}

QString ChatDraftStore::keyFor(const QString &chatId)
{
    const QString trimmed = chatId.trimmed();
    return trimmed.isEmpty() ? NoChatKey : trimmed;
}

int ChatDraftStore::count() const
{
    return static_cast<int>(m_book.drafts().size());
}

QString ChatDraftStore::draft(const QString &chatId) const
{
    return m_book.text(keyFor(chatId));
}

bool ChatDraftStore::hasDraft(const QString &chatId) const
{
    return m_book.has(keyFor(chatId));
}

void ChatDraftStore::setDraft(const QString &chatId, const QString &text)
{
    const QString key = keyFor(chatId);
    const bool existed = m_book.has(key);
    if (!m_book.set(key, text)) {
        return;
    }
    scheduleSave();
    emit draftChanged(key);
    if (existed != m_book.has(key)) {
        emit draftsChanged();
    }
}

void ChatDraftStore::clearDraft(const QString &chatId)
{
    const QString key = keyFor(chatId);
    if (!m_book.clear(key)) {
        return;
    }
    scheduleSave();
    emit draftChanged(key);
    emit draftsChanged();
}

void ChatDraftStore::adoptDraft(const QString &fromChatId, const QString &toChatId)
{
    const QString from = keyFor(fromChatId);
    const QString to = keyFor(toChatId);
    if (!m_book.rename(from, to)) {
        return;
    }
    scheduleSave();
    emit draftChanged(from);
    emit draftChanged(to);
    emit draftsChanged();
}

void ChatDraftStore::scheduleSave()
{
    m_dirty = true;
    m_saveTimer.start();
}

void ChatDraftStore::flush()
{
    m_saveTimer.stop();
    if (!m_dirty || !m_settings) {
        return;
    }
    m_dirty = false;
    if (m_book.drafts().isEmpty()) {
        m_settings->remove(SettingsKey);
    } else {
        const QJsonArray array = QJsonArray::fromVariantList(m_book.toVariant());
        m_settings->setValue(
            SettingsKey,
            QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact))
        );
    }
    m_settings->sync();
}

void ChatDraftStore::load()
{
    const QByteArray raw = m_settings->value(SettingsKey).toString().toUtf8();
    if (raw.isEmpty()) {
        return;
    }
    const QJsonDocument document = QJsonDocument::fromJson(raw);
    if (!document.isArray()) {
        return;
    }
    m_book = Chats::DraftBook::fromVariant(document.array().toVariantList());
}

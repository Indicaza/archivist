#include "Core/Domains/Chats/chat_drafts.h"

#include <QDateTime>
#include <QSet>
#include <QVariantMap>

#include <algorithm>

namespace Chats
{
namespace
{
bool blank(const QString &text)
{
    return text.trimmed().isEmpty();
}
} // namespace

DraftBook::DraftBook(int capacity)
    : m_capacity(std::max(1, capacity))
{
}

int DraftBook::indexOf(const QString &key) const
{
    for (int index = 0; index < m_drafts.size(); ++index) {
        if (m_drafts.at(index).key == key) {
            return index;
        }
    }
    return -1;
}

QString DraftBook::text(const QString &key) const
{
    const int index = indexOf(key);
    return index < 0 ? QString() : m_drafts.at(index).text;
}

bool DraftBook::has(const QString &key) const
{
    return indexOf(key) >= 0;
}

bool DraftBook::set(const QString &key, const QString &input, qint64 now)
{
    if (key.trimmed().isEmpty()) {
        return false;
    }
    if (blank(input)) {
        return clear(key);
    }

    const QString text = input.size() > MaxLength ? input.left(MaxLength) : input;
    const int index = indexOf(key);
    if (index >= 0 && m_drafts.at(index).text == text) {
        return false;
    }
    if (index >= 0) {
        m_drafts.removeAt(index);
    }

    Draft draft;
    draft.key = key;
    draft.text = text;
    draft.updatedAt = now > 0 ? now : QDateTime::currentMSecsSinceEpoch();
    m_drafts.prepend(draft);
    while (m_drafts.size() > m_capacity) {
        m_drafts.removeLast();
    }
    return true;
}

bool DraftBook::clear(const QString &key)
{
    const int index = indexOf(key);
    if (index < 0) {
        return false;
    }
    m_drafts.removeAt(index);
    return true;
}

bool DraftBook::rename(const QString &from, const QString &to)
{
    const int index = indexOf(from);
    if (index < 0 || from == to || to.trimmed().isEmpty()) {
        return false;
    }
    Draft draft = m_drafts.takeAt(index);
    // A draft already on the target wins: it's the newer intent there.
    if (indexOf(to) >= 0) {
        return true;
    }
    draft.key = to;
    m_drafts.insert(std::min(index, static_cast<int>(m_drafts.size())), draft);
    return true;
}

QVariantList DraftBook::toVariant() const
{
    QVariantList list;
    for (const Draft &draft : m_drafts) {
        list.append(QVariantMap{
            {QStringLiteral("key"), draft.key},
            {QStringLiteral("text"), draft.text},
            {QStringLiteral("updatedAt"), draft.updatedAt},
        });
    }
    return list;
}

DraftBook DraftBook::fromVariant(const QVariantList &value, int capacity)
{
    DraftBook book(capacity);
    QSet<QString> seen;
    for (const QVariant &entry : value) {
        const QVariantMap map = entry.toMap();
        Draft draft;
        draft.key = map.value(QStringLiteral("key")).toString();
        draft.text = map.value(QStringLiteral("text")).toString();
        draft.updatedAt = map.value(QStringLiteral("updatedAt")).toLongLong();
        if (draft.key.trimmed().isEmpty() || blank(draft.text) || seen.contains(draft.key)) {
            continue;
        }
        if (draft.text.size() > MaxLength) {
            draft.text = draft.text.left(MaxLength);
        }
        seen.insert(draft.key);
        book.m_drafts.append(draft);
        if (book.m_drafts.size() >= book.m_capacity) {
            break;
        }
    }
    return book;
}
} // namespace Chats

#include "Core/Domains/Shell/recent_items.h"

#include <QDateTime>
#include <QSet>

#include <algorithm>

namespace Shell
{
namespace
{
constexpr int MaxTextLength = 240;
constexpr int MaxDetails = 12;

QString clipped(const QString &value)
{
    const QString trimmed = value.trimmed();
    return trimmed.size() > MaxTextLength ? trimmed.left(MaxTextLength) : trimmed;
}

// Recents are saved on every change, so they stay small: scalar details only.
QVariantMap scalarDetails(const QVariantMap &details)
{
    QVariantMap result;
    for (auto it = details.cbegin(); it != details.cend() && result.size() < MaxDetails; ++it) {
        const QMetaType::Type type = static_cast<QMetaType::Type>(it.value().typeId());
        switch (type) {
        case QMetaType::QString:
            result.insert(it.key(), clipped(it.value().toString()));
            break;
        case QMetaType::Bool:
        case QMetaType::Int:
        case QMetaType::UInt:
        case QMetaType::LongLong:
        case QMetaType::ULongLong:
        case QMetaType::Double:
            result.insert(it.key(), it.value());
            break;
        default:
            break;
        }
    }
    return result;
}
} // namespace

QVariantMap RecentItem::toVariant() const
{
    return {
        {QStringLiteral("kind"), kind},
        {QStringLiteral("id"), id},
        {QStringLiteral("collectionId"), collectionId},
        {QStringLiteral("libraryId"), libraryId},
        {QStringLiteral("title"), title},
        {QStringLiteral("subtitle"), subtitle},
        {QStringLiteral("details"), details},
        {QStringLiteral("accessedAt"), accessedAt},
    };
}

RecentItem RecentItem::fromVariant(const QVariantMap &value)
{
    RecentItem item;
    item.kind = value.value(QStringLiteral("kind")).toString().trimmed();
    item.id = value.value(QStringLiteral("id")).toString().trimmed();
    item.collectionId = value.value(QStringLiteral("collectionId")).toString();
    item.libraryId = value.value(QStringLiteral("libraryId")).toString();
    item.title = clipped(value.value(QStringLiteral("title")).toString());
    item.subtitle = clipped(value.value(QStringLiteral("subtitle")).toString());
    item.details = scalarDetails(value.value(QStringLiteral("details")).toMap());
    item.accessedAt = value.value(QStringLiteral("accessedAt")).toLongLong();
    return item;
}

RecentItems::RecentItems(int capacity)
    : m_capacity(std::max(1, capacity))
{
}

bool RecentItems::note(const RecentItem &input)
{
    RecentItem item = input;
    item.kind = item.kind.trimmed();
    item.id = item.id.trimmed();
    if (!item.isValid()) {
        return false;
    }
    item.title = clipped(item.title);
    item.subtitle = clipped(item.subtitle);
    item.details = scalarDetails(item.details);
    if (item.accessedAt <= 0) {
        item.accessedAt = QDateTime::currentMSecsSinceEpoch();
    }

    const QString key = item.key();
    for (int index = 0; index < m_items.size(); ++index) {
        const RecentItem &existing = m_items.at(index);
        if (existing.key() != key) {
            continue;
        }
        if (item.title.isEmpty()) {
            item.title = existing.title;
        }
        if (item.subtitle.isEmpty()) {
            item.subtitle = existing.subtitle;
        }
        if (item.collectionId.isEmpty()) {
            item.collectionId = existing.collectionId;
        }
        if (item.libraryId.isEmpty()) {
            item.libraryId = existing.libraryId;
        }
        QVariantMap details = existing.details;
        for (auto it = item.details.cbegin(); it != item.details.cend(); ++it) {
            details.insert(it.key(), it.value());
        }
        item.details = scalarDetails(details);
        m_items.removeAt(index);
        break;
    }

    m_items.prepend(item);
    while (m_items.size() > m_capacity) {
        m_items.removeLast();
    }
    return true;
}

bool RecentItems::forget(const QString &kind, const QString &id)
{
    const QString key = kind + QLatin1Char('\n') + id;
    for (int index = 0; index < m_items.size(); ++index) {
        if (m_items.at(index).key() == key) {
            m_items.removeAt(index);
            return true;
        }
    }
    return false;
}

int RecentItems::forgetCollection(const QString &collectionId)
{
    if (collectionId.isEmpty()) {
        return 0;
    }
    const auto before = m_items.size();
    m_items.removeIf([&collectionId](const RecentItem &item) {
        return item.collectionId == collectionId
            || (item.kind == QStringLiteral("collection") && item.id == collectionId);
    });
    return static_cast<int>(before - m_items.size());
}

QList<RecentItem> RecentItems::items(const QString &kind, int limit) const
{
    QList<RecentItem> result;
    for (const RecentItem &item : m_items) {
        if (!kind.isEmpty() && item.kind != kind) {
            continue;
        }
        result.append(item);
        if (limit > 0 && result.size() >= limit) {
            break;
        }
    }
    return result;
}

const RecentItem *RecentItems::find(const QString &kind, const QString &id) const
{
    const QString key = kind + QLatin1Char('\n') + id;
    for (const RecentItem &item : m_items) {
        if (item.key() == key) {
            return &item;
        }
    }
    return nullptr;
}

QVariantList RecentItems::toVariant() const
{
    QVariantList list;
    for (const RecentItem &item : m_items) {
        list.append(item.toVariant());
    }
    return list;
}

RecentItems RecentItems::fromVariant(const QVariantList &value, int capacity)
{
    RecentItems recents(capacity);
    QSet<QString> seen;
    for (const QVariant &entry : value) {
        RecentItem item = RecentItem::fromVariant(entry.toMap());
        if (!item.isValid() || seen.contains(item.key())) {
            continue;
        }
        seen.insert(item.key());
        recents.m_items.append(item);
        if (recents.m_items.size() >= recents.m_capacity) {
            break;
        }
    }
    return recents;
}
} // namespace Shell

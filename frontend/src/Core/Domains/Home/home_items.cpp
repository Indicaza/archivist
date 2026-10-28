#include "Core/Domains/Home/home_items.h"

#include <QSet>

#include <algorithm>

namespace Home
{
namespace
{
constexpr int MaxTextLength = 240;

QString clipped(const QString &value)
{
    const QString trimmed = value.trimmed();
    return trimmed.size() > MaxTextLength ? trimmed.left(MaxTextLength) : trimmed;
}

QVariantMap linkToVariant(const ResourceLink &link)
{
    return {
        {QStringLiteral("kind"), link.kind},
        {QStringLiteral("id"), link.id},
        {QStringLiteral("collectionId"), link.collectionId},
        {QStringLiteral("libraryId"), link.libraryId},
    };
}

ResourceLink linkFromVariant(const QVariantMap &value)
{
    ResourceLink link;
    link.kind = value.value(QStringLiteral("kind")).toString().trimmed();
    link.id = value.value(QStringLiteral("id")).toString().trimmed();
    link.collectionId = value.value(QStringLiteral("collectionId")).toString();
    link.libraryId = value.value(QStringLiteral("libraryId")).toString();
    return link;
}
} // namespace

// ---- pins ------------------------------------------------------------------

QVariantMap Pin::toVariant() const
{
    QVariantMap value = linkToVariant(target);
    value.insert(QStringLiteral("title"), title);
    value.insert(QStringLiteral("subtitle"), subtitle);
    value.insert(QStringLiteral("pinnedAt"), pinnedAt);
    return value;
}

Pin Pin::fromVariant(const QVariantMap &value)
{
    Pin pin;
    pin.target = linkFromVariant(value);
    pin.title = clipped(value.value(QStringLiteral("title")).toString());
    pin.subtitle = clipped(value.value(QStringLiteral("subtitle")).toString());
    pin.pinnedAt = value.value(QStringLiteral("pinnedAt")).toLongLong();
    return pin;
}

Pins::Pins(int capacity)
    : m_capacity(std::max(1, capacity))
{
}

int Pins::indexOf(const QString &kind, const QString &id) const
{
    for (int index = 0; index < m_items.size(); ++index) {
        if (m_items.at(index).target.kind == kind && m_items.at(index).target.id == id) {
            return index;
        }
    }
    return -1;
}

bool Pins::pin(const Pin &input)
{
    Pin pin = input;
    pin.title = clipped(pin.title);
    pin.subtitle = clipped(pin.subtitle);
    if (!pin.isValid() || isFull() || contains(pin.target.kind, pin.target.id)) {
        return false;
    }
    m_items.append(pin);
    return true;
}

bool Pins::unpin(const QString &kind, const QString &id)
{
    const int index = indexOf(kind, id);
    if (index < 0) {
        return false;
    }
    m_items.removeAt(index);
    return true;
}

bool Pins::move(const QString &kind, const QString &id, int toIndex)
{
    const int from = indexOf(kind, id);
    if (from < 0) {
        return false;
    }
    const int to = std::clamp(toIndex, 0, static_cast<int>(m_items.size()) - 1);
    if (from == to) {
        return false;
    }
    m_items.move(from, to);
    return true;
}

bool Pins::rename(const QString &kind, const QString &id, const QString &title, const QString &subtitle)
{
    const int index = indexOf(kind, id);
    const QString nextTitle = clipped(title);
    const QString nextSubtitle = clipped(subtitle);
    if (index < 0 || nextTitle.isEmpty()) {
        return false;
    }
    Pin &pin = m_items[index];
    if (pin.title == nextTitle && (nextSubtitle.isEmpty() || pin.subtitle == nextSubtitle)) {
        return false;
    }
    pin.title = nextTitle;
    if (!nextSubtitle.isEmpty()) {
        pin.subtitle = nextSubtitle;
    }
    return true;
}

int Pins::forgetCollection(const QString &collectionId)
{
    if (collectionId.isEmpty()) {
        return 0;
    }
    const auto before = m_items.size();
    m_items.removeIf([&collectionId](const Pin &pin) {
        return pin.target.collectionId == collectionId
            || (pin.target.kind == QStringLiteral("collection") && pin.target.id == collectionId);
    });
    return static_cast<int>(before - m_items.size());
}

QVariantList Pins::toVariant() const
{
    QVariantList list;
    for (const Pin &pin : m_items) {
        list.append(pin.toVariant());
    }
    return list;
}

Pins Pins::fromVariant(const QVariantList &value, int capacity)
{
    Pins pins(capacity);
    for (const QVariant &raw : value) {
        pins.pin(Pin::fromVariant(raw.toMap()));
    }
    return pins;
}

// ---- activity --------------------------------------------------------------

QVariantMap ActivityEvent::toVariant() const
{
    QVariantMap value{
        {QStringLiteral("id"), id},
        {QStringLiteral("kind"), kind},
        {QStringLiteral("key"), key},
        {QStringLiteral("title"), title},
        {QStringLiteral("detail"), detail},
        {QStringLiteral("target"), linkToVariant(target)},
        {QStringLiteral("occurredAt"), occurredAt},
        {QStringLiteral("unread"), unread},
    };
    if (artifact.isValid()) {
        value.insert(QStringLiteral("artifact"), linkToVariant(artifact));
    }
    return value;
}

ActivityEvent ActivityEvent::fromVariant(const QVariantMap &value)
{
    ActivityEvent event;
    event.id = value.value(QStringLiteral("id")).toString().trimmed();
    event.kind = value.value(QStringLiteral("kind")).toString().trimmed();
    event.key = value.value(QStringLiteral("key")).toString().trimmed();
    event.title = clipped(value.value(QStringLiteral("title")).toString());
    event.detail = clipped(value.value(QStringLiteral("detail")).toString());
    event.target = linkFromVariant(value.value(QStringLiteral("target")).toMap());
    event.artifact = linkFromVariant(value.value(QStringLiteral("artifact")).toMap());
    event.occurredAt = value.value(QStringLiteral("occurredAt")).toLongLong();
    event.unread = value.value(QStringLiteral("unread"), true).toBool();
    return event;
}

ActivityLog::ActivityLog(int capacity)
    : m_capacity(std::max(1, capacity))
{
}

bool ActivityLog::note(const ActivityEvent &input)
{
    ActivityEvent event = input;
    event.title = clipped(event.title);
    event.detail = clipped(event.detail);
    if (!event.isValid()) {
        return false;
    }
    m_items.removeIf([&event](const ActivityEvent &existing) {
        return existing.id == event.id || (!event.key.isEmpty() && existing.key == event.key);
    });
    m_items.prepend(event);
    while (m_items.size() > m_capacity) {
        m_items.removeLast();
    }
    return true;
}

bool ActivityLog::dismiss(const QString &eventId)
{
    return m_items.removeIf([&eventId](const ActivityEvent &event) { return event.id == eventId; }) > 0;
}

int ActivityLog::markAllRead()
{
    int changed = 0;
    for (ActivityEvent &event : m_items) {
        if (event.unread) {
            event.unread = false;
            ++changed;
        }
    }
    return changed;
}

int ActivityLog::forgetCollection(const QString &collectionId)
{
    if (collectionId.isEmpty()) {
        return 0;
    }
    return static_cast<int>(m_items.removeIf([&collectionId](const ActivityEvent &event) {
        return event.target.collectionId == collectionId;
    }));
}

const ActivityEvent *ActivityLog::find(const QString &eventId) const
{
    for (const ActivityEvent &event : m_items) {
        if (event.id == eventId) {
            return &event;
        }
    }
    return nullptr;
}

int ActivityLog::unreadCount() const
{
    return static_cast<int>(std::count_if(m_items.cbegin(), m_items.cend(), [](const ActivityEvent &event) {
        return event.unread;
    }));
}

QVariantList ActivityLog::toVariant() const
{
    QVariantList list;
    for (const ActivityEvent &event : m_items) {
        list.append(event.toVariant());
    }
    return list;
}

ActivityLog ActivityLog::fromVariant(const QVariantList &value, int capacity)
{
    ActivityLog log(capacity);
    QSet<QString> seen;
    for (const QVariant &raw : value) {
        if (log.m_items.size() >= log.m_capacity) {
            break;
        }
        const ActivityEvent event = ActivityEvent::fromVariant(raw.toMap());
        if (!event.isValid() || seen.contains(event.id) || (!event.key.isEmpty() && seen.contains(QStringLiteral("key:") + event.key))) {
            continue;
        }
        seen.insert(event.id);
        if (!event.key.isEmpty()) {
            seen.insert(QStringLiteral("key:") + event.key);
        }
        log.m_items.append(event);
    }
    return log;
}
} // namespace Home

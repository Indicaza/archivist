#pragma once

#include <QList>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

// Pins and activity on Home.
//
// Both hold references, never content, like Shell's recent items. A pin says
// which conversation, project, file or layout; an activity event says what
// happened and which resource it is about. An event's id is its own: it is
// not the id of the conversation or artifact it points at, so dismissing an
// event never touches the resource and pinning from an event pins the
// resource, not the event.
namespace Home
{
// Which resource something points at, in its own store.
struct ResourceLink
{
    QString kind; // chat | collection | document | layout | ... (free, non-empty)
    QString id;
    QString collectionId;
    QString libraryId;

    [[nodiscard]] bool isValid() const { return !kind.isEmpty() && !id.isEmpty(); }
    [[nodiscard]] QString key() const { return kind + QLatin1Char('\n') + id; }
    bool operator==(const ResourceLink &other) const
    {
        return kind == other.kind && id == other.id && collectionId == other.collectionId && libraryId == other.libraryId;
    }
};

struct Pin
{
    ResourceLink target;
    QString title;
    QString subtitle;
    qint64 pinnedAt = 0;

    [[nodiscard]] bool isValid() const { return target.isValid(); }
    [[nodiscard]] QVariantMap toVariant() const;
    [[nodiscard]] static Pin fromVariant(const QVariantMap &value);
};

class Pins
{
public:
    static constexpr int DefaultCapacity = 24;

    explicit Pins(int capacity = DefaultCapacity);

    // Adds at the end. False when invalid, already pinned or full.
    bool pin(const Pin &pin);
    bool unpin(const QString &kind, const QString &id);
    bool move(const QString &kind, const QString &id, int toIndex);
    // Keeps the pin, refreshes what it's called.
    bool rename(const QString &kind, const QString &id, const QString &title, const QString &subtitle = {});
    int forgetCollection(const QString &collectionId);

    [[nodiscard]] int indexOf(const QString &kind, const QString &id) const;
    [[nodiscard]] bool contains(const QString &kind, const QString &id) const { return indexOf(kind, id) >= 0; }
    [[nodiscard]] const QList<Pin> &items() const { return m_items; }
    [[nodiscard]] int capacity() const { return m_capacity; }
    [[nodiscard]] bool isFull() const { return m_items.size() >= m_capacity; }

    [[nodiscard]] QVariantList toVariant() const;
    [[nodiscard]] static Pins fromVariant(const QVariantList &value, int capacity = DefaultCapacity);

private:
    int m_capacity;
    QList<Pin> m_items;
};

namespace ActivityKinds
{
inline const QString ReplyFinished = QStringLiteral("reply.finished");
inline const QString ReplyFailed = QStringLiteral("reply.failed");
} // namespace ActivityKinds

struct ActivityEvent
{
    QString id;   // the event's own id
    QString kind; // reply.finished | reply.failed | plugin.* ...
    // Events with the same key replace each other ("the latest reply in
    // this conversation"), so a busy conversation is one line, not twenty.
    QString key;
    QString title;
    QString detail;
    ResourceLink target;   // what opening the event shows
    ResourceLink artifact; // a lasting result it produced, when there is one
    qint64 occurredAt = 0; // ms since epoch
    bool unread = true;

    [[nodiscard]] bool isValid() const { return !id.isEmpty() && !kind.isEmpty() && !title.isEmpty(); }
    [[nodiscard]] QVariantMap toVariant() const;
    [[nodiscard]] static ActivityEvent fromVariant(const QVariantMap &value);
};

class ActivityLog
{
public:
    static constexpr int DefaultCapacity = 40;

    explicit ActivityLog(int capacity = DefaultCapacity);

    // Newest first. Replaces an event with the same key. False if invalid.
    bool note(const ActivityEvent &event);
    bool dismiss(const QString &eventId);
    // Marks every event read. Returns how many changed.
    int markAllRead();
    int forgetCollection(const QString &collectionId);
    void clear() { m_items.clear(); }

    [[nodiscard]] const ActivityEvent *find(const QString &eventId) const;
    [[nodiscard]] const QList<ActivityEvent> &items() const { return m_items; }
    [[nodiscard]] int unreadCount() const;

    [[nodiscard]] QVariantList toVariant() const;
    [[nodiscard]] static ActivityLog fromVariant(const QVariantList &value, int capacity = DefaultCapacity);

private:
    int m_capacity;
    QList<ActivityEvent> m_items;
};
} // namespace Home

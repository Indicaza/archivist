#pragma once

#include <QList>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

// What the person opened lately: conversations, files, projects. Home lists
// them, and later Orbit does too.
//
// Entries are references, never content. The resource stays with its own
// store (ChatStore, the document buffers, CollectionStore); an entry only
// says which one, where it lives and what to call it. Forgetting an entry
// never touches the resource.
//
// Most recent first, one entry per kind + id, at most `capacity` entries.
namespace Shell
{
struct RecentItem
{
    QString kind;         // chat | document | collection | ... (free, non-empty)
    QString id;           // the resource id in its own store
    QString collectionId; // the project it belongs to, when it has one
    QString libraryId;
    QString title;
    QString subtitle;
    QVariantMap details;  // small scalar values only (path, fileId, ...)
    qint64 accessedAt = 0; // ms since epoch

    [[nodiscard]] bool isValid() const { return !kind.isEmpty() && !id.isEmpty(); }
    [[nodiscard]] QString key() const { return kind + QLatin1Char('\n') + id; }
    [[nodiscard]] QVariantMap toVariant() const;
    [[nodiscard]] static RecentItem fromVariant(const QVariantMap &value);
};

class RecentItems
{
public:
    static constexpr int DefaultCapacity = 48;

    explicit RecentItems(int capacity = DefaultCapacity);

    // Moves the item to the front. Empty title or subtitle keep what was
    // known. Returns false for invalid items.
    bool note(const RecentItem &item);
    bool forget(const QString &kind, const QString &id);
    // Drops every entry of a project (it was removed). Returns how many.
    int forgetCollection(const QString &collectionId);
    void clear() { m_items.clear(); }

    [[nodiscard]] const QList<RecentItem> &items() const { return m_items; }
    // Filtered by kind (empty: all), newest first. limit <= 0: no limit.
    [[nodiscard]] QList<RecentItem> items(const QString &kind, int limit) const;
    [[nodiscard]] const RecentItem *find(const QString &kind, const QString &id) const;
    [[nodiscard]] int capacity() const { return m_capacity; }

    [[nodiscard]] QVariantList toVariant() const;
    // Forgiving: skips invalid entries and duplicates, keeps the order.
    [[nodiscard]] static RecentItems fromVariant(const QVariantList &value, int capacity = DefaultCapacity);

private:
    int m_capacity;
    QList<RecentItem> m_items;
};
} // namespace Shell

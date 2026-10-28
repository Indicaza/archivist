#pragma once

#include <QList>
#include <QString>
#include <QVariantList>

// Unsent text, one draft per conversation.
//
// A draft belongs to its conversation, not to the text field that shows
// it. Switching chats, moving or re-creating the composer, or restarting
// never mixes two conversations' drafts or loses one. Where the composer
// sits and how it looks is a separate concern; it only reads and writes
// drafts here.
//
// Whitespace-only text is no draft (setting it clears the entry). The
// book keeps the `capacity` most recently edited drafts.
namespace Chats
{
struct Draft
{
    QString key;          // the chat id ("new:..." for a chat not created yet)
    QString text;
    qint64 updatedAt = 0; // ms since epoch
};

class DraftBook
{
public:
    static constexpr int DefaultCapacity = 100;
    static constexpr int MaxLength = 200000; // characters kept per draft

    explicit DraftBook(int capacity = DefaultCapacity);

    [[nodiscard]] QString text(const QString &key) const;
    [[nodiscard]] bool has(const QString &key) const;
    // Returns true when the stored draft changed.
    bool set(const QString &key, const QString &text, qint64 now = 0);
    bool clear(const QString &key);
    // A chat created from a "new:..." draft takes the draft over.
    bool rename(const QString &from, const QString &to);

    [[nodiscard]] const QList<Draft> &drafts() const { return m_drafts; }
    [[nodiscard]] int capacity() const { return m_capacity; }

    [[nodiscard]] QVariantList toVariant() const;
    [[nodiscard]] static DraftBook fromVariant(const QVariantList &value, int capacity = DefaultCapacity);

private:
    [[nodiscard]] int indexOf(const QString &key) const;

    int m_capacity;
    QList<Draft> m_drafts; // most recently edited first
};
} // namespace Chats

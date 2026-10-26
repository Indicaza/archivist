#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QStringList>
#include <QVariantList>

// Row-level model for the Chat transcript.
//
// ChatStore keeps its QVariantList of messages as the source of truth and
// calls sync() whenever it changes. sync() turns the change into the smallest
// set of row inserts, removals and updates, so the ListView keeps its existing
// delegates and scroll position instead of rebuilding the whole transcript.
class ChatMessageModel final : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        MessageDataRole = Qt::UserRole + 1,
        MessageIdRole,
        // "Today · 10:31 AM" for the stretch of conversation a row belongs
        // to; a new stretch starts after a 30-minute pause. The transcript
        // shows it in the pinned time label at the top.
        TimeSectionRole,
        // The label of the stretch that starts right after this row, or an
        // empty string. The row draws that divider below itself, so loading
        // older history never changes the height of a row already on screen.
        SectionBreakAfterRole,
    };

    explicit ChatMessageModel(QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void sync(const QVariantList &messages);

    // The divider label for a message created at `created`, relative to
    // `now`; public for tests.
    [[nodiscard]] static QString sectionLabel(const QDateTime &created, const QDateTime &now);

private:
    [[nodiscard]] static QString idOf(const QVariant &message);
    [[nodiscard]] static QDateTime createdAt(const QVariant &message);
    struct Sections
    {
        QStringList labels;
        QStringList breaksAfter;
    };

    [[nodiscard]] Sections computeSections() const;
    void refreshSections();

    QVariantList m_messages;
    QStringList m_ids;
    QStringList m_sections;
    QStringList m_breaksAfter;
};

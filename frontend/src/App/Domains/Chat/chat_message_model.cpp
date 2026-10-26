#include "chat_message_model.h"

#include <algorithm>
#include <QVariantMap>

ChatMessageModel::ChatMessageModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int ChatMessageModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_messages.size());
}

QVariant ChatMessageModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_messages.size()) {
        return {};
    }

    switch (role) {
    case MessageDataRole:
        return m_messages.at(index.row());
    case MessageIdRole:
        return m_ids.at(index.row());
    case TimeSectionRole:
        return index.row() < m_sections.size() ? m_sections.at(index.row()) : QString();
    case SectionBreakAfterRole:
        return index.row() < m_breaksAfter.size() ? m_breaksAfter.at(index.row()) : QString();
    default:
        return {};
    }
}

QHash<int, QByteArray> ChatMessageModel::roleNames() const
{
    return {
        {MessageDataRole, QByteArrayLiteral("messageData")},
        {MessageIdRole, QByteArrayLiteral("messageId")},
        {TimeSectionRole, QByteArrayLiteral("timeSection")},
        {SectionBreakAfterRole, QByteArrayLiteral("sectionBreakAfter")},
    };
}

namespace
{
// A pause this long starts a new stretch of conversation.
constexpr qint64 sectionGapSeconds = 30 * 60;
}

QDateTime ChatMessageModel::createdAt(const QVariant &message)
{
    const QString value = message.toMap().value(QStringLiteral("createdAt")).toString();
    QDateTime timestamp = QDateTime::fromString(value, Qt::ISODateWithMs);

    if (!timestamp.isValid()) {
        timestamp = QDateTime::fromString(value, Qt::ISODate);
    }

    return timestamp;
}

QString ChatMessageModel::sectionLabel(const QDateTime &created, const QDateTime &now)
{
    if (!created.isValid()) {
        return {};
    }

    const QDateTime local = created.toLocalTime();
    const QDate date = local.date();
    const QDate today = now.toLocalTime().date();
    const qint64 days = date.daysTo(today);
    QString day;

    if (days == 0) {
        day = QStringLiteral("Today");
    } else if (days == 1) {
        day = QStringLiteral("Yesterday");
    } else if (days > 1 && days < 7) {
        day = local.toString(QStringLiteral("dddd"));
    } else if (date.year() == today.year()) {
        day = local.toString(QStringLiteral("ddd, MMM d"));
    } else {
        day = local.toString(QStringLiteral("MMM d, yyyy"));
    }

    return day + QStringLiteral(" \u00B7 ") + local.toString(QStringLiteral("h:mm AP"));
}

ChatMessageModel::Sections ChatMessageModel::computeSections() const
{
    Sections sections;
    sections.labels.reserve(m_messages.size());
    sections.breaksAfter.reserve(m_messages.size());
    const QDateTime now = QDateTime::currentDateTime();
    QDateTime previous;
    QString current;

    for (const QVariant &message : m_messages) {
        const QDateTime created = createdAt(message);
        const bool startsStretch = created.isValid()
            && (!previous.isValid() || previous.secsTo(created) >= sectionGapSeconds);

        if (startsStretch) {
            current = sectionLabel(created, now);

            // The divider hangs below the row before the pause.
            if (previous.isValid() && !sections.breaksAfter.isEmpty()) {
                sections.breaksAfter.last() = current;
            }
        }

        if (created.isValid()) {
            previous = created;
        }

        sections.labels.append(current);
        sections.breaksAfter.append(QString());
    }

    return sections;
}

void ChatMessageModel::refreshSections()
{
    const Sections next = computeSections();
    m_sections.resize(next.labels.size());
    m_breaksAfter.resize(next.breaksAfter.size());

    for (qsizetype row = 0; row < next.labels.size(); ++row) {
        QList<int> roles;

        if (m_sections.at(row) != next.labels.at(row)) {
            m_sections[row] = next.labels.at(row);
            roles.append(TimeSectionRole);
        }

        if (m_breaksAfter.at(row) != next.breaksAfter.at(row)) {
            m_breaksAfter[row] = next.breaksAfter.at(row);
            roles.append(SectionBreakAfterRole);
        }

        if (!roles.isEmpty()) {
            const QModelIndex changed = index(static_cast<int>(row));
            emit dataChanged(changed, changed, roles);
        }
    }
}

QString ChatMessageModel::idOf(const QVariant &message)
{
    return message.toMap().value(QStringLiteral("id")).toString();
}

void ChatMessageModel::sync(const QVariantList &messages)
{
    QStringList nextIds;
    nextIds.reserve(messages.size());

    for (const QVariant &message : messages) {
        nextIds.append(idOf(message));
    }

    const qsizetype oldCount = m_messages.size();
    const qsizetype newCount = messages.size();

    if (oldCount == 0 && newCount == 0) {
        return;
    }

    // Matching ids at the start and end are kept as the same rows. An empty id
    // never matches, so placeholder messages are always replaced.
    const qsizetype shortest = std::min(oldCount, newCount);
    qsizetype prefix = 0;

    while (
        prefix < shortest
        && !m_ids.at(prefix).isEmpty()
        && m_ids.at(prefix) == nextIds.at(prefix)
    ) {
        ++prefix;
    }

    qsizetype suffix = 0;

    while (
        suffix < shortest - prefix
        && !m_ids.at(oldCount - 1 - suffix).isEmpty()
        && m_ids.at(oldCount - 1 - suffix) == nextIds.at(newCount - 1 - suffix)
    ) {
        ++suffix;
    }

    // Nothing in common, such as switching to another Chat: a reset is the
    // honest signal and lets the view start fresh.
    if (oldCount > 0 && newCount > 0 && prefix == 0 && suffix == 0) {
        beginResetModel();
        m_messages = messages;
        m_ids = nextIds;
        const Sections sections = computeSections();
        m_sections = sections.labels;
        m_breaksAfter = sections.breaksAfter;
        endResetModel();
        return;
    }

    const qsizetype oldMiddle = oldCount - prefix - suffix;
    const qsizetype newMiddle = newCount - prefix - suffix;

    if (oldMiddle > 0) {
        beginRemoveRows({}, static_cast<int>(prefix), static_cast<int>(prefix + oldMiddle - 1));
        m_messages.remove(prefix, oldMiddle);
        m_ids.remove(prefix, oldMiddle);
        m_sections.remove(prefix, std::min(oldMiddle, m_sections.size() - prefix));
        m_breaksAfter.remove(prefix, std::min(oldMiddle, m_breaksAfter.size() - prefix));
        endRemoveRows();
    }

    if (newMiddle > 0) {
        beginInsertRows({}, static_cast<int>(prefix), static_cast<int>(prefix + newMiddle - 1));

        for (qsizetype offset = 0; offset < newMiddle; ++offset) {
            m_messages.insert(prefix + offset, messages.at(prefix + offset));
            m_ids.insert(prefix + offset, nextIds.at(prefix + offset));
        }

        // Inserted rows get their labels before the view reads them; rows
        // whose label changed because of the insert update below.
        const Sections sections = computeSections();

        for (qsizetype offset = 0; offset < newMiddle; ++offset) {
            m_sections.insert(std::min(prefix + offset, m_sections.size()), sections.labels.at(prefix + offset));
            m_breaksAfter.insert(
                std::min(prefix + offset, m_breaksAfter.size()),
                sections.breaksAfter.at(prefix + offset)
            );
        }

        endInsertRows();
    }

    // Rows that kept their id but changed content (streamed text, status,
    // edit-proposal state) update in place.
    for (qsizetype row = 0; row < m_messages.size(); ++row) {
        if (m_messages.at(row) == messages.at(row)) {
            continue;
        }

        m_messages[row] = messages.at(row);
        const QModelIndex changed = index(static_cast<int>(row));
        emit dataChanged(changed, changed, {MessageDataRole});
    }

    refreshSections();
}

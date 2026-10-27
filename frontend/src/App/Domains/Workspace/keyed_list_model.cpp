#include "App/Domains/Workspace/keyed_list_model.h"

#include <QSet>

KeyedListModel::KeyedListModel(const QString &keyRole, const QStringList &roles, QObject *parent)
    : QAbstractListModel(parent)
    , m_keyRole(keyRole)
    , m_roles(roles)
{
    if (!m_roles.contains(m_keyRole)) {
        m_roles.prepend(m_keyRole);
    }
}

int KeyedListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant KeyedListModel::data(const QModelIndex &index, int role) const
{
    const int column = role - Qt::UserRole - 1;

    if (!index.isValid() || index.row() >= m_rows.size() || column < 0 || column >= m_roles.size()) {
        return {};
    }

    return m_rows.at(index.row()).value(m_roles.at(column));
}

QHash<int, QByteArray> KeyedListModel::roleNames() const
{
    QHash<int, QByteArray> names;

    for (qsizetype i = 0; i < m_roles.size(); ++i) {
        names.insert(Qt::UserRole + 1 + int(i), m_roles.at(i).toUtf8());
    }

    return names;
}

void KeyedListModel::setRows(const QList<QVariantMap> &rows)
{
    const int before = count();
    QSet<QString> wanted;

    for (const QVariantMap &row : rows) {
        wanted.insert(row.value(m_keyRole).toString());
    }

    // Remove rows that went away, from the bottom up.
    for (int i = int(m_rows.size()) - 1; i >= 0; --i) {
        if (!wanted.contains(m_rows.at(i).value(m_keyRole).toString())) {
            beginRemoveRows({}, i, i);
            m_rows.removeAt(i);
            endRemoveRows();
        }
    }

    for (int target = 0; target < rows.size(); ++target) {
        const QVariantMap &row = rows.at(target);
        const QString key = row.value(m_keyRole).toString();
        const int current = indexOf(key);

        if (current < 0) {
            beginInsertRows({}, target, target);
            m_rows.insert(target, row);
            endInsertRows();
            continue;
        }

        if (current != target) {
            beginMoveRows({}, current, current, {}, current < target ? target + 1 : target);
            m_rows.move(current, target);
            endMoveRows();
        }

        if (m_rows.at(target) != row) {
            QList<int> changedRoles;
            const QVariantMap &old = m_rows.at(target);

            for (qsizetype i = 0; i < m_roles.size(); ++i) {
                if (old.value(m_roles.at(i)) != row.value(m_roles.at(i))) {
                    changedRoles.append(Qt::UserRole + 1 + int(i));
                }
            }

            m_rows[target] = row;
            emit dataChanged(index(target), index(target), changedRoles);
        }
    }

    if (count() != before) {
        emit countChanged();
    }
}

QVariantMap KeyedListModel::row(const QString &key) const
{
    const int at = indexOf(key);
    return at < 0 ? QVariantMap() : m_rows.at(at);
}

QVariantMap KeyedListModel::get(int index) const
{
    return index >= 0 && index < m_rows.size() ? m_rows.at(index) : QVariantMap();
}

int KeyedListModel::indexOf(const QString &key) const
{
    for (qsizetype i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).value(m_keyRole).toString() == key) {
            return int(i);
        }
    }
    return -1;
}

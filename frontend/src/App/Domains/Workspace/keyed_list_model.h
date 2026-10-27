#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// A list model whose rows are identified by a key. Replacing the rows
// inserts, removes and updates in place instead of resetting, so QML
// delegates (and the live views inside them) survive every layout change.
class KeyedListModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    KeyedListModel(const QString &keyRole, const QStringList &roles, QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int count() const { return int(m_rows.size()); }
    // Each row is a map from role name to value; rows are matched by the key
    // role. Order follows `rows`.
    void setRows(const QList<QVariantMap> &rows);
    [[nodiscard]] Q_INVOKABLE QVariantMap row(const QString &key) const;
    [[nodiscard]] Q_INVOKABLE QVariantMap get(int index) const;
    [[nodiscard]] Q_INVOKABLE int indexOf(const QString &key) const;

signals:
    void countChanged();

private:
    QString m_keyRole;
    QStringList m_roles;
    QList<QVariantMap> m_rows;
};

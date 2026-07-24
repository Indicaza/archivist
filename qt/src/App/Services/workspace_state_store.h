#pragma once

#include <QObject>
#include <QSettings>
#include <QVariant>

class WorkspaceStateStore final : public QObject
{
    Q_OBJECT

public:
    explicit WorkspaceStateStore(QObject *parent = nullptr);

    Q_INVOKABLE QVariant value(
        const QString &key,
        const QVariant &defaultValue = QVariant()
    ) const;
    Q_INVOKABLE void setValue(const QString &key, const QVariant &value);
    Q_INVOKABLE void remove(const QString &key);
    Q_INVOKABLE void sync();

private:
    mutable QSettings m_settings;
};

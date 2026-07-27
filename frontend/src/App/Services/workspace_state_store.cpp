#include "workspace_state_store.h"

WorkspaceStateStore::WorkspaceStateStore(QObject *parent)
    : QObject(parent)
{
}

QVariant WorkspaceStateStore::value(
    const QString &key,
    const QVariant &defaultValue
) const
{
    return m_settings.value(key, defaultValue);
}

void WorkspaceStateStore::setValue(const QString &key, const QVariant &value)
{
    m_settings.setValue(key, value);
}

void WorkspaceStateStore::remove(const QString &key)
{
    m_settings.remove(key);
}

void WorkspaceStateStore::sync()
{
    m_settings.sync();
}

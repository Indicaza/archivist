#include "Core/Support/app_profile.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QtEnvironmentVariables>

namespace AppProfile
{
QString directory()
{
    const QString configured = qEnvironmentVariable("ARCHIVIST_PROFILE_DIR").trimmed();
    return configured.isEmpty() ? QString() : QDir::cleanPath(QDir(configured).absolutePath());
}

bool active()
{
    return !directory().isEmpty();
}

QString dataLocation()
{
    return active() ? directory() : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

bool isolateSettings()
{
    if (!active()) {
        return true;
    }
    const QString settingsDir = QDir(directory()).filePath(QStringLiteral("settings"));
    if (!QDir().mkpath(settingsDir)) {
        return false;
    }
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir);
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settingsDir);
    return true;
}

// Canonical form of a path that may not exist yet: its nearest existing
// ancestor resolved (macOS /var is /private/var), the rest appended.
static QString resolved(const QString &path)
{
    QString existing = QDir::cleanPath(QDir(path).absolutePath());
    QString rest;
    while (!QFileInfo::exists(existing)) {
        const QFileInfo info(existing);
        const QString parent = info.absolutePath();
        if (parent == existing) {
            break;
        }
        rest = QLatin1Char('/') + info.fileName() + rest;
        existing = parent;
    }
    const QString canonical = QFileInfo(existing).canonicalFilePath();
    return QDir::cleanPath((canonical.isEmpty() ? existing : canonical) + rest);
}

bool contains(const QString &path)
{
    if (!active() || path.isEmpty()) {
        return false;
    }
    const QString root = resolved(directory());
    const QString target = resolved(path);
    return target == root || target.startsWith(root + QLatin1Char('/'));
}
} // namespace AppProfile

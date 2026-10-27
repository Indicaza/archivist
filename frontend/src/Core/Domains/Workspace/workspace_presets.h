#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

// Built-in layouts. Each is an ordinary layout document (see
// workspace_codec.h), so applying a preset re-homes the open views and never
// opens or closes anything. Every preset anchor is pinned, so an empty slot
// stays put until the user fills or closes it.
namespace Workspace::Presets
{
struct Preset
{
    QString id;
    QString name;
    QString description;
    QJsonObject layout;
};

[[nodiscard]] QList<Preset> all();
[[nodiscard]] QJsonObject layout(const QString &presetId); // empty when unknown
} // namespace Workspace::Presets

#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace Workspace::Compose
{
inline constexpr int kMaximumSlots = 6;
inline constexpr int kMaximumItemsPerSlot = 8;
inline constexpr int kMaximumItems = 16;

struct Template
{
    QString id;
    QString name;
    int version = 1;
    QString description;
    QJsonObject recipe;
};

[[nodiscard]] const QList<Template> &templates();
[[nodiscard]] QStringList templateIds();
[[nodiscard]] QStringList strategies();
[[nodiscard]] QStringList holdWords();
} // namespace Workspace::Compose

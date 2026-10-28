#pragma once

#include <QHash>
#include <QJsonArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// How the person likes their workspace, in a few plain settings:
//
//   chat_location   bottom | docked           "I like my chat at the bottom"
//   main_editor     large | normal            "Keep my main editor large"
//   rearranging     only_when_asked | when_helpful
//                                             "Don't rearrange my windows unless I ask"
//   area_purpose    a purpose word, for one panel
//                                             "Use the right panel for documentation"
//
// Each preference is either for this session or always (stored). A one-off
// request never becomes a preference by itself: something has to set it,
// and only a statement of preference (or the person in the UI) may set one
// that lasts. Explicit requests always win over preferences; preferences
// only change what happens when nobody said exactly what they wanted.
//
// Precedence: a session preference overrides a stored one with the same
// key (and panel); between equals, the newer one wins.
namespace Workspace::Preferences
{
struct Preference
{
    QString id;
    QString key;
    QString value;
    QString anchorId;     // area_purpose only
    QString collectionId; // area_purpose only: panels belong to a collection
    bool lasting = false; // stored, survives restarts
    QString setAt;        // ISO time
    QString setBy;        // "person" | "agent"
};

struct Effects
{
    bool chatAtBottom = false;
    bool keepMainLarge = false;
    bool onlyWhenAsked = false;
    QHash<QString, QString> purposeAreas; // purpose -> anchor
};

[[nodiscard]] QStringList keys();
[[nodiscard]] QStringList valuesFor(const QString &key); // empty: free text
// Empty when valid; otherwise a sentence for the person.
[[nodiscard]] QString validate(const QString &key, const QString &value, const QString &anchorId);
[[nodiscard]] QString normalizedPurpose(const QString &value);

// What it means, in the person's words ("Chat stays in the bottom panel").
[[nodiscard]] QString sentence(const Preference &preference, const QString &areaName = {});

// Adds or replaces (same key, panel and duration). Setting a lasting one
// drops the session one it supersedes.
void put(QList<Preference> &list, const Preference &preference);

[[nodiscard]] Effects effects(const QList<Preference> &list, const QString &collectionId);

[[nodiscard]] QJsonArray toJson(const QList<Preference> &list);
[[nodiscard]] QList<Preference> fromJson(const QJsonArray &array);
[[nodiscard]] QVariantMap toMap(const Preference &preference);
} // namespace Workspace::Preferences

#include "Core/Domains/Workspace/workspace_preferences.h"

#include <QJsonObject>

#include <algorithm>

namespace Workspace::Preferences
{
namespace
{
const QString ChatLocation = QStringLiteral("chat_location");
const QString MainEditor = QStringLiteral("main_editor");
const QString Rearranging = QStringLiteral("rearranging");
const QString AreaPurpose = QStringLiteral("area_purpose");

bool sameSlot(const Preference &a, const Preference &b)
{
    if (a.key != b.key) {
        return false;
    }
    if (a.key != AreaPurpose) {
        return true;
    }
    // One purpose per panel, and one panel per purpose.
    return a.collectionId == b.collectionId && (a.anchorId == b.anchorId || a.value == b.value);
}
} // namespace

QStringList keys()
{
    return {ChatLocation, MainEditor, Rearranging, AreaPurpose};
}

QStringList valuesFor(const QString &key)
{
    if (key == ChatLocation) {
        return {QStringLiteral("bottom"), QStringLiteral("docked")};
    }
    if (key == MainEditor) {
        return {QStringLiteral("large"), QStringLiteral("normal")};
    }
    if (key == Rearranging) {
        return {QStringLiteral("only_when_asked"), QStringLiteral("when_helpful")};
    }
    return {};
}

QString normalizedPurpose(const QString &value)
{
    QString purpose = value.trimmed().toLower();
    if (purpose.startsWith(QStringLiteral("for "))) {
        purpose = purpose.mid(4);
    }
    for (const QString &article : {QStringLiteral("the "), QStringLiteral("my ")}) {
        if (purpose.startsWith(article)) {
            purpose = purpose.mid(article.size());
        }
    }
    if (purpose == QStringLiteral("docs") || purpose == QStringLiteral("doc") || purpose == QStringLiteral("documents")
        || purpose == QStringLiteral("reference documents") || purpose == QStringLiteral("reference docs")) {
        return QStringLiteral("documentation");
    }
    return purpose.left(40);
}

QString validate(const QString &key, const QString &value, const QString &anchorId)
{
    if (!keys().contains(key)) {
        return QStringLiteral("Archivist doesn't have a “%1” preference.").arg(key);
    }
    if (key == AreaPurpose) {
        if (anchorId.isEmpty()) {
            return QStringLiteral("Which panel is it for?");
        }
        if (normalizedPurpose(value).isEmpty()) {
            return QStringLiteral("What is the panel for?");
        }
        return {};
    }
    if (!valuesFor(key).contains(value)) {
        return QStringLiteral("“%1” isn't one of: %2.").arg(value, valuesFor(key).join(QStringLiteral(", ")));
    }
    return {};
}

QString sentence(const Preference &preference, const QString &areaName)
{
    if (preference.key == ChatLocation) {
        return preference.value == QStringLiteral("bottom") ? QStringLiteral("Chat stays in the bottom panel")
                                                            : QStringLiteral("Chat may open as a panel");
    }
    if (preference.key == MainEditor) {
        return preference.value == QStringLiteral("large") ? QStringLiteral("Keep the main editor large")
                                                           : QStringLiteral("The main editor can be any size");
    }
    if (preference.key == Rearranging) {
        return preference.value == QStringLiteral("only_when_asked") ? QStringLiteral("Don't rearrange panels unless asked")
                                                                     : QStringLiteral("Panels may be rearranged when it helps");
    }
    return QStringLiteral("Use %1 for %2").arg(areaName.isEmpty() ? QStringLiteral("a panel") : QStringLiteral("“%1”").arg(areaName), preference.value);
}

void put(QList<Preference> &list, const Preference &preference)
{
    list.erase(
        std::remove_if(list.begin(), list.end(), [&](const Preference &existing) {
            // A lasting preference replaces both; a session one only its own kind.
            return sameSlot(existing, preference) && (preference.lasting || !existing.lasting);
        }),
        list.end()
    );
    list.append(preference);
}

Effects effects(const QList<Preference> &list, const QString &collectionId)
{
    QList<Preference> ordered;
    for (const Preference &preference : list) {
        if (preference.key == AreaPurpose && preference.collectionId != collectionId) {
            continue;
        }
        ordered.append(preference);
    }
    // Stored first, then session; each group oldest first. Later wins.
    std::stable_sort(ordered.begin(), ordered.end(), [](const Preference &a, const Preference &b) {
        if (a.lasting != b.lasting) {
            return a.lasting;
        }
        return a.setAt < b.setAt;
    });

    Effects out;
    QHash<QString, QString> anchorPurpose; // anchor -> purpose
    for (const Preference &preference : std::as_const(ordered)) {
        if (preference.key == ChatLocation) {
            out.chatAtBottom = preference.value == QStringLiteral("bottom");
        } else if (preference.key == MainEditor) {
            out.keepMainLarge = preference.value == QStringLiteral("large");
        } else if (preference.key == Rearranging) {
            out.onlyWhenAsked = preference.value == QStringLiteral("only_when_asked");
        } else if (preference.key == AreaPurpose) {
            // Later choices take the panel or the purpose over.
            for (auto it = anchorPurpose.begin(); it != anchorPurpose.end();) {
                it = it.value() == preference.value ? anchorPurpose.erase(it) : std::next(it);
            }
            anchorPurpose.insert(preference.anchorId, preference.value);
        }
    }
    for (auto it = anchorPurpose.cbegin(); it != anchorPurpose.cend(); ++it) {
        out.purposeAreas.insert(it.value(), it.key());
    }
    return out;
}

QJsonArray toJson(const QList<Preference> &list)
{
    QJsonArray array;
    for (const Preference &preference : list) {
        QJsonObject object{
            {QStringLiteral("id"), preference.id},
            {QStringLiteral("key"), preference.key},
            {QStringLiteral("value"), preference.value},
            {QStringLiteral("setAt"), preference.setAt},
            {QStringLiteral("setBy"), preference.setBy},
        };
        if (!preference.anchorId.isEmpty()) {
            object.insert(QStringLiteral("anchorId"), preference.anchorId);
            object.insert(QStringLiteral("collectionId"), preference.collectionId);
        }
        array.append(object);
    }
    return array;
}

QList<Preference> fromJson(const QJsonArray &array)
{
    QList<Preference> list;
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        Preference preference;
        preference.id = object.value(QStringLiteral("id")).toString();
        preference.key = object.value(QStringLiteral("key")).toString();
        preference.value = object.value(QStringLiteral("value")).toString();
        preference.anchorId = object.value(QStringLiteral("anchorId")).toString();
        preference.collectionId = object.value(QStringLiteral("collectionId")).toString();
        preference.setAt = object.value(QStringLiteral("setAt")).toString();
        preference.setBy = object.value(QStringLiteral("setBy")).toString();
        preference.lasting = true;
        // Unknown or damaged entries are skipped rather than guessed at.
        if (!preference.id.isEmpty() && validate(preference.key, preference.value, preference.anchorId).isEmpty()) {
            list.append(preference);
        }
    }
    return list;
}

QVariantMap toMap(const Preference &preference)
{
    return {
        {QStringLiteral("id"), preference.id},
        {QStringLiteral("key"), preference.key},
        {QStringLiteral("value"), preference.value},
        {QStringLiteral("anchorId"), preference.anchorId},
        {QStringLiteral("lasting"), preference.lasting},
        {QStringLiteral("duration"), preference.lasting ? QStringLiteral("always") : QStringLiteral("this session")},
        {QStringLiteral("setAt"), preference.setAt},
        {QStringLiteral("setBy"), preference.setBy},
    };
}
} // namespace Workspace::Preferences

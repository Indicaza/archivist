#include "library_repository.h"

#include "Core/Support/core_values.h"
#include "Core/core_error.h"

#include <QDir>
#include <QFileInfo>
#include <climits>

using namespace CoreValues;

namespace
{
const QString libraryColumns =
    QStringLiteral("id, name, description, root_path, archived_at, created_at, updated_at");

const QString nowSql = QStringLiteral("strftime('%Y-%m-%dT%H:%M:%fZ', 'now')");

QVariantMap mapLibrary(const QVariantMap &row)
{
    return {
        {QStringLiteral("id"), row.value(QStringLiteral("id"))},
        {QStringLiteral("name"), row.value(QStringLiteral("name"))},
        {QStringLiteral("description"), nullable(row.value(QStringLiteral("description")))},
        {QStringLiteral("rootPath"), row.value(QStringLiteral("root_path"))},
        {QStringLiteral("archivedAt"), nullable(row.value(QStringLiteral("archived_at")))},
        {QStringLiteral("createdAt"), row.value(QStringLiteral("created_at"))},
        {QStringLiteral("updatedAt"), row.value(QStringLiteral("updated_at"))},
    };
}

QVariantList loadLibraries(CoreDatabase &database, const QString &where, const QString &orderBy)
{
    QVariantList libraries;

    for (const QVariantMap &row :
         database.prepare(QStringLiteral("SELECT %1 FROM libraries WHERE %2 ORDER BY %3").arg(libraryColumns, where, orderBy))
             .all()) {
        libraries.append(mapLibrary(row));
    }

    return libraries;
}

void requireLibraryId(const QString &libraryId)
{
    if (!isUuid(libraryId)) {
        throw CoreError(QStringLiteral("Invalid Library ID."));
    }
}

QVariantMap requireLibrary(CoreDatabase &database, const QString &libraryId)
{
    const auto library = LibraryRepository::libraryById(database, libraryId);

    if (!library) {
        throw CoreError(QStringLiteral("Library not found."));
    }

    return *library;
}

QVariant selectedLibraryId(CoreDatabase &database)
{
    return nullable(
        database.prepare(QStringLiteral("SELECT selected_library_id FROM app_settings WHERE id = 1")).scalar()
    );
}

void setSelectedLibraryId(CoreDatabase &database, const QVariant &libraryId)
{
    database
        .prepare(QStringLiteral("UPDATE app_settings SET selected_library_id = ?, updated_at = %1 WHERE id = 1").arg(nowSql))
        .bind(1, libraryId)
        .run();
}

// Optional trimmed string field: missing -> nullopt; wrong type or bad length
// throws "Invalid Library data.".
std::optional<QVariant> stringField(
    const QVariantMap &input,
    const QString &key,
    int minimum,
    int maximum,
    bool nullable
)
{
    const auto found = input.constFind(key);

    if (found == input.constEnd() || !found->isValid()) {
        return std::nullopt;
    }

    if (nullable && found->typeId() == QMetaType::Nullptr) {
        return QVariant::fromValue(nullptr);
    }

    if (found->typeId() != QMetaType::QString) {
        throw CoreError(QStringLiteral("Invalid Library data."));
    }

    const QString value = found->toString().trimmed();

    if (value.size() < minimum || value.size() > maximum) {
        throw CoreError(QStringLiteral("Invalid Library data."));
    }

    return QVariant(value);
}
}

namespace LibraryRepository
{
QVariantList activeLibraries(CoreDatabase &database)
{
    return loadLibraries(database, QStringLiteral("archived_at IS NULL"), QStringLiteral("updated_at DESC, created_at DESC"));
}

QVariantList archivedLibraries(CoreDatabase &database)
{
    return loadLibraries(database, QStringLiteral("archived_at IS NOT NULL"), QStringLiteral("archived_at DESC"));
}

std::optional<QVariantMap> libraryById(CoreDatabase &database, const QString &libraryId)
{
    const auto row = database.prepare(QStringLiteral("SELECT %1 FROM libraries WHERE id = ?").arg(libraryColumns))
                         .bind(1, libraryId)
                         .one();

    if (!row) {
        return std::nullopt;
    }

    return mapLibrary(*row);
}

QString normalizeRootPath(const QString &rootPath)
{
    const QFileInfo info(QDir::cleanPath(QFileInfo(rootPath).absoluteFilePath()));

    if (!info.exists()) {
        throw CoreError(QStringLiteral("The selected folder does not exist."));
    }

    if (!info.isDir()) {
        throw CoreError(QStringLiteral("The selected path is not a folder."));
    }

    if (!info.isReadable()) {
        throw CoreError(QStringLiteral("Archivist cannot read the selected folder."));
    }

    return info.canonicalFilePath();
}

QVariantMap createLibrary(CoreDatabase &database, const QVariantMap &input)
{
    const auto rootInput = stringField(input, QStringLiteral("rootPath"), 1, INT_MAX, false);

    if (!rootInput) {
        throw CoreError(QStringLiteral("Invalid Library data."));
    }

    const auto nameInput = stringField(input, QStringLiteral("name"), 1, 120, false);
    const auto descriptionInput = stringField(input, QStringLiteral("description"), 0, 500, false);
    const QString rootPath = normalizeRootPath(rootInput->toString());

    const auto existing = database.prepare(QStringLiteral("SELECT id, archived_at FROM libraries WHERE root_path = ?"))
                              .bind(1, rootPath)
                              .one();

    if (existing) {
        const QString existingId = existing->value(QStringLiteral("id")).toString();

        if (!isNullish(existing->value(QStringLiteral("archived_at")))) {
            throw CoreError(
                QStringLiteral("This folder belongs to an archived Library. Restore it instead."),
                {{QStringLiteral("code"), QStringLiteral("LIBRARY_ARCHIVED")}, {QStringLiteral("libraryId"), existingId}}
            );
        }

        throw CoreError(
            QStringLiteral("This folder is already registered as a Library."),
            {{QStringLiteral("code"), QStringLiteral("LIBRARY_EXISTS")}, {QStringLiteral("libraryId"), existingId}}
        );
    }

    QString name = nameInput ? nameInput->toString() : QString();

    if (name.isEmpty()) {
        name = QFileInfo(rootPath).fileName().trimmed();
    }

    if (name.isEmpty()) {
        name = QStringLiteral("New Library");
    }

    const QString libraryId = newId();
    database.prepare(QStringLiteral("INSERT INTO libraries (id, name, description, root_path) VALUES (?, ?, ?, ?)"))
        .bind(1, libraryId)
        .bind(2, name)
        .bind(3, descriptionInput ? nullableString(descriptionInput->toString()) : QVariant())
        .bind(4, rootPath)
        .run();

    if (isNullish(selectedLibraryId(database))) {
        setSelectedLibraryId(database, libraryId);
    }

    const auto library = libraryById(database, libraryId);

    if (!library) {
        throw CoreError(QStringLiteral("The newly created Library could not be loaded."));
    }

    return {
        {QStringLiteral("library"), *library},
        {QStringLiteral("selectedLibraryId"), selectedLibraryId(database)},
    };
}

QVariantMap updateLibrary(CoreDatabase &database, const QString &libraryId, const QVariantMap &input)
{
    requireLibraryId(libraryId);
    const auto name = stringField(input, QStringLiteral("name"), 1, 120, false);
    const auto description = stringField(input, QStringLiteral("description"), 0, 500, true);

    if (!name && !description) {
        throw CoreError(QStringLiteral("Invalid Library data."));
    }

    const QVariantMap current = requireLibrary(database, libraryId);
    QVariant nextDescription = nullable(current.value(QStringLiteral("description")));

    if (description) {
        nextDescription = isNullish(*description) ? QVariant() : *description;
    }

    database.prepare(QStringLiteral("UPDATE libraries SET name = ?, description = ?, updated_at = %1 WHERE id = ?").arg(nowSql))
        .bind(1, name ? *name : current.value(QStringLiteral("name")))
        .bind(2, nextDescription)
        .bind(3, libraryId)
        .run();

    const auto library = libraryById(database, libraryId);

    if (!library) {
        throw CoreError(QStringLiteral("The updated Library could not be loaded."));
    }

    return *library;
}

QVariantMap archiveLibrary(CoreDatabase &database, const QString &libraryId)
{
    requireLibraryId(libraryId);
    const QVariantMap library = requireLibrary(database, libraryId);

    if (!isNullish(library.value(QStringLiteral("archivedAt")))) {
        throw CoreError(QStringLiteral("Library is already archived."));
    }

    database.prepare(QStringLiteral("UPDATE libraries SET archived_at = %1, updated_at = %1 WHERE id = ?").arg(nowSql))
        .bind(1, libraryId)
        .run();

    QVariant selected = selectedLibraryId(database);

    if (selected.toString() == libraryId) {
        selected = nullable(database
                                .prepare(QStringLiteral(
                                    "SELECT id FROM libraries WHERE archived_at IS NULL AND id != ? "
                                    "ORDER BY updated_at DESC, created_at DESC LIMIT 1"
                                ))
                                .bind(1, libraryId)
                                .scalar());
        setSelectedLibraryId(database, selected);
    }

    const auto archived = libraryById(database, libraryId);

    if (!archived) {
        throw CoreError(QStringLiteral("The archived Library could not be loaded."));
    }

    return {
        {QStringLiteral("library"), *archived},
        {QStringLiteral("selectedLibraryId"), selected},
    };
}

QVariantMap restoreLibrary(CoreDatabase &database, const QString &libraryId)
{
    requireLibraryId(libraryId);
    const QVariantMap library = requireLibrary(database, libraryId);

    if (isNullish(library.value(QStringLiteral("archivedAt")))) {
        throw CoreError(QStringLiteral("Library is already active."));
    }

    database.prepare(QStringLiteral("UPDATE libraries SET archived_at = NULL, updated_at = %1 WHERE id = ?").arg(nowSql))
        .bind(1, libraryId)
        .run();

    const auto restored = libraryById(database, libraryId);

    if (!restored) {
        throw CoreError(QStringLiteral("The restored Library could not be loaded."));
    }

    return *restored;
}
}

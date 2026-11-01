#include "ai_tools.h"
#include "ai_tools_read.h"

#include "Core/Domains/Libraries/library_edit_inspection.h"
#include "Core/Domains/Libraries/library_file_repository.h"
#include "Core/Domains/Libraries/library_repository.h"
#include "Core/Domains/Workspace/workspace_compose_catalog.h"
#include "Core/Domains/Workspace/workspace_gateway.h"
#include "Core/Support/core_values.h"
#include "Core/Support/js_compat.h"
#include "Core/Support/json_values.h"
#include "Core/core_error.h"
#include "edit_proposals.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

using namespace CoreValues;
using AITools::Context;
using AITools::ToolError;
using AITools::ReadTools::ToolCancelled;
using AITools::ReadTools::isNumber;
using AITools::ReadTools::throwIfCancelled;
using AITools::ReadTools::requireLibraryId;
using AITools::ReadTools::listDirectory;
using AITools::ReadTools::searchFilenames;
using AITools::ReadTools::searchLibrary;
using AITools::ReadTools::readFile;
using AITools::ReadTools::readFileRange;
using AITools::ReadTools::readFileRanges;

namespace
{
const QString nowSql = QStringLiteral("strftime('%Y-%m-%dT%H:%M:%fZ', 'now')");

constexpr int maximumDirectoryEntries = 200;
constexpr int maximumFilenameMatches = 100;
constexpr int maximumSearchMatches = 24;
constexpr int maximumReadRangeLines = 160;
constexpr int maximumBatchReadRanges = 3;
constexpr int maximumBatchReadLines = 240;

const QString fileReferenceDescription = QStringLiteral(
    "Catalog UUID from a source file-id attribute, or the exact Library-relative path from its path attribute."
);

// Thrown when the run is cancelled mid-tool (Node's AbortError).
struct InvalidInput
{
    QVariantMap issue;
};

struct Definition
{
    QString id;
    QString name;
    QString description;
    QString permission;
    QString executionMode;
    QVariantMap schema;
    std::function<QVariantMap(const QVariant &)> parse;
    std::function<QVariantMap(const QVariantMap &, Context &)> run;
    std::function<QVariantMap(const QVariantMap &)> summarizeInput;
    std::function<QVariantMap(const QVariantMap &)> summarizeOutput;
};

// ---- input parsing (the zod schemas) ----

[[noreturn]] void invalid(const QString &path, const QString &message)
{
    throw InvalidInput{{
        {QStringLiteral("code"), QStringLiteral("custom")},
        {QStringLiteral("path"), path.isEmpty() ? QVariantList() : QVariantList{path}},
        {QStringLiteral("message"), message},
    }};
}

QVariantMap strictObject(const QVariant &input, const QStringList &keys)
{
    if (input.typeId() != QMetaType::QVariantMap) {
        invalid({}, QStringLiteral("Expected object."));
    }

    const QVariantMap map = input.toMap();

    for (auto iterator = map.cbegin(); iterator != map.cend(); ++iterator) {
        if (!keys.contains(iterator.key())) {
            invalid(iterator.key(), QStringLiteral("Unrecognized key."));
        }
    }

    return map;
}

QString stringField(const QVariantMap &map, const QString &key, int minimum, int maximum, bool trim = false, std::optional<QString> fallback = std::nullopt)
{
    const QVariant value = map.value(key);

    if (!value.isValid() && fallback) {
        return *fallback;
    }

    if (value.typeId() != QMetaType::QString) {
        invalid(key, QStringLiteral("Expected string."));
    }

    const QString text = trim ? JsCompat::trimmed(value.toString()) : value.toString();

    if (text.size() < minimum || text.size() > maximum) {
        invalid(key, QStringLiteral("String length is out of range."));
    }

    return text;
}

int intField(const QVariantMap &map, const QString &key, qint64 minimum, qint64 maximum, std::optional<int> fallback = std::nullopt)
{
    const QVariant value = map.value(key);

    if (!value.isValid() && fallback) {
        return *fallback;
    }

    if (!isNumber(value)) {
        invalid(key, QStringLiteral("Expected number."));
    }

    const double number = value.toDouble();

    if (!std::isfinite(number) || std::floor(number) != number) {
        invalid(key, QStringLiteral("Expected integer."));
    }

    if (number < static_cast<double>(minimum) || number > static_cast<double>(maximum)) {
        invalid(key, QStringLiteral("Number is out of range."));
    }

    return static_cast<int>(number);
}

QVariantMap parseRange(const QVariant &input)
{
    const QVariantMap map = strictObject(input, {QStringLiteral("fileId"), QStringLiteral("startLine"), QStringLiteral("endLine")});
    const QString fileId = stringField(map, QStringLiteral("fileId"), 1, 2048);
    const int startLine = intField(map, QStringLiteral("startLine"), 1, std::numeric_limits<int>::max());
    const int endLine = intField(map, QStringLiteral("endLine"), 1, std::numeric_limits<int>::max());

    if (endLine < startLine) {
        invalid(QStringLiteral("endLine"), QStringLiteral("endLine must be greater than or equal to startLine."));
    }

    if (endLine - startLine + 1 > maximumReadRangeLines) {
        invalid(QStringLiteral("endLine"), QStringLiteral("A file range may include at most %1 lines.").arg(maximumReadRangeLines));
    }

    return {{QStringLiteral("fileId"), fileId}, {QStringLiteral("startLine"), startLine}, {QStringLiteral("endLine"), endLine}};
}

// ---- shared helpers ----

QString sha256Hex(const QString &text)
{
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QVariantMap objectSchema(const QVariantMap &properties, const QStringList &required)
{
    return {
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"), properties},
        {QStringLiteral("required"), toVariantList(required)},
        {QStringLiteral("additionalProperties"), false},
    };
}

QVariantMap stringSchema(int minimum, int maximum, const QString &description = {})
{
    QVariantMap schema{{QStringLiteral("type"), QStringLiteral("string")}};

    if (minimum > 0) {
        schema.insert(QStringLiteral("minLength"), minimum);
    }

    if (maximum > 0) {
        schema.insert(QStringLiteral("maxLength"), maximum);
    }

    if (!description.isEmpty()) {
        schema.insert(QStringLiteral("description"), description);
    }

    return schema;
}

QVariantMap integerSchema(int minimum, int maximum = 0)
{
    QVariantMap schema{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), minimum}};

    if (maximum > 0) {
        schema.insert(QStringLiteral("maximum"), maximum);
    }

    return schema;
}

QVariantMap rangeSchemaProperties()
{
    return {
        {QStringLiteral("fileId"), stringSchema(1, 2048, fileReferenceDescription)},
        {QStringLiteral("startLine"), integerSchema(1)},
        {QStringLiteral("endLine"), integerSchema(1)},
    };
}

// ---- proposal tools ----

QVariantMap proposeAndAnnounce(
    Context &context,
    const QString &summary,
    const QVariantMap &draft,
    const QString &failureMessage,
    const QString &expectedHash = {}
)
{
    const QVariantMap proposal = AIEditProposals::propose(*context.database, context.runId, summary, {draft});
    const QVariantList operations = proposal.value(QStringLiteral("operations")).toList();

    if (operations.isEmpty()) {
        throw CoreError(failureMessage);
    }

    const QVariantMap operation = operations.last().toMap();
    QVariantList types;

    for (const QVariant &item : operations) {
        types.append(item.toMap().value(QStringLiteral("type")));
    }

    QVariantMap payload{
        {QStringLiteral("proposalId"), proposal.value(QStringLiteral("id"))},
        {QStringLiteral("executionId"), context.executionId},
        {QStringLiteral("operationId"), operation.value(QStringLiteral("id"))},
        {QStringLiteral("operationCount"), operations.size()},
        {QStringLiteral("operationTypes"), types},
        {QStringLiteral("affectedPaths"), toVariantList(AIEditProposals::affectedPaths(operations))},
        {QStringLiteral("writesPerformed"), false},
    };

    if (!expectedHash.isEmpty()) {
        payload.insert(QStringLiteral("expectedHash"), expectedHash);
    }

    QVariantMap result{
        {QStringLiteral("proposalId"), proposal.value(QStringLiteral("id"))},
        {QStringLiteral("operationId"), operation.value(QStringLiteral("id"))},
    };

    if (context.emitEvent) {
        // Emitted after the caller builds its output, like the Node tools.
        result.insert(QStringLiteral("__payload"), payload);
    }

    return result;
}

QVariantMap finishProposal(Context &context, QVariantMap proposed, QVariantMap output)
{
    const QVariantMap payload = proposed.take(QStringLiteral("__payload")).toMap();
    output.insert(QStringLiteral("proposalId"), proposed.value(QStringLiteral("proposalId")));
    output.insert(QStringLiteral("operationId"), proposed.value(QStringLiteral("operationId")));
    output.insert(QStringLiteral("status"), QStringLiteral("proposed"));
    output.insert(QStringLiteral("writesPerformed"), false);

    if (context.emitEvent) {
        context.emitEvent(QStringLiteral("edit.proposed"), payload);
    }

    return output;
}

QVariantMap createFileTool(const QVariantMap &input, Context &context)
{
    throwIfCancelled(context);
    const QString libraryId = requireLibraryId(context);
    const auto target = LibraryEditInspection::createTarget(*context.database, libraryId, input.value(QStringLiteral("path")).toString());
    throwIfCancelled(context);
    const QString content = input.value(QStringLiteral("content")).toString();
    const QVariantMap proposed = proposeAndAnnounce(
        context,
        QStringLiteral("Create %1").arg(target.relativePath),
        {{QStringLiteral("type"), QStringLiteral("create_file")}, {QStringLiteral("destinationPath"), target.relativePath}, {QStringLiteral("afterContent"), content}},
        QStringLiteral("The create-file proposal did not retain its operation.")
    );

    return finishProposal(context, proposed, {
        {QStringLiteral("operationType"), QStringLiteral("create_file")},
        {QStringLiteral("relativePath"), target.relativePath},
        {QStringLiteral("expectedState"), target.expectedState},
        {QStringLiteral("contentSha256"), sha256Hex(content)},
    });
}

QVariantMap patchFileTool(const QVariantMap &input, Context &context)
{
    throwIfCancelled(context);
    const QString libraryId = requireLibraryId(context);
    const auto source = LibraryEditInspection::textFile(*context.database, libraryId, input.value(QStringLiteral("path")).toString());
    throwIfCancelled(context);
    const QString content = input.value(QStringLiteral("content")).toString();

    if (source.content == content) {
        throw ToolError(QStringLiteral("invalid_input"), QStringLiteral("patch_file requires proposed content to differ from the current file."));
    }

    const QVariantMap proposed = proposeAndAnnounce(
        context,
        QStringLiteral("Patch %1").arg(source.relativePath),
        {
            {QStringLiteral("type"), QStringLiteral("patch_file")},
            {QStringLiteral("sourcePath"), source.relativePath},
            {QStringLiteral("expectedHash"), source.sha256},
            {QStringLiteral("beforeContent"), source.content},
            {QStringLiteral("afterContent"), content},
        },
        QStringLiteral("The patch-file proposal did not retain its operation."),
        source.sha256
    );

    return finishProposal(context, proposed, {
        {QStringLiteral("operationType"), QStringLiteral("patch_file")},
        {QStringLiteral("relativePath"), source.relativePath},
        {QStringLiteral("expectedHash"), source.sha256},
        {QStringLiteral("proposedContentSha256"), sha256Hex(content)},
        {QStringLiteral("beforeSizeBytes"), source.sizeBytes},
        {QStringLiteral("afterSizeBytes"), content.toUtf8().size()},
        {QStringLiteral("sourceModifiedAt"), source.modifiedAt},
    });
}

QVariantMap createDirectoryTool(const QVariantMap &input, Context &context)
{
    throwIfCancelled(context);
    const QString libraryId = requireLibraryId(context);
    const auto target =
        LibraryEditInspection::directoryCreateTarget(*context.database, libraryId, input.value(QStringLiteral("path")).toString());
    throwIfCancelled(context);
    const QVariantMap proposed = proposeAndAnnounce(
        context,
        QStringLiteral("Create directory %1").arg(target.relativePath),
        {{QStringLiteral("type"), QStringLiteral("create_directory")}, {QStringLiteral("destinationPath"), target.relativePath}},
        QStringLiteral("The create-directory proposal did not retain its operation.")
    );

    return finishProposal(context, proposed, {
        {QStringLiteral("operationType"), QStringLiteral("create_directory")},
        {QStringLiteral("relativePath"), target.relativePath},
        {QStringLiteral("expectedState"), target.expectedState},
    });
}

QVariantMap relocateTool(const QVariantMap &input, Context &context, const QString &type)
{
    throwIfCancelled(context);
    const QString libraryId = requireLibraryId(context);
    const auto source = LibraryEditInspection::textFile(*context.database, libraryId, input.value(QStringLiteral("sourcePath")).toString());
    const auto destination =
        LibraryEditInspection::createTarget(*context.database, libraryId, input.value(QStringLiteral("destinationPath")).toString());
    throwIfCancelled(context);
    const bool rename = type == QStringLiteral("rename_file");
    const QVariantMap proposed = proposeAndAnnounce(
        context,
        QStringLiteral("%1 %2 to %3").arg(rename ? QStringLiteral("Rename") : QStringLiteral("Move"), source.relativePath, destination.relativePath),
        {
            {QStringLiteral("type"), type},
            {QStringLiteral("sourcePath"), source.relativePath},
            {QStringLiteral("destinationPath"), destination.relativePath},
            {QStringLiteral("expectedHash"), source.sha256},
        },
        rename ? QStringLiteral("The rename-file proposal did not retain its operation.")
               : QStringLiteral("The move-file proposal did not retain its operation."),
        source.sha256
    );

    return finishProposal(context, proposed, {
        {QStringLiteral("operationType"), type},
        {QStringLiteral("sourcePath"), source.relativePath},
        {QStringLiteral("destinationPath"), destination.relativePath},
        {QStringLiteral("expectedHash"), source.sha256},
        {QStringLiteral("sourceSizeBytes"), source.sizeBytes},
        {QStringLiteral("sourceModifiedAt"), source.modifiedAt},
    });
}

QVariantMap pick(const QVariantMap &map, const QStringList &keys)
{
    QVariantMap result;

    for (const QString &key : keys) {
        result.insert(key, map.value(key));
    }

    return result;
}

QVariantMap proposalOutputSummary(const QVariantMap &output, const QStringList &keys)
{
    QVariantMap summary = pick(output, keys);
    summary.insert(QStringLiteral("proposalId"), output.value(QStringLiteral("proposalId")));
    summary.insert(QStringLiteral("writesPerformed"), output.value(QStringLiteral("writesPerformed")));
    return summary;
}

int requestedLines(const QVariantList &ranges)
{
    int total = 0;

    for (const QVariant &range : ranges) {
        const QVariantMap map = range.toMap();
        total += map.value(QStringLiteral("endLine")).toInt() - map.value(QStringLiteral("startLine")).toInt() + 1;
    }

    return total;
}

// ---- workspace tools ----

QVariantMap enumSchema(const QStringList &values, const QString &description = {})
{
    QVariantMap schema{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("enum"), toVariantList(values)}};
    if (!description.isEmpty()) {
        schema.insert(QStringLiteral("description"), description);
    }
    return schema;
}

QString enumField(const QVariantMap &map, const QString &key, const QStringList &values)
{
    const QString value = stringField(map, key, 1, 64);
    if (!values.contains(value)) {
        invalid(key, QStringLiteral("Expected one of: %1.").arg(values.join(QStringLiteral(", "))));
    }
    return value;
}

bool boolField(const QVariantMap &map, const QString &key)
{
    const QVariant value = map.value(key);
    if (value.typeId() != QMetaType::Bool) {
        invalid(key, QStringLiteral("Expected boolean."));
    }
    return value.toBool();
}

const QStringList &showKinds()
{
    static const QStringList kinds{
        QStringLiteral("file"), QStringLiteral("chat"), QStringLiteral("terminal"), QStringLiteral("diff"), QStringLiteral("open_view"),
    };
    return kinds;
}

const QStringList &arrangeActions()
{
    static const QStringList actions{
        QStringLiteral("focus"), QStringLiteral("resize"), QStringLiteral("move"), QStringLiteral("keep"), QStringLiteral("label"),
        QStringLiteral("add"), QStringLiteral("remove"),
    };
    return actions;
}

const QStringList &restoreScopes()
{
    static const QStringList scopes{QStringLiteral("last_change"), QStringLiteral("redo"), QStringLiteral("before_task"), QStringLiteral("saved_layout")};
    return scopes;
}

const QStringList &layoutOps()
{
    static const QStringList ops{QStringLiteral("list"), QStringLiteral("apply"), QStringLiteral("save"), QStringLiteral("rename"), QStringLiteral("delete")};
    return ops;
}

const QStringList &preferenceOps()
{
    static const QStringList ops{QStringLiteral("set"), QStringLiteral("clear"), QStringLiteral("list")};
    return ops;
}

const QStringList &preferenceKeys()
{
    static const QStringList keys{QStringLiteral("chat_location"), QStringLiteral("main_editor"), QStringLiteral("rearranging"), QStringLiteral("area_purpose")};
    return keys;
}

const QStringList &preferenceDurations()
{
    static const QStringList durations{QStringLiteral("this_session"), QStringLiteral("always")};
    return durations;
}

// The Library a file reference belongs to: this chat's, or another by id or
// name (a worktree is a Library of its own).
QVariantMap resolveLibrary(CoreDatabase &database, const Context &context, const QString &reference)
{
    const QString wanted = JsCompat::trimmed(reference);
    const QVariantList libraries = LibraryRepository::activeLibraries(database);

    if (wanted.isEmpty()) {
        if (context.libraryId.isEmpty()) {
            throw ToolError(QStringLiteral("invalid_input"), QStringLiteral("This chat has no Library; name the Library the file is in."));
        }
        for (const QVariant &value : libraries) {
            if (value.toMap().value(QStringLiteral("id")).toString() == context.libraryId) {
                return value.toMap();
            }
        }
        return {{QStringLiteral("id"), context.libraryId}};
    }

    QVariantMap partial;
    for (const QVariant &value : libraries) {
        const QVariantMap library = value.toMap();
        const QString id = library.value(QStringLiteral("id")).toString();
        const QString name = library.value(QStringLiteral("name")).toString();
        if (id == wanted || name.compare(wanted, Qt::CaseInsensitive) == 0) {
            return library;
        }
        if (partial.isEmpty() && wanted.size() >= 3 && name.contains(wanted, Qt::CaseInsensitive)) {
            partial = library;
        }
    }

    if (!partial.isEmpty()) {
        return partial;
    }

    QStringList names;
    for (const QVariant &value : libraries) {
        names.append(value.toMap().value(QStringLiteral("name")).toString());
    }
    throw ToolError(
        QStringLiteral("invalid_input"),
        QStringLiteral("No Library is called \"%1\". Libraries: %2.").arg(wanted, names.join(QStringLiteral(", ")))
    );
}

QVariantMap callWorkspace(Context &context, QVariantMap request)
{
    request.insert(QStringLiteral("runId"), context.runId);
    request.insert(QStringLiteral("chatId"), context.chatId);
    request.insert(QStringLiteral("explicit"), context.workspaceExplicit);
    request.insert(QStringLiteral("asked"), context.workspaceAsked);
    const QVariantMap result = context.workspace ? context.workspace(request) : Workspace::Gateway::call(request);

    if (!result.value(QStringLiteral("ok")).toBool()) {
        const QString code = result.value(QStringLiteral("code")).toString();
        const QString message = result.value(QStringLiteral("error")).toString();
        const QString toolCode = code == QStringLiteral("LAYOUT_LOCKED") || code == QStringLiteral("CONTENT_LOCKED")
                || code == QStringLiteral("UNLOCK_REFUSED") || code == QStringLiteral("NOT_ALLOWED")
            ? QStringLiteral("permission_denied")
            : code == QStringLiteral("NOT_FOUND") || code == QStringLiteral("INVALID_ARGUMENT") || code == QStringLiteral("UNSUPPORTED")
            ? QStringLiteral("invalid_input")
            : QStringLiteral("execution_failed");
        throw ToolError(toolCode, message.isEmpty() ? QStringLiteral("The workspace change failed.") : message, {{QStringLiteral("workspaceCode"), code}});
    }

    QVariantMap output = result;
    output.remove(QStringLiteral("ok"));
    return output;
}

QVariantMap workspaceViewTool(const QVariantMap &, Context &context)
{
    return callWorkspace(context, {{QStringLiteral("action"), QStringLiteral("describe")}});
}

// A tool item (file by path, chat, terminal, open view) in workspace terms.
// Files are looked up here, where the Library is.
QVariantMap resolveShowItem(const QVariantMap &item, Context &context)
{
    const QString kind = item.value(QStringLiteral("kind")).toString();
    const QString reference = item.value(QStringLiteral("reference")).toString();

    if (kind == QStringLiteral("file")) {
        if (JsCompat::isBlank(reference)) {
            throw ToolError(QStringLiteral("invalid_input"), QStringLiteral("A file needs its Library path, file name or file id."));
        }
        // Looked up in the catalog here; the workspace then prefers a view
        // that already shows it (it may be open from elsewhere), so the
        // lookup's verdict travels along instead of failing the call.
        const QVariantMap library = resolveLibrary(*context.database, context, item.value(QStringLiteral("library")).toString());
        const QString libraryId = library.value(QStringLiteral("id")).toString();
        QVariantMap resolved{
            {QStringLiteral("kind"), kind},
            {QStringLiteral("reference"), reference},
            {QStringLiteral("libraryId"), libraryId},
        };
        const QString wanted = QString(reference).replace(QLatin1Char('\\'), QLatin1Char('/')).trimmed();
        const QString lowered = wanted.toLower();
        QVariantList exact;
        QVariantList byName;
        for (const QVariant &value : LibraryFileRepository::files(*context.database, libraryId)) {
            const QVariantMap file = value.toMap();
            const QString path = file.value(QStringLiteral("relativePath")).toString();
            if (file.value(QStringLiteral("id")).toString() == wanted || path == wanted) {
                exact.append(file);
            } else if (path.toLower() == lowered || path.toLower().endsWith(QLatin1Char('/') + lowered)
                       || file.value(QStringLiteral("name")).toString().toLower() == lowered) {
                byName.append(file);
            }
        }
        const QVariantList &matches = exact.isEmpty() ? byName : exact;
        if (matches.size() == 1) {
            const QString fileId = matches.first().toMap().value(QStringLiteral("id")).toString();
            const auto file = LibraryFileRepository::fileById(*context.database, libraryId, fileId);
            resolved.insert(QStringLiteral("lookup"), file ? QStringLiteral("found") : QStringLiteral("not_found"));
            if (file) {
                resolved.insert(QStringLiteral("file"), *file);
            }
        } else if (matches.size() > 1) {
            QStringList candidates;
            for (const QVariant &match : matches.mid(0, 5)) {
                candidates.append(match.toMap().value(QStringLiteral("relativePath")).toString());
            }
            resolved.insert(QStringLiteral("lookup"), QStringLiteral("ambiguous"));
            resolved.insert(QStringLiteral("candidates"), candidates);
        } else {
            resolved.insert(QStringLiteral("lookup"), QStringLiteral("not_found"));
        }
        return resolved;
    }

    QVariantMap resolved{{QStringLiteral("kind"), kind}, {QStringLiteral("reference"), reference}};
    if (kind == QStringLiteral("terminal") && !JsCompat::isBlank(item.value(QStringLiteral("library")).toString())) {
        resolved.insert(QStringLiteral("libraryId"), resolveLibrary(*context.database, context, item.value(QStringLiteral("library")).toString()).value(QStringLiteral("id")));
    }
    return resolved;
}

QVariantMap workspaceShowTool(const QVariantMap &input, Context &context)
{
    QVariantList items;
    for (const QVariant &value : input.value(QStringLiteral("items")).toList()) {
        items.append(resolveShowItem(value.toMap(), context));
    }

    return callWorkspace(context, {
        {QStringLiteral("action"), QStringLiteral("show")},
        {QStringLiteral("items"), items},
        {QStringLiteral("where"), input.value(QStringLiteral("where"))},
        {QStringLiteral("purpose"), input.value(QStringLiteral("purpose"))},
        {QStringLiteral("focus"), input.value(QStringLiteral("focus"))},
    });
}

QVariantMap workspaceArrangeTool(const QVariantMap &input, Context &context)
{
    QVariantList steps;
    for (const QVariant &value : input.value(QStringLiteral("steps")).toList()) {
        const QVariantMap step = value.toMap();
        steps.append(QVariantMap{
            {QStringLiteral("action"), step.value(QStringLiteral("action"))},
            {QStringLiteral("target"), step.value(QStringLiteral("area"))},
            {QStringLiteral("change"), step.value(QStringLiteral("change"))},
            {QStringLiteral("to"), step.value(QStringLiteral("to"))},
            {QStringLiteral("name"), step.value(QStringLiteral("name"))},
        });
    }

    // One step goes as itself; several go as one change that undoes together.
    QVariantMap request = steps.size() == 1
        ? steps.first().toMap()
        : QVariantMap{{QStringLiteral("action"), QStringLiteral("steps")}, {QStringLiteral("steps"), steps}};
    request.insert(QStringLiteral("saveAs"), input.value(QStringLiteral("save_as")));
    return callWorkspace(context, request);
}

QVariantMap workspaceComposeTool(const QVariantMap &input, Context &context)
{
    // Everything that can be found goes in; the rest is reported back.
    QStringList unresolved;
    QVariantList panelSlots;
    for (const QVariant &value : input.value(QStringLiteral("slots")).toList()) {
        QVariantMap slot = value.toMap();
        QVariantList items;
        for (const QVariant &item : slot.value(QStringLiteral("items")).toList()) {
            try {
                items.append(resolveShowItem(item.toMap(), context));
            } catch (const ToolError &error) {
                unresolved.append(error.message());
            }
        }
        slot.insert(QStringLiteral("items"), items);
        panelSlots.append(slot);
    }

    const QString templateId = input.value(QStringLiteral("template")).toString();
    const QString strategy = input.value(QStringLiteral("arrangement")).toString();
    return callWorkspace(context, {
        {QStringLiteral("action"), QStringLiteral("compose")},
        {QStringLiteral("mode"), input.value(QStringLiteral("mode"))},
        {QStringLiteral("template"), templateId == QStringLiteral("none") ? QString() : templateId},
        {QStringLiteral("strategy"), strategy == QStringLiteral("auto") ? QString() : strategy},
        {QStringLiteral("rows"), input.value(QStringLiteral("rows"))},
        {QStringLiteral("columns"), input.value(QStringLiteral("columns"))},
        {QStringLiteral("slots"), panelSlots},
        {QStringLiteral("focus"), input.value(QStringLiteral("focus"))},
        {QStringLiteral("saveAs"), input.value(QStringLiteral("save_as"))},
        {QStringLiteral("unresolved"), unresolved},
    });
}

QVariantMap workspaceRestoreTool(const QVariantMap &input, Context &context)
{
    return callWorkspace(context, {
        {QStringLiteral("action"), QStringLiteral("restore")},
        {QStringLiteral("scope"), input.value(QStringLiteral("scope"))},
        {QStringLiteral("layout"), input.value(QStringLiteral("layout"))},
    });
}

QVariantMap workspaceLayoutTool(const QVariantMap &input, Context &context)
{
    return callWorkspace(context, {
        {QStringLiteral("action"), QStringLiteral("layout")},
        {QStringLiteral("op"), input.value(QStringLiteral("action"))},
        {QStringLiteral("name"), input.value(QStringLiteral("name"))},
        {QStringLiteral("newName"), input.value(QStringLiteral("new_name"))},
        {QStringLiteral("replace"), input.value(QStringLiteral("replace"))},
    });
}

QVariantMap workspacePreferenceTool(const QVariantMap &input, Context &context)
{
    return callWorkspace(context, {
        {QStringLiteral("action"), QStringLiteral("preference")},
        {QStringLiteral("op"), input.value(QStringLiteral("action"))},
        {QStringLiteral("preference"), input.value(QStringLiteral("preference"))},
        {QStringLiteral("value"), input.value(QStringLiteral("value"))},
        {QStringLiteral("area"), input.value(QStringLiteral("area"))},
        {QStringLiteral("lasting"), input.value(QStringLiteral("duration")).toString() == QStringLiteral("always")},
        {QStringLiteral("lastingAllowed"), context.workspaceLastingPreference},
    });
}

QVariantMap workspaceOutputSummary(const QVariantMap &output)
{
    return {
        {QStringLiteral("status"), output.value(QStringLiteral("status"))},
        {QStringLiteral("notes"), output.value(QStringLiteral("notes"))},
    };
}

const QList<Definition> &definitions()
{
    static const QList<Definition> list = []() {
        QList<Definition> tools;
        const QString readOnly = QStringLiteral("read-only");
        const QString mutation = QStringLiteral("safe-local-mutation");
        const QString direct = QStringLiteral("direct");
        const QString proposal = QStringLiteral("proposal");

        tools.append({
            QStringLiteral("list_directory"),
            QStringLiteral("List Library Directory"),
            QStringLiteral("List the immediate files and folders beneath one directory in the active Library catalog."),
            readOnly, direct,
            objectSchema({{QStringLiteral("path"), stringSchema(0, 1024)}, {QStringLiteral("limit"), integerSchema(1, maximumDirectoryEntries)}},
                         {QStringLiteral("path"), QStringLiteral("limit")}),
            [](const QVariant &input) {
                const QVariantMap map = strictObject(input, {QStringLiteral("path"), QStringLiteral("limit")});
                return QVariantMap{
                    {QStringLiteral("path"), stringField(map, QStringLiteral("path"), 0, 1024, false, QString())},
                    {QStringLiteral("limit"), intField(map, QStringLiteral("limit"), 1, maximumDirectoryEntries, 100)},
                };
            },
            listDirectory,
            [](const QVariantMap &input) {
                const QString path = input.value(QStringLiteral("path")).toString();
                return QVariantMap{{QStringLiteral("path"), path.isEmpty() ? QStringLiteral("/") : path}, {QStringLiteral("limit"), input.value(QStringLiteral("limit"))}};
            },
            [](const QVariantMap &output) {
                const QString path = output.value(QStringLiteral("path")).toString();
                return QVariantMap{
                    {QStringLiteral("path"), path.isEmpty() ? QStringLiteral("/") : path},
                    {QStringLiteral("entryCount"), output.value(QStringLiteral("entries")).toList().size()},
                    {QStringLiteral("totalEntries"), output.value(QStringLiteral("totalEntries"))},
                    {QStringLiteral("truncated"), output.value(QStringLiteral("truncated"))},
                };
            },
        });

        tools.append({
            QStringLiteral("search_filenames"),
            QStringLiteral("Search Library Filenames"),
            QStringLiteral("Find active Library files whose name or relative path contains a case-insensitive text query."),
            readOnly, direct,
            objectSchema({{QStringLiteral("query"), stringSchema(1, 256)}, {QStringLiteral("limit"), integerSchema(1, maximumFilenameMatches)}},
                         {QStringLiteral("query"), QStringLiteral("limit")}),
            [](const QVariant &input) {
                const QVariantMap map = strictObject(input, {QStringLiteral("query"), QStringLiteral("limit")});
                return QVariantMap{
                    {QStringLiteral("query"), stringField(map, QStringLiteral("query"), 1, 256, true)},
                    {QStringLiteral("limit"), intField(map, QStringLiteral("limit"), 1, maximumFilenameMatches, 25)},
                };
            },
            searchFilenames,
            [](const QVariantMap &input) { return pick(input, {QStringLiteral("query"), QStringLiteral("limit")}); },
            [](const QVariantMap &output) {
                return QVariantMap{
                    {QStringLiteral("matchCount"), output.value(QStringLiteral("matches")).toList().size()},
                    {QStringLiteral("totalMatches"), output.value(QStringLiteral("totalMatches"))},
                    {QStringLiteral("truncated"), output.value(QStringLiteral("truncated"))},
                };
            },
        });

        tools.append({
            QStringLiteral("search_library"),
            QStringLiteral("Search Library Text"),
            QStringLiteral("Search indexed Library text with SQLite FTS5 and return grounded file excerpts with line ranges."),
            readOnly, direct,
            objectSchema({{QStringLiteral("query"), stringSchema(1, 2000)}, {QStringLiteral("limit"), integerSchema(1, maximumSearchMatches)}},
                         {QStringLiteral("query"), QStringLiteral("limit")}),
            [](const QVariant &input) {
                const QVariantMap map = strictObject(input, {QStringLiteral("query"), QStringLiteral("limit")});
                return QVariantMap{
                    {QStringLiteral("query"), stringField(map, QStringLiteral("query"), 1, 2000, true)},
                    {QStringLiteral("limit"), intField(map, QStringLiteral("limit"), 1, maximumSearchMatches, 8)},
                };
            },
            searchLibrary,
            [](const QVariantMap &input) { return pick(input, {QStringLiteral("query"), QStringLiteral("limit")}); },
            [](const QVariantMap &output) {
                const QVariantList matches = output.value(QStringLiteral("matches")).toList();
                QSet<QString> files;

                for (const QVariant &match : matches) {
                    files.insert(match.toMap().value(QStringLiteral("fileId")).toString());
                }

                return QVariantMap{
                    {QStringLiteral("matchCount"), matches.size()},
                    {QStringLiteral("fileCount"), files.size()},
                    {QStringLiteral("warningCount"), output.value(QStringLiteral("warnings")).toList().size()},
                    {QStringLiteral("durationMs"), output.value(QStringLiteral("durationMs"))},
                };
            },
        });

        tools.append({
            QStringLiteral("read_file"),
            QStringLiteral("Read Library File"),
            QStringLiteral("Read a guarded UTF-8 Library file by catalog ID or exact Library-relative path, capped for safe tool output."),
            readOnly, direct,
            objectSchema({{QStringLiteral("fileId"), stringSchema(1, 2048, fileReferenceDescription)}}, {QStringLiteral("fileId")}),
            [](const QVariant &input) {
                const QVariantMap map = strictObject(input, {QStringLiteral("fileId")});
                return QVariantMap{{QStringLiteral("fileId"), stringField(map, QStringLiteral("fileId"), 1, 2048)}};
            },
            readFile,
            [](const QVariantMap &input) { return QVariantMap{{QStringLiteral("fileReference"), input.value(QStringLiteral("fileId"))}}; },
            [](const QVariantMap &output) {
                return QVariantMap{
                    {QStringLiteral("relativePath"), output.value(QStringLiteral("file")).toMap().value(QStringLiteral("relativePath"))},
                    {QStringLiteral("lineCount"), output.value(QStringLiteral("lineCount"))},
                    {QStringLiteral("characterCount"), output.value(QStringLiteral("content")).toString().size()},
                    {QStringLiteral("truncated"), output.value(QStringLiteral("truncated"))},
                };
            },
        });

        tools.append({
            QStringLiteral("read_file_range"),
            QStringLiteral("Read Library File Range"),
            QStringLiteral("Read at most %1 lines from a guarded UTF-8 Library file by catalog ID or exact Library-relative path. Prefer copying one supplied source block's path and line range exactly.")
                .arg(maximumReadRangeLines),
            readOnly, direct,
            objectSchema(rangeSchemaProperties(), {QStringLiteral("fileId"), QStringLiteral("startLine"), QStringLiteral("endLine")}),
            parseRange,
            readFileRange,
            [](const QVariantMap &input) {
                return QVariantMap{
                    {QStringLiteral("fileReference"), input.value(QStringLiteral("fileId"))},
                    {QStringLiteral("startLine"), input.value(QStringLiteral("startLine"))},
                    {QStringLiteral("endLine"), input.value(QStringLiteral("endLine"))},
                };
            },
            [](const QVariantMap &output) {
                return QVariantMap{
                    {QStringLiteral("relativePath"), output.value(QStringLiteral("file")).toMap().value(QStringLiteral("relativePath"))},
                    {QStringLiteral("startLine"), output.value(QStringLiteral("startLine"))},
                    {QStringLiteral("endLine"), output.value(QStringLiteral("endLine"))},
                    {QStringLiteral("characterCount"), output.value(QStringLiteral("content")).toString().size()},
                    {QStringLiteral("truncated"), output.value(QStringLiteral("truncated"))},
                };
            },
        });

        QVariantMap rangesProperty{
            {QStringLiteral("type"), QStringLiteral("array")},
            {QStringLiteral("minItems"), 1},
            {QStringLiteral("maxItems"), maximumBatchReadRanges},
            {QStringLiteral("items"), objectSchema(rangeSchemaProperties(), {QStringLiteral("fileId"), QStringLiteral("startLine"), QStringLiteral("endLine")})},
        };

        tools.append({
            QStringLiteral("read_file_ranges"),
            QStringLiteral("Read Library File Ranges"),
            QStringLiteral("Read up to %1 guarded Library excerpts in one tool call. Each range is capped at %2 lines and the batch at %3 total lines. Prefer copying supplied source handles exactly.")
                .arg(maximumBatchReadRanges)
                .arg(maximumReadRangeLines)
                .arg(maximumBatchReadLines),
            readOnly, direct,
            objectSchema({{QStringLiteral("ranges"), rangesProperty}}, {QStringLiteral("ranges")}),
            [](const QVariant &input) {
                const QVariantMap map = strictObject(input, {QStringLiteral("ranges")});
                const QVariant value = map.value(QStringLiteral("ranges"));

                if (value.typeId() != QMetaType::QVariantList) {
                    invalid(QStringLiteral("ranges"), QStringLiteral("Expected array."));
                }

                const QVariantList list = value.toList();

                if (list.isEmpty() || list.size() > maximumBatchReadRanges) {
                    invalid(QStringLiteral("ranges"), QStringLiteral("Array length is out of range."));
                }

                QVariantList ranges;

                for (const QVariant &range : list) {
                    ranges.append(parseRange(range));
                }

                if (requestedLines(ranges) > maximumBatchReadLines) {
                    invalid(QStringLiteral("ranges"), QStringLiteral("A batched file read may request at most %1 total lines.").arg(maximumBatchReadLines));
                }

                return QVariantMap{{QStringLiteral("ranges"), ranges}};
            },
            readFileRanges,
            [](const QVariantMap &input) {
                const QVariantList ranges = input.value(QStringLiteral("ranges")).toList();
                QVariantList paths;

                for (const QVariant &range : ranges) {
                    paths.append(range.toMap().value(QStringLiteral("fileId")));
                }

                return QVariantMap{
                    {QStringLiteral("rangeCount"), ranges.size()},
                    {QStringLiteral("totalRequestedLines"), requestedLines(ranges)},
                    {QStringLiteral("paths"), paths},
                };
            },
            [](const QVariantMap &output) {
                QVariantList paths;

                for (const QVariant &range : output.value(QStringLiteral("ranges")).toList()) {
                    paths.append(range.toMap().value(QStringLiteral("file")).toMap().value(QStringLiteral("relativePath")));
                }

                return QVariantMap{
                    {QStringLiteral("rangeCount"), output.value(QStringLiteral("rangeCount"))},
                    {QStringLiteral("paths"), paths},
                    {QStringLiteral("returnedCharacterCount"), output.value(QStringLiteral("returnedCharacterCount"))},
                    {QStringLiteral("truncated"), output.value(QStringLiteral("truncated"))},
                };
            },
        });

        const auto pathAndContent = [](const QVariant &input) {
            const QVariantMap map = strictObject(input, {QStringLiteral("path"), QStringLiteral("content")});
            const QString path = stringField(map, QStringLiteral("path"), 1, 1024);
            return QVariantMap{
                {QStringLiteral("path"), path},
                {QStringLiteral("content"), stringField(map, QStringLiteral("content"), 0, std::numeric_limits<int>::max())},
            };
        };
        const auto pathAndContentSummary = [](const QVariantMap &input) {
            return QVariantMap{
                {QStringLiteral("relativePath"), input.value(QStringLiteral("path"))},
                {QStringLiteral("contentCharacterCount"), input.value(QStringLiteral("content")).toString().size()},
            };
        };
        const auto relocate = [](const QVariant &input) {
            const QVariantMap map = strictObject(input, {QStringLiteral("sourcePath"), QStringLiteral("destinationPath")});
            const QString source = stringField(map, QStringLiteral("sourcePath"), 1, 1024);
            return QVariantMap{
                {QStringLiteral("sourcePath"), source},
                {QStringLiteral("destinationPath"), stringField(map, QStringLiteral("destinationPath"), 1, 1024)},
            };
        };
        const auto relocateSummary = [](const QVariantMap &input) {
            return pick(input, {QStringLiteral("sourcePath"), QStringLiteral("destinationPath")});
        };

        tools.append({
            QStringLiteral("create_file"),
            QStringLiteral("Propose Library File Creation"),
            QStringLiteral("Create a reviewable proposal for a new UTF-8 text file in the active Library. This tool never writes the file; execution requires later user approval."),
            mutation, proposal,
            objectSchema(
                {
                    {QStringLiteral("path"), stringSchema(1, 1024, QStringLiteral("Library-relative destination path for the proposed new file."))},
                    {QStringLiteral("content"), stringSchema(0, 0, QStringLiteral("Complete UTF-8 text content proposed for the new file."))},
                },
                {QStringLiteral("path"), QStringLiteral("content")}
            ),
            pathAndContent,
            createFileTool,
            pathAndContentSummary,
            [](const QVariantMap &output) {
                return proposalOutputSummary(output, {QStringLiteral("relativePath"), QStringLiteral("expectedState")});
            },
        });

        tools.append({
            QStringLiteral("patch_file"),
            QStringLiteral("Propose Library File Patch"),
            QStringLiteral("Create a reviewable proposal to replace the complete UTF-8 text content of an existing Library file. Archivist snapshots the exact current content and SHA-256 first; this tool never writes the file."),
            mutation, proposal,
            objectSchema(
                {
                    {QStringLiteral("path"), stringSchema(1, 1024, QStringLiteral("Library-relative path of the existing text file to change."))},
                    {QStringLiteral("content"), stringSchema(0, 0, QStringLiteral("Complete replacement UTF-8 text content proposed for the file. Preserve any content that should remain unchanged."))},
                },
                {QStringLiteral("path"), QStringLiteral("content")}
            ),
            pathAndContent,
            patchFileTool,
            pathAndContentSummary,
            [](const QVariantMap &output) {
                return proposalOutputSummary(output, {QStringLiteral("relativePath"), QStringLiteral("expectedHash")});
            },
        });

        tools.append({
            QStringLiteral("create_directory"),
            QStringLiteral("Propose Library Directory Creation"),
            QStringLiteral("Create a reviewable proposal for a new directory in the active Library. The parent directory must already exist. This tool never creates the directory; execution requires later user approval."),
            mutation, proposal,
            objectSchema(
                {{QStringLiteral("path"), stringSchema(1, 1024, QStringLiteral("Library-relative path for the proposed new directory. Its parent directory must already exist."))}},
                {QStringLiteral("path")}
            ),
            [](const QVariant &input) {
                const QVariantMap map = strictObject(input, {QStringLiteral("path")});
                return QVariantMap{{QStringLiteral("path"), stringField(map, QStringLiteral("path"), 1, 1024)}};
            },
            createDirectoryTool,
            [](const QVariantMap &input) { return QVariantMap{{QStringLiteral("relativePath"), input.value(QStringLiteral("path"))}}; },
            [](const QVariantMap &output) {
                return proposalOutputSummary(output, {QStringLiteral("relativePath"), QStringLiteral("expectedState")});
            },
        });

        tools.append({
            QStringLiteral("rename_file"),
            QStringLiteral("Propose Library File Rename"),
            QStringLiteral("Create a reviewable proposal to rename an existing supported UTF-8 Library text file within its current directory. Archivist snapshots the exact current SHA-256 and requires the destination to be absent. This tool never renames the file."),
            mutation, proposal,
            objectSchema(
                {
                    {QStringLiteral("sourcePath"), stringSchema(1, 1024, QStringLiteral("Library-relative path of the existing text file to rename."))},
                    {QStringLiteral("destinationPath"), stringSchema(1, 1024, QStringLiteral("Library-relative destination path in the same directory, using the new file name."))},
                },
                {QStringLiteral("sourcePath"), QStringLiteral("destinationPath")}
            ),
            relocate,
            [](const QVariantMap &input, Context &context) { return relocateTool(input, context, QStringLiteral("rename_file")); },
            relocateSummary,
            [](const QVariantMap &output) {
                return proposalOutputSummary(output, {QStringLiteral("sourcePath"), QStringLiteral("destinationPath"), QStringLiteral("expectedHash")});
            },
        });

        tools.append({
            QStringLiteral("move_file"),
            QStringLiteral("Propose Library File Move"),
            QStringLiteral("Create a reviewable proposal to move an existing supported UTF-8 Library text file to another existing Library directory while preserving its file name. Archivist snapshots the exact current SHA-256 and requires the destination to be absent. This tool never moves the file."),
            mutation, proposal,
            objectSchema(
                {
                    {QStringLiteral("sourcePath"), stringSchema(1, 1024, QStringLiteral("Library-relative path of the existing text file to move."))},
                    {QStringLiteral("destinationPath"), stringSchema(1, 1024, QStringLiteral("Library-relative path in another existing directory. Preserve the source file name."))},
                },
                {QStringLiteral("sourcePath"), QStringLiteral("destinationPath")}
            ),
            relocate,
            [](const QVariantMap &input, Context &context) { return relocateTool(input, context, QStringLiteral("move_file")); },
            relocateSummary,
            [](const QVariantMap &output) {
                return proposalOutputSummary(output, {QStringLiteral("sourcePath"), QStringLiteral("destinationPath"), QStringLiteral("expectedHash")});
            },
        });

        const QString workspaceMode = QStringLiteral("workspace");
        const QString areaDescription = QStringLiteral(
            "An area from workspace_view (its name or id), or a plain description: \"the code\", \"the chat\", \"the documentation\", \"the right panel\", \"this\"."
        );

        tools.append({
            QStringLiteral("workspace_view"),
            QStringLiteral("View Workspace"),
            QStringLiteral("Describe the person's workspace: each area (panel) with its name, position, size, what it shows and its locks, where the conversation is, whether a temporary arrangement can be undone, and the person's workspace preferences. Usually not needed: a summary is in your instructions when the person asks for a change."),
            readOnly, workspaceMode,
            objectSchema({}, {}),
            [](const QVariant &input) { return strictObject(input, {}); },
            workspaceViewTool,
            [](const QVariantMap &) { return QVariantMap(); },
            [](const QVariantMap &output) {
                return QVariantMap{{QStringLiteral("areaCount"), output.value(QStringLiteral("areas")).toList().size()}};
            },
        });

        const QVariantMap showItem = objectSchema(
            {
                {QStringLiteral("kind"), enumSchema(showKinds(), QStringLiteral("file: a Library file. chat: a conversation (\"\" for this one). terminal: \"\" for the terminal, \"new\" for another. diff: changes under review. open_view: something already open, by view id or title."))},
                {QStringLiteral("reference"), stringSchema(0, 1024, QStringLiteral("file: Library-relative path or file id. chat: chat id or \"\". terminal: \"\" or \"new\". open_view: view id or title."))},
                {QStringLiteral("library"), stringSchema(0, 256, QStringLiteral("\"\" for this chat's Library, else a Library or worktree name or id. Files keep the Library they belong to."))},
            },
            {QStringLiteral("kind"), QStringLiteral("reference"), QStringLiteral("library")}
        );

        tools.append({
            QStringLiteral("workspace_show"),
            QStringLiteral("Show in Workspace"),
            QStringLiteral("Open or bring forward files, chats or terminals in the workspace. Things already open are reused, not duplicated; several files share one area as tabs. Put every item in one call and leave where empty unless the person said where. Only call this when the person asked to see, open or bring something up; to find which files matter, use Library evidence first and show two to five."),
            mutation, workspaceMode,
            objectSchema(
                {
                    {QStringLiteral("items"), QVariantMap{{QStringLiteral("type"), QStringLiteral("array")}, {QStringLiteral("items"), showItem}, {QStringLiteral("minItems"), 1}, {QStringLiteral("maxItems"), 8}}},
                    {QStringLiteral("where"), stringSchema(0, 200, QStringLiteral("\"\" to choose, or e.g. \"right\", \"left\", \"bottom\", \"beside the chat\", an area name, \"new panel\"."))},
                    {QStringLiteral("purpose"), stringSchema(0, 64, QStringLiteral("\"\", or what the items are for: \"documentation\", \"reference\", \"code\"."))},
                    {QStringLiteral("focus"), QVariantMap{{QStringLiteral("type"), QStringLiteral("boolean")}, {QStringLiteral("description"), QStringLiteral("Move keyboard focus there. Only when the person asked to switch to it.")}}},
                },
                {QStringLiteral("items"), QStringLiteral("where"), QStringLiteral("purpose"), QStringLiteral("focus")}
            ),
            [](const QVariant &input) {
                const QVariantMap map = strictObject(input, {QStringLiteral("items"), QStringLiteral("where"), QStringLiteral("purpose"), QStringLiteral("focus")});
                const QVariant itemsValue = map.value(QStringLiteral("items"));
                if (itemsValue.typeId() != QMetaType::QVariantList) {
                    invalid(QStringLiteral("items"), QStringLiteral("Expected array."));
                }
                const QVariantList list = itemsValue.toList();
                if (list.isEmpty() || list.size() > 8) {
                    invalid(QStringLiteral("items"), QStringLiteral("Show between one and eight items."));
                }
                QVariantList items;
                for (const QVariant &value : list) {
                    const QVariantMap item = strictObject(value, {QStringLiteral("kind"), QStringLiteral("reference"), QStringLiteral("library")});
                    items.append(QVariantMap{
                        {QStringLiteral("kind"), enumField(item, QStringLiteral("kind"), showKinds())},
                        {QStringLiteral("reference"), stringField(item, QStringLiteral("reference"), 0, 1024, true, QString())},
                        {QStringLiteral("library"), stringField(item, QStringLiteral("library"), 0, 256, true, QString())},
                    });
                }
                return QVariantMap{
                    {QStringLiteral("items"), items},
                    {QStringLiteral("where"), stringField(map, QStringLiteral("where"), 0, 200, true, QString())},
                    {QStringLiteral("purpose"), stringField(map, QStringLiteral("purpose"), 0, 64, true, QString())},
                    {QStringLiteral("focus"), map.contains(QStringLiteral("focus")) ? boolField(map, QStringLiteral("focus")) : false},
                };
            },
            workspaceShowTool,
            [](const QVariantMap &input) {
                QStringList references;
                for (const QVariant &item : input.value(QStringLiteral("items")).toList()) {
                    references.append(item.toMap().value(QStringLiteral("kind")).toString() + QLatin1Char(':') + item.toMap().value(QStringLiteral("reference")).toString());
                }
                return QVariantMap{{QStringLiteral("items"), references}, {QStringLiteral("where"), input.value(QStringLiteral("where"))}};
            },
            workspaceOutputSummary,
        });

        const QStringList stepFields{QStringLiteral("action"), QStringLiteral("area"), QStringLiteral("change"), QStringLiteral("to"), QStringLiteral("name")};
        const QVariantMap arrangeStep = objectSchema(
            {
                {QStringLiteral("action"), enumSchema(arrangeActions())},
                {QStringLiteral("area"), stringSchema(0, 200, areaDescription)},
                {QStringLiteral("change"), stringSchema(0, 32, QStringLiteral("resize only, else \"\"."))},
                {QStringLiteral("to"), stringSchema(0, 200, QStringLiteral("move, add and remove only, else \"\"."))},
                {QStringLiteral("name"), stringSchema(0, 80, QStringLiteral("label and add only, else \"\"."))},
            },
            stepFields
        );

        tools.append({
            QStringLiteral("workspace_arrange"),
            QStringLiteral("Arrange Workspace"),
            QStringLiteral("Small edits to the panels that exist: steps run in order as one change, and nothing applies unless every step works. For any arrangement of several panels (grids, columns, \"4 panes\", a new layout) use workspace_compose instead; at most one add per call. Step actions: focus (bring an area forward), resize (change: larger, smaller, maximize, restore, equal, or a share like 0.6), move (to: left, right, top, bottom, \"beside <area>\", or another area), keep (lock it where it is), label (name: what the person calls it), add (a new empty panel: to = side, area = the area it goes beside or \"\" for the window edge, name = optional), remove (take a panel away; its tabs move to the area in to, else the main editor; nothing closes). save_as: after the steps, save the result as a new named layout (\"\" to not save). Locked areas refuse changes; never try to unlock one."),
            mutation, workspaceMode,
            objectSchema(
                {
                    {QStringLiteral("steps"), QVariantMap{{QStringLiteral("type"), QStringLiteral("array")}, {QStringLiteral("items"), arrangeStep}, {QStringLiteral("minItems"), 1}, {QStringLiteral("maxItems"), 12}}},
                    {QStringLiteral("save_as"), stringSchema(0, 80, QStringLiteral("A new layout name to save the result as, else \"\"."))},
                },
                {QStringLiteral("steps"), QStringLiteral("save_as")}
            ),
            [stepFields](const QVariant &input) {
                const QVariantMap map = strictObject(input, {QStringLiteral("steps"), QStringLiteral("save_as")});
                const QVariant stepsValue = map.value(QStringLiteral("steps"));
                if (stepsValue.typeId() != QMetaType::QVariantList) {
                    invalid(QStringLiteral("steps"), QStringLiteral("Expected array."));
                }
                const QVariantList list = stepsValue.toList();
                if (list.isEmpty() || list.size() > 12) {
                    invalid(QStringLiteral("steps"), QStringLiteral("Give between one and twelve steps."));
                }
                QVariantList steps;
                for (const QVariant &value : list) {
                    const QVariantMap step = strictObject(value, stepFields);
                    steps.append(QVariantMap{
                        {QStringLiteral("action"), enumField(step, QStringLiteral("action"), arrangeActions())},
                        {QStringLiteral("area"), stringField(step, QStringLiteral("area"), 0, 200, true, QString())},
                        {QStringLiteral("change"), stringField(step, QStringLiteral("change"), 0, 32, true, QString())},
                        {QStringLiteral("to"), stringField(step, QStringLiteral("to"), 0, 200, true, QString())},
                        {QStringLiteral("name"), stringField(step, QStringLiteral("name"), 0, 80, true, QString())},
                    });
                }
                // Building an arrangement from adds and moves is what
                // produced stacks of strips; that's workspace_compose's job.
                qsizetype adds = 0;
                for (const QVariant &step : std::as_const(steps)) {
                    adds += step.toMap().value(QStringLiteral("action")).toString() == QStringLiteral("add") ? 1 : 0;
                }
                if (adds > 1) {
                    invalid(QStringLiteral("steps"), QStringLiteral("Use workspace_compose (for example arrangement grid) to set up several panels; arrange adds at most one."));
                }
                return QVariantMap{
                    {QStringLiteral("steps"), steps},
                    {QStringLiteral("save_as"), stringField(map, QStringLiteral("save_as"), 0, 80, true, QString())},
                };
            },
            workspaceArrangeTool,
            [](const QVariantMap &input) {
                QStringList actions;
                for (const QVariant &step : input.value(QStringLiteral("steps")).toList()) {
                    actions.append(step.toMap().value(QStringLiteral("action")).toString());
                }
                return QVariantMap{{QStringLiteral("steps"), actions}, {QStringLiteral("saveAs"), input.value(QStringLiteral("save_as"))}};
            },
            workspaceOutputSummary,
        });

        const QStringList slotFields{QStringLiteral("name"), QStringLiteral("holds"), QStringLiteral("weight"), QStringLiteral("position"), QStringLiteral("items")};
        const QVariantMap composeSlot = objectSchema(
            {
                {QStringLiteral("name"), stringSchema(1, 40, QStringLiteral("What the panel is called: \"Code\", \"Docs\", \"Chat\"."))},
                {QStringLiteral("holds"), QVariantMap{{QStringLiteral("type"), QStringLiteral("array")}, {QStringLiteral("items"), enumSchema(Workspace::Compose::holdWords())}, {QStringLiteral("maxItems"), 4}, {QStringLiteral("description"), QStringLiteral("What kind of content belongs there; can be empty when items say it.")}}},
                {QStringLiteral("weight"), QVariantMap{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}, {QStringLiteral("maximum"), 10}, {QStringLiteral("description"), QStringLiteral("Relative room: 1 normal, 2 about twice; 0 keeps the default.")}}},
                {QStringLiteral("position"), stringSchema(0, 24, QStringLiteral("Where it goes. grid: top-left, top-right, bottom-left, bottom-right, top, bottom-center, r2c1. columns: left, center, right. rows: top, middle, bottom. split: primary for the large one; the first other panel's right/left/top/bottom says the side. "": the next free place."))},
                {QStringLiteral("items"), QVariantMap{{QStringLiteral("type"), QStringLiteral("array")}, {QStringLiteral("items"), showItem}, {QStringLiteral("maxItems"), Workspace::Compose::kMaximumItemsPerSlot}}},
            },
            slotFields
        );
        QStringList templateChoices{QStringLiteral("none")};
        QStringList templateLines;
        for (const Workspace::Compose::Template &recipe : Workspace::Compose::templates()) {
            templateChoices.append(recipe.id);
            QStringList slotNames;
            for (const QJsonValue &slot : recipe.recipe.value(QStringLiteral("slots")).toArray()) {
                slotNames.append(slot.toObject().value(QStringLiteral("name")).toString());
            }
            templateLines.append(QStringLiteral("%1 (%2: %3)").arg(recipe.id, recipe.description.chopped(1), slotNames.join(QStringLiteral(", "))));
        }

        tools.append({
            QStringLiteral("workspace_compose"),
            QStringLiteral("Compose Workspace"),
            QStringLiteral("Set the workspace to the state the person asked for, in one call. You only choose; Archivist finds the files, builds and sizes the panels, reuses what is open and applies it as one change the person can undo or redo. Never build layouts from moves or splits. mode patch (the usual case): the current panels stay where they are; a slot named like an existing panel gets its items there, a new name adds one panel at its position (left, right, top or bottom), and panels you don't name are left alone. Use it for \"put the PDF beside the chat\" or \"add my notes on the right\": arrangement auto, rows and columns 0. mode replace: a whole new arrangement from a recipe, only when the person asks for one (\"4 panes\", \"three columns\", \"chat dominant\", a template). Recipes: grid (rows x columns; \"4 panes, chat bottom-left\" is grid, rows 2, columns 2, chat at bottom-left; empty places become panels showing what was open), columns (left, center, right), rows (top, middle, bottom), split (one large panel at position primary, the rest stacked on one side: \"chat dominant, three images stacked on the right\"), stack (everything as tabs in one panel). Weights are relative (1 normal, 2 about twice); panels that don't fit become tabs. Files can be named by file name, path or an open tab's title; nothing closes. Templates fill their panels by name: %1. save_as also saves it as a named layout (\"\" keeps it temporary). focus: a panel name to bring forward, only when asked.").arg(templateLines.join(QStringLiteral("; "))),
            mutation, workspaceMode,
            objectSchema(
                {
                    {QStringLiteral("mode"), enumSchema(QStringList{QStringLiteral("patch"), QStringLiteral("replace")})},
                    {QStringLiteral("template"), enumSchema(templateChoices)},
                    {QStringLiteral("arrangement"), enumSchema(QStringList{QStringLiteral("auto")} + Workspace::Compose::strategies())},
                    {QStringLiteral("rows"), QVariantMap{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}, {QStringLiteral("maximum"), 3}, {QStringLiteral("description"), QStringLiteral("grid only, else 0.")}}},
                    {QStringLiteral("columns"), QVariantMap{{QStringLiteral("type"), QStringLiteral("integer")}, {QStringLiteral("minimum"), 0}, {QStringLiteral("maximum"), 3}, {QStringLiteral("description"), QStringLiteral("grid only, else 0.")}}},
                    {QStringLiteral("slots"), QVariantMap{{QStringLiteral("type"), QStringLiteral("array")}, {QStringLiteral("items"), composeSlot}, {QStringLiteral("maxItems"), Workspace::Compose::kMaximumSlots}}},
                    {QStringLiteral("focus"), stringSchema(0, 40, QStringLiteral("A panel name, or \"\"."))},
                    {QStringLiteral("save_as"), stringSchema(0, 80, QStringLiteral("A new layout name, or \"\"."))},
                },
                {QStringLiteral("mode"), QStringLiteral("template"), QStringLiteral("arrangement"), QStringLiteral("rows"), QStringLiteral("columns"), QStringLiteral("slots"), QStringLiteral("focus"), QStringLiteral("save_as")}
            ),
            [slotFields, templateChoices](const QVariant &input) {
                const QVariantMap map = strictObject(input, {QStringLiteral("mode"), QStringLiteral("template"), QStringLiteral("arrangement"), QStringLiteral("rows"), QStringLiteral("columns"), QStringLiteral("slots"), QStringLiteral("focus"), QStringLiteral("save_as")});
                const QVariant slotsValue = map.value(QStringLiteral("slots"));
                if (slotsValue.typeId() != QMetaType::QVariantList) {
                    invalid(QStringLiteral("slots"), QStringLiteral("Expected array."));
                }
                const QVariantList list = slotsValue.toList();
                if (list.size() > Workspace::Compose::kMaximumSlots) {
                    invalid(QStringLiteral("slots"), QStringLiteral("At most %1 panels.").arg(Workspace::Compose::kMaximumSlots));
                }
                QVariantList panelSlots;
                for (const QVariant &value : list) {
                    const QVariantMap slot = strictObject(value, slotFields);
                    QStringList holds;
                    const QVariantList holdList = slot.value(QStringLiteral("holds")).toList();
                    if (holdList.size() > 4) {
                        invalid(QStringLiteral("holds"), QStringLiteral("At most four kinds."));
                    }
                    for (const QVariant &hold : holdList) {
                        const QString word = hold.toString();
                        if (!Workspace::Compose::holdWords().contains(word)) {
                            invalid(QStringLiteral("holds"), QStringLiteral("Use: %1.").arg(Workspace::Compose::holdWords().join(QStringLiteral(", "))));
                        }
                        holds.append(word);
                    }
                    const QVariantList itemList = slot.value(QStringLiteral("items")).toList();
                    if (itemList.size() > Workspace::Compose::kMaximumItemsPerSlot) {
                        invalid(QStringLiteral("items"), QStringLiteral("At most %1 items per panel.").arg(Workspace::Compose::kMaximumItemsPerSlot));
                    }
                    QVariantList items;
                    for (const QVariant &itemValue : itemList) {
                        const QVariantMap item = strictObject(itemValue, {QStringLiteral("kind"), QStringLiteral("reference"), QStringLiteral("library")});
                        items.append(QVariantMap{
                            {QStringLiteral("kind"), enumField(item, QStringLiteral("kind"), showKinds())},
                            {QStringLiteral("reference"), stringField(item, QStringLiteral("reference"), 0, 1024, true, QString())},
                            {QStringLiteral("library"), stringField(item, QStringLiteral("library"), 0, 256, true, QString())},
                        });
                    }
                    panelSlots.append(QVariantMap{
                        {QStringLiteral("name"), stringField(slot, QStringLiteral("name"), 1, 40, true)},
                        {QStringLiteral("holds"), holds},
                        {QStringLiteral("weight"), intField(slot, QStringLiteral("weight"), 0, 10, 0)},
                        {QStringLiteral("position"), slot.contains(QStringLiteral("position")) ? stringField(slot, QStringLiteral("position"), 0, 24, true, QString()) : QString()},
                        {QStringLiteral("items"), items},
                    });
                }
                return QVariantMap{
                    {QStringLiteral("mode"), map.contains(QStringLiteral("mode")) ? enumField(map, QStringLiteral("mode"), QStringList{QStringLiteral("patch"), QStringLiteral("replace")}) : QString()},
                    {QStringLiteral("template"), enumField(map, QStringLiteral("template"), templateChoices)},
                    {QStringLiteral("arrangement"), enumField(map, QStringLiteral("arrangement"), QStringList{QStringLiteral("auto")} + Workspace::Compose::strategies())},
                    {QStringLiteral("rows"), map.contains(QStringLiteral("rows")) ? intField(map, QStringLiteral("rows"), 0, 3, 0) : 0},
                    {QStringLiteral("columns"), map.contains(QStringLiteral("columns")) ? intField(map, QStringLiteral("columns"), 0, 3, 0) : 0},
                    {QStringLiteral("slots"), panelSlots},
                    {QStringLiteral("focus"), stringField(map, QStringLiteral("focus"), 0, 40, true, QString())},
                    {QStringLiteral("save_as"), stringField(map, QStringLiteral("save_as"), 0, 80, true, QString())},
                };
            },
            workspaceComposeTool,
            [](const QVariantMap &input) {
                QStringList names;
                for (const QVariant &slot : input.value(QStringLiteral("slots")).toList()) {
                    names.append(slot.toMap().value(QStringLiteral("name")).toString());
                }
                return QVariantMap{{QStringLiteral("template"), input.value(QStringLiteral("template"))}, {QStringLiteral("panels"), names}};
            },
            [](const QVariantMap &output) {
                QVariantMap summary = workspaceOutputSummary(output);
                if (output.contains(QStringLiteral("unresolved"))) {
                    summary.insert(QStringLiteral("unresolved"), output.value(QStringLiteral("unresolved")));
                }
                return summary;
            },
        });

        tools.append({
            QStringLiteral("workspace_restore"),
            QStringLiteral("Restore Workspace"),
            QStringLiteral("Put the workspace back. before_task: how it was before you started rearranging (\"put everything back\", \"restore my workspace\", \"put it back\"). last_change: undo the most recent workspace change (\"undo that\", \"switch back\" after a change). redo: redo the change just undone (\"redo\", \"actually, put it back how you had it\"). saved_layout: apply one of the person's saved layouts by name. Open files, chats and terminals stay open; unsaved work is never closed."),
            mutation, workspaceMode,
            objectSchema(
                {
                    {QStringLiteral("scope"), enumSchema(restoreScopes())},
                    {QStringLiteral("layout"), stringSchema(0, 120, QStringLiteral("saved_layout only: the layout's name, else \"\"."))},
                },
                {QStringLiteral("scope"), QStringLiteral("layout")}
            ),
            [](const QVariant &input) {
                const QVariantMap map = strictObject(input, {QStringLiteral("scope"), QStringLiteral("layout")});
                return QVariantMap{
                    {QStringLiteral("scope"), enumField(map, QStringLiteral("scope"), restoreScopes())},
                    {QStringLiteral("layout"), stringField(map, QStringLiteral("layout"), 0, 120, true, QString())},
                };
            },
            workspaceRestoreTool,
            [](const QVariantMap &input) { return QVariantMap{{QStringLiteral("scope"), input.value(QStringLiteral("scope"))}}; },
            workspaceOutputSummary,
        });

        tools.append({
            QStringLiteral("workspace_layout"),
            QStringLiteral("Workspace Layouts"),
            QStringLiteral("Named layouts: whole arrangements of panels the person can switch between. list shows the built-in and saved ones. apply switches to one by name (open tabs move into it; nothing closes; undoable); \"try another\" means apply a different one than the current layout. save stores the current arrangement under name (workspace_arrange save_as does this in the same call as the arranging). Saving over an existing name needs replace true, and only when the person said to replace it. rename (new_name) and delete work on the person's saved layouts only, and only when they ask."),
            mutation, workspaceMode,
            objectSchema(
                {
                    {QStringLiteral("action"), enumSchema(layoutOps())},
                    {QStringLiteral("name"), stringSchema(0, 80, QStringLiteral("The layout's name; \"\" for list."))},
                    {QStringLiteral("new_name"), stringSchema(0, 80, QStringLiteral("rename only, else \"\"."))},
                    {QStringLiteral("replace"), QVariantMap{{QStringLiteral("type"), QStringLiteral("boolean")}, {QStringLiteral("description"), QStringLiteral("save only: replace a saved layout with the same name.")}}},
                },
                {QStringLiteral("action"), QStringLiteral("name"), QStringLiteral("new_name"), QStringLiteral("replace")}
            ),
            [](const QVariant &input) {
                const QVariantMap map = strictObject(input, {QStringLiteral("action"), QStringLiteral("name"), QStringLiteral("new_name"), QStringLiteral("replace")});
                return QVariantMap{
                    {QStringLiteral("action"), enumField(map, QStringLiteral("action"), layoutOps())},
                    {QStringLiteral("name"), stringField(map, QStringLiteral("name"), 0, 80, true, QString())},
                    {QStringLiteral("new_name"), stringField(map, QStringLiteral("new_name"), 0, 80, true, QString())},
                    {QStringLiteral("replace"), boolField(map, QStringLiteral("replace"))},
                };
            },
            workspaceLayoutTool,
            [](const QVariantMap &input) {
                return QVariantMap{{QStringLiteral("action"), input.value(QStringLiteral("action"))}, {QStringLiteral("name"), input.value(QStringLiteral("name"))}};
            },
            workspaceOutputSummary,
        });

        tools.append({
            QStringLiteral("workspace_preference"),
            QStringLiteral("Workspace Preference"),
            QStringLiteral("Remember how the person likes their workspace, only when they state a preference (not for a one-off request). chat_location: bottom | docked (\"I like my chat at the bottom\"). main_editor: large | normal (\"keep my main editor large\"). rearranging: only_when_asked | when_helpful (\"don't rearrange my windows unless I ask\"). area_purpose: value is what a panel is for, e.g. documentation, and area names the panel (\"use the right panel for documentation\"). duration: always only when they say always, from now on, by default or describe a standing habit; otherwise this_session. clear forgets one; list shows them."),
            mutation, workspaceMode,
            objectSchema(
                {
                    {QStringLiteral("action"), enumSchema(preferenceOps())},
                    {QStringLiteral("preference"), enumSchema(preferenceKeys())},
                    {QStringLiteral("value"), stringSchema(0, 40, QStringLiteral("set: the value; clear: \"\" (or the purpose for area_purpose)."))},
                    {QStringLiteral("area"), stringSchema(0, 200, QStringLiteral("area_purpose only: the panel, e.g. \"the right panel\" or \"this panel\"; else \"\"."))},
                    {QStringLiteral("duration"), enumSchema(preferenceDurations())},
                },
                {QStringLiteral("action"), QStringLiteral("preference"), QStringLiteral("value"), QStringLiteral("area"), QStringLiteral("duration")}
            ),
            [](const QVariant &input) {
                const QVariantMap map = strictObject(input, {QStringLiteral("action"), QStringLiteral("preference"), QStringLiteral("value"), QStringLiteral("area"), QStringLiteral("duration")});
                return QVariantMap{
                    {QStringLiteral("action"), enumField(map, QStringLiteral("action"), preferenceOps())},
                    {QStringLiteral("preference"), enumField(map, QStringLiteral("preference"), preferenceKeys())},
                    {QStringLiteral("value"), stringField(map, QStringLiteral("value"), 0, 40, true, QString())},
                    {QStringLiteral("area"), stringField(map, QStringLiteral("area"), 0, 200, true, QString())},
                    {QStringLiteral("duration"), enumField(map, QStringLiteral("duration"), preferenceDurations())},
                };
            },
            workspacePreferenceTool,
            [](const QVariantMap &input) {
                return QVariantMap{{QStringLiteral("action"), input.value(QStringLiteral("action"))}, {QStringLiteral("preference"), input.value(QStringLiteral("preference"))}, {QStringLiteral("duration"), input.value(QStringLiteral("duration"))}};
            },
            workspaceOutputSummary,
        });

        std::sort(tools.begin(), tools.end(), [](const Definition &first, const Definition &second) {
            return first.id < second.id;
        });
        return tools;
    }();

    return list;
}

const Definition *findTool(const QString &toolId)
{
    for (const Definition &definition : definitions()) {
        if (definition.id == toolId) {
            return &definition;
        }
    }

    return nullptr;
}

// ---- ai_tool_executions ----

const QString executionSelect = QStringLiteral(
    "SELECT id, run_id, tool_id, permission_level, status, input_json, output_json, error_code, error_message, "
    "requested_at, started_at, completed_at, duration_ms FROM ai_tool_executions "
);

QVariant parseStoredJson(const QVariant &value)
{
    if (isNullish(value)) {
        return QVariant::fromValue(nullptr);
    }

    bool ok = false;
    const QVariant parsed = JsonValues::parse(value.toString(), &ok);
    return ok ? parsed : QVariant::fromValue(nullptr);
}

QVariantMap mapExecution(const QVariantMap &row)
{
    return {
        {QStringLiteral("id"), row.value(QStringLiteral("id"))},
        {QStringLiteral("runId"), row.value(QStringLiteral("run_id"))},
        {QStringLiteral("toolId"), row.value(QStringLiteral("tool_id"))},
        {QStringLiteral("permission"), row.value(QStringLiteral("permission_level"))},
        {QStringLiteral("status"), row.value(QStringLiteral("status"))},
        {QStringLiteral("input"), parseStoredJson(row.value(QStringLiteral("input_json")))},
        {QStringLiteral("output"), parseStoredJson(row.value(QStringLiteral("output_json")))},
        {QStringLiteral("errorCode"), nullable(row.value(QStringLiteral("error_code")))},
        {QStringLiteral("errorMessage"), nullable(row.value(QStringLiteral("error_message")))},
        {QStringLiteral("requestedAt"), row.value(QStringLiteral("requested_at"))},
        {QStringLiteral("startedAt"), nullable(row.value(QStringLiteral("started_at")))},
        {QStringLiteral("completedAt"), nullable(row.value(QStringLiteral("completed_at")))},
        {QStringLiteral("durationMs"), nullable(row.value(QStringLiteral("duration_ms")))},
    };
}

QVariantMap requireExecution(CoreDatabase &database, const QString &executionId)
{
    const auto row = database.prepare(executionSelect + QStringLiteral("WHERE id = ?")).bind(1, executionId).one();

    if (!row) {
        throw CoreError(QStringLiteral("AI tool execution %1 could not be loaded.").arg(executionId));
    }

    return mapExecution(*row);
}

QString serialized(const QVariant &value)
{
    return value.isValid() ? JsonValues::toJsonString(value) : QStringLiteral("null");
}

double elapsedMs(const QElapsedTimer &timer)
{
    return std::round(timer.nsecsElapsed() / 1000.0) / 1000.0;
}

void finishFailed(Context &context, const Definition &tool, const ToolError &error, double durationMs)
{
    CoreDatabase &database = *context.database;
    const bool cancelled = error.code() == QStringLiteral("cancelled");
    database
        .prepare(QStringLiteral(
            "UPDATE ai_tool_executions SET status = ?, error_code = ?, error_message = ?, completed_at = %1, duration_ms = ? "
            "WHERE id = ? AND status IN ('requested', 'running')"
        ).arg(nowSql))
        .bind(1, cancelled ? QStringLiteral("cancelled") : QStringLiteral("failed"))
        .bind(2, error.code())
        .bind(3, error.message())
        .bind(4, durationMs)
        .bind(5, context.executionId)
        .run();

    if (database.changes() != 1) {
        throw CoreError(QStringLiteral("AI tool execution %1 could not be finalized.").arg(context.executionId));
    }

    if (context.emitEvent) {
        context.emitEvent(cancelled ? QStringLiteral("tool.cancelled") : QStringLiteral("tool.failed"), {
            {QStringLiteral("executionId"), context.executionId},
            {QStringLiteral("toolId"), tool.id},
            {QStringLiteral("errorCode"), error.code()},
            {QStringLiteral("message"), error.message()},
            {QStringLiteral("durationMs"), durationMs},
        });
    }
}

QVariantMap safeSummary(const std::function<QVariantMap(const QVariantMap &)> &summarize, const QVariantMap &value)
{
    try {
        return summarize ? summarize(value) : QVariantMap();
    } catch (...) {
        return {{QStringLiteral("summaryUnavailable"), true}};
    }
}
}

namespace AITools
{
QVariantList providerTools(const Availability &availability)
{
    static const QSet<QString> discovery{QStringLiteral("list_directory"), QStringLiteral("search_filenames"), QStringLiteral("search_library")};
    const bool verificationOnly = !availability.discovery && !availability.fullFileRead;
    QVariantList result;

    for (const Definition &tool : definitions()) {
        bool include = false;

        if (tool.executionMode == QStringLiteral("workspace")) {
            include = availability.workspace;
        } else if (tool.permission == QStringLiteral("safe-local-mutation") && tool.executionMode == QStringLiteral("proposal")) {
            include = availability.mutationProposals;
        } else if (tool.permission == QStringLiteral("read-only")) {
            include = verificationOnly
                ? availability.verification && tool.id == QStringLiteral("read_file_ranges")
                : (availability.discovery || !discovery.contains(tool.id))
                    && (availability.fullFileRead || tool.id != QStringLiteral("read_file"));
        }

        if (include) {
            result.append(QVariantMap{
                {QStringLiteral("name"), tool.id},
                {QStringLiteral("description"), tool.description},
                {QStringLiteral("parameters"), tool.schema},
            });
        }
    }

    return result;
}

bool isMutationProposalTool(const QString &toolId)
{
    const Definition *tool = findTool(toolId);
    return tool && tool->executionMode == QStringLiteral("proposal");
}

bool isWorkspaceTool(const QString &toolId)
{
    const Definition *tool = findTool(toolId);
    return tool && tool->executionMode == QStringLiteral("workspace");
}

QStringList toolIds()
{
    QStringList ids;

    for (const Definition &tool : definitions()) {
        ids.append(tool.id);
    }

    return ids;
}

QVariantMap execute(Context context, const QString &toolId, const QVariant &input)
{
    const Definition *found = findTool(toolId);

    if (!found) {
        throw ToolError(QStringLiteral("tool_not_found"), QStringLiteral("AI tool \"%1\" is not registered.").arg(toolId));
    }

    const Definition &tool = *found;
    CoreDatabase &database = *context.database;
    QElapsedTimer timer;
    timer.start();
    context.executionId = newId();
    database
        .prepare(QStringLiteral(
            "INSERT INTO ai_tool_executions (id, run_id, tool_id, permission_level, status, input_json) "
            "VALUES (?, ?, ?, ?, 'requested', ?)"
        ))
        .bind(1, context.executionId)
        .bind(2, context.runId)
        .bind(3, tool.id)
        .bind(4, tool.permission)
        .bind(5, serialized(input))
        .run();

    if (context.emitEvent) {
        context.emitEvent(QStringLiteral("tool.requested"), {
            {QStringLiteral("executionId"), context.executionId},
            {QStringLiteral("toolId"), tool.id},
            {QStringLiteral("toolName"), tool.name},
            {QStringLiteral("permission"), tool.permission},
            {QStringLiteral("executionMode"), tool.executionMode},
        });
    }

    QVariantMap parsed;

    try {
        parsed = tool.parse(input);
    } catch (const InvalidInput &issue) {
        const ToolError error(
            QStringLiteral("invalid_input"),
            QStringLiteral("AI tool \"%1\" received invalid input.").arg(tool.id),
            {{QStringLiteral("issues"), QVariantList{issue.issue}}}
        );
        finishFailed(context, tool, error, elapsedMs(timer));
        throw error;
    }

    // Workspace changes only move views between panels: they never touch
    // files, and the workspace can undo them.
    const bool permitted = tool.permission == QStringLiteral("read-only")
        || (tool.permission == QStringLiteral("safe-local-mutation") && tool.executionMode == QStringLiteral("proposal"))
        || (tool.permission == QStringLiteral("safe-local-mutation") && tool.executionMode == QStringLiteral("workspace"));

    if (!permitted) {
        const ToolError error(
            QStringLiteral("permission_denied"),
            QStringLiteral("AI tool \"%1\" requires %2 permission.").arg(tool.id, tool.permission),
            {{QStringLiteral("requiredPermission"), tool.permission}, {QStringLiteral("grantedPermissions"), QVariantList{QStringLiteral("read-only")}}}
        );
        finishFailed(context, tool, error, elapsedMs(timer));
        throw error;
    }

    const QVariantMap inputSummary = safeSummary(tool.summarizeInput, parsed);
    database
        .prepare(QStringLiteral(
            "UPDATE ai_tool_executions SET status = 'running', started_at = %1 WHERE id = ? AND status = 'requested'"
        ).arg(nowSql))
        .bind(1, context.executionId)
        .run();

    if (database.changes() != 1) {
        throw CoreError(QStringLiteral("AI tool execution %1 could not be started.").arg(context.executionId));
    }

    if (context.emitEvent) {
        context.emitEvent(QStringLiteral("tool.started"), {
            {QStringLiteral("executionId"), context.executionId},
            {QStringLiteral("toolId"), tool.id},
            {QStringLiteral("toolName"), tool.name},
            {QStringLiteral("permission"), tool.permission},
            {QStringLiteral("executionMode"), tool.executionMode},
            {QStringLiteral("input"), inputSummary},
        });
    }

    std::optional<ToolError> failure;
    QVariantMap output;

    try {
        output = tool.run(parsed, context);

        if (context.cancelled && context.cancelled()) {
            throw ToolCancelled{};
        }
    } catch (const ToolCancelled &) {
        failure.emplace(QStringLiteral("cancelled"), QStringLiteral("The AI tool was cancelled."));
    } catch (const ToolError &error) {
        failure = error;
    } catch (const CoreError &error) {
        failure.emplace(
            QStringLiteral("execution_failed"),
            JsCompat::isBlank(error.message()) ? QStringLiteral("The AI tool failed unexpectedly.") : error.message()
        );
    } catch (const std::exception &error) {
        const QString message = QString::fromUtf8(error.what());
        failure.emplace(
            QStringLiteral("execution_failed"),
            JsCompat::isBlank(message) ? QStringLiteral("The AI tool failed unexpectedly.") : message
        );
    }

    if (failure && context.cancelled && context.cancelled()) {
        failure.emplace(QStringLiteral("cancelled"), QStringLiteral("The AI tool was cancelled."));
    }

    if (failure) {
        finishFailed(context, tool, *failure, elapsedMs(timer));
        throw *failure;
    }

    const double durationMs = elapsedMs(timer);
    database
        .prepare(QStringLiteral(
            "UPDATE ai_tool_executions SET status = 'completed', output_json = ?, error_code = NULL, error_message = NULL, "
            "completed_at = %1, duration_ms = ? WHERE id = ? AND status = 'running'"
        ).arg(nowSql))
        .bind(1, serialized(output))
        .bind(2, durationMs)
        .bind(3, context.executionId)
        .run();

    if (database.changes() != 1) {
        throw CoreError(QStringLiteral("AI tool execution %1 could not be completed.").arg(context.executionId));
    }

    if (context.emitEvent) {
        context.emitEvent(QStringLiteral("tool.completed"), {
            {QStringLiteral("executionId"), context.executionId},
            {QStringLiteral("toolId"), tool.id},
            {QStringLiteral("toolName"), tool.name},
            {QStringLiteral("durationMs"), durationMs},
            {QStringLiteral("output"), safeSummary(tool.summarizeOutput, output)},
        });
    }

    QVariantMap execution = requireExecution(database, context.executionId);
    execution.insert(QStringLiteral("output"), output);
    return execution;
}

QVariantList executionsByRun(CoreDatabase &database, const QString &runId)
{
    QVariantList result;

    for (const QVariantMap &row :
         database.prepare(executionSelect + QStringLiteral("WHERE run_id = ? ORDER BY requested_at ASC, rowid ASC")).bind(1, runId).all()) {
        result.append(mapExecution(row));
    }

    return result;
}
}

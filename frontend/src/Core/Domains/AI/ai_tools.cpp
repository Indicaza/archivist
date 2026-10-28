#include "ai_tools.h"

#include "Core/Domains/Libraries/library_edit_inspection.h"
#include "Core/Domains/Libraries/library_file_repository.h"
#include "Core/Domains/Libraries/library_files.h"
#include "Core/Domains/Libraries/library_repository.h"
#include "Core/Domains/Workspace/workspace_gateway.h"
#include "Core/Support/core_values.h"
#include "Core/Support/js_compat.h"
#include "Core/Support/json_values.h"
#include "Core/core_error.h"
#include "edit_proposals.h"
#include "library_retrieval.h"

#include <QCollator>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QHash>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

using namespace CoreValues;
using AITools::Context;
using AITools::ToolError;

namespace
{
const QString nowSql = QStringLiteral("strftime('%Y-%m-%dT%H:%M:%fZ', 'now')");

constexpr int maximumDirectoryEntries = 200;
constexpr int maximumFilenameMatches = 100;
constexpr int maximumSearchMatches = 24;
constexpr int maximumReadCharacters = 64000;
constexpr int maximumReadRangeLines = 160;
constexpr int maximumBatchReadRanges = 3;
constexpr int maximumBatchReadLines = 240;
constexpr int maximumBatchReadCharacters = 48000;

const QString fileReferenceDescription = QStringLiteral(
    "Catalog UUID from a source file-id attribute, or the exact Library-relative path from its path attribute."
);

// Thrown when the run is cancelled mid-tool (Node's AbortError).
struct ToolCancelled
{
};

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

bool isNumber(const QVariant &value)
{
    switch (value.typeId()) {
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Double:
        return true;
    default:
        return false;
    }
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

void throwIfCancelled(const Context &context)
{
    if (context.cancelled && context.cancelled()) {
        throw ToolCancelled{};
    }
}

QString requireLibraryId(const Context &context)
{
    if (context.libraryId.isEmpty()) {
        throw ToolError(QStringLiteral("invalid_input"), QStringLiteral("This AI tool requires an active Library."));
    }

    return context.libraryId;
}

QVariantMap fileSummary(const QVariantMap &file)
{
    return {
        {QStringLiteral("fileId"), file.value(QStringLiteral("id"))},
        {QStringLiteral("relativePath"), file.value(QStringLiteral("relativePath"))},
        {QStringLiteral("name"), file.value(QStringLiteral("name"))},
        {QStringLiteral("extension"), file.value(QStringLiteral("extension"))},
        {QStringLiteral("sizeBytes"), file.value(QStringLiteral("sizeBytes"))},
        {QStringLiteral("status"), file.value(QStringLiteral("status"))},
    };
}

QStringList nonEmptyParts(QString value)
{
    value.replace(QLatin1Char('\\'), QLatin1Char('/'));
    QStringList parts;

    for (const QString &part : value.split(QLatin1Char('/'))) {
        if (!part.isEmpty()) {
            parts.append(part);
        }
    }

    return parts;
}

bool hasTraversal(const QStringList &parts)
{
    return std::any_of(parts.cbegin(), parts.cend(), [](const QString &part) {
        return part == QStringLiteral(".") || part == QStringLiteral("..");
    });
}

QString normalizeRelativeDirectory(const QString &value)
{
    const QStringList parts = nonEmptyParts(value);

    if (hasTraversal(parts)) {
        throw ToolError(QStringLiteral("invalid_input"), QStringLiteral("Library directory paths may not contain traversal segments."));
    }

    return parts.join(QLatin1Char('/'));
}

QString resolveLibraryFileId(CoreDatabase &database, const QString &libraryId, const QString &reference)
{
    const QVariantList files = LibraryFileRepository::files(database, libraryId);

    for (const QVariant &file : files) {
        if (file.toMap().value(QStringLiteral("id")).toString() == reference) {
            return reference;
        }
    }

    const QStringList parts = nonEmptyParts(JsCompat::trimmed(QString(reference).replace(QLatin1Char('\\'), QLatin1Char('/'))));

    if (parts.isEmpty() || hasTraversal(parts)) {
        throw ToolError(
            QStringLiteral("invalid_input"),
            QStringLiteral("Library file references must be a catalog ID or safe Library-relative path.")
        );
    }

    const QString relativePath = parts.join(QLatin1Char('/'));

    for (const QVariant &file : files) {
        const QVariantMap map = file.toMap();

        if (map.value(QStringLiteral("relativePath")).toString() == relativePath) {
            return map.value(QStringLiteral("id")).toString();
        }
    }

    throw ToolError(
        QStringLiteral("invalid_input"),
        QStringLiteral("Library file reference \"%1\" was not found in the active catalog.").arg(relativePath)
    );
}

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

// ---- read tools ----

QVariantMap listDirectory(const QVariantMap &input, Context &context)
{
    throwIfCancelled(context);
    const QString libraryId = requireLibraryId(context);
    const QString directory = normalizeRelativeDirectory(input.value(QStringLiteral("path")).toString());
    const QString prefix = directory.isEmpty() ? QString() : directory + QLatin1Char('/');
    QList<QVariantMap> entries;
    QSet<QString> keys;

    for (const QVariant &value : LibraryFileRepository::files(*context.database, libraryId)) {
        const QVariantMap file = value.toMap();
        const QString relativePath = file.value(QStringLiteral("relativePath")).toString();

        if (!relativePath.startsWith(prefix)) {
            continue;
        }

        const QString remainder = relativePath.mid(prefix.size());

        if (remainder.isEmpty()) {
            continue;
        }

        const qsizetype separator = remainder.indexOf(QLatin1Char('/'));

        if (separator >= 0) {
            const QString name = remainder.left(separator);
            const QString path = directory.isEmpty() ? name : directory + QLatin1Char('/') + name;
            const QString key = QStringLiteral("directory:") + path;

            if (!keys.contains(key)) {
                keys.insert(key);
                entries.append({
                    {QStringLiteral("type"), QStringLiteral("directory")},
                    {QStringLiteral("name"), name},
                    {QStringLiteral("relativePath"), path},
                    {QStringLiteral("file"), QVariant::fromValue(nullptr)},
                });
            }

            continue;
        }

        const QString key = QStringLiteral("file:") + file.value(QStringLiteral("id")).toString();

        if (!keys.contains(key)) {
            keys.insert(key);
            entries.append({
                {QStringLiteral("type"), QStringLiteral("file")},
                {QStringLiteral("name"), file.value(QStringLiteral("name"))},
                {QStringLiteral("relativePath"), relativePath},
                {QStringLiteral("file"), fileSummary(file)},
            });
        }
    }

    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::stable_sort(entries.begin(), entries.end(), [&](const QVariantMap &first, const QVariantMap &second) {
        const QString firstType = first.value(QStringLiteral("type")).toString();
        const QString secondType = second.value(QStringLiteral("type")).toString();

        if (firstType != secondType) {
            return firstType == QStringLiteral("directory");
        }

        return collator.compare(first.value(QStringLiteral("name")).toString(), second.value(QStringLiteral("name")).toString()) < 0;
    });

    const int limit = input.value(QStringLiteral("limit")).toInt();
    QVariantList limited;

    for (qsizetype index = 0; index < entries.size() && index < limit; ++index) {
        limited.append(entries.at(index));
    }

    return {
        {QStringLiteral("path"), directory},
        {QStringLiteral("entries"), limited},
        {QStringLiteral("totalEntries"), entries.size()},
        {QStringLiteral("truncated"), entries.size() > limit},
    };
}

QVariantMap searchFilenames(const QVariantMap &input, Context &context)
{
    throwIfCancelled(context);
    const QString libraryId = requireLibraryId(context);
    const QString query = input.value(QStringLiteral("query")).toString();
    const QString normalizedQuery = query.toLower();
    const int limit = input.value(QStringLiteral("limit")).toInt();
    QVariantList matches;
    int total = 0;

    for (const QVariant &value : LibraryFileRepository::files(*context.database, libraryId)) {
        const QVariantMap file = value.toMap();
        const QString haystack = file.value(QStringLiteral("name")).toString() + QLatin1Char('\n')
            + file.value(QStringLiteral("relativePath")).toString();

        if (haystack.toLower().contains(normalizedQuery)) {
            if (total < limit) {
                matches.append(fileSummary(file));
            }

            ++total;
        }
    }

    return {
        {QStringLiteral("query"), query},
        {QStringLiteral("matches"), matches},
        {QStringLiteral("totalMatches"), total},
        {QStringLiteral("truncated"), total > limit},
    };
}

QVariantMap searchLibrary(const QVariantMap &input, Context &context)
{
    throwIfCancelled(context);
    const QString libraryId = requireLibraryId(context);
    const QVariantMap result = LibraryRetrieval::search(
        *context.database, input.value(QStringLiteral("query")).toString(), libraryId, input.value(QStringLiteral("limit")).toInt()
    );
    QVariantList matches;

    for (const QVariant &value : result.value(QStringLiteral("candidates")).toList()) {
        const QVariantMap candidate = value.toMap();
        const QVariantMap metadata = candidate.value(QStringLiteral("metadata")).toMap();
        const auto text = [&](const char *key) {
            const QVariant item = metadata.value(QLatin1String(key));
            return item.typeId() == QMetaType::QString ? item.toString() : QString();
        };
        const auto line = [&](const char *key) {
            const QVariant item = metadata.value(QLatin1String(key));
            return std::max(1, isNumber(item) ? item.toInt() : 0);
        };

        matches.append(QVariantMap{
            {QStringLiteral("fileId"), text("fileId")},
            {QStringLiteral("relativePath"), text("relativePath")},
            {QStringLiteral("fileName"), text("fileName")},
            {QStringLiteral("startLine"), line("startLine")},
            {QStringLiteral("endLine"), line("endLine")},
            {QStringLiteral("content"), candidate.value(QStringLiteral("content"))},
            {QStringLiteral("estimatedTokens"), candidate.value(QStringLiteral("estimatedTokens"))},
            {QStringLiteral("score"), candidate.value(QStringLiteral("score"))},
            {QStringLiteral("reason"), candidate.value(QStringLiteral("reason"))},
        });
    }

    return {
        {QStringLiteral("query"), result.value(QStringLiteral("query"))},
        {QStringLiteral("matches"), matches},
        {QStringLiteral("warnings"), result.value(QStringLiteral("warnings"))},
        {QStringLiteral("searchedAt"), result.value(QStringLiteral("searchedAt"))},
        {QStringLiteral("durationMs"), result.value(QStringLiteral("durationMs"))},
    };
}

QVariantMap readFile(const QVariantMap &input, Context &context)
{
    throwIfCancelled(context);
    const QString libraryId = requireLibraryId(context);
    const QString fileId = resolveLibraryFileId(*context.database, libraryId, input.value(QStringLiteral("fileId")).toString());
    const QVariantMap preview = LibraryFiles::readText(*context.database, libraryId, fileId);
    throwIfCancelled(context);
    const QString content = preview.value(QStringLiteral("content")).toString();

    return {
        {QStringLiteral("file"), fileSummary(preview.value(QStringLiteral("file")).toMap())},
        {QStringLiteral("content"), content.left(maximumReadCharacters)},
        {QStringLiteral("encoding"), preview.value(QStringLiteral("encoding"))},
        {QStringLiteral("lineCount"), preview.value(QStringLiteral("lineCount"))},
        {QStringLiteral("totalCharacterCount"), content.size()},
        {QStringLiteral("truncated"), content.size() > maximumReadCharacters},
        {QStringLiteral("readAt"), preview.value(QStringLiteral("readAt"))},
    };
}

QVariantMap readFileRange(const QVariantMap &input, Context &context)
{
    throwIfCancelled(context);
    const QString libraryId = requireLibraryId(context);
    const QString fileId = resolveLibraryFileId(*context.database, libraryId, input.value(QStringLiteral("fileId")).toString());
    const QVariantMap preview = LibraryFiles::readText(*context.database, libraryId, fileId);
    throwIfCancelled(context);
    const int startLine = input.value(QStringLiteral("startLine")).toInt();
    const int requestedEnd = input.value(QStringLiteral("endLine")).toInt();
    const int lineCount = preview.value(QStringLiteral("lineCount")).toInt();

    if (lineCount == 0 || startLine > lineCount) {
        throw ToolError(
            QStringLiteral("invalid_input"),
            QStringLiteral("startLine %1 is outside this %2-line file.").arg(startLine).arg(lineCount)
        );
    }

    static const QRegularExpression lineBreak(QStringLiteral("\\r\\n|\\r|\\n"));
    const QStringList lines = preview.value(QStringLiteral("content")).toString().split(lineBreak);
    const int endLine = std::min<int>(requestedEnd, lines.size());
    const QString fullContent = lines.mid(startLine - 1, endLine - startLine + 1).join(QLatin1Char('\n'));

    return {
        {QStringLiteral("file"), fileSummary(preview.value(QStringLiteral("file")).toMap())},
        {QStringLiteral("requestedStartLine"), startLine},
        {QStringLiteral("requestedEndLine"), requestedEnd},
        {QStringLiteral("startLine"), startLine},
        {QStringLiteral("endLine"), endLine},
        {QStringLiteral("totalLineCount"), lineCount},
        {QStringLiteral("content"), fullContent.left(maximumReadCharacters)},
        {QStringLiteral("totalCharacterCount"), fullContent.size()},
        {QStringLiteral("truncated"), fullContent.size() > maximumReadCharacters},
        {QStringLiteral("readAt"), preview.value(QStringLiteral("readAt"))},
    };
}

QVariantMap readFileRanges(const QVariantMap &input, Context &context)
{
    QVariantList results;
    int remaining = maximumBatchReadCharacters;
    int totalCharacters = 0;
    int returnedCharacters = 0;
    bool truncated = false;

    for (const QVariant &range : input.value(QStringLiteral("ranges")).toList()) {
        throwIfCancelled(context);
        QVariantMap result = readFileRange(range.toMap(), context);
        totalCharacters += result.value(QStringLiteral("totalCharacterCount")).toInt();
        const QString full = result.value(QStringLiteral("content")).toString();
        const QString content = full.left(remaining);
        const bool rangeTruncated = result.value(QStringLiteral("truncated")).toBool() || content.size() < full.size();
        truncated = truncated || rangeTruncated;
        remaining = std::max<int>(0, remaining - content.size());
        returnedCharacters += content.size();
        result.insert(QStringLiteral("content"), content);
        result.insert(QStringLiteral("truncated"), rangeTruncated);
        results.append(result);
    }

    return {
        {QStringLiteral("ranges"), results},
        {QStringLiteral("rangeCount"), results.size()},
        {QStringLiteral("totalCharacterCount"), totalCharacters},
        {QStringLiteral("returnedCharacterCount"), returnedCharacters},
        {QStringLiteral("truncated"), truncated},
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
    static const QStringList scopes{QStringLiteral("last_change"), QStringLiteral("before_task"), QStringLiteral("saved_layout")};
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

QVariantMap workspaceShowTool(const QVariantMap &input, Context &context)
{
    QVariantList items;

    for (const QVariant &value : input.value(QStringLiteral("items")).toList()) {
        const QVariantMap item = value.toMap();
        const QString kind = item.value(QStringLiteral("kind")).toString();
        const QString reference = item.value(QStringLiteral("reference")).toString();

        if (kind == QStringLiteral("file")) {
            if (JsCompat::isBlank(reference)) {
                throw ToolError(QStringLiteral("invalid_input"), QStringLiteral("A file needs its Library path or file id."));
            }
            const QVariantMap library = resolveLibrary(*context.database, context, item.value(QStringLiteral("library")).toString());
            const QString libraryId = library.value(QStringLiteral("id")).toString();
            const QString fileId = resolveLibraryFileId(*context.database, libraryId, reference);
            const auto file = LibraryFileRepository::fileById(*context.database, libraryId, fileId);
            if (!file) {
                throw ToolError(QStringLiteral("invalid_input"), QStringLiteral("Library file \"%1\" was not found.").arg(reference));
            }
            items.append(QVariantMap{
                {QStringLiteral("kind"), kind},
                {QStringLiteral("libraryId"), libraryId},
                {QStringLiteral("file"), *file},
            });
            continue;
        }

        QVariantMap resolved{{QStringLiteral("kind"), kind}, {QStringLiteral("reference"), reference}};
        if (kind == QStringLiteral("terminal") && !JsCompat::isBlank(item.value(QStringLiteral("library")).toString())) {
            resolved.insert(QStringLiteral("libraryId"), resolveLibrary(*context.database, context, item.value(QStringLiteral("library")).toString()).value(QStringLiteral("id")));
        }
        items.append(resolved);
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
    return callWorkspace(context, {
        {QStringLiteral("action"), input.value(QStringLiteral("action"))},
        {QStringLiteral("target"), input.value(QStringLiteral("area"))},
        {QStringLiteral("change"), input.value(QStringLiteral("change"))},
        {QStringLiteral("to"), input.value(QStringLiteral("to"))},
        {QStringLiteral("name"), input.value(QStringLiteral("name"))},
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
            QStringLiteral("Describe the person's workspace: each area (panel) with its name, position, size, what it shows and its locks, where the conversation is, whether a temporary arrangement can be undone, and the person's workspace preferences. Read it before arranging anything."),
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
            QStringLiteral("Open or bring forward files, chats or terminals in the workspace. Things already open are reused, not duplicated; several files share one area as tabs. Leave where empty to let Archivist choose. Only call this when the person asked to see, open or bring something up."),
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

        tools.append({
            QStringLiteral("workspace_arrange"),
            QStringLiteral("Arrange Workspace"),
            QStringLiteral("Change one area of the workspace: focus (bring it forward), resize (change: larger, smaller, maximize, restore, equal, or a share like 0.6), move (to: left, right, top, bottom, \"beside <area>\", or another area), keep (lock it where it is), label (name: what the person calls it), add (a new empty panel: to = side, area = the area it goes beside or \"\" for the window edge, name = optional), remove (take a panel away; its tabs move to the area in to, else the main editor; nothing closes). Locked areas refuse changes; never try to unlock one."),
            mutation, workspaceMode,
            objectSchema(
                {
                    {QStringLiteral("action"), enumSchema(arrangeActions())},
                    {QStringLiteral("area"), stringSchema(0, 200, areaDescription)},
                    {QStringLiteral("change"), stringSchema(0, 32, QStringLiteral("resize only, else \"\"."))},
                    {QStringLiteral("to"), stringSchema(0, 200, QStringLiteral("move, add and remove only, else \"\"."))},
                    {QStringLiteral("name"), stringSchema(0, 80, QStringLiteral("label and add only, else \"\"."))},
                },
                {QStringLiteral("action"), QStringLiteral("area"), QStringLiteral("change"), QStringLiteral("to"), QStringLiteral("name")}
            ),
            [](const QVariant &input) {
                const QVariantMap map = strictObject(input, {QStringLiteral("action"), QStringLiteral("area"), QStringLiteral("change"), QStringLiteral("to"), QStringLiteral("name")});
                return QVariantMap{
                    {QStringLiteral("action"), enumField(map, QStringLiteral("action"), arrangeActions())},
                    {QStringLiteral("area"), stringField(map, QStringLiteral("area"), 0, 200, true, QString())},
                    {QStringLiteral("change"), stringField(map, QStringLiteral("change"), 0, 32, true, QString())},
                    {QStringLiteral("to"), stringField(map, QStringLiteral("to"), 0, 200, true, QString())},
                    {QStringLiteral("name"), stringField(map, QStringLiteral("name"), 0, 80, true, QString())},
                };
            },
            workspaceArrangeTool,
            [](const QVariantMap &input) {
                return QVariantMap{{QStringLiteral("action"), input.value(QStringLiteral("action"))}, {QStringLiteral("area"), input.value(QStringLiteral("area"))}};
            },
            workspaceOutputSummary,
        });

        tools.append({
            QStringLiteral("workspace_restore"),
            QStringLiteral("Restore Workspace"),
            QStringLiteral("Put the workspace back. before_task: how it was before you started rearranging for this task (\"put everything back\", \"restore my workspace\"). last_change: undo only your most recent change. saved_layout: apply one of the person's saved layouts by name. Open files, chats and terminals stay open; unsaved work is never closed."),
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
            QStringLiteral("Named layouts: whole arrangements of panels the person can switch between. list shows the built-in and saved ones. apply switches to one by name (open tabs move into it; nothing closes; undoable). save stores the current arrangement under name; to build a new layout, arrange it first with workspace_arrange and workspace_show, then save. Saving over an existing name needs replace true, and only when the person said to replace it. rename (new_name) and delete work on the person's saved layouts only, and only when they ask."),
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

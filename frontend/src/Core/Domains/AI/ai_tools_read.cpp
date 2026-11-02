#include "ai_tools_read.h"
#include "ai_tools_definition.h"
#include "ai_tools_input.h"

#include "Core/Domains/Libraries/library_file_repository.h"
#include "Core/Domains/Libraries/library_files.h"
#include "Core/Support/js_compat.h"
#include "library_retrieval.h"

#include <QCollator>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <limits>

namespace AITools::ReadTools
{
namespace
{
constexpr int maximumReadCharacters = 64000;
constexpr int maximumBatchReadCharacters = 48000;
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

} // namespace AITools::ReadTools

// ---- tool definitions: schemas, input parsing and summaries ----

namespace
{
using namespace AITools::Input;

constexpr int maximumDirectoryEntries = 200;
constexpr int maximumFilenameMatches = 100;
constexpr int maximumSearchMatches = 24;
constexpr int maximumReadRangeLines = 160;
constexpr int maximumBatchReadRanges = 3;
constexpr int maximumBatchReadLines = 240;

const QString fileReferenceDescription = QStringLiteral(
    "Catalog UUID from a source file-id attribute, or the exact Library-relative path from its path attribute."
);

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

QVariantMap rangeSchemaProperties()
{
    return {
        {QStringLiteral("fileId"), stringSchema(1, 2048, fileReferenceDescription)},
        {QStringLiteral("startLine"), integerSchema(1)},
        {QStringLiteral("endLine"), integerSchema(1)},
    };
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
} // namespace

namespace AITools
{
QList<Definition> libraryReadTools()
{
    using namespace AITools::Input;
    QList<Definition> tools;
    const QString readOnly = readOnlyPermission;
    const QString direct = directMode;

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
        ReadTools::listDirectory,
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
        ReadTools::searchFilenames,
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
        ReadTools::searchLibrary,
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
        ReadTools::readFile,
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
        ReadTools::readFileRange,
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
        ReadTools::readFileRanges,
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

    return tools;
}
} // namespace AITools

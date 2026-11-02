#include "ai_tools_read.h"

#include "Core/Domains/Libraries/library_file_repository.h"
#include "Core/Domains/Libraries/library_files.h"
#include "Core/Support/js_compat.h"
#include "library_retrieval.h"

#include <QCollator>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

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

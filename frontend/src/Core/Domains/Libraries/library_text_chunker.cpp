#include "library_text_chunker.h"

#include "Core/Support/js_compat.h"

#include <QCryptographicHash>
#include <QStringList>
#include <algorithm>

namespace
{
constexpr int maximumChunkCharacters = 3200;
constexpr int maximumChunkLines = 80;
constexpr int sourceOverlapLines = 12;

struct LineRange
{
    int startLine = 0; // 1-based, inclusive
    int endLine = 0;   // 1-based, inclusive
};

qsizetype joinedLength(const QStringList &lines, int startLine, int endLine)
{
    qsizetype length = 0;

    for (int index = startLine - 1; index < endLine; ++index) {
        length += lines.at(index).size();
    }

    return length + std::max(0, endLine - startLine);
}

QString joined(const QStringList &lines, int startLine, int endLine)
{
    return lines.mid(startLine - 1, endLine - startLine + 1).join(QLatin1Char('\n'));
}

// /^#{1,6}\s/
bool isHeading(const QString &line)
{
    int hashes = 0;

    while (hashes < line.size() && line.at(hashes) == QLatin1Char('#')) {
        ++hashes;
    }

    return hashes >= 1 && hashes <= 6 && hashes < line.size() && JsCompat::isSpace(line.at(hashes));
}

QList<LineRange> sourceRanges(const QStringList &lines)
{
    QList<LineRange> ranges;
    int startIndex = 0;
    const int count = static_cast<int>(lines.size());

    while (startIndex < count) {
        int endIndex = startIndex;
        qsizetype characterCount = 0;

        while (endIndex < count && endIndex - startIndex < maximumChunkLines) {
            const qsizetype nextLength = lines.at(endIndex).size() + 1;

            if (endIndex > startIndex && characterCount + nextLength > maximumChunkCharacters) {
                break;
            }

            characterCount += nextLength;
            ++endIndex;
        }

        if (endIndex == startIndex) {
            ++endIndex;
        }

        ranges.append({startIndex + 1, endIndex});

        if (endIndex >= count) {
            break;
        }

        startIndex = std::max(startIndex + 1, endIndex - sourceOverlapLines);
    }

    return ranges;
}

QList<LineRange> proseBlocks(const QStringList &lines)
{
    QList<LineRange> blocks;
    int blockStart = -1;

    auto closeBlock = [&](int endIndexExclusive) {
        if (blockStart < 0) {
            return;
        }

        blocks.append({blockStart + 1, endIndexExclusive});
        blockStart = -1;
    };

    for (int index = 0; index < lines.size(); ++index) {
        const QString &line = lines.at(index);

        if (JsCompat::isBlank(line)) {
            closeBlock(index);
            continue;
        }

        const bool heading = isHeading(line);

        if (heading && blockStart >= 0) {
            closeBlock(index);
        }

        if (blockStart < 0) {
            blockStart = index;
        }

        if (heading) {
            closeBlock(index + 1);
        }
    }

    closeBlock(static_cast<int>(lines.size()));
    return blocks;
}

QList<LineRange> proseRanges(const QStringList &lines)
{
    const QList<LineRange> blocks = proseBlocks(lines);

    if (blocks.isEmpty()) {
        return {};
    }

    QList<LineRange> ranges;
    int currentStart = blocks.first().startLine;
    int currentEnd = blocks.first().endLine;
    qsizetype currentLength = joinedLength(lines, currentStart, currentEnd);

    for (qsizetype index = 1; index < blocks.size(); ++index) {
        const LineRange &block = blocks.at(index);
        const qsizetype blockLength = joinedLength(lines, block.startLine, block.endLine);

        if (currentLength > 0 && currentLength + blockLength + 2 > maximumChunkCharacters) {
            ranges.append({currentStart, currentEnd});
            currentStart = block.startLine;
            currentEnd = block.endLine;
            currentLength = blockLength;
            continue;
        }

        currentEnd = block.endLine;
        currentLength += blockLength + 2;
    }

    ranges.append({currentStart, currentEnd});

    QList<LineRange> result;

    for (const LineRange &range : ranges) {
        const qsizetype length = joinedLength(lines, range.startLine, range.endLine);

        if (length <= maximumChunkCharacters && range.endLine - range.startLine + 1 <= maximumChunkLines) {
            result.append(range);
            continue;
        }

        for (const LineRange &nested : sourceRanges(lines.mid(range.startLine - 1, range.endLine - range.startLine + 1))) {
            result.append({range.startLine + nested.startLine - 1, range.startLine + nested.endLine - 1});
        }
    }

    return result;
}
}

namespace LibraryTextChunker
{
QString normalize(const QString &content)
{
    QString normalized = content;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return normalized;
}

QString hash(const QString &content)
{
    return QString::fromLatin1(QCryptographicHash::hash(content.toUtf8(), QCryptographicHash::Sha256).toHex());
}

int estimateTokens(const QString &content)
{
    if (content.isEmpty()) {
        return 0;
    }

    return std::max<int>(1, static_cast<int>((content.size() + 3) / 4));
}

QList<LibraryFileRepository::TextChunk> chunk(
    const QString &libraryId,
    const QString &libraryFileId,
    const QString &extension,
    const QString &content
)
{
    const QString normalized = normalize(content);

    if (JsCompat::isBlank(normalized)) {
        return {};
    }

    const QStringList lines = normalized.split(QLatin1Char('\n'));
    const QString lowerExtension = extension.toLower();
    const bool prose = lowerExtension == QStringLiteral(".md") || lowerExtension == QStringLiteral(".txt");
    const QList<LineRange> ranges = prose ? proseRanges(lines) : sourceRanges(lines);

    QList<LibraryFileRepository::TextChunk> chunks;

    for (const LineRange &range : ranges) {
        const QString chunkContent = JsCompat::trimmed(joined(lines, range.startLine, range.endLine));

        if (chunkContent.isEmpty()) {
            continue;
        }

        const int ordinal = static_cast<int>(chunks.size());
        const QString contentHash = hash(chunkContent);
        const QString id = hash(QStringLiteral("%1:%2:%3:%4:%5")
                                    .arg(libraryFileId)
                                    .arg(ordinal)
                                    .arg(range.startLine)
                                    .arg(range.endLine)
                                    .arg(contentHash));

        chunks.append({
            id,
            libraryFileId,
            libraryId,
            ordinal,
            range.startLine,
            range.endLine,
            chunkContent,
            estimateTokens(chunkContent),
            contentHash,
        });
    }

    return chunks;
}
}

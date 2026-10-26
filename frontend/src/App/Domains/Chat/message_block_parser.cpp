#include "message_block_parser.h"

#include <QRegularExpression>
#include <QStringList>
#include <QVariantMap>
#include <algorithm>
#include <optional>

namespace
{
struct Fence
{
    QChar marker;
    qsizetype markerLength = 0;
    QString remainder;
};

bool isBlank(const QString &line)
{
    return line.trimmed().isEmpty();
}

std::optional<Fence> fenceDescriptor(const QString &line)
{
    qsizetype leadingSpaces = 0;

    while (leadingSpaces < line.size() && leadingSpaces < 3 && line.at(leadingSpaces) == QLatin1Char(' ')) {
        ++leadingSpaces;
    }

    const QStringView source = QStringView(line).mid(leadingSpaces);

    if (source.isEmpty() || (source.at(0) != QLatin1Char('`') && source.at(0) != QLatin1Char('~'))) {
        return std::nullopt;
    }

    const QChar marker = source.at(0);
    qsizetype markerLength = 0;

    while (markerLength < source.size() && source.at(markerLength) == marker) {
        ++markerLength;
    }

    if (markerLength < 3) {
        return std::nullopt;
    }

    return Fence{marker, markerLength, source.mid(markerLength).toString()};
}

QString normalizedLanguage(const QString &info)
{
    static const QRegularExpression whitespace(QStringLiteral("\\s+"));
    static const QRegularExpression leading(QStringLiteral("^\\{?\\.?"));
    static const QRegularExpression trailing(QStringLiteral("\\}?$"));
    static const QRegularExpression disallowed(QStringLiteral("[^A-Za-z0-9_+#.-]"));

    // "python:src/app.py" names the language before the colon.
    QString token = info.trimmed().split(whitespace).value(0).section(QLatin1Char(':'), 0, 0);
    token.replace(leading, QString());
    token.replace(trailing, QString());
    token.replace(disallowed, QString());
    return token;
}

// The file a fence names, if any: ```ts src/app.ts, ```python:app.py or
// ```js title="app.js".
QString fenceFileName(const QString &info)
{
    static const QRegularExpression whitespace(QStringLiteral("\\s+"));
    static const QRegularExpression attribute(
        QStringLiteral(R"re((?:title|file|filename|name)\s*=\s*"?([^"\s]+)"?)re"), QRegularExpression::CaseInsensitiveOption);
    const auto looksLikePath = [](const QString &value) {
        return !value.isEmpty() && !value.contains(QLatin1Char('='))
            && (value.contains(QLatin1Char('.')) || value.contains(QLatin1Char('/')));
    };

    const QString trimmed = info.trimmed();
    const auto match = attribute.match(trimmed);

    if (match.hasMatch()) {
        return match.captured(1);
    }

    const QStringList tokens = trimmed.split(whitespace, Qt::SkipEmptyParts);
    const QString first = tokens.value(0);

    if (first.contains(QLatin1Char(':'))) {
        const QString named = first.section(QLatin1Char(':'), 1);

        if (looksLikePath(named)) {
            return named;
        }
    }

    const QString second = tokens.value(1);
    return looksLikePath(second) ? second : QString();
}

QString trimBoundaryLines(const QString &value)
{
    qsizetype start = 0;
    qsizetype end = value.size();

    while (start < end && value.at(start) == QLatin1Char('\n')) {
        ++start;
    }

    while (end > start && value.at(end - 1) == QLatin1Char('\n')) {
        --end;
    }

    return value.mid(start, end - start);
}

bool thematicBreak(const QString &line)
{
    static const QRegularExpression pattern(
        QStringLiteral("^ {0,3}(?:(?:\\*[ \\t]*){3,}|(?:_[ \\t]*){3,}|(?:-[ \\t]*){3,})$")
    );
    return pattern.match(line).hasMatch();
}

bool markdownTableDelimiter(const QString &line)
{
    static const QRegularExpression delimiterCell(QStringLiteral("^:?-{3,}:?$"));
    const QString source = line.trimmed();

    if (!source.contains(QLatin1Char('|'))) {
        return false;
    }

    QStringList cells = source.split(QLatin1Char('|'));

    if (!cells.isEmpty() && isBlank(cells.first())) {
        cells.removeFirst();
    }

    if (!cells.isEmpty() && isBlank(cells.last())) {
        cells.removeLast();
    }

    if (cells.size() < 2) {
        return false;
    }

    return std::all_of(cells.cbegin(), cells.cend(), [](const QString &cell) {
        return delimiterCell.match(cell.trimmed()).hasMatch();
    });
}

bool markdownTableRow(const QString &line)
{
    const QString source = line.trimmed();
    return !source.isEmpty() && source.contains(QLatin1Char('|'));
}

class BlockBuilder
{
public:
    QVariantList result;

    void pushMarkdown(const QStringList &lines, bool structured)
    {
        const QString markdown = trimBoundaryLines(lines.join(QLatin1Char('\n')));

        if (markdown.isEmpty()) {
            return;
        }

        const qsizetype rowCount = markdown.count(QLatin1Char('\n')) + 1;
        QVariantMap block;
        block.insert(QStringLiteral("kind"), structured ? QStringLiteral("structuredMarkdown") : QStringLiteral("markdown"));
        block.insert(QStringLiteral("content"), markdown);
        block.insert(
            QStringLiteral("revealLength"),
            structured ? std::min<qsizetype>(48, std::max<qsizetype>(16, rowCount * 6)) : markdown.size()
        );
        result.append(block);
    }

    void flushSection(QStringList &sectionLines)
    {
        QStringList proseLines;

        for (qsizetype index = 0; index < sectionLines.size(); ++index) {
            const bool beginsTable = index + 1 < sectionLines.size() && markdownTableRow(sectionLines.at(index))
                && markdownTableDelimiter(sectionLines.at(index + 1));

            if (!beginsTable) {
                proseLines.append(sectionLines.at(index));
                continue;
            }

            pushMarkdown(proseLines, false);
            proseLines.clear();

            QStringList tableLines{sectionLines.at(index), sectionLines.at(index + 1)};
            index += 2;

            while (index < sectionLines.size() && markdownTableRow(sectionLines.at(index))) {
                tableLines.append(sectionLines.at(index));
                ++index;
            }

            --index;
            pushMarkdown(tableLines, true);
        }

        pushMarkdown(proseLines, false);
        sectionLines.clear();
    }

    void flushMarkdown(QStringList &markdownLines)
    {
        QStringList sectionLines;

        for (qsizetype index = 0; index < markdownLines.size(); ++index) {
            const QString &line = markdownLines.at(index);
            const bool previousBlank = index == 0 || isBlank(markdownLines.at(index - 1));
            const bool nextBlank = index == markdownLines.size() - 1 || isBlank(markdownLines.at(index + 1));

            if (thematicBreak(line) && previousBlank && nextBlank) {
                flushSection(sectionLines);
                result.append(QVariantMap{{QStringLiteral("kind"), QStringLiteral("divider")}});
            } else {
                sectionLines.append(line);
            }
        }

        flushSection(sectionLines);
        markdownLines.clear();
    }

    void pushCode(const QStringList &codeLines, const QString &language, const QString &fileName, bool complete)
    {
        QVariantMap block{
            {QStringLiteral("kind"), QStringLiteral("code")},
            {QStringLiteral("content"), codeLines.join(QLatin1Char('\n'))},
            {QStringLiteral("language"), language},
            {QStringLiteral("complete"), complete},
        };

        // Only fences that name a file carry one ("```ts src/app.ts").
        if (!fileName.isEmpty()) {
            block.insert(QStringLiteral("fileName"), fileName);
        }

        result.append(block);
    }
};
}

MessageBlockParser::MessageBlockParser(QObject *parent)
    : QObject(parent)
{
}

QVariantList MessageBlockParser::parse(const QString &content) const
{
    return parseBlocks(content);
}

QVariantList MessageBlockParser::parseBlocks(const QString &content)
{
    QString normalized = content;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    const QStringList lines = normalized.split(QLatin1Char('\n'));
    BlockBuilder builder;
    QStringList markdownLines;
    QStringList codeLines;
    QChar activeMarker;
    qsizetype activeMarkerLength = 0;
    QString language;
    QString fileName;

    for (const QString &line : lines) {
        const auto fence = fenceDescriptor(line);

        if (activeMarker.isNull()) {
            if (fence) {
                builder.flushMarkdown(markdownLines);
                activeMarker = fence->marker;
                activeMarkerLength = fence->markerLength;
                language = normalizedLanguage(fence->remainder);
                fileName = fenceFileName(fence->remainder);
                codeLines.clear();
            } else {
                markdownLines.append(line);
            }

            continue;
        }

        const bool closesFence = fence && fence->marker == activeMarker && fence->markerLength >= activeMarkerLength
            && isBlank(fence->remainder);

        if (closesFence) {
            builder.pushCode(codeLines, language, fileName, true);
            activeMarker = QChar();
            activeMarkerLength = 0;
            language.clear();
            fileName.clear();
            codeLines.clear();
        } else {
            codeLines.append(line);
        }
    }

    if (!activeMarker.isNull()) {
        builder.pushCode(codeLines, language, fileName, false);
    } else {
        builder.flushMarkdown(markdownLines);
    }

    QVariantList &result = builder.result;

    if (result.isEmpty()) {
        result.append(QVariantMap{{QStringLiteral("kind"), QStringLiteral("markdown")}, {QStringLiteral("content"), QString()}});
    }

    qsizetype revealOffset = 0;

    for (QVariant &value : result) {
        QVariantMap block = value.toMap();
        const QString kind = block.value(QStringLiteral("kind")).toString();
        qsizetype revealLength = 0;

        if (block.contains(QStringLiteral("revealLength"))) {
            revealLength = std::max<qsizetype>(1, block.value(QStringLiteral("revealLength")).toLongLong());
        } else if (kind == QStringLiteral("divider")) {
            revealLength = 1;
        } else {
            revealLength = block.value(QStringLiteral("content")).toString().size();
        }

        block.insert(QStringLiteral("revealStart"), revealOffset);
        block.insert(QStringLiteral("revealEnd"), revealOffset + revealLength);
        revealOffset += revealLength;
        value = block;
    }

    return result;
}

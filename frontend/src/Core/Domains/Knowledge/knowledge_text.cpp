#include "knowledge_text.h"

#include <QCryptographicHash>
#include <QRegularExpression>
#include <QVector>

#include <algorithm>

namespace KnowledgeText
{
QString sha256(const QString &text)
{
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QString collapsedWhitespace(const QString &text)
{
    return text.simplified();
}

QString normalizedStatement(const QString &text)
{
    QString value = text.simplified().toLower();

    while (!value.isEmpty() && QStringLiteral(".!;:,").contains(value.back())) {
        value.chop(1);
    }

    return value.trimmed();
}

QString statementKey(const QString &text)
{
    return sha256(normalizedStatement(text));
}

QString quoteKey(const QString &quote)
{
    return sha256(collapsedWhitespace(quote));
}

QString pageEmbeddingText(const QString &title, const QString &content)
{
    // Must match the SQL in EmbeddingIndex::pendingKnowledgeSources:
    // title || char(10) || char(10) || content (titles are stored trimmed).
    return title.trimmed() + QStringLiteral("\n\n") + content;
}

QString pageContentHash(const QString &title, const QString &content)
{
    return sha256(pageEmbeddingText(title, content));
}

QString slugify(const QString &title)
{
    static const QRegularExpression nonWord(QStringLiteral("[^a-z0-9]+"));
    QString slug = title.toLower();
    slug.replace(nonWord, QStringLiteral("-"));

    while (slug.startsWith(QLatin1Char('-'))) {
        slug.remove(0, 1);
    }

    while (slug.endsWith(QLatin1Char('-'))) {
        slug.chop(1);
    }

    slug = slug.left(80);
    return slug.isEmpty() ? QStringLiteral("page") : slug;
}

QuoteMatch locateQuote(const QString &content, const QString &quote, int firstLine)
{
    QuoteMatch match;
    const QString needle = collapsedWhitespace(quote);

    if (needle.isEmpty()) {
        return match;
    }

    // Build the collapsed text with a map back to original line numbers.
    QString collapsed;
    QVector<int> lineOf;
    collapsed.reserve(content.size());
    lineOf.reserve(content.size());
    int line = firstLine;
    bool pendingSpace = false;

    for (const QChar character : content) {
        if (character.isSpace()) {
            pendingSpace = !collapsed.isEmpty();
        } else {
            if (pendingSpace) {
                collapsed.append(QLatin1Char(' '));
                lineOf.append(line);
                pendingSpace = false;
            }

            collapsed.append(character);
            lineOf.append(line);
        }

        if (character == QLatin1Char('\n')) {
            ++line;
        }
    }

    const qsizetype at = collapsed.indexOf(needle);

    if (at < 0) {
        return match;
    }

    match.found = true;
    match.startLine = lineOf.at(at);
    match.endLine = lineOf.at(at + needle.size() - 1);
    return match;
}

QString lineDiff(const QString &before, const QString &after)
{
    if (before == after) {
        return {};
    }

    const QStringList a = before.isEmpty() ? QStringList() : before.split(QLatin1Char('\n'));
    const QStringList b = after.isEmpty() ? QStringList() : after.split(QLatin1Char('\n'));
    QStringList out;
    const qsizetype n = a.size();
    const qsizetype m = b.size();

    if (n * m > 4'000'000) {
        for (const QString &line : a) {
            out.append(QStringLiteral("- ") + line);
        }

        for (const QString &line : b) {
            out.append(QStringLiteral("+ ") + line);
        }

        return out.join(QLatin1Char('\n'));
    }

    // Longest common subsequence table, then a forward walk.
    QVector<QVector<int>> lcs(n + 1, QVector<int>(m + 1, 0));

    for (qsizetype i = n - 1; i >= 0; --i) {
        for (qsizetype j = m - 1; j >= 0; --j) {
            lcs[i][j] = a.at(i) == b.at(j) ? lcs[i + 1][j + 1] + 1 : std::max(lcs[i + 1][j], lcs[i][j + 1]);
        }
    }

    qsizetype i = 0;
    qsizetype j = 0;

    while (i < n && j < m) {
        if (a.at(i) == b.at(j)) {
            out.append(QStringLiteral("  ") + a.at(i));
            ++i;
            ++j;
        } else if (lcs[i + 1][j] >= lcs[i][j + 1]) {
            out.append(QStringLiteral("- ") + a.at(i++));
        } else {
            out.append(QStringLiteral("+ ") + b.at(j++));
        }
    }

    while (i < n) {
        out.append(QStringLiteral("- ") + a.at(i++));
    }

    while (j < m) {
        out.append(QStringLiteral("+ ") + b.at(j++));
    }

    return out.join(QLatin1Char('\n'));
}
}

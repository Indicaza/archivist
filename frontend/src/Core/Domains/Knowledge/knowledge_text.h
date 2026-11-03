#pragma once

#include <QString>
#include <QStringList>

// Text helpers for the Knowledge Store: hashes, keys, slugs, quote matching
// and a small line diff. Pure functions, no database.
namespace KnowledgeText
{
// SHA-256 hex of the UTF-8 text (the same hash Library chunks use).
[[nodiscard]] QString sha256(const QString &text);

// Whitespace collapsed, trimmed, lower-cased, trailing punctuation dropped:
// two statements with the same key say the same thing.
[[nodiscard]] QString normalizedStatement(const QString &text);
[[nodiscard]] QString statementKey(const QString &text);

// Whitespace collapsed and trimmed; case kept. Quotes are matched on this.
[[nodiscard]] QString collapsedWhitespace(const QString &text);
[[nodiscard]] QString quoteKey(const QString &quote);

// The text a page is embedded and searched as, and its hash.
[[nodiscard]] QString pageEmbeddingText(const QString &title, const QString &content);
[[nodiscard]] QString pageContentHash(const QString &title, const QString &content);

// "Release Process" -> "release-process"; never empty.
[[nodiscard]] QString slugify(const QString &title);

struct QuoteMatch
{
    bool found = false;
    int startLine = 0; // 1-based, in the coordinates of `firstLine`
    int endLine = 0;
};

// Finds `quote` in `content` ignoring differences in whitespace, and returns
// the lines it covers, counting `content`'s first line as `firstLine`.
[[nodiscard]] QuoteMatch locateQuote(const QString &content, const QString &quote, int firstLine);

// A unified-style line diff ("  same", "- removed", "+ added"). Empty when
// both texts are equal. Bounded: very long inputs fall back to a
// remove-all/add-all diff.
[[nodiscard]] QString lineDiff(const QString &before, const QString &after);
}

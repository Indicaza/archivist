#include "library_retrieval.h"

#include "Core/Support/js_compat.h"
#include "Core/core_error.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace
{
constexpr int defaultLimit = 12;
constexpr int maximumLimit = 50;
constexpr int candidateMultiplier = 4;
constexpr int minimumCandidatePool = 24;

const QSet<QString> &ignoredSearchTokens()
{
    static const QSet<QString> tokens{
        QStringLiteral("a"), QStringLiteral("about"), QStringLiteral("also"), QStringLiteral("an"),
        QStringLiteral("and"), QStringLiteral("are"), QStringLiteral("as"), QStringLiteral("at"),
        QStringLiteral("be"), QStringLiteral("but"), QStringLiteral("by"), QStringLiteral("can"),
        QStringLiteral("check"), QStringLiteral("describe"), QStringLiteral("do"), QStringLiteral("does"),
        QStringLiteral("explain"), QStringLiteral("fits"), QStringLiteral("for"), QStringLiteral("from"),
        QStringLiteral("get"), QStringLiteral("give"), QStringLiteral("had"), QStringLiteral("has"),
        QStringLiteral("have"), QStringLiteral("hello"), QStringLiteral("hey"), QStringLiteral("hi"),
        QStringLiteral("how"), QStringLiteral("i"), QStringLiteral("in"), QStringLiteral("is"),
        QStringLiteral("it"), QStringLiteral("know"), QStringLiteral("me"), QStringLiteral("more"),
        QStringLiteral("my"), QStringLiteral("of"), QStringLiteral("on"), QStringLiteral("one"),
        QStringLiteral("or"), QStringLiteral("our"), QStringLiteral("overall"), QStringLiteral("please"),
        QStringLiteral("tell"), QStringLiteral("that"), QStringLiteral("the"), QStringLiteral("their"),
        QStringLiteral("this"), QStringLiteral("to"), QStringLiteral("tool"), QStringLiteral("tools"),
        QStringLiteral("use"), QStringLiteral("vibe"), QStringLiteral("was"), QStringLiteral("what"),
        QStringLiteral("when"), QStringLiteral("where"), QStringLiteral("which"), QStringLiteral("who"),
        QStringLiteral("why"), QStringLiteral("with"), QStringLiteral("you"), QStringLiteral("your"),
    };
    return tokens;
}

const QSet<QString> &broadContextTokens()
{
    static const QSet<QString> tokens{
        QStringLiteral("background"), QStringLiteral("history"), QStringLiteral("lore"), QStringLiteral("overview"),
        QStringLiteral("category"), QStringLiteral("race"), QStringLiteral("setting"), QStringLiteral("theme"),
        QStringLiteral("themes"), QStringLiteral("tone"), QStringLiteral("vibe"), QStringLiteral("world"),
        QStringLiteral("type"),
    };
    return tokens;
}

const QSet<QString> &implementationIntentTokens()
{
    static const QSet<QString> tokens{
        QStringLiteral("code"), QStringLiteral("controller"), QStringLiteral("gameplay"),
        QStringLiteral("implementation"), QStringLiteral("mechanic"), QStringLiteral("mechanics"),
        QStringLiteral("script"), QStringLiteral("scripts"), QStringLiteral("system"), QStringLiteral("systems"),
    };
    return tokens;
}

const QSet<QString> &implementationPathTokens()
{
    static const QSet<QString> tokens{
        QStringLiteral("backend"), QStringLiteral("code"), QStringLiteral("controller"), QStringLiteral("frontend"),
        QStringLiteral("game"), QStringLiteral("mechanics"), QStringLiteral("script"), QStringLiteral("scripts"),
        QStringLiteral("src"), QStringLiteral("test"), QStringLiteral("tests"),
    };
    return tokens;
}

struct Row
{
    QVariantMap values;
    QString chunkId;
    QString relativePath;
    QString fileName;
    QString content;
    double rank = 0;
    QSet<QString> pathTokens;
};

QStringList matches(const QRegularExpression &pattern, const QString &text)
{
    QStringList result;
    auto iterator = pattern.globalMatch(text);

    while (iterator.hasNext()) {
        result.append(iterator.next().captured(0));
    }

    return result;
}

QString canonicalSubjectToken(const QString &token)
{
    if (token.size() > 4 && token.endsWith(QStringLiteral("ies"))) {
        return token.left(token.size() - 3) + QLatin1Char('y');
    }

    if (token.size() > 3 && token.endsWith(QLatin1Char('s')) && !token.endsWith(QStringLiteral("ss"))) {
        return token.left(token.size() - 1);
    }

    return token;
}

void appendUnique(QStringList &list, QSet<QString> &seen, const QString &value)
{
    if (!seen.contains(value)) {
        seen.insert(value);
        list.append(value);
    }
}

QStringList tokenizeQuery(const QString &query)
{
    static const QRegularExpression word(QStringLiteral("[\\p{L}\\p{N}_]+"));
    QStringList expanded;
    QSet<QString> seen;

    for (const QString &token : matches(word, query.toLower())) {
        if (token.size() < 2 || ignoredSearchTokens().contains(token)) {
            continue;
        }

        appendUnique(expanded, seen, token);
        appendUnique(expanded, seen, canonicalSubjectToken(token));
    }

    return expanded.mid(0, 24);
}

QSet<QString> tokenizePath(const QString &value)
{
    static const QRegularExpression camel(QStringLiteral("([\\p{Ll}\\p{N}])(\\p{Lu})"));
    static const QRegularExpression word(QStringLiteral("[\\p{L}\\p{N}]+"));
    QString normalized = value;
    normalized.replace(camel, QStringLiteral("\\1 \\2"));
    normalized.replace(QLatin1Char('_'), QLatin1Char(' '));
    QSet<QString> tokens;

    for (const QString &token : matches(word, normalized.toLower())) {
        tokens.insert(canonicalSubjectToken(token));
    }

    return tokens;
}

QString ftsQuery(const QString &query)
{
    QStringList quoted;

    for (QString token : tokenizeQuery(query)) {
        quoted.append(QLatin1Char('"') + token.replace(QLatin1Char('"'), QStringLiteral("\"\"")) + QLatin1Char('"'));
    }

    return quoted.join(QStringLiteral(" OR "));
}

double normalizeScore(double rank)
{
    if (!std::isfinite(rank)) {
        return 0;
    }

    return std::round(std::max(0.0, -rank) * 1e6) / 1e6;
}

// JS RegExp `\b` is ASCII-only even with the u flag, which matches PCRE2
// without the Unicode-properties option.
QRegularExpression boundedToken(const QString &token, bool caseInsensitive)
{
    return QRegularExpression(
        QStringLiteral("\\b") + token + QStringLiteral("\\b"),
        caseInsensitive ? QRegularExpression::CaseInsensitiveOption : QRegularExpression::NoPatternOption
    );
}

int contentTokenCount(const QString &content, const QString &token)
{
    const QRegularExpression pattern = boundedToken(token, false);
    int count = 0;
    auto iterator = pattern.globalMatch(content.toLower());

    while (iterator.hasNext() && count < 4) {
        iterator.next();
        ++count;
    }

    return count;
}

double relevanceScore(const Row &row, const QStringList &queryTokens, const QStringList &directSubjects)
{
    int directPathMatches = 0;
    int directContentMatches = 0;
    int queryPathMatches = 0;
    int queryContentMatches = 0;

    for (const QString &token : directSubjects) {
        directPathMatches += row.pathTokens.contains(token) ? 1 : 0;
        directContentMatches += contentTokenCount(row.content, token);
    }

    for (const QString &token : queryTokens) {
        queryPathMatches += row.pathTokens.contains(token) ? 1 : 0;
        queryContentMatches += contentTokenCount(row.content, token);
    }

    return directPathMatches * 10000.0 + directContentMatches * 500.0 + queryPathMatches * 100.0
        + queryContentMatches * 5.0 + normalizeScore(row.rank);
}

bool isImplementationRow(const Row &row)
{
    for (const QString &token : implementationPathTokens()) {
        if (row.pathTokens.contains(token)) {
            return true;
        }
    }

    return false;
}

bool containsAny(const QSet<QString> &tokens, const QStringList &subjects)
{
    return std::any_of(subjects.cbegin(), subjects.cend(), [&](const QString &subject) { return tokens.contains(subject); });
}

QList<Row> subjectBalancedRows(const QString &query, const QList<Row> &rows, int limit)
{
    const QStringList queryTokens = tokenizeQuery(query);
    QStringList directSubjects;

    for (const QString &token : queryTokens) {
        if (std::any_of(rows.cbegin(), rows.cend(), [&](const Row &row) { return row.pathTokens.contains(token); })) {
            directSubjects.append(token);
        }
    }

    if (directSubjects.isEmpty()) {
        return rows.mid(0, limit);
    }

    QStringList specificSubjects;
    QStringList broadSubjects;

    for (const QString &subject : directSubjects) {
        (broadContextTokens().contains(subject) ? broadSubjects : specificSubjects).append(subject);
    }

    const bool requestsBroadContext = std::any_of(queryTokens.cbegin(), queryTokens.cend(), [](const QString &token) {
        return broadContextTokens().contains(token);
    });
    const bool requestsImplementation = std::any_of(queryTokens.cbegin(), queryTokens.cend(), [](const QString &token) {
        return implementationIntentTokens().contains(token);
    });

    QList<QPair<double, Row>> scored;

    for (const Row &row : rows) {
        scored.append({relevanceScore(row, queryTokens, directSubjects), row});
    }

    std::stable_sort(scored.begin(), scored.end(), [](const auto &first, const auto &second) {
        if (first.first != second.first) {
            return first.first > second.first;
        }

        return first.second.rank < second.second.rank;
    });

    QList<Row> eligibleRows;

    for (const auto &[score, row] : scored) {
        if (requestsImplementation || !isImplementationRow(row) || containsAny(row.pathTokens, specificSubjects)) {
            eligibleRows.append(row);
        }
    }

    const int contextAllowance = requestsBroadContext ? 1 : 0;
    const int targetLimit = std::min<int>(
        limit,
        !specificSubjects.isEmpty() ? std::min<int>(6, specificSubjects.size() * 2 + contextAllowance)
                                    : std::min<int>(4, eligibleRows.size())
    );

    QList<Row> directRows;
    QList<Row> supportingRows;
    QList<Row> fallbackRows;
    QSet<QString> directIds;

    for (const Row &row : eligibleRows) {
        if (containsAny(row.pathTokens, directSubjects)) {
            directRows.append(row);
            directIds.insert(row.chunkId);
        }
    }

    QSet<QString> supportingIds;

    for (const Row &row : eligibleRows) {
        if (directIds.contains(row.chunkId)) {
            continue;
        }

        const bool supports = std::any_of(specificSubjects.cbegin(), specificSubjects.cend(), [&](const QString &subject) {
            return boundedToken(subject, true).match(row.content).hasMatch();
        });

        if (supports) {
            supportingRows.append(row);
            supportingIds.insert(row.chunkId);
        }
    }

    for (const Row &row : eligibleRows) {
        if (!directIds.contains(row.chunkId) && !supportingIds.contains(row.chunkId)) {
            fallbackRows.append(row);
        }
    }

    QList<Row> selected;
    QSet<QString> selectedIds;

    for (const QString &subject : specificSubjects) {
        for (const Row &row : directRows) {
            if (!selectedIds.contains(row.chunkId) && row.pathTokens.contains(subject)) {
                selected.append(row);
                selectedIds.insert(row.chunkId);
                break;
            }
        }
    }

    for (const Row &row : directRows) {
        if (!selectedIds.contains(row.chunkId) && containsAny(row.pathTokens, broadSubjects)) {
            if (selected.size() < targetLimit) {
                selected.append(row);
                selectedIds.insert(row.chunkId);
            }

            break;
        }
    }

    for (const QList<Row> *group : {&directRows, &supportingRows, &fallbackRows}) {
        for (const Row &row : *group) {
            if (selected.size() >= targetLimit) {
                return selected;
            }

            if (!selectedIds.contains(row.chunkId)) {
                selected.append(row);
                selectedIds.insert(row.chunkId);
            }
        }
    }

    return selected;
}

QVariantMap mapCandidate(const Row &row)
{
    const QVariantMap &values = row.values;
    return {
        {QStringLiteral("id"), row.chunkId},
        {QStringLiteral("source"), QStringLiteral("library-file")},
        {QStringLiteral("content"), row.content},
        {QStringLiteral("estimatedTokens"), values.value(QStringLiteral("estimatedTokens"))},
        {QStringLiteral("score"), normalizeScore(row.rank)},
        {QStringLiteral("reason"), QStringLiteral("Matched indexed Library text using SQLite FTS5 with subject-aware reranking.")},
        {
            QStringLiteral("metadata"),
            QVariantMap{
                {QStringLiteral("libraryId"), values.value(QStringLiteral("libraryId"))},
                {QStringLiteral("fileId"), values.value(QStringLiteral("fileId"))},
                {QStringLiteral("chunkId"), row.chunkId},
                {QStringLiteral("relativePath"), row.relativePath},
                {QStringLiteral("fileName"), row.fileName},
                {QStringLiteral("extension"), values.value(QStringLiteral("extension"))},
                {QStringLiteral("startLine"), values.value(QStringLiteral("startLine"))},
                {QStringLiteral("endLine"), values.value(QStringLiteral("endLine"))},
                {QStringLiteral("contentHash"), values.value(QStringLiteral("contentHash"))},
                {QStringLiteral("excerpt"), values.value(QStringLiteral("excerpt"))},
                {QStringLiteral("ftsRank"), row.rank},
            },
        },
    };
}

double elapsed(const QElapsedTimer &timer)
{
    return std::round(timer.nsecsElapsed() / 1000.0) / 1000.0;
}
}

namespace LibraryRetrieval
{
QVariantMap search(CoreDatabase &database, const QString &query, const QString &libraryId, int limit)
{
    QElapsedTimer timer;
    timer.start();
    const QString normalizedQuery = JsCompat::trimmed(query);
    const QString fts = ftsQuery(normalizedQuery);
    const int clampedLimit = limit <= 0 ? defaultLimit : std::min(maximumLimit, std::max(1, limit));
    QVariantMap result{
        {QStringLiteral("tool"), toolId},
        {QStringLiteral("query"), normalizedQuery},
        {QStringLiteral("candidates"), QVariantList()},
    };
    const auto finish = [&](const QVariantList &warnings) {
        result.insert(QStringLiteral("searchedAt"), JsCompat::isoTimestamp(QDateTime::currentDateTimeUtc()));
        result.insert(QStringLiteral("durationMs"), elapsed(timer));
        result.insert(QStringLiteral("warnings"), warnings);
        return result;
    };

    if (libraryId.isEmpty()) {
        return finish({QStringLiteral("A Library ID is required for Library text search.")});
    }

    if (fts.isEmpty()) {
        return finish({QStringLiteral("The query did not contain any searchable terms.")});
    }

    const int candidateLimit = std::min(maximumLimit, std::max(minimumCandidatePool, clampedLimit * candidateMultiplier));
    const auto runQuery = [&]() {
        return database
        .prepare(QStringLiteral(
            "SELECT library_chunk_search.chunk_id AS chunkId, library_chunk_search.library_id AS libraryId, "
            "library_chunk_search.library_file_id AS fileId, library_files.relative_path AS relativePath, "
            "library_files.name AS fileName, library_files.extension AS extension, "
            "library_chunks.start_line AS startLine, library_chunks.end_line AS endLine, "
            "library_chunks.content AS content, library_chunks.estimated_tokens AS estimatedTokens, "
            "library_chunks.content_hash AS contentHash, "
            "bm25(library_chunk_search, 0.0, 0.0, 0.0, 0.4, 0.6, 1.0) AS rank, "
            "snippet(library_chunk_search, 5, '[', ']', ' … ', 32) AS excerpt "
            "FROM library_chunk_search "
            "INNER JOIN library_chunks ON library_chunks.id = library_chunk_search.chunk_id "
            "INNER JOIN library_files ON library_files.id = library_chunk_search.library_file_id "
            "INNER JOIN library_documents ON library_documents.library_file_id = library_files.id "
            "WHERE library_chunk_search MATCH ? AND library_chunk_search.library_id = ? "
            "AND library_documents.status = 'indexed' AND library_files.status = 'available' "
            "ORDER BY rank ASC, library_files.relative_path COLLATE NOCASE ASC, library_chunks.ordinal ASC LIMIT ?"
        ))
        .bind(1, fts)
        .bind(2, libraryId)
        .bind(3, candidateLimit)
        .all();
    };

    // Search is an aid, never a reason for a chat to fail. A damaged index
    // is rebuilt once from the chunks it was made from.
    QList<QVariantMap> rows;
    QVariantList repairWarnings;
    try {
        rows = runQuery();
    } catch (const CoreError &error) {
        const QString message = error.message();
        const bool damaged = message.contains(QStringLiteral("malformed"), Qt::CaseInsensitive)
            || message.contains(QStringLiteral("corrupt"), Qt::CaseInsensitive);
        if (!damaged || database.inTransaction()) {
            return finish({QStringLiteral("Library text search failed, so this answer has no search results: ") + message});
        }
        try {
            repairSearchIndex(database);
            rows = runQuery();
            repairWarnings.append(QStringLiteral("The Library search index was damaged and has been rebuilt."));
        } catch (const CoreError &again) {
            return finish({QStringLiteral("The Library search index is damaged and couldn't be rebuilt: ") + again.message()});
        }
    }

    QList<Row> parsed;

    for (const QVariantMap &values : rows) {
        Row row;
        row.values = values;
        row.chunkId = values.value(QStringLiteral("chunkId")).toString();
        row.relativePath = values.value(QStringLiteral("relativePath")).toString();
        row.fileName = values.value(QStringLiteral("fileName")).toString();
        row.content = values.value(QStringLiteral("content")).toString();
        row.rank = values.value(QStringLiteral("rank")).toDouble();
        row.pathTokens = tokenizePath(row.relativePath + QLatin1Char('\n') + row.fileName);
        parsed.append(row);
    }

    QVariantList candidates;

    for (const Row &row : subjectBalancedRows(normalizedQuery, parsed, clampedLimit)) {
        candidates.append(mapCandidate(row));
    }

    result.insert(QStringLiteral("candidates"), candidates);
    if (rows.isEmpty()) {
        repairWarnings.append(QStringLiteral("No indexed Library text matched the search query."));
    }
    return finish(repairWarnings);
}

void repairSearchIndex(CoreDatabase &database)
{
    database.transaction([&database]() {
        database.exec(QStringLiteral("DROP TABLE IF EXISTS library_chunk_search"));
        database.exec(QStringLiteral(
            "CREATE VIRTUAL TABLE library_chunk_search USING fts5("
            "chunk_id UNINDEXED, library_id UNINDEXED, library_file_id UNINDEXED, "
            "relative_path, file_name, content, tokenize = 'unicode61')"
        ));
        database.exec(QStringLiteral(
            "INSERT INTO library_chunk_search (chunk_id, library_id, library_file_id, relative_path, file_name, content) "
            "SELECT library_chunks.id, library_chunks.library_id, library_chunks.library_file_id, "
            "library_files.relative_path, library_files.name, library_chunks.content "
            "FROM library_chunks INNER JOIN library_files ON library_files.id = library_chunks.library_file_id"
        ));
    });
}
}

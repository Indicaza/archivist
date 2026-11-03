#pragma once

#include "Core/Database/core_database.h"
#include "embedding_config.h"
#include "vector_store.h"

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

// Hybrid Library search: FTS5 keyword retrieval and semantic vector
// similarity, fused with weighted reciprocal rank fusion, with exact matches
// for filenames, identifiers and code symbols kept on top.
//
// Deterministic: the same index, query and vector give the same order every
// time (ties break on chunk id).
namespace Retrieval
{
// The retrieval policy. Every field has a default; Context Assembly v3 reads
// the overrides from the Agent's compiler config (see fromConfig and
// RETRIEVAL.md). No UI edits these yet.
struct HybridPolicy
{
    bool semanticEnabled = true;  // config "semanticRetrieval": "auto" (default) | "off"
    double lexicalWeight = 1.0;   // config "lexicalWeight": 0-100, percent (default 100)
    double semanticWeight = 1.0;  // config "semanticWeight": 0-100, percent (default 100)
    double minSimilarity = 0.2;   // config "semanticMinSimilarity": 0-100, percent (default 20)
    int rrfK = 60;                // rank damping; the usual RRF constant
    int lexicalLimit = 24;        // keyword candidates fused
    int semanticLimit = 24;       // vector candidates fused
    // Added to the fused score of an exact filename match (half for an exact
    // identifier in the text). Far above any RRF score (at most
    // (lexicalWeight + semanticWeight) / (rrfK + 1)), so exact matches lead.
    double exactMatchBoost = 1.0;
    int maxScannedVectors = 200000; // bound on one exact vector scan

    [[nodiscard]] static HybridPolicy fromConfig(const QVariantMap &config);
    [[nodiscard]] QVariantMap toVariant() const;
};

// Terms in a query that should match exactly: file names (`notes.md`),
// identifiers with inner capitals or underscores (`parseConfig`,
// `MAX_ITEMS`), qualified names (`Context::assemble`) and backticked text.
[[nodiscard]] QStringList exactTerms(const QString &query);

enum class ExactMatch
{
    None,
    Identifier, // an exact term appears in the text or path, case-sensitively
    FileName,   // an exact term is the file's name or the end of its path
};
// "file-name", "identifier" or empty.
[[nodiscard]] QString exactMatchName(ExactMatch exact);
[[nodiscard]] ExactMatch exactMatch(const QStringList &terms, const QString &relativePath, const QString &fileName, const QString &content);

// One input to fusion.
struct FusionInput
{
    QString id;
    int lexicalRank = 0;   // 1-based; 0 = not found by keyword search
    int semanticRank = 0;  // 1-based; 0 = not found by vector search
    ExactMatch exact = ExactMatch::None;
};

struct FusedRank
{
    QString id;
    double score = 0;
    int lexicalRank = 0;
    int semanticRank = 0;
    ExactMatch exact = ExactMatch::None;
};

// score = lexicalWeight / (k + lexicalRank) + semanticWeight / (k + semanticRank)
//       + exact boost. Best first; ties: keyword rank, then id.
[[nodiscard]] QList<FusedRank> fuse(const QList<FusionInput> &inputs, const HybridPolicy &policy);

struct HybridHit
{
    QString chunkId;
    QString libraryId;
    QString fileId;
    QString relativePath;
    QString fileName;
    QString content;
    QString contentHash;
    int startLine = 0;
    int endLine = 0;
    double score = 0;        // fused
    int lexicalRank = 0;
    int semanticRank = 0;
    double similarity = 0;   // cosine, when found by vector search
    ExactMatch exact = ExactMatch::None;
    QString reason;          // which signals found it, for the Context Inspector
};

struct HybridResult
{
    QList<HybridHit> hits;
    QStringList warnings;
    // mode ("hybrid" | "lexical"), lexicalCandidates, semanticCandidates,
    // vectorsScanned, bytesScanned, truncated, lexicalMs, vectorMs, totalMs,
    // store (vector backend name)
    QVariantMap diagnostics;
};

// Searches one Library. Without a query vector (semantic off, not indexed,
// offline) this is keyword search alone, with the same result shape.
[[nodiscard]] HybridResult searchLibrary(
    CoreDatabase &database,
    const QString &libraryId,
    const QString &query,
    const QVector<float> &queryVector,
    const HybridPolicy &policy,
    int limit,
    const EmbeddingConfig &config = currentEmbeddingConfig(),
    const VectorStore &store = defaultVectorStore()
);

// True when the Library opted in and has at least one searchable vector.
[[nodiscard]] bool semanticAvailable(CoreDatabase &database, const QString &libraryId, const EmbeddingConfig &config = currentEmbeddingConfig());
}

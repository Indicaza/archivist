#pragma once

#include "Core/Database/core_database.h"
#include "Core/Domains/Retrieval/embedding_config.h"
#include "Core/Domains/Retrieval/hybrid_retrieval.h"
#include "Core/Domains/Retrieval/vector_store.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

// Searching the Wiki: FTS5 over page and claim text, semantic similarity
// over their embeddings (the shared vectors of migration 22), fused with the
// same reciprocal rank fusion as Library search. One Library at a time.
//
// What is searchable: active pages and their claims that still stand
// (not retracted, not stale). Each hit carries confidence and freshness so
// callers never present a Wiki interpretation as an original source.
namespace KnowledgeRetrieval
{
struct Hit
{
    QString entryType; // "claim" or "page"
    QString entryId;
    QString libraryId;
    QString pageId;
    QString pageTitle;
    QString pageSlug;
    QString text;      // the statement, or the page body
    QString claimType;
    QString verificationStatus;
    double confidence = 0;
    QString reviewStatus;   // of the page
    QString updatedAt;
    QString reconciledAt;
    int supportingPassages = 0;
    QStringList sourceLabels; // "path:12-30" of the original passages behind a claim
    double score = 0;
    int lexicalRank = 0;
    int semanticRank = 0;
    double similarity = 0;
    QString reason;
};

struct Result
{
    QList<Hit> hits;
    // {mode: "hybrid"|"lexical", lexicalCandidates, semanticCandidates, vectorsScanned, totalMs}
    QVariantMap diagnostics;
};

[[nodiscard]] Result search(
    CoreDatabase &database,
    const QString &libraryId,
    const QString &query,
    const QVector<float> &queryVector,
    int limit,
    const Retrieval::HybridPolicy &policy = {},
    const Retrieval::EmbeddingConfig &config = Retrieval::currentEmbeddingConfig(),
    const Retrieval::VectorStore &store = Retrieval::defaultVectorStore()
);

// True when the Library has at least one active Wiki page.
[[nodiscard]] bool hasKnowledge(CoreDatabase &database, const QString &libraryId);

// The text a hit is sent to a model as: labelled as a Wiki interpretation
// with its status, confidence and the original sources it rests on.
[[nodiscard]] QString contextText(const Hit &hit);
}

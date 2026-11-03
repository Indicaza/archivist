#pragma once

#include "knowledge_model.h"
#include "knowledge_proposals.h"
#include "knowledge_sources.h"

#include "Core/Database/core_database.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

// Turning Library evidence into a reviewable Wiki proposal:
//
//   request ──► selectEvidence() ──► buildPrompt() ──► model ──► parseResponse() ──► proposal
//
// Each step is a plain function so the job runner can stop, resume or retry
// between them, and tests can drive them with canned model output. The model
// only ever sees original Library passages as evidence; existing Wiki text is
// shown as the page being revised, never as a source to cite.
namespace KnowledgeGeneration
{
// Bump when the prompt or the reply schema changes: it is part of every
// source fingerprint, so a new prompt regenerates instead of being skipped.
inline constexpr int promptVersion = 1;

struct Request
{
    QString libraryId;
    QString jobType;       // generate-page or expand-page
    QString topic;         // what the page is about; the search query
    QStringList fileIds;   // explicit Library files to summarize (optional)
    QString targetPageId;  // revise this page (expand-page, or a refresh)
    int maxPassages = 12;
    QString model;         // OpenAI model id
    // Provenance for citations, when a Chat or Agent asked for the page.
    QString chatId;
    QString agentId;
    QString runId;

    [[nodiscard]] static Request fromVariant(const QString &libraryId, const QString &jobType, const QVariantMap &request);
    [[nodiscard]] QVariantMap toVariant() const;
};

struct Evidence
{
    QList<KnowledgeSources::Passage> passages; // labelled P1..Pn in this order
    QString targetPageId;                      // empty: a new page
    int targetRevision = 0;
    QVariantMap targetPage;                    // pageRow() of the target
    QVariantList existingClaims;               // the target's claims, labelled E1..En
    QString fingerprint;                       // request + passages + target revision + prompt version
};

// Picks the passages: the given files' chunks, or a search for the topic
// (hybrid when `queryVector` is set, keyword otherwise), plus the passages
// the target page already cites. An existing active page whose slug matches
// the topic becomes the target, so asking twice revises instead of
// duplicating. Throws CoreError {code: "WIKI_NO_EVIDENCE"} when nothing in
// the Library matches.
[[nodiscard]] Evidence selectEvidence(CoreDatabase &database, const Request &request, const QVector<float> &queryVector = {});

[[nodiscard]] Knowledge::ModelRequest buildPrompt(const Request &request, const Evidence &evidence);
// The strict JSON schema of the reply.
[[nodiscard]] QJsonObject responseSchema();

// Validates the model's JSON against the schema and the evidence, and
// builds the proposal. Invalid citations, uncited facts and relationships
// without evidence are dropped and listed in `validation`. Throws CoreError
// {code: "WIKI_INVALID_RESPONSE"} when nothing usable is left.
[[nodiscard]] KnowledgeProposals::ProposalDraft parseResponse(
    CoreDatabase &database,
    const Request &request,
    const Evidence &evidence,
    const QString &responseText,
    const QString &jobId,
    const QString &modelId
);
}

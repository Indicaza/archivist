#pragma once

#include "budget_ledger.h"
#include "context_contracts.h"
#include "token_estimator.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// Context Assembly v3: deterministic, budgeted selection of everything a model
// sees for one turn. Pure: no database, provider, UI or run state. The runtime
// gathers a ContextRequest from the sources, calls assemble(), and sends the
// CompiledContext's messages.
namespace Context
{
// The Context Compiler registry entry that selects this pipeline. Saved
// Agents keep whatever compiler they have; v3 is used only when chosen.
inline const QString assemblyCompilerId = QStringLiteral("context-assembly");
constexpr int assemblyCompilerVersion = 3;
inline const QString toolResultReserve = QStringLiteral("tool-results");

[[nodiscard]] bool isAssembly(const QString &compilerId, int version);

// Builds the policy from a parsed context-assembly:v3 config.
[[nodiscard]] Policy policyFromConfig(const QVariantMap &config);

struct ProviderMessage
{
    QString role;
    QString content;
};

struct Outcome
{
    ContextItem item;
    int tokens = 0;
    bool included = false;
    QString reason; // an Omission:: value when not included
};

struct CompiledContext
{
    // Conversation in order, then one user-role reference message (when any
    // reference material was selected), then the current request. The
    // instructions travel separately as the provider's system text.
    QList<ProviderMessage> messages;
    // Every item considered, in selection order.
    QList<Outcome> outcomes;
    BudgetLedger ledger{0, 0};
    QHash<Kind, int> caps;
    QStringList warnings;
    int envelopeTokens = 0;
    QString estimator;

    [[nodiscard]] int estimatedInputTokens() const { return ledger.used(); }
    [[nodiscard]] int includedCount(Kind kind) const;
    [[nodiscard]] bool referenceIncluded() const;

    // The manifest stored with the context run: the fields the Context
    // Inspector already reads (compiler, counts, estimatedInputTokens,
    // totalBudget, responseTokenReserve, includedSources, ...) plus an
    // `assembly` block with the ledger and every item's outcome.
    [[nodiscard]] QVariantMap manifest(const QVariantMap &compilerDescriptor, double compilationTimeMs) const;
    // Attachment and retrieval outcomes in the shape of ChatEvidence's
    // ContextSourceOutcome, for the Context Inspector's source list.
    [[nodiscard]] QVariantList sourceOutcomes() const;
};

[[nodiscard]] CompiledContext assemble(const ContextRequest &request, const TokenEstimator &estimator);

// Tool output arriving during the model loop. Charges the ledger's tool-result
// reserve; when the output does not fit, returns a valid JSON object that says
// so and carries as much of the output as fits, instead of sending it
// silently over budget.
struct ToolResultAdmission
{
    QString output;
    int tokens = 0;
    int originalTokens = 0;
    bool truncated = false;
};

[[nodiscard]] ToolResultAdmission admitToolResult(BudgetLedger &ledger, const QString &serialized, const TokenEstimator &estimator);
}

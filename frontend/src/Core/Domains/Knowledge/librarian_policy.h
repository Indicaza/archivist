#pragma once

#include "librarian_store.h"

#include "Core/Database/core_database.h"

#include <QDateTime>
#include <QString>
#include <QVariantMap>

namespace Knowledge
{
struct ModelRequest;
}

// The rules every librarian job follows before it does anything costly.
// Deterministic work (diffing sources, reconciling citations, surveying
// coverage) only needs the Library's librarians to be enabled and not
// paused. A model call additionally needs the person's consent to send
// Library text to OpenAI, room in the run's and the day's budgets, and no
// provider backoff in effect.
namespace LibrarianPolicy
{
struct Admission
{
    bool ok = true;
    QString code;    // LIBRARIAN_DISABLED, LIBRARIAN_PAUSED, LIBRARIAN_NO_CONSENT, LIBRARIAN_RUN_BUDGET, LIBRARIAN_DAILY_BUDGET, LIBRARIAN_BACKOFF
    QString message; // for the person

    [[nodiscard]] static Admission refuse(const QString &code, const QString &message) { return {false, code, message}; }
};

// May a queued librarian job of this Library start now? Interactive work
// (a chat stream) holds librarian jobs back; a person's own Wiki jobs are
// never held. `job` is a KnowledgeJobs::job() map.
[[nodiscard]] Admission mayStart(CoreDatabase &database, const QVariantMap &job, const QDateTime &now, bool interactiveBusy);

// May this job send a request of about `estimatedTokens` (input plus the
// expected output) to the model?
[[nodiscard]] Admission admitModelCall(CoreDatabase &database, const QString &libraryId, const QString &runId, int estimatedTokens, const QDateTime &now);

// Rough token count of a request: its text and the output it may produce.
[[nodiscard]] int estimateTokens(const Knowledge::ModelRequest &request);
// Estimated USD for a call, from published per-million-token prices of the
// OpenAI models Archivist offers (unknown models are priced like gpt-5).
[[nodiscard]] double estimateCostUsd(const QString &model, int inputTokens, int outputTokens);

// Adds one model call to the run and to the Library's day.
void recordUsage(CoreDatabase &database, const QString &libraryId, const QString &runId, const QString &model, int inputTokens, int outputTokens, const QDateTime &now);
}

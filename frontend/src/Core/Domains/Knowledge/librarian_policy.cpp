#include "librarian_policy.h"

#include "knowledge_model.h"
#include "librarian_types.h"

#include <QHash>

#include <algorithm>

namespace LibrarianPolicy
{
Admission mayStart(CoreDatabase &database, const QVariantMap &job, const QDateTime &now, bool interactiveBusy)
{
    const QString librarian = job.value(QStringLiteral("librarian")).toString();

    if (librarian.isEmpty()) {
        return {};
    }

    const auto settings = LibrarianStore::settings(database, job.value(QStringLiteral("libraryId")).toString());

    if (!settings.enabled) {
        return Admission::refuse(QStringLiteral("LIBRARIAN_DISABLED"), QStringLiteral("Librarians are turned off for this Library."));
    }

    if (settings.paused) {
        return Admission::refuse(QStringLiteral("LIBRARIAN_PAUSED"), QStringLiteral("Librarians are paused for this Library."));
    }

    if (interactiveBusy) {
        return Admission::refuse(QStringLiteral("LIBRARIAN_INTERACTIVE"), QStringLiteral("Waiting for the conversation to finish."));
    }

    if (!settings.backoffUntil.isEmpty()) {
        const QDateTime until = QDateTime::fromString(settings.backoffUntil, Qt::ISODateWithMs);

        if (until.isValid() && until > now) {
            return Admission::refuse(QStringLiteral("LIBRARIAN_BACKOFF"), QStringLiteral("OpenAI failed recently; waiting before trying again."));
        }
    }

    return {};
}

Admission admitModelCall(CoreDatabase &database, const QString &libraryId, const QString &runId, int estimatedTokens, const QDateTime &now)
{
    const auto settings = LibrarianStore::settings(database, libraryId);

    if (!settings.enabled) {
        return Admission::refuse(QStringLiteral("LIBRARIAN_DISABLED"), QStringLiteral("Librarians are turned off for this Library."));
    }

    if (settings.paused) {
        return Admission::refuse(QStringLiteral("LIBRARIAN_PAUSED"), QStringLiteral("Librarians are paused for this Library."));
    }

    if (!settings.consented()) {
        return Admission::refuse(QStringLiteral("LIBRARIAN_NO_CONSENT"),
                                 QStringLiteral("Librarians need your permission to send this Library's text to OpenAI. Nothing was sent."));
    }

    if (!settings.backoffUntil.isEmpty()) {
        const QDateTime until = QDateTime::fromString(settings.backoffUntil, Qt::ISODateWithMs);

        if (until.isValid() && until > now) {
            return Admission::refuse(QStringLiteral("LIBRARIAN_BACKOFF"), QStringLiteral("OpenAI failed recently; waiting before trying again."));
        }
    }

    if (!runId.isEmpty()) {
        if (const auto run = LibrarianStore::run(database, runId)) {
            const int calls = run->value(QStringLiteral("modelCalls")).toInt();
            const int tokens = run->value(QStringLiteral("inputTokens")).toInt() + run->value(QStringLiteral("outputTokens")).toInt();

            if (calls >= settings.maxCallsPerRun || tokens + estimatedTokens > settings.maxTokensPerRun) {
                return Admission::refuse(QStringLiteral("LIBRARIAN_RUN_BUDGET"),
                                         QStringLiteral("This run reached its budget (%1 calls, %2 tokens).").arg(settings.maxCallsPerRun).arg(settings.maxTokensPerRun));
            }
        }
    }

    const auto today = LibrarianStore::dailyUsage(database, libraryId, LibrarianStore::usageDay(now));

    if (today.calls >= settings.dailyCallBudget || today.tokens() + estimatedTokens > settings.dailyTokenBudget) {
        return Admission::refuse(QStringLiteral("LIBRARIAN_DAILY_BUDGET"),
                                 QStringLiteral("Today's librarian budget is used up (%1 calls, %2 tokens). Work continues tomorrow (UTC).")
                                     .arg(settings.dailyCallBudget)
                                     .arg(settings.dailyTokenBudget));
    }

    return {};
}

int estimateTokens(const Knowledge::ModelRequest &request)
{
    // About four characters per token for English text and code, plus a
    // share of the output allowance (replies are rarely near the cap).
    const qsizetype characters = request.instructions.size() + request.input.size();
    return static_cast<int>(characters / 4) + std::min(request.maxOutputTokens, 3000);
}

double estimateCostUsd(const QString &model, int inputTokens, int outputTokens)
{
    struct Price
    {
        double input;
        double output;
    };

    // USD per million tokens (OpenAI list prices; an estimate, not a bill).
    static const QHash<QString, Price> prices{
        {QStringLiteral("gpt-5"), {1.25, 10.0}},
        {QStringLiteral("gpt-5-mini"), {0.25, 2.0}},
        {QStringLiteral("gpt-5-nano"), {0.05, 0.40}},
        {QStringLiteral("gpt-4.1"), {2.0, 8.0}},
        {QStringLiteral("gpt-4.1-mini"), {0.40, 1.60}},
        {QStringLiteral("gpt-4.1-nano"), {0.10, 0.40}},
        {QStringLiteral("gpt-4o"), {2.50, 10.0}},
        {QStringLiteral("gpt-4o-mini"), {0.15, 0.60}},
    };
    QString key = model.trimmed().toLower();
    Price price = prices.value(QStringLiteral("gpt-5"));

    // "gpt-5-mini-2025-08-07" prices like "gpt-5-mini": longest known prefix.
    int best = 0;

    for (auto it = prices.cbegin(); it != prices.cend(); ++it) {
        if (key.startsWith(it.key()) && it.key().size() > best) {
            best = static_cast<int>(it.key().size());
            price = it.value();
        }
    }

    return (std::max(0, inputTokens) * price.input + std::max(0, outputTokens) * price.output) / 1000000.0;
}

void recordUsage(CoreDatabase &database, const QString &libraryId, const QString &runId, const QString &model, int inputTokens, int outputTokens, const QDateTime &now)
{
    LibrarianStore::Usage usage;
    usage.calls = 1;
    usage.inputTokens = std::max(0, inputTokens);
    usage.outputTokens = std::max(0, outputTokens);
    usage.costUsd = estimateCostUsd(model, inputTokens, outputTokens);
    LibrarianStore::addUsage(database, libraryId, LibrarianStore::usageDay(now), usage);

    if (!runId.isEmpty()) {
        LibrarianStore::addRunUsage(database, runId, 1, usage.inputTokens, usage.outputTokens, usage.costUsd);
    }
}
}

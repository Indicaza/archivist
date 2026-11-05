// The job runner's librarian jobs: maintenance passes, research discovery
// and verification. They run on the same queue, thread and model interface
// as a person's Wiki jobs (see knowledge_jobs.cpp and LIBRARIAN.md).

#include "knowledge_jobs.h"

#include "knowledge_types.h"
#include "librarian_analysis.h"
#include "librarian_policy.h"
#include "librarian_store.h"
#include "librarian_types.h"

#include "Core/Domains/Observatory/live_events.h"
#include "Core/Support/js_compat.h"
#include "Core/core_error.h"

#include <algorithm>

namespace
{
QString messageOf(const std::exception &error)
{
    const auto *core = dynamic_cast<const CoreError *>(&error);
    return core ? core->message() : QString::fromUtf8(error.what());
}

QString codeOf(const std::exception &error)
{
    const auto *core = dynamic_cast<const CoreError *>(&error);
    return core ? core->details().value(QStringLiteral("code")).toString() : QString();
}
}

namespace Knowledge
{
using namespace Librarian;

void KnowledgeJobRunner::setInteractiveBusy(bool busy)
{
    if (m_interactiveBusy == busy) {
        return;
    }

    m_interactiveBusy = busy;

    if (!busy) {
        poke();
    }
}

void KnowledgeJobRunner::suspendLibrarians(const QString &libraryId)
{
    if (m_activeJobId.isEmpty() || m_activeLibraryId != libraryId || m_activeLibrarian.isEmpty()) {
        return;
    }

    const QString jobId = m_activeJobId;

    try {
        // Back to the queue as it was: the attempt did not count.
        m_database.transaction([&]() {
            const auto job = KnowledgeJobs::job(m_database, jobId);

            if (job && job->value(QStringLiteral("status")).toString() == JobStatus::running) {
                KnowledgeJobs::update(m_database, jobId, {
                    {QStringLiteral("status"), JobStatus::queued},
                    {QStringLiteral("stage"), QStringLiteral("paused")},
                    {QStringLiteral("attemptCount"), std::max(0, job->value(QStringLiteral("attemptCount")).toInt() - 1)},
                });
            }
        });
    } catch (const std::exception &) {
    }

    // The callbacks see the job is no longer running and drop the result.
    if (m_embedder && m_embedder->busy()) {
        m_embedder->cancel();
    }

    if (m_model && m_model->busy()) {
        m_model->cancel();
    }
}

LibrarianAnalysis::Context KnowledgeJobRunner::librarianContext(const QVariantMap &job)
{
    LibrarianAnalysis::Context context;
    context.libraryId = job.value(QStringLiteral("libraryId")).toString();
    context.runId = job.value(QStringLiteral("runId")).toString();
    context.jobId = job.value(QStringLiteral("id")).toString();
    context.settings = LibrarianStore::settings(m_database, context.libraryId);
    // Planning with model work in mind needs consent; the budget is checked
    // again at every actual call.
    const auto admission = LibrarianPolicy::admitModelCall(m_database, context.libraryId, context.runId, 0, now());
    context.mayUseModel = admission.ok;
    return context;
}

bool KnowledgeJobRunner::markRunning(const QVariantMap &job, const QString &stage)
{
    try {
        m_database.transaction([&]() {
            KnowledgeJobs::update(m_database, job.value(QStringLiteral("id")).toString(), {
                {QStringLiteral("status"), JobStatus::running},
                {QStringLiteral("stage"), stage},
                {QStringLiteral("progress"), 10},
                {QStringLiteral("startedAt"), JsCompat::isoTimestamp(now())},
                {QStringLiteral("attemptCount"), job.value(QStringLiteral("attemptCount")).toInt() + 1},
                {QStringLiteral("maxAttempts"), m_policy.maxAttempts},
                {QStringLiteral("nextAttemptAt"), QVariant()},
            });
        });
    } catch (const std::exception &) {
        return false;
    }

    emit jobChanged(m_activeLibraryId, job.value(QStringLiteral("id")).toString());
    return true;
}

void KnowledgeJobRunner::completeJob(const QString &jobId, const QVariantMap &result, const QString &stage)
{
    KnowledgeJobs::update(m_database, jobId, {
        {QStringLiteral("status"), JobStatus::completed},
        {QStringLiteral("stage"), stage},
        {QStringLiteral("progress"), 100},
        {QStringLiteral("result"), result},
        {QStringLiteral("errorCode"), QVariant()},
        {QStringLiteral("errorMessage"), QVariant()},
        {QStringLiteral("completedAt"), JsCompat::isoTimestamp(now())},
    });
}

void KnowledgeJobRunner::accountModelCall(const ModelResult &result, const QString &model)
{
    if (m_activeLibrarian.isEmpty() || result.error == ModelResult::Error::Cancelled || result.error == ModelResult::Error::NotConfigured) {
        return;
    }

    try {
        m_database.transaction([&]() {
            LibrarianPolicy::recordUsage(m_database, m_activeLibraryId, m_activeRunId, result.model.isEmpty() ? model : result.model, result.inputTokens,
                                         result.outputTokens, now());

            if (result.ok()) {
                LibrarianStore::recordProviderSuccess(m_database, m_activeLibraryId);
            } else if (result.retryable() || result.error == ModelResult::Error::Unauthorized) {
                // Back off the whole Library's librarians, not just this job.
                LibrarianStore::recordProviderFailure(m_database, m_activeLibraryId, result.message, now());
            }
        });
    } catch (const std::exception &) {
    }
}

void KnowledgeJobRunner::settleRunOf(const QString &libraryId, const QString &runId)
{
    if (runId.isEmpty()) {
        return;
    }

    try {
        const bool settled = m_database.transaction([&]() { return LibrarianStore::settleRun(m_database, runId); });

        if (!settled) {
            return;
        }

        if (const auto run = LibrarianStore::run(m_database, runId)) {
            // The run's numbers for the Observatory timeline.
            LiveEvents::marker(QStringLiteral("librarian"), QStringLiteral("run"), {
                {QStringLiteral("librarian"), run->value(QStringLiteral("librarian"))},
                {QStringLiteral("trigger"), run->value(QStringLiteral("trigger"))},
                {QStringLiteral("status"), run->value(QStringLiteral("status"))},
                {QStringLiteral("calls"), run->value(QStringLiteral("modelCalls"))},
                {QStringLiteral("inputTokens"), run->value(QStringLiteral("inputTokens"))},
                {QStringLiteral("outputTokens"), run->value(QStringLiteral("outputTokens"))},
                {QStringLiteral("jobs"), run->value(QStringLiteral("jobsCreated"))},
                {QStringLiteral("findings"), run->value(QStringLiteral("findingsCreated"))},
                {QStringLiteral("proposals"), run->value(QStringLiteral("proposalsCreated"))},
            }, run->value(QStringLiteral("status")).toString() == RunStatus::failed ? 0 : 1);
        }

        emit runFinished(libraryId, runId);
    } catch (const std::exception &) {
    }
}

// ---- Maintenance -----------------------------------------------------------

void KnowledgeJobRunner::runMaintenance(const QVariantMap &job)
{
    const QString jobId = job.value(QStringLiteral("id")).toString();

    if (!markRunning(job, QStringLiteral("reconciling"))) {
        finishActive();
        return;
    }

    LiveEvents::Step step(QStringLiteral("librarian"), QStringLiteral("maintenance"));

    try {
        const QVariantMap summary = m_database.transaction([&]() {
            const LibrarianAnalysis::Context context = librarianContext(job);
            const LibrarianAnalysis::Maintenance outcome = LibrarianAnalysis::maintain(m_database, context);
            int refreshes = 0;

            // Refreshing a page from changed sources is model work: only
            // with consent, at most a run's worth, and each call is checked
            // against the budgets when it is about to be sent.
            if (context.mayUseModel) {
                for (const QString &pageId : outcome.refreshPageIds) {
                    if (refreshes >= context.settings.maxCallsPerRun) {
                        break;
                    }

                    KnowledgeJobs::JobOptions options;
                    options.librarian = Kind::maintenance;
                    options.runId = context.runId;
                    options.priority = Priority::maintenance;
                    const QVariantMap queued = KnowledgeJobs::enqueue(m_database, context.libraryId, JobType::generatePage, {
                        {QStringLiteral("targetPageId"), pageId},
                        {QStringLiteral("model"), context.settings.effectiveModel()},
                    }, options);
                    refreshes += queued.value(QStringLiteral("created")).toBool() ? 1 : 0;
                }
            }

            QVariantMap result = outcome.toVariant();
            result.insert(QStringLiteral("refreshJobs"), refreshes);
            result.insert(QStringLiteral("consentNeeded"), !context.mayUseModel && !outcome.refreshPageIds.isEmpty());
            completeJob(jobId, result);
            LibrarianStore::noteRun(m_database, context.runId, {{QStringLiteral("maintenance"), result}});
            LibrarianStore::markRan(m_database, context.libraryId, Kind::maintenance, now());
            return result;
        });
        step.done(1, {
            {QStringLiteral("checked"), summary.value(QStringLiteral("reconcile")).toMap().value(QStringLiteral("checked"))},
            {QStringLiteral("findings"), summary.value(QStringLiteral("findings"))},
            {QStringLiteral("refreshJobs"), summary.value(QStringLiteral("refreshJobs"))},
        });
        emit knowledgeChanged(m_activeLibraryId);
    } catch (const std::exception &error) {
        step.done(0);
        failOrRetry(jobId, QStringLiteral("maintenance-failed"), messageOf(error), false);
        return;
    }

    finishActive();
}

// ---- Research --------------------------------------------------------------

void KnowledgeJobRunner::runDiscovery(const QVariantMap &job)
{
    const QString jobId = job.value(QStringLiteral("id")).toString();

    if (!markRunning(job, QStringLiteral("surveying"))) {
        finishActive();
        return;
    }

    LiveEvents::Step step(QStringLiteral("librarian"), QStringLiteral("discovery"));

    try {
        const QVariantMap summary = m_database.transaction([&]() {
            const LibrarianAnalysis::Context context = librarianContext(job);
            const auto run = LibrarianStore::run(m_database, context.runId);
            const auto today = LibrarianStore::dailyUsage(m_database, context.libraryId, LibrarianStore::usageDay(now()));
            // Never plan more investigations than the run and the day can pay for.
            const int room = std::max(0, std::min(context.settings.maxCallsPerRun - (run ? run->value(QStringLiteral("modelCalls")).toInt() : 0),
                                                  context.settings.dailyCallBudget - today.calls));
            const QString manualTopic = job.value(QStringLiteral("request")).toMap().value(QStringLiteral("topic")).toString();
            const LibrarianAnalysis::Discovery discovery = LibrarianAnalysis::discover(m_database, context, manualTopic, room);
            int queued = 0;

            for (const LibrarianAnalysis::Subject &subject : discovery.selected) {
                KnowledgeJobs::JobOptions options;
                options.librarian = Kind::research;
                options.runId = context.runId;
                options.priority = Priority::research;
                QVariantMap request{
                    {QStringLiteral("topic"), subject.topic},
                    {QStringLiteral("model"), context.settings.effectiveModel()},
                    {QStringLiteral("maxPassages"), 10},
                    {QStringLiteral("topicId"), subject.topicId},
                };

                if (!subject.fileIds.isEmpty()) {
                    request.insert(QStringLiteral("fileIds"), subject.fileIds);
                }

                if (!subject.targetPageId.isEmpty()) {
                    request.insert(QStringLiteral("targetPageId"), subject.targetPageId);
                }

                const QVariantMap created = KnowledgeJobs::enqueue(m_database, context.libraryId, JobType::investigateGap, request, options);
                LibrarianStore::updateTopic(m_database, subject.topicId, {
                    {QStringLiteral("status"), TopicStatus::queued},
                    {QStringLiteral("lastJobId"), created.value(QStringLiteral("id"))},
                });
                queued += created.value(QStringLiteral("created")).toBool() ? 1 : 0;
            }

            QVariantMap result = discovery.toVariant();
            result.insert(QStringLiteral("queued"), queued);
            result.insert(QStringLiteral("consentNeeded"), !context.mayUseModel);
            completeJob(jobId, result);
            LibrarianStore::noteRun(m_database, context.runId, {{QStringLiteral("discovery"), result}});
            LibrarianStore::markRan(m_database, context.libraryId, Kind::research, now());
            return result;
        });
        step.done(1, {
            {QStringLiteral("surveyed"), summary.value(QStringLiteral("surveyed"))},
            {QStringLiteral("queued"), summary.value(QStringLiteral("queued"))},
            {QStringLiteral("unchanged"), summary.value(QStringLiteral("unchanged"))},
        });
        emit knowledgeChanged(m_activeLibraryId);
    } catch (const std::exception &error) {
        step.done(0);
        failOrRetry(jobId, QStringLiteral("discovery-failed"), messageOf(error), false);
        return;
    }

    finishActive();
}

// ---- Verification ----------------------------------------------------------

void KnowledgeJobRunner::runVerification(const QVariantMap &job)
{
    const QString jobId = job.value(QStringLiteral("id")).toString();

    if (!markRunning(job, QStringLiteral("checking"))) {
        finishActive();
        return;
    }

    LiveEvents::Step step(QStringLiteral("librarian"), QStringLiteral("verification"));
    ModelRequest prompt;

    try {
        const bool needsModel = m_database.transaction([&]() {
            m_context = librarianContext(job);
            m_plan = LibrarianAnalysis::planVerification(m_database, m_context, m_policy.maxVerificationClaims);
            m_verificationModel = m_context.settings.effectiveModel();

            if (!m_plan.needsModel()) {
                QVariantMap result{{QStringLiteral("plan"), m_plan.toVariant()}, {QStringLiteral("consentNeeded"), !m_context.mayUseModel}};
                completeJob(jobId, result);
                LibrarianStore::noteRun(m_database, m_context.runId, {{QStringLiteral("verification"), result}});
                LibrarianStore::markRan(m_database, m_context.libraryId, Kind::verification, now());
                return false;
            }

            prompt = LibrarianAnalysis::verificationPrompt(m_plan, m_verificationModel);
            const auto admission = LibrarianPolicy::admitModelCall(m_database, m_context.libraryId, m_context.runId, LibrarianPolicy::estimateTokens(prompt), now());

            if (!admission.ok) {
                // The local checks stand; the model check waits for budget.
                QVariantMap result{{QStringLiteral("plan"), m_plan.toVariant()}, {QStringLiteral("modelSkipped"), admission.code}, {QStringLiteral("reason"), admission.message}};
                completeJob(jobId, result, QStringLiteral("budget"));
                LibrarianStore::noteRun(m_database, m_context.runId, {{QStringLiteral("verification"), result}});
                LibrarianStore::markRan(m_database, m_context.libraryId, Kind::verification, now());
                return false;
            }

            KnowledgeJobs::update(m_database, jobId, {
                {QStringLiteral("stage"), QStringLiteral("verifying")},
                {QStringLiteral("progress"), 40},
                {QStringLiteral("model"), m_verificationModel},
            });
            return true;
        });

        step.done(1, {{QStringLiteral("claims"), m_plan.checks.size()}, {QStringLiteral("findings"), m_plan.findings}});

        if (!needsModel) {
            emit knowledgeChanged(m_activeLibraryId);
            finishActive();
            return;
        }
    } catch (const std::exception &error) {
        step.done(0);
        failOrRetry(jobId, QStringLiteral("verification-failed"), messageOf(error), false);
        return;
    }

    emit jobChanged(m_activeLibraryId, jobId);

    if (!m_model) {
        m_model = m_models ? m_models() : nullptr;
    }

    if (!m_model) {
        failOrRetry(jobId, QStringLiteral("not-configured"), QStringLiteral("No model is available for Wiki verification."), false);
        return;
    }

    m_model->generate(prompt, [this, jobId](const ModelResult &result) {
        QMetaObject::invokeMethod(this, [this, jobId, result]() { onVerificationResult(jobId, result); }, Qt::QueuedConnection);
    });
}

void KnowledgeJobRunner::onVerificationResult(const QString &jobId, const ModelResult &result)
{
    if (jobId != m_activeJobId) {
        return;
    }

    if (!stillRunning(jobId)) {
        finishActive();
        return;
    }

    accountModelCall(result, m_verificationModel);

    if (!result.ok()) {
        failOrRetry(jobId, result.code(), result.message, result.retryable(), result.retryAfterMs);
        return;
    }

    LiveEvents::Step step(QStringLiteral("librarian"), QStringLiteral("verify-validate"));

    try {
        const QVariantMap summary = m_database.transaction([&]() {
            const LibrarianAnalysis::VerificationOutcome outcome = LibrarianAnalysis::applyVerification(
                m_database, m_context, m_plan, result.text, result.model.isEmpty() ? m_verificationModel : result.model);
            QVariantMap value{
                {QStringLiteral("plan"), m_plan.toVariant()},
                {QStringLiteral("outcome"), outcome.toVariant()},
                {QStringLiteral("inputTokens"), result.inputTokens},
                {QStringLiteral("outputTokens"), result.outputTokens},
            };
            completeJob(jobId, value);
            LibrarianStore::noteRun(m_database, m_context.runId, {{QStringLiteral("verification"), value}});
            LibrarianStore::markRan(m_database, m_context.libraryId, Kind::verification, now());
            return value;
        });
        step.done(1, summary.value(QStringLiteral("outcome")).toMap());
    } catch (const std::exception &error) {
        step.done(0);
        const QString code = codeOf(error);
        failOrRetry(jobId, code.isEmpty() ? QStringLiteral("persist-failed") : code, messageOf(error), code == QStringLiteral("WIKI_INVALID_RESPONSE"));
        return;
    }

    emit knowledgeChanged(m_activeLibraryId);
    finishActive();
}
}

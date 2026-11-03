#include "knowledge_jobs.h"

#include "knowledge_proposals.h"
#include "knowledge_repository.h"
#include "knowledge_sources.h"
#include "knowledge_text.h"
#include "knowledge_types.h"

#include "Core/Domains/Observatory/live_events.h"
#include "Core/Domains/Retrieval/embedding_config.h"
#include "Core/Domains/Retrieval/hybrid_retrieval.h"
#include "Core/Support/core_values.h"
#include "Core/Support/js_compat.h"
#include "Core/Support/json_values.h"
#include "Core/Support/transactions.h"
#include "Core/core_error.h"

#include <algorithm>

using namespace CoreValues;

namespace
{
const QString nowSql = QStringLiteral("strftime('%Y-%m-%dT%H:%M:%fZ', 'now')");

const QString jobColumns = QStringLiteral(
    "id, library_id AS libraryId, job_type AS jobType, status, stage, request, request_key AS requestKey, "
    "source_fingerprint AS sourceFingerprint, progress, attempt_count AS attemptCount, max_attempts AS maxAttempts, "
    "error_code AS errorCode, error_message AS errorMessage, result, requested_by AS requestedBy, model, "
    "next_attempt_at AS nextAttemptAt, created_at AS createdAt, started_at AS startedAt, updated_at AS updatedAt, "
    "completed_at AS completedAt"
);

QVariantMap jobMap(const QVariantMap &row)
{
    QVariantMap job;

    for (auto it = row.cbegin(); it != row.cend(); ++it) {
        job.insert(it.key(), nullable(it.value()));
    }

    job.insert(QStringLiteral("request"), JsonValues::parseObject(row.value(QStringLiteral("request")).toString()));
    const QString result = row.value(QStringLiteral("result")).toString();
    job.insert(QStringLiteral("result"), result.isEmpty() ? QVariant::fromValue(nullptr) : QVariant(JsonValues::parseObject(result)));
    return job;
}

QString column(const QString &key)
{
    static const QHash<QString, QString> columns{
        {QStringLiteral("status"), QStringLiteral("status")},
        {QStringLiteral("stage"), QStringLiteral("stage")},
        {QStringLiteral("sourceFingerprint"), QStringLiteral("source_fingerprint")},
        {QStringLiteral("progress"), QStringLiteral("progress")},
        {QStringLiteral("attemptCount"), QStringLiteral("attempt_count")},
        {QStringLiteral("maxAttempts"), QStringLiteral("max_attempts")},
        {QStringLiteral("errorCode"), QStringLiteral("error_code")},
        {QStringLiteral("errorMessage"), QStringLiteral("error_message")},
        {QStringLiteral("result"), QStringLiteral("result")},
        {QStringLiteral("model"), QStringLiteral("model")},
        {QStringLiteral("nextAttemptAt"), QStringLiteral("next_attempt_at")},
        {QStringLiteral("startedAt"), QStringLiteral("started_at")},
        {QStringLiteral("completedAt"), QStringLiteral("completed_at")},
    };
    const QString name = columns.value(key);

    if (name.isEmpty()) {
        throw CoreError(QStringLiteral("Unknown Wiki job field: %1").arg(key));
    }

    return name;
}

QString codeOf(const std::exception &error)
{
    const auto *core = dynamic_cast<const CoreError *>(&error);
    return core ? core->details().value(QStringLiteral("code")).toString() : QString();
}

QString messageOf(const std::exception &error)
{
    const auto *core = dynamic_cast<const CoreError *>(&error);
    return core ? core->message() : QString::fromUtf8(error.what());
}
}

namespace KnowledgeJobs
{
bool isRunnable(const QString &jobType)
{
    return jobType == Knowledge::JobType::generatePage || jobType == Knowledge::JobType::expandPage || jobType == Knowledge::JobType::reconcileSources;
}

QVariantMap enqueue(CoreDatabase &database, const QString &libraryId, const QString &jobType, const QVariantMap &request, const QString &requestedBy)
{
    if (!isRunnable(jobType)) {
        throw CoreError(QStringLiteral("The “%1” librarian is not available yet.").arg(jobType), {{QStringLiteral("code"), QStringLiteral("WIKI_JOB_NOT_AVAILABLE")}});
    }

    return Transactions::join(database, [&]() {
        const QString requestJson = JsonValues::toJsonString(request);
        const QString key = KnowledgeText::sha256(requestJson);
        const auto existing = database
                                  .prepare(QStringLiteral("SELECT id FROM knowledge_jobs WHERE library_id = ? AND job_type = ? AND request_key = ? AND status IN ('queued', 'running')"))
                                  .bind(1, libraryId)
                                  .bind(2, jobType)
                                  .bind(3, key)
                                  .scalar();

        if (existing.isValid() && !existing.isNull()) {
            return *job(database, existing.toString());
        }

        const QString id = newId();
        database
            .prepare(QStringLiteral(
                "INSERT INTO knowledge_jobs (id, library_id, job_type, status, stage, request, request_key, requested_by) "
                "VALUES (?, ?, ?, 'queued', 'queued', ?, ?, ?)"
            ))
            .bind(1, id)
            .bind(2, libraryId)
            .bind(3, jobType)
            .bind(4, requestJson)
            .bind(5, key)
            .bind(6, requestedBy)
            .run();
        return *job(database, id);
    });
}

std::optional<QVariantMap> job(CoreDatabase &database, const QString &jobId)
{
    const auto row = database.prepare(QStringLiteral("SELECT %1 FROM knowledge_jobs WHERE id = ?").arg(jobColumns)).bind(1, jobId).one();
    return row ? std::optional<QVariantMap>(jobMap(*row)) : std::nullopt;
}

QVariantList jobs(CoreDatabase &database, const QString &libraryId, int limit)
{
    QVariantList result;

    for (const QVariantMap &row : database.prepare(QStringLiteral("SELECT %1 FROM knowledge_jobs WHERE library_id = ? ORDER BY created_at DESC, id DESC LIMIT ?").arg(jobColumns))
                                      .bind(1, libraryId)
                                      .bind(2, std::max(1, limit))
                                      .all()) {
        result.append(jobMap(row));
    }

    return result;
}

void update(CoreDatabase &database, const QString &jobId, const QVariantMap &fields)
{
    if (fields.isEmpty()) {
        return;
    }

    QStringList assignments;
    QVariantList values;

    for (auto it = fields.cbegin(); it != fields.cend(); ++it) {
        assignments.append(column(it.key()) + QStringLiteral(" = ?"));
        const QVariant value = it.key() == QStringLiteral("result") && it.value().typeId() == QMetaType::QVariantMap
            ? QVariant(JsonValues::toJsonString(it.value()))
            : it.value();
        values.append(isNullish(value) && value.typeId() != QMetaType::Int ? QVariant() : value);
    }

    auto statement = database.prepare(QStringLiteral("UPDATE knowledge_jobs SET %1, updated_at = %2 WHERE id = ?").arg(assignments.join(QStringLiteral(", ")), nowSql));
    statement.bindAll(values);
    statement.bind(static_cast<int>(values.size()) + 1, jobId);
    statement.run();
}

int recoverInterrupted(CoreDatabase &database)
{
    database.prepare(QStringLiteral(
                         "UPDATE knowledge_jobs SET status = 'queued', stage = 'queued', next_attempt_at = NULL, "
                         "error_message = 'Interrupted; resumed after restart.', updated_at = %1 WHERE status = 'running'"
    )
                         .arg(nowSql))
        .run();
    return database.changes();
}

bool cancel(CoreDatabase &database, const QString &jobId)
{
    database.prepare(QStringLiteral(
                         "UPDATE knowledge_jobs SET status = 'cancelled', stage = 'cancelled', completed_at = %1, updated_at = %1 "
                         "WHERE id = ? AND status IN ('queued', 'running')"
    )
                         .arg(nowSql))
        .bind(1, jobId)
        .run();
    return database.changes() == 1;
}

bool retry(CoreDatabase &database, const QString &jobId)
{
    // A retried job must not collide with an identical job queued since.
    const auto row = database.prepare(QStringLiteral("SELECT library_id, job_type, request_key FROM knowledge_jobs WHERE id = ?")).bind(1, jobId).one();

    if (!row) {
        return false;
    }

    const bool duplicate = database
                               .prepare(QStringLiteral(
                                   "SELECT 1 FROM knowledge_jobs WHERE library_id = ? AND job_type = ? AND request_key = ? AND status IN ('queued', 'running') AND id != ?"
                               ))
                               .bind(1, row->value(QStringLiteral("library_id")))
                               .bind(2, row->value(QStringLiteral("job_type")))
                               .bind(3, row->value(QStringLiteral("request_key")))
                               .bind(4, jobId)
                               .scalar()
                               .isValid();

    if (duplicate) {
        return false;
    }

    database.prepare(QStringLiteral(
                         "UPDATE knowledge_jobs SET status = 'queued', stage = 'queued', attempt_count = 0, error_code = NULL, error_message = NULL, "
                         "next_attempt_at = NULL, completed_at = NULL, progress = 0, updated_at = %1 WHERE id = ? AND status IN ('failed', 'cancelled')"
    )
                         .arg(nowSql))
        .bind(1, jobId)
        .run();
    return database.changes() == 1;
}

QList<QVariantMap> runnable(CoreDatabase &database, const QDateTime &now)
{
    QList<QVariantMap> result;

    for (const QVariantMap &row : database
                                      .prepare(QStringLiteral(
                                                   "SELECT %1 FROM knowledge_jobs WHERE status = 'queued' AND (next_attempt_at IS NULL OR next_attempt_at <= ?) "
                                                   "ORDER BY created_at ASC, id ASC"
                                      )
                                                   .arg(jobColumns))
                                      .bind(1, JsCompat::isoTimestamp(now))
                                      .all()) {
        result.append(jobMap(row));
    }

    return result;
}

QDateTime nextAttempt(CoreDatabase &database)
{
    const QVariant value = database.prepare(QStringLiteral("SELECT MIN(next_attempt_at) FROM knowledge_jobs WHERE status = 'queued' AND next_attempt_at IS NOT NULL")).scalar();
    return value.isNull() ? QDateTime() : QDateTime::fromString(value.toString(), Qt::ISODateWithMs);
}
}

namespace Knowledge
{
KnowledgeJobRunner::KnowledgeJobRunner(CoreDatabase &database, ModelFactory models, RunnerPolicy policy, QObject *parent)
    : QObject(parent)
    , m_database(database)
    , m_models(std::move(models))
    , m_policy(policy)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &KnowledgeJobRunner::processNext);
    m_embedTimer.setSingleShot(true);
}

KnowledgeJobRunner::~KnowledgeJobRunner()
{
    if (m_model) {
        m_model->cancel();
    }

    if (m_embedder) {
        m_embedder->cancel();
    }
}

void KnowledgeJobRunner::setEmbedder(EmbedderFactory embedder)
{
    m_embedders = std::move(embedder);
}

void KnowledgeJobRunner::setClock(std::function<QDateTime()> clock)
{
    m_clock = std::move(clock);
}

QDateTime KnowledgeJobRunner::now() const
{
    return m_clock ? m_clock() : QDateTime::currentDateTimeUtc();
}

void KnowledgeJobRunner::start()
{
    if (!m_started) {
        m_started = true;
        m_database.transaction([&]() { KnowledgeJobs::recoverInterrupted(m_database); });
    }

    schedule();
}

void KnowledgeJobRunner::poke()
{
    if (m_started) {
        schedule();
    }
}

void KnowledgeJobRunner::schedule(int delayMs)
{
    if (!m_activeJobId.isEmpty()) {
        return;
    }

    if (!m_timer.isActive() || m_timer.remainingTime() > delayMs) {
        m_timer.start(std::max(0, delayMs));
    }
}

bool KnowledgeJobRunner::idle() const
{
    if (!m_activeJobId.isEmpty() || m_timer.isActive()) {
        // A timer waiting for a retry far in the future is idle for callers.
        return m_activeJobId.isEmpty() && m_timer.remainingTime() > 1000;
    }

    return true;
}

bool KnowledgeJobRunner::stillRunning(const QString &jobId)
{
    const auto job = KnowledgeJobs::job(m_database, jobId);
    return job && job->value(QStringLiteral("status")).toString() == JobStatus::running;
}

void KnowledgeJobRunner::cancelJob(const QString &jobId)
{
    QString libraryId;
    m_database.transaction([&]() {
        const auto job = KnowledgeJobs::job(m_database, jobId);
        libraryId = job ? job->value(QStringLiteral("libraryId")).toString() : QString();
        KnowledgeJobs::cancel(m_database, jobId);
    });

    if (jobId == m_activeJobId) {
        // The callbacks see the job is no longer running and drop the result.
        if (m_embedder && m_embedder->busy()) {
            m_embedder->cancel();
        }

        if (m_model && m_model->busy()) {
            m_model->cancel();
        }
    }

    if (!libraryId.isEmpty()) {
        emit jobChanged(libraryId, jobId);
    }
}

void KnowledgeJobRunner::finishActive()
{
    const QString libraryId = m_activeLibraryId;
    const QString jobId = m_activeJobId;
    m_activeJobId.clear();
    m_activeLibraryId.clear();
    m_embedTimer.stop();

    if (!jobId.isEmpty()) {
        emit jobChanged(libraryId, jobId);
    }

    schedule();
}

void KnowledgeJobRunner::processNext()
{
    if (!m_activeJobId.isEmpty()) {
        return;
    }

    QList<QVariantMap> due;

    try {
        due = KnowledgeJobs::runnable(m_database, now());
    } catch (const std::exception &) {
        schedule(1000);
        return;
    }

    if (due.isEmpty()) {
        const QDateTime next = KnowledgeJobs::nextAttempt(m_database);

        if (next.isValid()) {
            m_timer.start(static_cast<int>(std::clamp<qint64>(now().msecsTo(next), 0, m_policy.maxBackoffMs)));
        }

        return;
    }

    const QVariantMap job = due.first();
    m_activeJobId = job.value(QStringLiteral("id")).toString();
    m_activeLibraryId = job.value(QStringLiteral("libraryId")).toString();

    if (job.value(QStringLiteral("jobType")).toString() == JobType::reconcileSources) {
        runReconcile(job);
    } else {
        runGeneration(job);
    }
}

void KnowledgeJobRunner::runReconcile(const QVariantMap &job)
{
    const QString jobId = job.value(QStringLiteral("id")).toString();
    LiveEvents::Step step(QStringLiteral("knowledge"), QStringLiteral("reconcile"));

    try {
        const KnowledgeSources::ReconcileReport report = m_database.transaction([&]() {
            KnowledgeJobs::update(m_database, jobId, {
                {QStringLiteral("status"), JobStatus::running},
                {QStringLiteral("stage"), QStringLiteral("reconciling")},
                {QStringLiteral("startedAt"), JsCompat::isoTimestamp(now())},
                {QStringLiteral("attemptCount"), job.value(QStringLiteral("attemptCount")).toInt() + 1},
            });
            const auto result = KnowledgeSources::reconcile(m_database, m_activeLibraryId, {Actor::system, QStringLiteral("reconciler")});
            KnowledgeJobs::update(m_database, jobId, {
                {QStringLiteral("status"), JobStatus::completed},
                {QStringLiteral("stage"), QStringLiteral("done")},
                {QStringLiteral("progress"), 100},
                {QStringLiteral("result"), result.toVariant()},
                {QStringLiteral("errorCode"), QVariant()},
                {QStringLiteral("errorMessage"), QVariant()},
                {QStringLiteral("completedAt"), JsCompat::isoTimestamp(now())},
            });
            return result;
        });
        step.done(1, {{QStringLiteral("checked"), report.checked}, {QStringLiteral("stale"), report.staleClaimIds.size()}});
        emit knowledgeChanged(m_activeLibraryId);
    } catch (const std::exception &error) {
        step.done(0);
        failOrRetry(jobId, QStringLiteral("reconcile-failed"), messageOf(error), false);
        return;
    }

    finishActive();
}

void KnowledgeJobRunner::runGeneration(const QVariantMap &job)
{
    const QString jobId = job.value(QStringLiteral("id")).toString();
    m_request = KnowledgeGeneration::Request::fromVariant(m_activeLibraryId, job.value(QStringLiteral("jobType")).toString(), job.value(QStringLiteral("request")).toMap());
    m_evidence = {};

    try {
        m_database.transaction([&]() {
            KnowledgeJobs::update(m_database, jobId, {
                {QStringLiteral("status"), JobStatus::running},
                {QStringLiteral("stage"), QStringLiteral("retrieving")},
                {QStringLiteral("progress"), 10},
                {QStringLiteral("model"), m_request.model},
                {QStringLiteral("startedAt"), JsCompat::isoTimestamp(now())},
                {QStringLiteral("attemptCount"), job.value(QStringLiteral("attemptCount")).toInt() + 1},
                {QStringLiteral("maxAttempts"), m_policy.maxAttempts},
                {QStringLiteral("nextAttemptAt"), QVariant()},
            });
        });
    } catch (const std::exception &) {
        finishActive();
        return;
    }

    emit jobChanged(m_activeLibraryId, jobId);
    const QString query = m_request.topic;

    if (m_embedders && !query.isEmpty() && Retrieval::semanticAvailable(m_database, m_activeLibraryId)) {
        // Hybrid evidence when the Library opted in; any failure falls back
        // to keyword search so the job never depends on embeddings.
        m_embedder = m_embedders();

        if (m_embedder) {
            disconnect(&m_embedTimer, nullptr, this, nullptr);
            connect(&m_embedTimer, &QTimer::timeout, this, [this, jobId]() {
                if (m_embedder && m_embedder->busy()) {
                    m_embedder->cancel();
                }
            });
            m_embedTimer.start(m_policy.embedTimeoutMs);
            m_embedder->embed({query}, Retrieval::currentEmbeddingConfig(), [this, jobId](const Retrieval::EmbeddingResult &result) {
                m_embedTimer.stop();
                const bool ok = result.ok() && !result.vectors.isEmpty();
                const QVector<float> vector = ok ? result.vectors.first() : QVector<float>();
                QMetaObject::invokeMethod(this, [this, jobId, vector, ok, message = result.message]() {
                    m_embedder.reset();
                    continueGeneration(jobId, vector, ok ? QString() : QStringLiteral("Semantic search was unavailable (%1); keyword search was used.").arg(message));
                }, Qt::QueuedConnection);
            });
            return;
        }
    }

    continueGeneration(jobId, {}, {});
}

void KnowledgeJobRunner::continueGeneration(const QString &jobId, const QVector<float> &queryVector, const QString &semanticNote)
{
    if (!stillRunning(jobId)) {
        finishActive();
        return;
    }

    LiveEvents::Step step(QStringLiteral("knowledge"), QStringLiteral("evidence"));

    try {
        m_evidence = KnowledgeGeneration::selectEvidence(m_database, m_request, queryVector);
    } catch (const std::exception &error) {
        step.done(0);
        failOrRetry(jobId, codeOf(error).isEmpty() ? QStringLiteral("evidence-failed") : codeOf(error), messageOf(error), false);
        return;
    }

    step.done(1, {{QStringLiteral("passages"), m_evidence.passages.size()}, {QStringLiteral("semantic"), !queryVector.isEmpty()}});

    // Same request, same sources, same page revision as a proposal that is
    // still open or already applied: nothing new to say.
    const auto previous = m_database
                              .prepare(QStringLiteral(
                                  "SELECT j.id AS jobId, x.id AS proposalId, x.status AS status, x.applied_revision AS appliedRevision, "
                                  "p.revision AS pageRevision FROM knowledge_jobs j "
                                  "INNER JOIN knowledge_proposals x ON x.job_id = j.id "
                                  "LEFT JOIN knowledge_pages p ON p.id = x.target_page_id "
                                  "WHERE j.library_id = ? AND j.job_type = ? AND j.source_fingerprint = ? AND j.id != ? "
                                  "AND (x.status = 'pending' OR (x.status = 'applied' AND p.revision = x.applied_revision)) "
                                  "ORDER BY x.created_at DESC LIMIT 1"
                              ))
                              .bind(1, m_activeLibraryId)
                              .bind(2, m_request.jobType)
                              .bind(3, m_evidence.fingerprint)
                              .bind(4, jobId)
                              .one();

    try {
        if (previous) {
            m_database.transaction([&]() {
                KnowledgeJobs::update(m_database, jobId, {
                    {QStringLiteral("status"), JobStatus::completed},
                    {QStringLiteral("stage"), QStringLiteral("unchanged")},
                    {QStringLiteral("progress"), 100},
                    {QStringLiteral("sourceFingerprint"), m_evidence.fingerprint},
                    {QStringLiteral("result"), QVariantMap{
                                                   {QStringLiteral("unchanged"), true},
                                                   {QStringLiteral("proposalId"), previous->value(QStringLiteral("proposalId"))},
                                                   {QStringLiteral("previousJobId"), previous->value(QStringLiteral("jobId"))},
                                                   {QStringLiteral("reason"), previous->value(QStringLiteral("status")).toString() == ProposalStatus::pending
                                                                                  ? QStringLiteral("An open proposal already covers the same sources.")
                                                                                  : QStringLiteral("The page already reflects these sources.")},
                                               }},
                    {QStringLiteral("completedAt"), JsCompat::isoTimestamp(now())},
                });
            });
            finishActive();
            return;
        }

        m_database.transaction([&]() {
            KnowledgeJobs::update(m_database, jobId, {
                {QStringLiteral("stage"), QStringLiteral("generating")},
                {QStringLiteral("progress"), 40},
                {QStringLiteral("sourceFingerprint"), m_evidence.fingerprint},
                {QStringLiteral("errorMessage"), semanticNote.isEmpty() ? QVariant() : QVariant(semanticNote)},
            });
        });
    } catch (const std::exception &error) {
        failOrRetry(jobId, QStringLiteral("database"), messageOf(error), true);
        return;
    }

    emit jobChanged(m_activeLibraryId, jobId);

    if (!m_model) {
        m_model = m_models ? m_models() : nullptr;
    }

    if (!m_model) {
        failOrRetry(jobId, QStringLiteral("not-configured"), QStringLiteral("No model is available for Wiki generation."), false);
        return;
    }

    const ModelRequest prompt = KnowledgeGeneration::buildPrompt(m_request, m_evidence);
    m_model->generate(prompt, [this, jobId](const ModelResult &result) {
        QMetaObject::invokeMethod(this, [this, jobId, result]() { onModelResult(jobId, result); }, Qt::QueuedConnection);
    });
}

void KnowledgeJobRunner::onModelResult(const QString &jobId, const ModelResult &result)
{
    if (jobId != m_activeJobId) {
        return;
    }

    // Cancelled while the model was writing: the reply is dropped.
    if (!stillRunning(jobId)) {
        finishActive();
        return;
    }

    if (!result.ok()) {
        failOrRetry(jobId, result.code(), result.message, result.retryable(), result.retryAfterMs);
        return;
    }

    LiveEvents::Step step(QStringLiteral("knowledge"), QStringLiteral("validate"));

    try {
        m_database.transaction([&]() {
            KnowledgeJobs::update(m_database, jobId, {{QStringLiteral("stage"), QStringLiteral("validating")}, {QStringLiteral("progress"), 80}});
            const KnowledgeProposals::ProposalDraft draft = KnowledgeGeneration::parseResponse(
                m_database, m_request, m_evidence, result.text, jobId, result.model.isEmpty() ? m_request.model : result.model);
            const KnowledgeProposals::Submission submission = KnowledgeProposals::submit(m_database, draft);
            const QVariantMap validation = draft.validation;
            KnowledgeJobs::update(m_database, jobId, {
                {QStringLiteral("status"), JobStatus::completed},
                {QStringLiteral("stage"), QStringLiteral("done")},
                {QStringLiteral("progress"), 100},
                {QStringLiteral("errorCode"), QVariant()},
                {QStringLiteral("result"), QVariantMap{
                                               {QStringLiteral("proposalId"), submission.proposalId},
                                               {QStringLiteral("created"), submission.created},
                                               {QStringLiteral("proposalType"), draft.proposalType},
                                               {QStringLiteral("keptClaims"), validation.value(QStringLiteral("keptClaims"))},
                                               {QStringLiteral("droppedClaims"), validation.value(QStringLiteral("droppedClaims")).toList().size()},
                                               {QStringLiteral("droppedCitations"), validation.value(QStringLiteral("droppedCitations")).toList().size()},
                                               {QStringLiteral("inputTokens"), result.inputTokens},
                                               {QStringLiteral("outputTokens"), result.outputTokens},
                                           }},
                {QStringLiteral("completedAt"), JsCompat::isoTimestamp(now())},
            });
        });
        step.done(1);
    } catch (const std::exception &error) {
        step.done(0);
        const QString code = codeOf(error);
        // A malformed reply may be a one-off; anything else is not retried.
        failOrRetry(jobId, code.isEmpty() ? QStringLiteral("persist-failed") : code, messageOf(error), code == QStringLiteral("WIKI_INVALID_RESPONSE"));
        return;
    }

    emit knowledgeChanged(m_activeLibraryId);
    finishActive();
}

void KnowledgeJobRunner::failOrRetry(const QString &jobId, const QString &code, const QString &message, bool retryable, int retryAfterMs)
{
    try {
        m_database.transaction([&]() {
            const auto job = KnowledgeJobs::job(m_database, jobId);

            if (!job || job->value(QStringLiteral("status")).toString() != JobStatus::running) {
                return;
            }

            const int attempts = job->value(QStringLiteral("attemptCount")).toInt();
            const int maxAttempts = job->value(QStringLiteral("maxAttempts")).toInt();

            if (retryable && attempts < maxAttempts) {
                const int backoff = retryAfterMs > 0 ? retryAfterMs
                                                     : std::min(m_policy.maxBackoffMs, m_policy.baseBackoffMs * (1 << std::min(attempts - 1, 10)));
                KnowledgeJobs::update(m_database, jobId, {
                    {QStringLiteral("status"), JobStatus::queued},
                    {QStringLiteral("stage"), QStringLiteral("waiting-to-retry")},
                    {QStringLiteral("errorCode"), code},
                    {QStringLiteral("errorMessage"), message},
                    {QStringLiteral("nextAttemptAt"), JsCompat::isoTimestamp(now().addMSecs(backoff))},
                });
            } else {
                KnowledgeJobs::update(m_database, jobId, {
                    {QStringLiteral("status"), JobStatus::failed},
                    {QStringLiteral("stage"), QStringLiteral("failed")},
                    {QStringLiteral("errorCode"), code},
                    {QStringLiteral("errorMessage"), message},
                    {QStringLiteral("completedAt"), JsCompat::isoTimestamp(now())},
                });
            }
        });
    } catch (const std::exception &) {
    }

    finishActive();
}
}

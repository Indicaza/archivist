#include "knowledge_service.h"

#include "knowledge_jobs.h"
#include "knowledge_model.h"
#include "knowledge_proposals.h"
#include "knowledge_repository.h"
#include "knowledge_types.h"

#include "Core/Domains/AI/ai_provider.h"
#include "Core/Domains/Retrieval/embedding_client.h"
#include "Core/core_error.h"

#include <QDebug>
#include <QMetaObject>
#include <QNetworkAccessManager>

namespace Knowledge
{
namespace
{
const QString reviewer = QStringLiteral("user");

QVariantMap failure(const QString &code, const QString &message)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("code"), code}, {QStringLiteral("message"), message}};
}

QString libraryOf(CoreDatabase &database, const QString &table, const QString &id)
{
    return database.prepare(QStringLiteral("SELECT library_id FROM %1 WHERE id = ?").arg(table)).bind(1, id).scalar().toString();
}
}

// Lives on the Wiki thread.
class KnowledgeWorker final : public QObject
{
    Q_OBJECT

public:
    explicit KnowledgeWorker(QString databasePath)
        : m_databasePath(std::move(databasePath))
    {
    }

    void open()
    {
        m_database.open(m_databasePath);
        m_network = new QNetworkAccessManager(this);
        QNetworkAccessManager *network = m_network;
        const QString path = m_databasePath;
        // The key is read when a job starts, so a key added in Settings is
        // used by the next job without a restart.
        m_runner = new KnowledgeJobRunner(m_database, [network, path]() -> std::unique_ptr<KnowledgeModel> {
            return std::make_unique<OpenAIKnowledgeModel>(network, [path]() { return AIProvider::openAIConfig(path); });
        }, RunnerPolicy{}, this);
        m_runner->setEmbedder([network, path]() -> std::unique_ptr<Retrieval::EmbeddingClient> {
            return std::make_unique<Retrieval::OpenAIEmbeddingClient>(network, [path]() { return AIProvider::openAIConfig(path); });
        });
        connect(m_runner, &KnowledgeJobRunner::jobChanged, this, [this](const QString &libraryId, const QString &) { emit changed(libraryId, false); });
        connect(m_runner, &KnowledgeJobRunner::knowledgeChanged, this, [this](const QString &libraryId) { emit changed(libraryId, true); });
        m_runner->start();
    }

    void close()
    {
        delete m_runner;
        m_runner = nullptr;
        m_database.close();
    }

    void poke()
    {
        if (m_runner) {
            m_runner->poke();
        }
    }

    void cancel(const QString &jobId)
    {
        if (m_runner) {
            m_runner->cancelJob(jobId);
        }
    }

signals:
    void changed(const QString &libraryId, bool content);

private:
    QString m_databasePath;
    CoreDatabase m_database;
    QNetworkAccessManager *m_network = nullptr;
    KnowledgeJobRunner *m_runner = nullptr;
};

KnowledgeService::KnowledgeService(QObject *parent)
    : QObject(parent)
{
}

KnowledgeService::~KnowledgeService()
{
    stop();
}

bool KnowledgeService::start(const QString &databasePath)
{
    if (m_ready) {
        return true;
    }

    try {
        m_database.open(databasePath);
    } catch (const std::exception &error) {
        qWarning().noquote() << "[Wiki] Unavailable:" << error.what();
        return false;
    }

    auto *worker = new KnowledgeWorker(databasePath);
    worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &KnowledgeWorker::changed, this, &KnowledgeService::bump);
    m_thread.setObjectName(QStringLiteral("ArchivistWiki"));
    m_thread.start(QThread::LowPriority);
    m_worker = worker;
    bool opened = false;
    QMetaObject::invokeMethod(worker, [worker, &opened]() {
        try {
            worker->open();
            opened = true;
        } catch (const std::exception &error) {
            qWarning().noquote() << "[Wiki] Job runner unavailable:" << error.what();
        }
    }, Qt::BlockingQueuedConnection);

    if (!opened) {
        stop();
        return false;
    }

    m_ready = true;
    emit readyChanged();
    return true;
}

void KnowledgeService::stop()
{
    if (m_thread.isRunning()) {
        if (KnowledgeWorker *worker = m_worker) {
            QMetaObject::invokeMethod(worker, [worker]() { worker->close(); }, Qt::BlockingQueuedConnection);
        }

        m_thread.quit();
        m_thread.wait();
    }

    if (m_database.isOpen()) {
        m_database.close();
    }

    if (m_ready) {
        m_ready = false;
        emit readyChanged();
    }
}

void KnowledgeService::bump(const QString &libraryId, bool content)
{
    ++m_revision;
    emit changed(libraryId);

    if (content) {
        emit knowledgeChanged(libraryId);
    }
}

void KnowledgeService::pokeWorker()
{
    if (KnowledgeWorker *worker = m_worker) {
        QMetaObject::invokeMethod(worker, [worker]() { worker->poke(); });
    }
}

template<typename Work>
QVariantMap KnowledgeService::act(const QString &libraryId, Work &&work)
{
    if (!m_ready) {
        return failure(QStringLiteral("WIKI_UNAVAILABLE"), tr("The Wiki is not available."));
    }

    try {
        QVariantMap result = work();
        result.insert(QStringLiteral("ok"), true);
        bump(libraryId.isEmpty() ? result.value(QStringLiteral("libraryId")).toString() : libraryId, true);
        return result;
    } catch (const CoreError &error) {
        QVariantMap result = failure(error.details().value(QStringLiteral("code"), QStringLiteral("WIKI_ERROR")).toString(), error.message());
        result.insert(QStringLiteral("details"), error.details());
        return result;
    } catch (const std::exception &error) {
        return failure(QStringLiteral("WIKI_ERROR"), QString::fromUtf8(error.what()));
    }
}

#define WIKI_READ(expression, fallback)                                     \
    if (!m_ready) {                                                         \
        return fallback;                                                    \
    }                                                                       \
    try {                                                                   \
        return expression;                                                  \
    } catch (const std::exception &error) {                                 \
        qWarning().noquote() << "[Wiki]" << error.what();                   \
        return fallback;                                                    \
    }

QVariantMap KnowledgeService::overview(const QString &libraryId)
{
    WIKI_READ(KnowledgeRepository::overview(m_database, libraryId), QVariantMap{})
}

QVariantList KnowledgeService::pages(const QString &libraryId, bool includeInactive)
{
    WIKI_READ(KnowledgeRepository::pages(m_database, libraryId, includeInactive), QVariantList{})
}

QVariantMap KnowledgeService::page(const QString &pageId)
{
    WIKI_READ(KnowledgeRepository::pageDetail(m_database, pageId).value_or(QVariantMap{}), QVariantMap{})
}

QVariantList KnowledgeService::revisions(const QString &pageId)
{
    WIKI_READ(KnowledgeRepository::revisions(m_database, pageId), QVariantList{})
}

QVariantMap KnowledgeService::pageRevision(const QString &revisionId)
{
    WIKI_READ(KnowledgeRepository::revision(m_database, revisionId).value_or(QVariantMap{}), QVariantMap{})
}

QVariantList KnowledgeService::proposals(const QString &libraryId, const QString &status)
{
    WIKI_READ(KnowledgeProposals::proposals(m_database, libraryId, status), QVariantList{})
}

QVariantMap KnowledgeService::proposal(const QString &proposalId)
{
    WIKI_READ(KnowledgeProposals::proposal(m_database, proposalId).value_or(QVariantMap{}), QVariantMap{})
}

QVariantList KnowledgeService::jobs(const QString &libraryId, int limit)
{
    WIKI_READ(KnowledgeJobs::jobs(m_database, libraryId, limit), QVariantList{})
}

#undef WIKI_READ

QVariantMap KnowledgeService::generate(const QString &libraryId, const QVariantMap &request)
{
    const QString type = request.value(QStringLiteral("targetPageId")).toString().isEmpty() ? JobType::generatePage : JobType::expandPage;
    QVariantMap result = act(libraryId, [&]() { return KnowledgeJobs::enqueue(m_database, libraryId, type, request, reviewer); });
    pokeWorker();
    return result;
}

QVariantMap KnowledgeService::expand(const QString &libraryId, const QString &pageId)
{
    return generate(libraryId, {{QStringLiteral("targetPageId"), pageId}});
}

QVariantMap KnowledgeService::reconcile(const QString &libraryId)
{
    QVariantMap result = act(libraryId, [&]() { return KnowledgeJobs::enqueue(m_database, libraryId, JobType::reconcileSources, {}, reviewer); });
    pokeWorker();
    return result;
}

QVariantMap KnowledgeService::cancelJob(const QString &jobId)
{
    if (KnowledgeWorker *worker = m_worker) {
        // The runner drops the model request if this job is the one running.
        QMetaObject::invokeMethod(worker, [worker, jobId]() { worker->cancel(jobId); });
    }

    return act(m_ready ? libraryOf(m_database, QStringLiteral("knowledge_jobs"), jobId) : QString(), [&]() {
        return QVariantMap{{QStringLiteral("cancelled"), KnowledgeJobs::cancel(m_database, jobId)}};
    });
}

QVariantMap KnowledgeService::retryJob(const QString &jobId)
{
    QVariantMap result = act(m_ready ? libraryOf(m_database, QStringLiteral("knowledge_jobs"), jobId) : QString(), [&]() {
        return QVariantMap{{QStringLiteral("queued"), KnowledgeJobs::retry(m_database, jobId)}};
    });
    pokeWorker();
    return result;
}

QVariantMap KnowledgeService::approve(const QString &proposalId, const QString &note)
{
    const QString libraryId = m_ready ? libraryOf(m_database, QStringLiteral("knowledge_proposals"), proposalId) : QString();
    return act(libraryId, [&]() { return KnowledgeProposals::approve(m_database, proposalId, reviewer, note); });
}

QVariantMap KnowledgeService::reject(const QString &proposalId, const QString &note)
{
    const QString libraryId = m_ready ? libraryOf(m_database, QStringLiteral("knowledge_proposals"), proposalId) : QString();
    return act(libraryId, [&]() { return KnowledgeProposals::reject(m_database, proposalId, reviewer, note); });
}

QVariantMap KnowledgeService::createPage(const QString &libraryId, const QString &title, const QString &pageType, const QString &content)
{
    return act(libraryId, [&]() {
        return QVariantMap{{QStringLiteral("pageId"), KnowledgeProposals::createManualPage(m_database, libraryId, title, pageType, content, reviewer)}};
    });
}

QVariantMap KnowledgeService::editPage(const QString &pageId, const QString &title, const QString &pageType, const QString &content, int baseRevision)
{
    const QString libraryId = m_ready ? libraryOf(m_database, QStringLiteral("knowledge_pages"), pageId) : QString();
    return act(libraryId, [&]() {
        return QVariantMap{{QStringLiteral("revision"), KnowledgeProposals::editPage(m_database, pageId, title, pageType, content, baseRevision, reviewer)}};
    });
}

QVariantMap KnowledgeService::reviewClaim(const QString &claimId, const QString &action, const QString &note)
{
    const QString libraryId = m_ready ? libraryOf(m_database, QStringLiteral("knowledge_claims"), claimId) : QString();
    return act(libraryId, [&]() {
        return QVariantMap{{QStringLiteral("revision"), KnowledgeProposals::reviewClaim(m_database, claimId, action, reviewer, note)}};
    });
}

QVariantMap KnowledgeService::restoreRevision(const QString &pageId, const QString &revisionId)
{
    const QString libraryId = m_ready ? libraryOf(m_database, QStringLiteral("knowledge_pages"), pageId) : QString();
    return act(libraryId, [&]() {
        return QVariantMap{{QStringLiteral("revision"), KnowledgeProposals::restoreRevision(m_database, pageId, revisionId, reviewer)}};
    });
}

QVariantMap KnowledgeService::proposeDeprecation(const QString &pageId, const QString &reason)
{
    const QString libraryId = m_ready ? libraryOf(m_database, QStringLiteral("knowledge_pages"), pageId) : QString();
    return act(libraryId, [&]() {
        const auto submission = KnowledgeProposals::proposeDeprecation(m_database, pageId, reason, {Actor::user, reviewer});
        return QVariantMap{{QStringLiteral("proposalId"), submission.proposalId}};
    });
}

QVariantMap KnowledgeService::proposeMerge(const QString &sourcePageId, const QString &targetPageId, const QString &reason)
{
    const QString libraryId = m_ready ? libraryOf(m_database, QStringLiteral("knowledge_pages"), sourcePageId) : QString();
    return act(libraryId, [&]() {
        const auto submission = KnowledgeProposals::proposeMerge(m_database, sourcePageId, targetPageId, reason, {Actor::user, reviewer});
        return QVariantMap{{QStringLiteral("proposalId"), submission.proposalId}};
    });
}
}

#include "knowledge_service.moc"

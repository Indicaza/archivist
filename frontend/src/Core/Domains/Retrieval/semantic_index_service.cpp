#include "semantic_index_service.h"

#include "embedding_index.h"
#include "embedding_indexer.h"

#include "Core/Database/core_database.h"
#include "Core/Domains/AI/ai_provider.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QSet>
#include <QTimer>

namespace Retrieval
{
namespace
{
// The OpenAI key and base URL, re-read at most once a minute. Every batch
// needs them, and reading the Keychain for each one is needless (and on
// macOS can mean a permission prompt per read for unsigned dev builds).
// Used on the embeddings thread only.
class CachedOpenAIConfig
{
public:
    explicit CachedOpenAIConfig(QString databasePath)
        : m_databasePath(std::move(databasePath))
    {
    }

    AIProvider::OpenAIConfig get()
    {
        if (!m_readAt.isValid() || m_readAt.elapsed() > 60 * 1000) {
            m_config = AIProvider::openAIConfig(m_databasePath);
            m_readAt.start();
        }

        return m_config;
    }

    void invalidate() { m_readAt.invalidate(); }

private:
    QString m_databasePath;
    AIProvider::OpenAIConfig m_config;
    QElapsedTimer m_readAt;
};
}

// Lives on the embeddings thread.
class SemanticIndexWorker final : public QObject
{
    Q_OBJECT

public:
    explicit SemanticIndexWorker(QString databasePath)
        : m_databasePath(std::move(databasePath))
    {
    }

    void open()
    {
        m_database.open(m_databasePath);
        m_network = new QNetworkAccessManager(this);
        m_providerConfig = std::make_shared<CachedOpenAIConfig>(m_databasePath);
        QNetworkAccessManager *network = m_network;
        const std::shared_ptr<CachedOpenAIConfig> providerConfig = m_providerConfig;
        m_indexer = new EmbeddingIndexer(m_database, [network, providerConfig]() -> std::unique_ptr<EmbeddingClient> {
            return std::make_unique<OpenAIEmbeddingClient>(network, [providerConfig]() { return providerConfig->get(); });
        }, IndexerPolicy{}, currentEmbeddingConfig(), this);
        connect(m_indexer, &EmbeddingIndexer::libraryChanged, this, &SemanticIndexWorker::queueStatus);

        m_statusTimer = new QTimer(this);
        m_statusTimer->setSingleShot(true);
        m_statusTimer->setInterval(200);
        connect(m_statusTimer, &QTimer::timeout, this, &SemanticIndexWorker::publishStatus);

        // Files change in many ways (scans, saves, AI edits, git); a cheap
        // periodic sync catches anything no signal reported.
        m_syncTimer = new QTimer(this);
        m_syncTimer->setInterval(90 * 1000);
        connect(m_syncTimer, &QTimer::timeout, this, [this]() { guard([this]() { m_indexer->syncAll(); }); });
        m_syncTimer->start();

        guard([this]() { m_indexer->recover(); });

        for (const QString &libraryId : EmbeddingIndex::enabledLibraries(m_database)) {
            queueStatus(libraryId);
        }
    }

    void close()
    {
        delete m_indexer;
        m_indexer = nullptr;
        m_database.close();
    }

    void enable(const QString &libraryId)
    {
        m_providerConfig->invalidate();
        guard([&]() { m_indexer->enable(libraryId); });
        queueStatus(libraryId);
    }
    void disable(const QString &libraryId) { guard([&]() { m_indexer->disable(libraryId); }); queueStatus(libraryId); }
    void pause(const QString &libraryId) { guard([&]() { m_indexer->pause(libraryId); }); queueStatus(libraryId); }
    void resume(const QString &libraryId)
    {
        m_providerConfig->invalidate(); // the user may have just added or fixed the key
        guard([&]() { m_indexer->resume(libraryId); });
        queueStatus(libraryId);
    }
    void cancel(const QString &libraryId) { guard([&]() { m_indexer->cancel(libraryId); }); queueStatus(libraryId); }
    void sync(const QString &libraryId) { guard([&]() { m_indexer->sync(libraryId); }); }
    void refresh(const QString &libraryId) { queueStatus(libraryId); }

signals:
    void status(const QString &libraryId, const QVariantMap &status);

private:
    template<typename Work>
    void guard(Work &&work)
    {
        try {
            work();
        } catch (const std::exception &error) {
            qWarning().noquote() << "[Embeddings]" << error.what();
        }
    }

    void queueStatus(const QString &libraryId)
    {
        if (!libraryId.isEmpty()) {
            m_pendingStatus.insert(libraryId);

            if (!m_statusTimer->isActive()) {
                m_statusTimer->start();
            }
        }
    }

    void publishStatus()
    {
        const QSet<QString> pending = std::exchange(m_pendingStatus, {});
        const bool keyConfigured = !m_providerConfig->get().apiKey.isEmpty();

        for (const QString &libraryId : pending) {
            try {
                QVariantMap value = EmbeddingIndex::libraryStatus(m_database, libraryId, m_indexer->config());
                value.insert(QStringLiteral("keyConfigured"), keyConfigured);
                value.insert(QStringLiteral("dailyTokenBudget"), m_indexer->policy().dailyTokenBudget);
                value.insert(QStringLiteral("tokensUsedToday"), EmbeddingIndex::tokensUsedOn(m_database, QDateTime::currentDateTimeUtc()));
                emit status(libraryId, value);
            } catch (const std::exception &error) {
                qWarning().noquote() << "[Embeddings] Status failed:" << error.what();
            }
        }
    }

    QString m_databasePath;
    CoreDatabase m_database;
    QNetworkAccessManager *m_network = nullptr;
    std::shared_ptr<CachedOpenAIConfig> m_providerConfig;
    EmbeddingIndexer *m_indexer = nullptr;
    QTimer *m_statusTimer = nullptr;
    QTimer *m_syncTimer = nullptr;
    QSet<QString> m_pendingStatus;
};

SemanticIndexService::SemanticIndexService(QObject *parent)
    : QObject(parent)
{
}

SemanticIndexService::~SemanticIndexService()
{
    stop();
}

QString SemanticIndexService::model() const
{
    return currentEmbeddingConfig().model;
}

bool SemanticIndexService::start(const QString &databasePath)
{
    if (m_ready) {
        return true;
    }

    m_databasePath = databasePath;
    auto *worker = new SemanticIndexWorker(databasePath);
    worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &SemanticIndexWorker::status, this, &SemanticIndexService::onStatus);
    m_thread.setObjectName(QStringLiteral("ArchivistEmbeddings"));
    m_thread.start(QThread::LowPriority);
    m_worker = worker;
    bool opened = false;
    QMetaObject::invokeMethod(worker, [worker, &opened]() {
        try {
            worker->open();
            opened = true;
        } catch (const std::exception &error) {
            qWarning().noquote() << "[Embeddings] Semantic indexing unavailable:" << error.what();
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

void SemanticIndexService::stop()
{
    if (!m_thread.isRunning()) {
        return;
    }

    if (m_worker) {
        SemanticIndexWorker *worker = m_worker;
        QMetaObject::invokeMethod(worker, [worker]() { worker->close(); }, Qt::BlockingQueuedConnection);
    }

    m_thread.quit();
    m_thread.wait();

    if (m_ready) {
        m_ready = false;
        emit readyChanged();
    }
}

QVariantMap SemanticIndexService::status(const QString &libraryId)
{
    if (!m_status.contains(libraryId)) {
        refresh(libraryId);
    }

    return m_status.value(libraryId);
}

void SemanticIndexService::onStatus(const QString &libraryId, const QVariantMap &status)
{
    m_status.insert(libraryId, status);
    ++m_revision;
    emit statusChanged(libraryId);
}

#define SEMANTIC_FORWARD(method)                                                                   \
    void SemanticIndexService::method(const QString &libraryId)                                    \
    {                                                                                              \
        if (SemanticIndexWorker *worker = m_worker; worker && !libraryId.isEmpty()) {              \
            QMetaObject::invokeMethod(worker, [worker, libraryId]() { worker->method(libraryId); }); \
        }                                                                                          \
    }

SEMANTIC_FORWARD(refresh)
SEMANTIC_FORWARD(enable)
SEMANTIC_FORWARD(disable)
SEMANTIC_FORWARD(pause)
SEMANTIC_FORWARD(resume)
SEMANTIC_FORWARD(cancel)
SEMANTIC_FORWARD(sync)

#undef SEMANTIC_FORWARD
}

#include "semantic_index_service.moc"

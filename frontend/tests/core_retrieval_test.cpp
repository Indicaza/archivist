#include "Core/Database/core_database.h"
#include "Core/Domains/AI/library_retrieval.h"
#include "Core/Domains/Context/context_assembler.h"
#include "Core/Domains/Context/context_sources.h"
#include "Core/Domains/Libraries/library_files.h"
#include "Core/Domains/Libraries/library_repository.h"
#include "Core/Domains/Retrieval/embedding_client.h"
#include "Core/Domains/Retrieval/embedding_index.h"
#include "Core/Domains/Retrieval/embedding_indexer.h"
#include "Core/Domains/Retrieval/hybrid_retrieval.h"
#include "Core/Domains/Retrieval/semantic_index_service.h"
#include "Core/Domains/Retrieval/vector_codec.h"
#include "Core/Domains/Retrieval/vector_store.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

#include <memory>

using namespace Retrieval;

namespace
{
// Eight "concept" axes. Words on the same axis embed close together, so a
// query can match a chunk it shares no keyword with, the way real
// embeddings relate "vessel" and "harbour".
QVector<float> conceptVector(const QString &text)
{
    static const QList<QStringList> axes{
        {QStringLiteral("boat"), QStringLiteral("ship"), QStringLiteral("harbour"), QStringLiteral("vessel"), QStringLiteral("sail"), QStringLiteral("anchor"), QStringLiteral("maritime")},
        {QStringLiteral("money"), QStringLiteral("budget"), QStringLiteral("cost"), QStringLiteral("finance"), QStringLiteral("price"), QStringLiteral("spending")},
        {QStringLiteral("dragon"), QStringLiteral("wyrm"), QStringLiteral("serpent"), QStringLiteral("scales")},
        {QStringLiteral("parse"), QStringLiteral("config"), QStringLiteral("settings"), QStringLiteral("options")},
        {QStringLiteral("garden"), QStringLiteral("flower"), QStringLiteral("bloom")},
        {QStringLiteral("winter"), QStringLiteral("snow"), QStringLiteral("frost")},
        {QStringLiteral("music"), QStringLiteral("song"), QStringLiteral("melody")},
    };
    QVector<float> vector(8, 0.0f);
    const QString lower = text.toLower();

    for (int axis = 0; axis < axes.size(); ++axis) {
        for (const QString &word : axes.at(axis)) {
            vector[axis] += static_cast<float>(lower.count(word));
        }
    }

    vector[7] = 0.05f; // never a zero vector
    return VectorCodec::normalized(vector);
}

// Shared state for every fake client one indexer creates.
struct FakeBackend
{
    QList<QStringList> calls;           // inputs of each request
    QList<EmbeddingResult::Error> script; // errors to answer with first, in order
    bool hang = false;                  // never answer (until cancelled)
    int tokensPerInput = 7;

    int inputCount() const
    {
        int count = 0;

        for (const QStringList &call : calls) {
            count += static_cast<int>(call.size());
        }

        return count;
    }
};

class FakeEmbeddingClient final : public QObject, public EmbeddingClient
{
public:
    explicit FakeEmbeddingClient(std::shared_ptr<FakeBackend> backend)
        : m_backend(std::move(backend))
    {
    }

    void embed(const QStringList &inputs, const EmbeddingConfig &config, Callback done) override
    {
        m_done = std::move(done);
        m_backend->calls.append(inputs);

        if (m_backend->hang) {
            return;
        }

        const EmbeddingResult::Error error = m_backend->script.isEmpty() ? EmbeddingResult::Error::None : m_backend->script.takeFirst();
        QTimer::singleShot(0, this, [this, inputs, config, error]() {
            EmbeddingResult result;
            result.error = error;

            if (error == EmbeddingResult::Error::None) {
                for (const QString &input : inputs) {
                    QVector<float> vector = conceptVector(input);
                    vector.resize(config.dimensions);
                    result.vectors.append(VectorCodec::normalized(vector));
                }

                result.promptTokens = static_cast<int>(inputs.size()) * m_backend->tokensPerInput;
            } else {
                result.message = QStringLiteral("scripted failure");
                result.retryAfterMs = 0;
            }

            deliver(result);
        });
    }

    void cancel() override
    {
        if (m_done) {
            EmbeddingResult result;
            result.error = EmbeddingResult::Error::Cancelled;
            deliver(result);
        }
    }

    bool busy() const override { return static_cast<bool>(m_done); }

private:
    void deliver(const EmbeddingResult &result)
    {
        if (Callback done = std::exchange(m_done, nullptr)) {
            done(result);
        }
    }

    std::shared_ptr<FakeBackend> m_backend;
    Callback m_done;
};

EmbeddingConfig testConfig()
{
    EmbeddingConfig config;
    config.model = QStringLiteral("text-embedding-3-small");
    config.dimensions = 8;
    return config;
}

IndexerPolicy fastPolicy()
{
    IndexerPolicy policy;
    policy.minRequestIntervalMs = 0;
    policy.baseBackoffMs = 10;
    policy.maxBackoffMs = 40;
    policy.batchSize = 3;
    return policy;
}

void writeFile(const QString &root, const QString &relativePath, const QString &content)
{
    const QString path = QDir(root).filePath(relativePath);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(content.toUtf8());
}

// A Library on disk, scanned and FTS-indexed like the app does it.
struct Fixture
{
    QTemporaryDir directory;
    CoreDatabase database;
    std::shared_ptr<FakeBackend> backend = std::make_shared<FakeBackend>();

    QString root(const QString &name = QStringLiteral("library")) const { return directory.filePath(name); }

    void open()
    {
        database.open(directory.filePath(QStringLiteral("retrieval.db")));
        database.runMigrations();
    }

    QString createLibrary(const QString &name = QStringLiteral("library"))
    {
        QDir().mkpath(root(name));
        return LibraryRepository::createLibrary(database, {{QStringLiteral("rootPath"), root(name)}})
            .value(QStringLiteral("library"))
            .toMap()
            .value(QStringLiteral("id"))
            .toString();
    }

    void scan(const QString &libraryId) { (void)LibraryFiles::scan(database, libraryId); }

    std::unique_ptr<EmbeddingIndexer> indexer(IndexerPolicy policy = fastPolicy())
    {
        auto backendCopy = backend;
        return std::make_unique<EmbeddingIndexer>(database, [backendCopy]() -> std::unique_ptr<EmbeddingClient> {
            return std::make_unique<FakeEmbeddingClient>(backendCopy);
        }, policy, testConfig());
    }

    QVariantMap job(const QString &libraryId)
    {
        return EmbeddingIndex::latestJob(database, ScopeType::library, libraryId).value_or(QVariantMap());
    }

    QString jobStatus(const QString &libraryId) { return job(libraryId).value(QStringLiteral("status")).toString(); }

    int count(const QString &sql)
    {
        return database.prepare(sql).scalar().toInt();
    }
};

bool settle(EmbeddingIndexer &indexer, int timeoutMs = 5000)
{
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);

        if (indexer.idle()) {
            // One more turn for retired clients and queued timers.
            QCoreApplication::processEvents();

            if (indexer.idle()) {
                return true;
            }
        }
    }

    return false;
}
}

// Semantic retrieval: vectors, the content-addressed cache, the background
// indexer, hybrid search and its Context Assembly v3 adapter. Every
// embedding comes from a fake client; nothing reaches the network.
class CoreRetrievalTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("ArchivistTest"));
        QCoreApplication::setApplicationName(QStringLiteral("core-retrieval-test"));
    }

    void vectorsRoundTripAsLittleEndianFloat32()
    {
        const QVector<float> vector = VectorCodec::normalized({3.0f, 4.0f, 0.0f});
        QCOMPARE(vector.at(0), 0.6f);
        QCOMPARE(vector.at(1), 0.8f);
        const QByteArray blob = VectorCodec::encode(vector);
        QCOMPARE(blob.size(), 12);
        // 0.6f = 0x3F19999A, little-endian on disk.
        QCOMPARE(static_cast<uchar>(blob.at(0)), uchar(0x9A));
        QCOMPARE(static_cast<uchar>(blob.at(3)), uchar(0x3F));
        QCOMPARE(VectorCodec::decode(blob, 3), vector);
        QVERIFY(VectorCodec::decode(blob, 4).isEmpty());
        QVERIFY(qAbs(VectorCodec::dot(vector, blob) - 1.0f) < 1e-6f);
        QCOMPARE(VectorCodec::dot({1.0f, 0.0f}, blob), 0.0f); // size mismatch
        QCOMPARE(VectorCodec::normalized({0.0f, 0.0f}), (QVector<float>{0.0f, 0.0f}));
    }

    void exactTermsFindIdentifiersFilesAndSymbols()
    {
        const QStringList terms = exactTerms(QStringLiteral("Where does parseConfig read MAX_ITEMS in settings_loader.cpp or `load options` and Context::assemble?"));
        QVERIFY(terms.contains(QStringLiteral("parseConfig")));
        QVERIFY(terms.contains(QStringLiteral("MAX_ITEMS")));
        QVERIFY(terms.contains(QStringLiteral("settings_loader.cpp")));
        QVERIFY(terms.contains(QStringLiteral("load options")));
        QVERIFY(terms.contains(QStringLiteral("Context::assemble")));
        QVERIFY(!terms.contains(QStringLiteral("Where")));
        QVERIFY(exactTerms(QStringLiteral("what is the tone of the story")).isEmpty());

        QCOMPARE(exactMatch({QStringLiteral("notes.md")}, QStringLiteral("docs/notes.md"), QStringLiteral("notes.md"), QString()), ExactMatch::FileName);
        QCOMPARE(exactMatch({QStringLiteral("parseConfig")}, QStringLiteral("src/a.cpp"), QStringLiteral("a.cpp"), QStringLiteral("void parseConfig();")), ExactMatch::Identifier);
        // Case-sensitive and bounded: no partial identifiers.
        QCOMPARE(exactMatch({QStringLiteral("parseConfig")}, QStringLiteral("src/a.cpp"), QStringLiteral("a.cpp"), QStringLiteral("parseconfig parseConfigFile")), ExactMatch::None);
    }

    void fusionIsDeterministicWeightedAndKeepsExactMatches()
    {
        HybridPolicy policy;
        const QList<FusionInput> inputs{
            {QStringLiteral("lexical-only"), 1, 0, ExactMatch::None},
            {QStringLiteral("both"), 3, 2, ExactMatch::None},
            {QStringLiteral("semantic-only"), 0, 1, ExactMatch::None},
            {QStringLiteral("exact-file"), 9, 0, ExactMatch::FileName},
            {QStringLiteral("exact-symbol"), 0, 12, ExactMatch::Identifier},
        };
        const QList<FusedRank> fused = fuse(inputs, policy);
        QStringList order;

        for (const FusedRank &rank : fused) {
            order.append(rank.id);
        }

        // Exact matches lead; found by both signals beats either alone; a tie
        // between keyword #1 and semantic #1 goes to the keyword hit.
        QCOMPARE(order, (QStringList{QStringLiteral("exact-file"), QStringLiteral("exact-symbol"), QStringLiteral("both"), QStringLiteral("lexical-only"), QStringLiteral("semantic-only")}));
        QCOMPARE(fuse(inputs, policy).first().score, fused.first().score);
        QVERIFY(qAbs(fused.at(2).score - (1.0 / 63 + 1.0 / 62)) < 1e-12);

        // Weights shift the balance.
        HybridPolicy semanticHeavy;
        semanticHeavy.lexicalWeight = 0.2;
        const QList<FusedRank> shifted = fuse({inputs.at(0), inputs.at(2)}, semanticHeavy);
        QCOMPARE(shifted.first().id, QStringLiteral("semantic-only"));

        // Policy from the v3 config, in percent.
        const HybridPolicy configured = HybridPolicy::fromConfig({
            {QStringLiteral("lexicalWeight"), 40},
            {QStringLiteral("semanticWeight"), 100},
            {QStringLiteral("semanticMinSimilarity"), 35},
        });
        QCOMPARE(configured.lexicalWeight, 0.4);
        QCOMPARE(configured.minSimilarity, 0.35);
        QVERIFY(configured.semanticEnabled);
        QVERIFY(!HybridPolicy::fromConfig({{QStringLiteral("semanticRetrieval"), QStringLiteral("off")}}).semanticEnabled);
        QVERIFY(!HybridPolicy::fromConfig({{QStringLiteral("semanticWeight"), 0}}).semanticEnabled);
    }

    void openAIClientSpeaksTheEmbeddingsApi()
    {
        // A one-shot HTTP stand-in for POST /v1/embeddings.
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QByteArray requestBody;
        QByteArray nextStatus = "200 OK";
        QByteArray nextBody;
        QByteArray extraHeaders;
        connect(&server, &QTcpServer::newConnection, this, [&]() {
            QTcpSocket *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket]() {
                static QHash<QTcpSocket *, QByteArray> buffers;
                QByteArray &data = buffers[socket];
                data += socket->readAll();
                const qsizetype end = data.indexOf("\r\n\r\n");
                if (end < 0) {
                    return;
                }
                qsizetype length = 0;
                for (const QByteArray &line : data.left(end).split('\n')) {
                    if (line.toLower().startsWith("content-length:")) {
                        length = line.mid(15).trimmed().toLongLong();
                    }
                }
                if (data.size() < end + 4 + length) {
                    return;
                }
                requestBody = data.mid(end + 4, length);
                data.clear();
                socket->write("HTTP/1.1 " + nextStatus + "\r\nContent-Type: application/json\r\n" + extraHeaders + "Content-Length: "
                              + QByteArray::number(nextBody.size()) + "\r\nConnection: close\r\n\r\n" + nextBody);
                socket->disconnectFromHost();
            });
        });

        QNetworkAccessManager network;
        AIProvider::OpenAIConfig provider;
        provider.apiKey = QStringLiteral("sk-test");
        provider.baseUrl = QUrl(QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort()));
        OpenAIEmbeddingClient client(&network, [provider]() { return provider; }, 5000);
        EmbeddingConfig config = testConfig();
        config.dimensions = 2;

        const auto run = [&](const QStringList &inputs) -> EmbeddingResult {
            std::optional<EmbeddingResult> received;
            client.embed(inputs, config, [&](const EmbeddingResult &result) { received = result; });
            QElapsedTimer timer;
            timer.start();
            while (!received && timer.elapsed() < 5000) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            }
            if (!received) {
                EmbeddingResult timeout;
                timeout.error = EmbeddingResult::Error::Cancelled;
                timeout.message = QStringLiteral("test timed out");
                return timeout;
            }
            return *received;
        };

        // Answers may arrive out of order; index decides.
        nextBody = R"({"data":[{"index":1,"embedding":[0,2]},{"index":0,"embedding":[3,4]}],"usage":{"prompt_tokens":11}})";
        EmbeddingResult ok = run({QStringLiteral("first"), QStringLiteral("second")});
        QVERIFY(ok.ok());
        QCOMPARE(ok.promptTokens, 11);
        QCOMPARE(ok.vectors.at(0), (QVector<float>{0.6f, 0.8f}));
        QCOMPARE(ok.vectors.at(1), (QVector<float>{0.0f, 1.0f}));
        const QJsonObject sent = QJsonDocument::fromJson(requestBody).object();
        QCOMPARE(sent.value(QStringLiteral("model")).toString(), QStringLiteral("text-embedding-3-small"));
        QCOMPARE(sent.value(QStringLiteral("dimensions")).toInt(), 2);
        QCOMPARE(sent.value(QStringLiteral("encoding_format")).toString(), QStringLiteral("float"));
        QCOMPARE(sent.value(QStringLiteral("input")).toArray().size(), 2);

        nextStatus = "401 Unauthorized";
        nextBody = R"({"error":{"message":"Incorrect API key provided"}})";
        EmbeddingResult unauthorized = run({QStringLiteral("x")});
        QCOMPARE(unauthorized.error, EmbeddingResult::Error::Unauthorized);
        QVERIFY(!unauthorized.retryable());

        nextStatus = "429 Too Many Requests";
        extraHeaders = "retry-after-ms: 1500\r\n";
        nextBody = R"({"error":{"message":"Rate limit reached"}})";
        EmbeddingResult limited = run({QStringLiteral("x")});
        QCOMPARE(limited.error, EmbeddingResult::Error::RateLimited);
        QVERIFY(limited.retryable());
        QCOMPARE(limited.retryAfterMs, 1500);

        extraHeaders.clear();
        nextStatus = "200 OK";
        nextBody = R"({"data":[{"index":0,"embedding":[1,2,3]}]})";
        QCOMPARE(run({QStringLiteral("x")}).error, EmbeddingResult::Error::InvalidRequest); // wrong dimensions

        // Offline: nothing listens.
        server.close();
        QCOMPARE(run({QStringLiteral("x")}).error, EmbeddingResult::Error::Network);

        // No key: nothing is sent.
        OpenAIEmbeddingClient keyless(&network, []() { return AIProvider::OpenAIConfig{}; });
        std::optional<EmbeddingResult> received;
        keyless.embed({QStringLiteral("x")}, config, [&](const EmbeddingResult &result) { received = result; });
        QVERIFY(!received.has_value()); // never synchronous
        QTRY_VERIFY(received.has_value());
        QCOMPARE(received->error, EmbeddingResult::Error::NotConfigured);
    }

    void indexingIsOptInAndPersistsVectors()
    {
        Fixture fixture;
        fixture.open();
        const QString libraryId = fixture.createLibrary();
        writeFile(fixture.root(), QStringLiteral("harbour.md"), QStringLiteral("The vessel rests at anchor in the harbour.\n"));
        writeFile(fixture.root(), QStringLiteral("ledger.md"), QStringLiteral("Quarterly budget and spending notes.\n"));
        fixture.scan(libraryId);
        auto indexer = fixture.indexer();

        // Nothing happens without consent.
        indexer->sync(libraryId);
        QVERIFY(settle(*indexer));
        QCOMPARE(fixture.backend->calls.size(), 0);
        QVERIFY(fixture.job(libraryId).isEmpty());

        indexer->enable(libraryId);
        QVERIFY(settle(*indexer));
        QCOMPARE(fixture.jobStatus(libraryId), EmbeddingIndex::JobStatus::completed);
        QCOMPARE(fixture.backend->inputCount(), 2);
        QCOMPARE(fixture.count(QStringLiteral("SELECT COUNT(*) FROM embedding_vectors")), 2);
        QCOMPARE(fixture.count(QStringLiteral("SELECT COUNT(*) FROM embedding_vectors WHERE length(vector) = 32 AND dimensions = 8")), 2);
        QCOMPARE(fixture.count(QStringLiteral("SELECT COUNT(*) FROM embedding_sources WHERE source_type = 'library-chunk'")), 2);

        // Sources keep chunk identity, file, path and lines.
        const auto source = fixture.database
                                .prepare(QStringLiteral(
                                    "SELECT s.source_id AS sourceId, s.parent_id AS parentId, s.locator AS locator, s.start_line AS startLine, "
                                    "s.content_hash AS hash, c.content_hash AS chunkHash, c.library_file_id AS fileId "
                                    "FROM embedding_sources s JOIN library_chunks c ON c.id = s.source_id WHERE s.locator = 'harbour.md'"
                                ))
                                .one();
        QVERIFY(source.has_value());
        QCOMPARE(source->value(QStringLiteral("hash")), source->value(QStringLiteral("chunkHash")));
        QCOMPARE(source->value(QStringLiteral("parentId")), source->value(QStringLiteral("fileId")));
        QCOMPARE(source->value(QStringLiteral("startLine")).toInt(), 1);

        const QVariantMap job = fixture.job(libraryId);
        QCOMPARE(job.value(QStringLiteral("processedItems")).toInt(), 2);
        QCOMPARE(job.value(QStringLiteral("embeddedItems")).toInt(), 2);
        QCOMPARE(job.value(QStringLiteral("tokensUsed")).toInt(), 14);
        QCOMPARE(EmbeddingIndex::tokensUsedOn(fixture.database, QDateTime::currentDateTimeUtc()), 14);

        const QVariantMap status = EmbeddingIndex::libraryStatus(fixture.database, libraryId, testConfig());
        QCOMPARE(status.value(QStringLiteral("state")).toString(), QStringLiteral("ready"));
        QCOMPARE(status.value(QStringLiteral("searchable")).toInt(), 2);
        QVERIFY(semanticAvailable(fixture.database, libraryId, testConfig()));

        // A second sync with nothing changed sends nothing and makes no job.
        const int jobs = fixture.count(QStringLiteral("SELECT COUNT(*) FROM embedding_jobs"));
        indexer->sync(libraryId);
        QVERIFY(settle(*indexer));
        QCOMPARE(fixture.backend->calls.size(), 1);
        QCOMPARE(fixture.count(QStringLiteral("SELECT COUNT(*) FROM embedding_jobs")), jobs);
    }

    void identicalContentIsEmbeddedOnceAndMovesReuseIt()
    {
        Fixture fixture;
        fixture.open();
        const QString libraryId = fixture.createLibrary();
        const QString text = QStringLiteral("A dragon sleeps on its scales.\n");
        writeFile(fixture.root(), QStringLiteral("a.md"), text);
        writeFile(fixture.root(), QStringLiteral("copy/b.md"), text);
        fixture.scan(libraryId);
        auto indexer = fixture.indexer();
        indexer->enable(libraryId);
        QVERIFY(settle(*indexer));

        // Two chunks, one hash, one input, one vector.
        QCOMPARE(fixture.backend->inputCount(), 1);
        QCOMPARE(fixture.count(QStringLiteral("SELECT COUNT(*) FROM embedding_sources")), 2);
        QCOMPARE(fixture.count(QStringLiteral("SELECT COUNT(*) FROM embedding_vectors")), 1);

        // Move a file: new path, new chunk id, same content. No API call.
        QDir().mkpath(QDir(fixture.root()).filePath(QStringLiteral("moved")));
        QVERIFY(QFile::rename(QDir(fixture.root()).filePath(QStringLiteral("a.md")), QDir(fixture.root()).filePath(QStringLiteral("moved/a.md"))));
        fixture.scan(libraryId);
        indexer->sync(libraryId);
        QVERIFY(settle(*indexer));
        QCOMPARE(fixture.backend->inputCount(), 1);
        QCOMPARE(fixture.count(QStringLiteral("SELECT COUNT(*) FROM embedding_sources WHERE locator = 'moved/a.md'")), 1);
        QCOMPARE(fixture.count(QStringLiteral("SELECT COUNT(*) FROM embedding_sources WHERE locator = 'a.md'")), 0);
        QCOMPARE(fixture.count(QStringLiteral("SELECT COUNT(*) FROM embedding_vectors")), 1);
        QVERIFY(fixture.job(libraryId).value(QStringLiteral("reusedItems")).toInt() >= 1);

        // Rebuilding the FTS index does not touch vectors either.
        LibraryRetrieval::repairSearchIndex(fixture.database);
        indexer->sync(libraryId);
        QVERIFY(settle(*indexer));
        QCOMPARE(fixture.backend->inputCount(), 1);
    }

    void changedFilesReindexIncrementallyAndDeletionsCleanUp()
    {
        Fixture fixture;
        fixture.open();
        const QString libraryId = fixture.createLibrary();
        writeFile(fixture.root(), QStringLiteral("one.md"), QStringLiteral("Snow and frost in winter.\n"));
        writeFile(fixture.root(), QStringLiteral("two.md"), QStringLiteral("A melody for a song.\n"));
        writeFile(fixture.root(), QStringLiteral("three.md"), QStringLiteral("A garden in bloom.\n"));
        fixture.scan(libraryId);
        auto indexer = fixture.indexer();
        indexer->enable(libraryId);
        QVERIFY(settle(*indexer));
        QCOMPARE(fixture.backend->inputCount(), 3);

        // Edit one file: only its new text is sent; its old vector goes.
        writeFile(fixture.root(), QStringLiteral("one.md"), QStringLiteral("Snow and frost in winter, and a sail on the harbour.\n"));
        fixture.scan(libraryId);
        indexer->sync(libraryId);
        QVERIFY(settle(*indexer));
        QCOMPARE(fixture.backend->inputCount(), 4);
        QVERIFY(fixture.backend->calls.last().first().contains(QStringLiteral("sail")));
        QCOMPARE(fixture.count(QStringLiteral("SELECT COUNT(*) FROM embedding_vectors")), 3);
        QCOMPARE(fixture.count(QStringLiteral("SELECT COUNT(*) FROM embedding_sources")), 3);

        // Delete one: its source and its now-unused vector are removed.
        QVERIFY(QFile::remove(QDir(fixture.root()).filePath(QStringLiteral("two.md"))));
        fixture.scan(libraryId);
        indexer->sync(libraryId);
        QVERIFY(settle(*indexer));
        QCOMPARE(fixture.count(QStringLiteral("SELECT COUNT(*) FROM embedding_sources")), 2);
        QCOMPARE(fixture.count(QStringLiteral("SELECT COUNT(*) FROM embedding_vectors")), 2);
        QCOMPARE(fixture.backend->inputCount(), 4);

        // Changing the embedding configuration recomputes everything.
        EmbeddingConfig changed = testConfig();
        changed.inputVersion = 2;
        auto backend = fixture.backend;
        EmbeddingIndexer upgraded(fixture.database, [backend]() -> std::unique_ptr<EmbeddingClient> {
            return std::make_unique<FakeEmbeddingClient>(backend);
        }, fastPolicy(), changed);
        upgraded.sync(libraryId);
        QVERIFY(settle(upgraded));
        QCOMPARE(fixture.backend->inputCount(), 6);
        QCOMPARE(fixture.count(QStringLiteral("SELECT COUNT(*) FROM embedding_vectors WHERE input_version = 2")), 2);
        QCOMPARE(fixture.count(QStringLiteral("SELECT COUNT(*) FROM embedding_vectors WHERE input_version = 1")), 0);

        // Turning the Library off forgets its sources and vectors.
        upgraded.disable(libraryId);
        QVERIFY(settle(upgraded));
        QCOMPARE(fixture.count(QStringLiteral("SELECT COUNT(*) FROM embedding_sources")), 0);
        QCOMPARE(fixture.count(QStringLiteral("SELECT COUNT(*) FROM embedding_vectors")), 0);
        QVERIFY(!semanticAvailable(fixture.database, libraryId, changed));
    }

    void librariesStayIsolated()
    {
        Fixture fixture;
        fixture.open();
        const QString first = fixture.createLibrary(QStringLiteral("first"));
        const QString second = fixture.createLibrary(QStringLiteral("second"));
        writeFile(fixture.root(QStringLiteral("first")), QStringLiteral("ships.md"), QStringLiteral("Every ship needs an anchor.\n"));
        writeFile(fixture.root(QStringLiteral("second")), QStringLiteral("boats.md"), QStringLiteral("The boat sails past the harbour.\n"));
        fixture.scan(first);
        fixture.scan(second);
        auto indexer = fixture.indexer();
        indexer->enable(first);
        QVERIFY(settle(*indexer));
        QCOMPARE(fixture.count(QStringLiteral("SELECT COUNT(*) FROM embedding_sources")), 1); // only the opted-in Library

        indexer->enable(second);
        QVERIFY(settle(*indexer));

        const QVector<float> query = conceptVector(QStringLiteral("maritime vessel"));
        HybridPolicy policy;
        policy.minSimilarity = 0;
        const HybridResult result = searchLibrary(fixture.database, first, QStringLiteral("maritime vessel"), query, policy, 10, testConfig());
        QCOMPARE(result.diagnostics.value(QStringLiteral("mode")).toString(), QStringLiteral("hybrid"));
        QCOMPARE(result.hits.size(), 1);
        QCOMPARE(result.hits.first().libraryId, first);
        QCOMPARE(result.hits.first().relativePath, QStringLiteral("ships.md"));

        // The vector store itself only reads the scopes it is given.
        VectorQuery vectorQuery;
        vectorQuery.vector = query;
        vectorQuery.config = testConfig();
        vectorQuery.sourceType = SourceType::libraryChunk;
        vectorQuery.scopeType = ScopeType::library;
        vectorQuery.scopeIds = {second};
        const VectorSearchResult vectors = defaultVectorStore().search(fixture.database, vectorQuery);
        QCOMPARE(vectors.hits.size(), 1);
        QCOMPARE(vectors.hits.first().scopeId, second);
        vectorQuery.scopeIds.clear();
        QVERIFY(defaultVectorStore().search(fixture.database, vectorQuery).hits.isEmpty());
    }

    void hybridSearchFindsConceptsAndKeepsExactMatches()
    {
        Fixture fixture;
        fixture.open();
        const QString libraryId = fixture.createLibrary();
        writeFile(fixture.root(), QStringLiteral("harbour.md"), QStringLiteral("The ship rests at anchor in the harbour.\n"));
        writeFile(fixture.root(), QStringLiteral("budget.md"), QStringLiteral("Spending and cost review for the year.\n"));
        writeFile(fixture.root(), QStringLiteral("src/settings_loader.cpp"), QStringLiteral("Options parseConfig(const Path &path);\n"));
        writeFile(fixture.root(), QStringLiteral("notes.md"), QStringLiteral("Mentions settings and options and config a lot: config config options.\n"));
        fixture.scan(libraryId);
        auto indexer = fixture.indexer();
        indexer->enable(libraryId);
        QVERIFY(settle(*indexer));

        HybridPolicy policy;
        policy.minSimilarity = 0.5;

        // No shared keyword: FTS alone finds nothing, semantic search finds it.
        const QString conceptQuery = QStringLiteral("maritime vessel");
        QVERIFY(LibraryRetrieval::search(fixture.database, conceptQuery, libraryId, 8).value(QStringLiteral("candidates")).toList().isEmpty());
        const HybridResult conceptual = searchLibrary(fixture.database, libraryId, conceptQuery, conceptVector(conceptQuery), policy, 8, testConfig());
        QVERIFY(!conceptual.hits.isEmpty());
        QCOMPARE(conceptual.hits.first().relativePath, QStringLiteral("harbour.md"));
        QCOMPARE(conceptual.hits.first().lexicalRank, 0);
        QCOMPARE(conceptual.hits.first().semanticRank, 1);
        QVERIFY(conceptual.hits.first().reason.contains(QStringLiteral("semantic #1")));

        // An exact symbol beats chunks that are semantically closer.
        const QString symbolQuery = QStringLiteral("where is parseConfig defined for settings options");
        const HybridResult symbol = searchLibrary(fixture.database, libraryId, symbolQuery, conceptVector(symbolQuery), policy, 8, testConfig());
        QVERIFY(!symbol.hits.isEmpty());
        QCOMPARE(symbol.hits.first().relativePath, QStringLiteral("src/settings_loader.cpp"));
        QCOMPARE(symbol.hits.first().exact, ExactMatch::Identifier);
        // Keyword search alone misses it (its subject reranking skips code
        // paths for this question); hybrid search still surfaces it.
        QCOMPARE(symbol.hits.first().lexicalRank, 0);

        // An exact file name leads too.
        const QString fileQuery = QStringLiteral("open budget.md and the harbour ship notes");
        const HybridResult file = searchLibrary(fixture.database, libraryId, fileQuery, conceptVector(fileQuery), policy, 8, testConfig());
        QCOMPARE(file.hits.first().relativePath, QStringLiteral("budget.md"));
        QCOMPARE(file.hits.first().exact, ExactMatch::FileName);

        // Same inputs, same order.
        const HybridResult again = searchLibrary(fixture.database, libraryId, fileQuery, conceptVector(fileQuery), policy, 8, testConfig());
        QCOMPARE(again.hits.size(), file.hits.size());
        for (qsizetype index = 0; index < file.hits.size(); ++index) {
            QCOMPARE(again.hits.at(index).chunkId, file.hits.at(index).chunkId);
        }

        // Without a query vector it is keyword search, same shape.
        const HybridResult lexicalOnly = searchLibrary(fixture.database, libraryId, QStringLiteral("harbour"), {}, policy, 8, testConfig());
        QCOMPARE(lexicalOnly.diagnostics.value(QStringLiteral("mode")).toString(), QStringLiteral("lexical"));
        QCOMPARE(lexicalOnly.hits.first().relativePath, QStringLiteral("harbour.md"));

        QVERIFY(conceptual.diagnostics.value(QStringLiteral("vectorsScanned")).toInt() == 4);
        QVERIFY(conceptual.diagnostics.contains(QStringLiteral("vectorMs")));
    }

    void cancellationAndInterruptedJobsRecover()
    {
        Fixture fixture;
        fixture.open();
        const QString libraryId = fixture.createLibrary();
        writeFile(fixture.root(), QStringLiteral("a.md"), QStringLiteral("winter snow\n"));
        writeFile(fixture.root(), QStringLiteral("b.md"), QStringLiteral("garden flower\n"));
        fixture.scan(libraryId);

        // A request that never answers, then Cancel.
        fixture.backend->hang = true;
        auto indexer = fixture.indexer();
        indexer->enable(libraryId);
        QTRY_COMPARE(indexer->activeRequests(), 1);
        QCOMPARE(fixture.jobStatus(libraryId), EmbeddingIndex::JobStatus::running);
        indexer->cancel(libraryId);
        QVERIFY(settle(*indexer));
        QCOMPARE(fixture.jobStatus(libraryId), EmbeddingIndex::JobStatus::cancelled);
        QCOMPARE(fixture.count(QStringLiteral("SELECT COUNT(*) FROM embedding_vectors")), 0);
        // Cancelled stays stopped: a sync does not restart it.
        indexer->sync(libraryId);
        QVERIFY(settle(*indexer));
        QCOMPARE(fixture.jobStatus(libraryId), EmbeddingIndex::JobStatus::cancelled);

        // Resume starts again and finishes.
        fixture.backend->hang = false;
        indexer->resume(libraryId);
        QVERIFY(settle(*indexer));
        QCOMPARE(fixture.jobStatus(libraryId), EmbeddingIndex::JobStatus::completed);
        QCOMPARE(fixture.count(QStringLiteral("SELECT COUNT(*) FROM embedding_sources")), 2);

        // Pause mid-request keeps the job; resume continues it.
        writeFile(fixture.root(), QStringLiteral("c.md"), QStringLiteral("melody song\n"));
        fixture.scan(libraryId);
        fixture.backend->hang = true;
        indexer->sync(libraryId);
        QTRY_COMPARE(indexer->activeRequests(), 1);
        const QString pausedJob = fixture.job(libraryId).value(QStringLiteral("id")).toString();
        indexer->pause(libraryId);
        QCOMPARE(fixture.jobStatus(libraryId), EmbeddingIndex::JobStatus::paused);
        QCOMPARE(fixture.job(libraryId).value(QStringLiteral("pauseReason")).toString(), EmbeddingIndex::PauseReason::user);

        // The app quits here: a new indexer on the same database. A paused
        // job stays paused; a running one is re-queued.
        indexer.reset();
        EmbeddingIndex::updateJob(fixture.database, pausedJob, {{QStringLiteral("status"), EmbeddingIndex::JobStatus::running}});
        EmbeddingIndex::setLibraryPaused(fixture.database, libraryId, false);
        fixture.backend->hang = false;
        auto restarted = fixture.indexer();
        QCOMPARE(restarted->recover(), 1);
        QVERIFY(settle(*restarted));
        QCOMPARE(fixture.job(libraryId).value(QStringLiteral("id")).toString(), pausedJob);
        QCOMPARE(fixture.jobStatus(libraryId), EmbeddingIndex::JobStatus::completed);
        QCOMPARE(fixture.count(QStringLiteral("SELECT COUNT(*) FROM embedding_sources")), 3);
    }

    void failuresRetryPauseOrFailAndKeywordSearchKeepsWorking()
    {
        Fixture fixture;
        fixture.open();
        const QString libraryId = fixture.createLibrary();
        writeFile(fixture.root(), QStringLiteral("harbour.md"), QStringLiteral("The harbour at dawn.\n"));
        fixture.scan(libraryId);
        IndexerPolicy policy = fastPolicy();
        policy.maxAttempts = 3;
        auto indexer = fixture.indexer(policy);

        // Offline twice, then back: retries with backoff and completes.
        fixture.backend->script = {EmbeddingResult::Error::Network, EmbeddingResult::Error::RateLimited};
        indexer->enable(libraryId);
        QTRY_COMPARE_WITH_TIMEOUT(fixture.jobStatus(libraryId), EmbeddingIndex::JobStatus::completed, 5000);
        QCOMPARE(fixture.backend->calls.size(), 3);
        QCOMPARE(fixture.job(libraryId).value(QStringLiteral("attemptCount")).toInt(), 0);

        // Keyword search never depended on any of it.
        QVERIFY(!LibraryRetrieval::search(fixture.database, QStringLiteral("harbour"), libraryId, 8).value(QStringLiteral("candidates")).toList().isEmpty());

        // Persistent outage: fails after maxAttempts, with the error kept.
        writeFile(fixture.root(), QStringLiteral("harbour.md"), QStringLiteral("The harbour at dusk.\n"));
        fixture.scan(libraryId);
        fixture.backend->script = {EmbeddingResult::Error::Server, EmbeddingResult::Error::Server, EmbeddingResult::Error::Server};
        indexer->sync(libraryId);
        QTRY_COMPARE_WITH_TIMEOUT(fixture.jobStatus(libraryId), EmbeddingIndex::JobStatus::failed, 5000);
        QCOMPARE(fixture.job(libraryId).value(QStringLiteral("errorMessage")).toString(), QStringLiteral("scripted failure"));
        QCOMPARE(EmbeddingIndex::libraryStatus(fixture.database, libraryId, testConfig()).value(QStringLiteral("state")).toString(), QStringLiteral("failed"));
        // The periodic sync leaves a fresh failure alone.
        indexer->sync(libraryId);
        QCOMPARE(fixture.jobStatus(libraryId), EmbeddingIndex::JobStatus::failed);
        // The changed text is still found by keyword search meanwhile.
        QVERIFY(!LibraryRetrieval::search(fixture.database, QStringLiteral("dusk"), libraryId, 8).value(QStringLiteral("candidates")).toList().isEmpty());

        // A refused key pauses instead of retrying.
        fixture.backend->script = {EmbeddingResult::Error::Unauthorized};
        indexer->resume(libraryId);
        QTRY_COMPARE_WITH_TIMEOUT(fixture.jobStatus(libraryId), EmbeddingIndex::JobStatus::paused, 5000);
        QCOMPARE(fixture.job(libraryId).value(QStringLiteral("pauseReason")).toString(), EmbeddingIndex::PauseReason::unauthorized);

        // No key: paused as not configured; resume after a key works.
        fixture.backend->script = {EmbeddingResult::Error::NotConfigured};
        indexer->resume(libraryId);
        QTRY_COMPARE_WITH_TIMEOUT(fixture.job(libraryId).value(QStringLiteral("pauseReason")).toString(), EmbeddingIndex::PauseReason::notConfigured, 5000);
        indexer->resume(libraryId);
        QTRY_COMPARE_WITH_TIMEOUT(fixture.jobStatus(libraryId), EmbeddingIndex::JobStatus::completed, 5000);
    }

    void spendingLimitsPauseBeforeSending()
    {
        Fixture fixture;
        fixture.open();
        const QString libraryId = fixture.createLibrary();
        writeFile(fixture.root(), QStringLiteral("a.md"), QStringLiteral("winter snow frost\n"));
        fixture.scan(libraryId);
        IndexerPolicy policy = fastPolicy();
        policy.dailyTokenBudget = 1; // less than any chunk
        auto indexer = fixture.indexer(policy);
        indexer->enable(libraryId);
        QVERIFY(settle(*indexer));
        QCOMPARE(fixture.backend->calls.size(), 0);
        QCOMPARE(fixture.jobStatus(libraryId), EmbeddingIndex::JobStatus::paused);
        QCOMPARE(fixture.job(libraryId).value(QStringLiteral("pauseReason")).toString(), EmbeddingIndex::PauseReason::budget);

        IndexerPolicy perJob = fastPolicy();
        perJob.maxJobTokens = 1;
        auto limited = fixture.indexer(perJob);
        limited->resume(libraryId);
        QVERIFY(settle(*limited));
        QCOMPARE(fixture.backend->calls.size(), 0);
        QVERIFY(fixture.job(libraryId).value(QStringLiteral("errorMessage")).toString().contains(QStringLiteral("limit")));
    }

    void hybridEvidenceFitsContextAssemblyV3()
    {
        Fixture fixture;
        fixture.open();
        const QString libraryId = fixture.createLibrary();
        const QString otherId = fixture.createLibrary(QStringLiteral("other"));
        writeFile(fixture.root(), QStringLiteral("harbour.md"), QStringLiteral("The ship rests at anchor in the harbour.\n"));
        writeFile(fixture.root(), QStringLiteral("budget.md"), QStringLiteral("Spending and cost review.\n"));
        writeFile(fixture.root(QStringLiteral("other")), QStringLiteral("boats.md"), QStringLiteral("Boat and sail.\n"));
        fixture.scan(libraryId);
        fixture.scan(otherId);
        auto indexer = fixture.indexer();
        indexer->enable(libraryId);
        indexer->enable(otherId);
        QVERIFY(settle(*indexer));

        // The adapter embeds with the app's 1536-d config, so the 8-d test
        // vectors go through searchLibrary directly here.
        HybridPolicy policy;
        policy.minSimilarity = 0.5;
        const HybridResult search = searchLibrary(fixture.database, libraryId, QStringLiteral("maritime vessel"), conceptVector(QStringLiteral("maritime vessel")), policy, 8, testConfig());
        QVERIFY(!search.hits.isEmpty());

        // Keyword-only through the adapter: identical refs to libraryEvidence.
        const ContextSources::Retrieval keyword = ContextSources::hybridLibraryEvidence(fixture.database, libraryId, QStringLiteral("harbour"), {}, policy, 8);
        const ContextSources::Retrieval legacy = ContextSources::libraryEvidence(fixture.database, libraryId, QStringLiteral("harbour"), 8);
        QCOMPARE(keyword.candidates.size(), legacy.candidates.size());
        QCOMPARE(keyword.candidates.first().ref.key(), legacy.candidates.first().ref.key());
        QCOMPARE(keyword.candidates.first().ref.label(), legacy.candidates.first().ref.label());
        QCOMPARE(keyword.candidates.first().ref.producer, QStringLiteral("library-file-fts"));
        QCOMPARE(keyword.diagnostics.value(QStringLiteral("mode")).toString(), QStringLiteral("lexical"));

        // Hybrid candidates through the assembler: cited, untrusted, scoped.
        QList<Context::SearchCandidate> candidates;
        for (const HybridHit &hit : search.hits) {
            Context::SearchCandidate candidate;
            candidate.kind = Context::Kind::LibraryEvidence;
            candidate.content = hit.content;
            candidate.score = hit.score;
            candidate.reason = hit.reason;
            candidate.ref.sourceType = QStringLiteral("library-chunk");
            candidate.ref.sourceId = hit.chunkId;
            candidate.ref.libraryId = hit.libraryId;
            candidate.ref.fileId = hit.fileId;
            candidate.ref.relativePath = hit.relativePath;
            candidate.ref.startLine = hit.startLine;
            candidate.ref.endLine = hit.endLine;
            candidate.ref.producer = QStringLiteral("library-semantic");
            candidates.append(candidate);
        }
        // A candidate from another Library is kept out by the access scope.
        Context::SearchCandidate foreign = candidates.first();
        foreign.ref.sourceId = QStringLiteral("foreign-chunk");
        foreign.ref.libraryId = otherId;
        foreign.content = QStringLiteral("Boat and sail.");
        candidates.append(foreign);

        Context::ContextRequest request;
        request.chatId = QStringLiteral("chat");
        request.instructions = QStringLiteral("You are Archivist.");
        request.currentRequest.id = QStringLiteral("current");
        request.currentRequest.kind = Context::Kind::CurrentRequest;
        request.currentRequest.trust = Context::Trust::UserIntent;
        request.currentRequest.role = QStringLiteral("user");
        request.currentRequest.content = QStringLiteral("Tell me about the maritime vessel.");
        request.currentRequest.required = true;
        request.candidates = candidates;
        request.policy = Context::policyFromConfig({{QStringLiteral("totalTokens"), 8000}, {QStringLiteral("responseTokenReserve"), 1000}});
        request.scope.restricted = true;
        request.scope.libraryIds = {libraryId};
        const Context::CompiledContext compiled = Context::assemble(request, Context::CharacterEstimator());
        QVERIFY(compiled.includedCount(Context::Kind::LibraryEvidence) >= 1);

        bool sawForeignOmitted = false;
        bool sawHarbourIncluded = false;
        for (const Context::Outcome &outcome : compiled.outcomes) {
            if (outcome.item.ref.sourceId == QStringLiteral("foreign-chunk")) {
                sawForeignOmitted = !outcome.included && outcome.reason == Context::Omission::outOfScope;
            }
            if (outcome.included && outcome.item.ref.relativePath == QStringLiteral("harbour.md")) {
                sawHarbourIncluded = true;
                QCOMPARE(outcome.item.trust, Context::Trust::Untrusted);
                QCOMPARE(outcome.item.ref.producer, QStringLiteral("library-semantic"));
            }
        }
        QVERIFY(sawForeignOmitted);
        QVERIFY(sawHarbourIncluded);

        // Reference material is quoted user-role data, with its citation.
        const QString envelope = compiled.messages.at(compiled.messages.size() - 2).content;
        QVERIFY(envelope.contains(QStringLiteral("archivist-reference-material")));
        QVERIFY(envelope.contains(QStringLiteral("harbour.md")));
        QVERIFY(!envelope.contains(QStringLiteral("Boat and sail")));
    }

    // The app-facing service: its own thread, calls that return at once,
    // status pushed back. Without an OpenAI key the job waits, paused.
    void serviceIndexesOnItsOwnThread()
    {
        qunsetenv("OPENAI_API_KEY");
        Fixture fixture;
        fixture.open();
        const QString libraryId = fixture.createLibrary();
        writeFile(fixture.root(), QStringLiteral("a.md"), QStringLiteral("winter snow\n"));
        fixture.scan(libraryId);
        fixture.database.close();

        SemanticIndexService service;
        QVERIFY(service.start(fixture.directory.filePath(QStringLiteral("retrieval.db"))));
        QVERIFY(service.ready());
        QSignalSpy changed(&service, &SemanticIndexService::statusChanged);
        (void)service.status(libraryId);
        QTRY_VERIFY(!service.status(libraryId).isEmpty());
        QCOMPARE(service.status(libraryId).value(QStringLiteral("state")).toString(), QStringLiteral("off"));
        QCOMPARE(service.status(libraryId).value(QStringLiteral("pending")).toInt(), 1);

        service.enable(libraryId);
        QTRY_COMPARE_WITH_TIMEOUT(service.status(libraryId).value(QStringLiteral("state")).toString(), QStringLiteral("paused"), 5000);
        const QVariantMap job = service.status(libraryId).value(QStringLiteral("job")).toMap();
        QCOMPARE(job.value(QStringLiteral("pauseReason")).toString(), EmbeddingIndex::PauseReason::notConfigured);
        QCOMPARE(service.status(libraryId).value(QStringLiteral("keyConfigured")).toBool(), false);
        QVERIFY(changed.size() >= 2);

        service.disable(libraryId);
        QTRY_COMPARE_WITH_TIMEOUT(service.status(libraryId).value(QStringLiteral("state")).toString(), QStringLiteral("off"), 5000);
        service.stop();
        QVERIFY(!service.ready());
    }

    // Not a pass/fail check: how long a bounded exact scan takes, to decide
    // when an ANN index or vector extension is worth it. Prints numbers.
    void exactScanLatency()
    {
        QTemporaryDir directory;
        CoreDatabase database;
        database.open(directory.filePath(QStringLiteral("scan.db")));
        database.runMigrations();
        EmbeddingConfig config;
        const int count = qEnvironmentVariableIntValue("ARCHIVIST_VECTOR_BENCH_COUNT") > 0 ? qEnvironmentVariableIntValue("ARCHIVIST_VECTOR_BENCH_COUNT") : 5000;
        QRandomGenerator random(42);
        database.transaction([&]() {
            for (int index = 0; index < count; ++index) {
                QVector<float> vector(config.dimensions);
                for (float &value : vector) {
                    value = static_cast<float>(random.generateDouble() - 0.5);
                }
                const QString hash = QStringLiteral("hash-%1").arg(index);
                EmbeddingIndex::storeVector(database, hash, config, VectorCodec::normalized(vector), 0);
                EmbeddingIndex::SourceText source;
                source.sourceType = SourceType::libraryChunk;
                source.sourceId = QStringLiteral("chunk-%1").arg(index);
                source.scopeType = ScopeType::library;
                source.scopeId = QStringLiteral("bench");
                source.contentHash = hash;
                EmbeddingIndex::upsertSource(database, source);
            }
        });

        VectorQuery query;
        query.vector = VectorCodec::normalized(QVector<float>(config.dimensions, 1.0f));
        query.config = config;
        query.sourceType = SourceType::libraryChunk;
        query.scopeType = ScopeType::library;
        query.scopeIds = {QStringLiteral("bench")};
        query.limit = 24;
        VectorSearchResult result;
        double best = 1e9;
        for (int run = 0; run < 3; ++run) {
            result = defaultVectorStore().search(database, query);
            best = std::min(best, result.durationMs);
        }
        QCOMPARE(result.scanned, count);
        QCOMPARE(result.hits.size(), 24);
        QVERIFY(result.hits.first().similarity >= result.hits.last().similarity);
        qInfo().noquote() << QStringLiteral("[bench] exact scan: %1 vectors x %2 dims, %3 MB read, best of 3: %4 ms")
                                 .arg(count)
                                 .arg(config.dimensions)
                                 .arg(result.bytesScanned / 1048576.0, 0, 'f', 1)
                                 .arg(best, 0, 'f', 1);

        query.maxScanned = 100;
        const VectorSearchResult bounded = defaultVectorStore().search(database, query);
        QCOMPARE(bounded.scanned, 100);
        QVERIFY(bounded.truncated);
    }
};

QTEST_GUILESS_MAIN(CoreRetrievalTest)
#include "core_retrieval_test.moc"

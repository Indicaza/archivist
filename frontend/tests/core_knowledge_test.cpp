#include "Core/Database/core_database.h"
#include "Core/Domains/AI/context_compilers.h"
#include "Core/Domains/Context/context_assembler.h"
#include "Core/Domains/Context/context_sources.h"
#include "Core/Domains/Knowledge/knowledge_generation.h"
#include "Core/Domains/Knowledge/knowledge_jobs.h"
#include "Core/Domains/Knowledge/knowledge_model.h"
#include "Core/Domains/Knowledge/knowledge_proposals.h"
#include "Core/Domains/Knowledge/knowledge_repository.h"
#include "Core/Domains/Knowledge/knowledge_retrieval.h"
#include "Core/Domains/Knowledge/knowledge_service.h"
#include "Core/Domains/Knowledge/knowledge_sources.h"
#include "Core/Domains/Knowledge/knowledge_text.h"
#include "Core/Domains/Knowledge/knowledge_types.h"
#include "Core/Domains/Libraries/library_file_repository.h"
#include "Core/Domains/Libraries/library_files.h"
#include "Core/Domains/Libraries/library_repository.h"
#include "Core/Domains/Retrieval/embedding_client.h"
#include "Core/Domains/Retrieval/embedding_index.h"
#include "Core/Domains/Retrieval/embedding_indexer.h"
#include "Core/Domains/Retrieval/vector_codec.h"
#include "Core/Support/json_values.h"
#include "Core/core_error.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

#include <memory>

using namespace Knowledge;
using KnowledgeRepository::ActorRef;

namespace
{
const QString releaseDoc = QStringLiteral(
    "# Release process\n"
    "\n"
    "Releases ship every second Tuesday.\n"
    "The release manager signs the build with the hardware key.\n"
    "\n"
    "A rollback restores the previous build within ten minutes.\n"
);

const QString budgetDoc = QStringLiteral(
    "Budget notes\n"
    "The project budget for 2026 is forty thousand euros.\n"
    "Spending is reviewed monthly by the finance group.\n"
);

// ---- Canned model ----------------------------------------------------------

struct Script
{
    std::function<ModelResult(const ModelRequest &)> respond;
    QList<ModelRequest> requests;
    bool hang = false;
};

class FakeModel final : public QObject, public KnowledgeModel
{
public:
    explicit FakeModel(std::shared_ptr<Script> script)
        : m_script(std::move(script))
    {
    }

    void generate(const ModelRequest &request, Callback done) override
    {
        m_done = std::move(done);
        m_script->requests.append(request);

        if (m_script->hang) {
            return;
        }

        const ModelResult result = m_script->respond(request);
        QTimer::singleShot(0, this, [this, result]() { deliver(result); });
    }

    void cancel() override
    {
        ModelResult result;
        result.error = ModelResult::Error::Cancelled;
        deliver(result);
    }

    bool busy() const override { return static_cast<bool>(m_done); }

private:
    void deliver(const ModelResult &result)
    {
        if (Callback done = std::exchange(m_done, nullptr)) {
            done(result);
        }
    }

    std::shared_ptr<Script> m_script;
    Callback m_done;
};

ModelResult reply(const QJsonObject &json)
{
    ModelResult result;
    result.text = QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact));
    result.model = QStringLiteral("gpt-test");
    return result;
}

ModelResult failure(ModelResult::Error error, const QString &message = QStringLiteral("scripted failure"))
{
    ModelResult result;
    result.error = error;
    result.message = message;
    return result;
}

// The passage label (P1, P2, ...) the prompt gave a file.
QString label(const QString &input, const QString &path)
{
    const QRegularExpression pattern(QStringLiteral("<passage id=\"(P\\d+)\" path=\"%1\"").arg(QRegularExpression::escape(path)));
    return pattern.match(input).captured(1);
}

QJsonObject claim(const QString &ref, const QString &type, const QString &statement, double confidence, const QJsonArray &citations)
{
    return {
        {QStringLiteral("ref"), ref},
        {QStringLiteral("type"), type},
        {QStringLiteral("statement"), statement},
        {QStringLiteral("confidence"), confidence},
        {QStringLiteral("citations"), citations},
    };
}

QJsonObject cite(const QString &passage, const QString &quote)
{
    return {{QStringLiteral("passage"), passage}, {QStringLiteral("quote"), quote}};
}

QJsonObject page(const QString &title, const QString &content, const QJsonArray &claims, const QJsonArray &relations = {})
{
    return {
        {QStringLiteral("title"), title},
        {QStringLiteral("pageType"), QStringLiteral("topic")},
        {QStringLiteral("explanation"), QStringLiteral("Summarizes the release notes.")},
        {QStringLiteral("content"), content},
        {QStringLiteral("claims"), claims},
        {QStringLiteral("relations"), relations},
    };
}

// The standard "Release process" page, citing whatever label the prompt
// gave docs/release.md.
ModelResult releasePage(const ModelRequest &request)
{
    const QString p = label(request.input, QStringLiteral("docs/release.md"));
    return reply(page(QStringLiteral("Release process"), QStringLiteral("Releases ship fortnightly [P1]."), {
        claim(QStringLiteral("c1"), ClaimType::fact, QStringLiteral("Releases ship every second Tuesday."), 0.9, {cite(p, QStringLiteral("Releases ship every second Tuesday."))}),
        claim(QStringLiteral("c2"), ClaimType::fact, QStringLiteral("A rollback takes at most ten minutes."), 0.8, {cite(p, QStringLiteral("restores the previous build within ten minutes"))}),
        claim(QStringLiteral("c3"), ClaimType::question, QStringLiteral("Who holds the hardware key during holidays?"), 0.5, {}),
    }));
}

// ---- Fake embeddings (same idea as core_retrieval_test) --------------------

QVector<float> conceptVector(const QString &text)
{
    static const QList<QStringList> axes{
        {QStringLiteral("release"), QStringLiteral("ship"), QStringLiteral("deploy"), QStringLiteral("launch")},
        {QStringLiteral("budget"), QStringLiteral("money"), QStringLiteral("spending"), QStringLiteral("euros")},
        {QStringLiteral("rollback"), QStringLiteral("revert"), QStringLiteral("undo")},
    };
    QVector<float> vector(8, 0.0f);
    const QString lower = text.toLower();

    for (int axis = 0; axis < axes.size(); ++axis) {
        for (const QString &word : axes.at(axis)) {
            vector[axis] += static_cast<float>(lower.count(word));
        }
    }

    vector[7] = 0.05f;
    return VectorCodec::normalized(vector);
}

class FakeEmbeddings final : public QObject, public Retrieval::EmbeddingClient
{
public:
    void embed(const QStringList &inputs, const Retrieval::EmbeddingConfig &config, Callback done) override
    {
        m_done = std::move(done);
        QTimer::singleShot(0, this, [this, inputs, config]() {
            Retrieval::EmbeddingResult result;

            for (const QString &input : inputs) {
                QVector<float> vector = conceptVector(input);
                vector.resize(config.dimensions);
                result.vectors.append(VectorCodec::normalized(vector));
            }

            result.promptTokens = static_cast<int>(inputs.size()) * 5;

            if (Callback done = std::exchange(m_done, nullptr)) {
                done(result);
            }
        });
    }

    void cancel() override {}
    bool busy() const override { return static_cast<bool>(m_done); }

private:
    Callback m_done;
};

Retrieval::EmbeddingConfig testEmbeddingConfig()
{
    Retrieval::EmbeddingConfig config;
    config.dimensions = 8;
    return config;
}

// ---- Fixture -----------------------------------------------------------------

struct Fixture
{
    QTemporaryDir directory;
    CoreDatabase database;
    std::shared_ptr<Script> script = std::make_shared<Script>();
    std::unique_ptr<KnowledgeJobRunner> runner;

    Fixture()
    {
        database.open(directory.filePath(QStringLiteral("knowledge.db")));
        database.runMigrations();
        script->respond = releasePage;
    }

    QString root(const QString &name) const { return directory.filePath(name); }

    QString createLibrary(const QString &name = QStringLiteral("library"))
    {
        QDir().mkpath(root(name));
        return LibraryRepository::createLibrary(database, {{QStringLiteral("rootPath"), root(name)}})
            .value(QStringLiteral("library"))
            .toMap()
            .value(QStringLiteral("id"))
            .toString();
    }

    void write(const QString &library, const QString &relativePath, const QString &content)
    {
        const QString path = QDir(root(library)).filePath(relativePath);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(content.toUtf8());
        file.close();
        // Make the change visible to a scan that compares mtime and size.
        QFile touched(path);
        if (touched.open(QIODevice::ReadWrite)) {
            touched.setFileTime(QDateTime::currentDateTimeUtc().addSecs(++m_tick), QFileDevice::FileModificationTime);
        }
    }

    void scan(const QString &libraryId) { (void)LibraryFiles::scan(database, libraryId); }

    // A Library with the two standard files, scanned.
    QString standardLibrary(const QString &name = QStringLiteral("library"))
    {
        const QString id = createLibrary(name);
        write(name, QStringLiteral("docs/release.md"), releaseDoc);
        write(name, QStringLiteral("notes/budget.txt"), budgetDoc);
        scan(id);
        return id;
    }

    QString fileId(const QString &libraryId, const QString &relativePath)
    {
        return LibraryFileRepository::fileByRelativePath(database, libraryId, relativePath).value_or(QVariantMap()).value(QStringLiteral("id")).toString();
    }

    KnowledgeJobRunner &startRunner(RunnerPolicy policy = fastPolicy())
    {
        auto copy = script;
        runner = std::make_unique<KnowledgeJobRunner>(database, [copy]() -> std::unique_ptr<KnowledgeModel> { return std::make_unique<FakeModel>(copy); }, policy);
        runner->start();
        return *runner;
    }

    static RunnerPolicy fastPolicy()
    {
        RunnerPolicy policy;
        policy.baseBackoffMs = 5;
        policy.maxBackoffMs = 20;
        policy.maxAttempts = 3;
        return policy;
    }

    bool settle(int timeoutMs = 5000)
    {
        QElapsedTimer timer;
        timer.start();

        while (timer.elapsed() < timeoutMs) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);

            if (runner->idle()) {
                QCoreApplication::processEvents();

                if (runner->idle()) {
                    return true;
                }
            }
        }

        return false;
    }

    QVariantMap runJob(const QString &libraryId, const QString &jobType, const QVariantMap &request)
    {
        if (!runner) {
            startRunner();
        }

        const QVariantMap job = database.transaction([&]() { return KnowledgeJobs::enqueue(database, libraryId, jobType, request); });
        runner->poke();
        settle();
        return KnowledgeJobs::job(database, job.value(QStringLiteral("id")).toString()).value_or(QVariantMap());
    }

    // Generates and approves the release page. Returns its id.
    QString approvedReleasePage(const QString &libraryId)
    {
        const QVariantMap job = runJob(libraryId, JobType::generatePage, {{QStringLiteral("topic"), QStringLiteral("Release process")}});
        const QString proposalId = job.value(QStringLiteral("result")).toMap().value(QStringLiteral("proposalId")).toString();
        const QVariantMap applied = KnowledgeProposals::approve(database, proposalId, QStringLiteral("tester"));
        return applied.value(QStringLiteral("pageId")).toString();
    }

    int count(const QString &sql) { return database.prepare(sql).scalar().toInt(); }

    QVariantMap claimByStatement(const QString &pageId, const QString &statement)
    {
        for (const QVariant &value : KnowledgeRepository::claims(database, pageId, true)) {
            if (value.toMap().value(QStringLiteral("statement")).toString() == statement) {
                return value.toMap();
            }
        }

        return {};
    }

private:
    int m_tick = 0;
};
}

// The Living Wiki's Knowledge Store: persistence, the generation and review
// pipeline, source reconciliation, relationships, revisions, retrieval and
// the job queue. Every model reply is canned; nothing reaches the network.
class CoreKnowledgeTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("ArchivistTest"));
        QCoreApplication::setApplicationName(QStringLiteral("core-knowledge-test"));
    }

    void textHelpers()
    {
        QCOMPARE(KnowledgeText::slugify(QStringLiteral("  Release Process: v2!  ")), QStringLiteral("release-process-v2"));
        QCOMPARE(KnowledgeText::slugify(QStringLiteral("!!!")), QStringLiteral("page"));
        QCOMPARE(KnowledgeText::statementKey(QStringLiteral("Releases ship  on Tuesday.")), KnowledgeText::statementKey(QStringLiteral("releases ship on tuesday")));
        const auto match = KnowledgeText::locateQuote(QStringLiteral("alpha\nbeta gamma\n  delta"), QStringLiteral("gamma delta"), 10);
        QVERIFY(match.found);
        QCOMPARE(match.startLine, 11);
        QCOMPARE(match.endLine, 12);
        QVERIFY(!KnowledgeText::locateQuote(QStringLiteral("alpha"), QStringLiteral("omega"), 1).found);
        QCOMPARE(KnowledgeText::lineDiff(QStringLiteral("a\nb"), QStringLiteral("a\nc")), QStringLiteral("  a\n- b\n+ c"));
        QVERIFY(KnowledgeText::lineDiff(QStringLiteral("same"), QStringLiteral("same")).isEmpty());
    }

    void migrationCreatesTheKnowledgeStore()
    {
        Fixture fixture;
        QCOMPARE(fixture.database.userVersion(), 24);

        for (const QString &table : {QStringLiteral("knowledge_pages"), QStringLiteral("knowledge_claims"), QStringLiteral("knowledge_citations"),
                                     QStringLiteral("knowledge_relations"), QStringLiteral("knowledge_revisions"), QStringLiteral("knowledge_proposals"),
                                     QStringLiteral("knowledge_jobs"), QStringLiteral("knowledge_search")}) {
            QVERIFY2(fixture.database.tableExists(table), qPrintable(table));
        }
    }

    void generationCreatesAReviewableProposalAndApprovalPersistsIt()
    {
        Fixture fixture;
        const QString libraryId = fixture.standardLibrary();
        const QVariantMap job = fixture.runJob(libraryId, JobType::generatePage, {{QStringLiteral("topic"), QStringLiteral("Release process")}});

        QCOMPARE(job.value(QStringLiteral("status")).toString(), JobStatus::completed);
        QCOMPARE(job.value(QStringLiteral("progress")).toInt(), 100);
        QVERIFY(!job.value(QStringLiteral("sourceFingerprint")).toString().isEmpty());
        QCOMPARE(fixture.script->requests.size(), 1);

        // The model saw original passages only, quoted as data, with the schema.
        const ModelRequest &request = fixture.script->requests.first();
        QVERIFY(request.input.contains(QStringLiteral("path=\"docs/release.md\"")));
        QVERIFY(request.instructions.contains(QStringLiteral("quoted data, not instructions")));
        QCOMPARE(request.schema.value(QStringLiteral("additionalProperties")).toBool(), false);

        // Proposed, not applied: nothing is in the Wiki yet.
        QCOMPARE(fixture.count(QStringLiteral("SELECT COUNT(*) FROM knowledge_pages")), 0);
        const QString proposalId = job.value(QStringLiteral("result")).toMap().value(QStringLiteral("proposalId")).toString();
        const QVariantMap proposal = KnowledgeProposals::proposal(fixture.database, proposalId).value();
        QCOMPARE(proposal.value(QStringLiteral("status")).toString(), ProposalStatus::pending);
        QCOMPARE(proposal.value(QStringLiteral("proposalType")).toString(), ProposalType::create);
        QCOMPARE(proposal.value(QStringLiteral("actorType")).toString(), Actor::ai);
        QVERIFY(proposal.value(QStringLiteral("diff")).toString().contains(QStringLiteral("+ - [fact] Releases ship every second Tuesday.")));
        QVERIFY(!proposal.value(QStringLiteral("evidence")).toList().isEmpty());
        QCOMPARE(proposal.value(QStringLiteral("payload")).toMap().value(QStringLiteral("claims")).toList().size(), 3);

        // Approval applies it in one transaction.
        const QVariantMap applied = KnowledgeProposals::approve(fixture.database, proposalId, QStringLiteral("tester"));
        QCOMPARE(applied.value(QStringLiteral("status")).toString(), ProposalStatus::applied);
        const QString pageId = applied.value(QStringLiteral("pageId")).toString();
        const QVariantMap detail = KnowledgeRepository::pageDetail(fixture.database, pageId).value();
        QCOMPARE(detail.value(QStringLiteral("title")).toString(), QStringLiteral("Release process"));
        QCOMPARE(detail.value(QStringLiteral("virtualPath")).toString(), QStringLiteral("Wiki/release-process.md"));
        QCOMPARE(detail.value(QStringLiteral("revision")).toInt(), 1);
        QCOMPARE(detail.value(QStringLiteral("origin")).toString(), Origin::ai);
        QCOMPARE(detail.value(QStringLiteral("claims")).toList().size(), 3);

        const QVariantMap fact = fixture.claimByStatement(pageId, QStringLiteral("Releases ship every second Tuesday."));
        QCOMPARE(fact.value(QStringLiteral("verificationStatus")).toString(), Verification::supported);
        QCOMPARE(fact.value(QStringLiteral("confidence")).toDouble(), 0.6);
        const QVariantList citations = KnowledgeRepository::citations(fixture.database, fact.value(QStringLiteral("id")).toString());
        QCOMPARE(citations.size(), 1);
        const QVariantMap citation = citations.first().toMap();
        QCOMPARE(citation.value(QStringLiteral("relativePath")).toString(), QStringLiteral("docs/release.md"));
        QCOMPARE(citation.value(QStringLiteral("startLine")).toInt(), 3);
        QCOMPARE(citation.value(QStringLiteral("endLine")).toInt(), 3);
        QCOMPARE(citation.value(QStringLiteral("fileId")).toString(), fixture.fileId(libraryId, QStringLiteral("docs/release.md")));
        QCOMPARE(citation.value(QStringLiteral("producerJobId")).toString(), job.value(QStringLiteral("id")).toString());
        QCOMPARE(citation.value(QStringLiteral("status")).toString(), CitationStatus::valid);

        // The open question stands without a source, at low confidence.
        const QVariantMap question = fixture.claimByStatement(pageId, QStringLiteral("Who holds the hardware key during holidays?"));
        QCOMPARE(question.value(QStringLiteral("verificationStatus")).toString(), Verification::unverified);
        QCOMPARE(question.value(QStringLiteral("confidence")).toDouble(), 0.3);

        // Revision 1 captured the claims; the proposal is applied, not pending.
        const QVariantList revisions = detail.value(QStringLiteral("revisions")).toList();
        QCOMPARE(revisions.size(), 1);
        const QVariantMap first = KnowledgeRepository::revision(fixture.database, revisions.first().toMap().value(QStringLiteral("id")).toString()).value();
        QCOMPARE(first.value(QStringLiteral("claims")).toList().size(), 3);
        QCOMPARE(first.value(QStringLiteral("actorType")).toString(), Actor::ai);
        QVERIFY(first.value(QStringLiteral("note")).toString().contains(QStringLiteral("tester")));
        QCOMPARE(KnowledgeProposals::proposal(fixture.database, proposalId)->value(QStringLiteral("appliedRevision")).toInt(), 1);

        // Nothing was written into the Library folder.
        QVERIFY(!QDir(fixture.root(QStringLiteral("library"))).exists(QStringLiteral("Wiki")));
        QCOMPARE(QDir(fixture.root(QStringLiteral("library"))).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).size(), 2);

        // A proposal is applied once.
        QVERIFY_THROWS_EXCEPTION(CoreError, KnowledgeProposals::approve(fixture.database, proposalId, QStringLiteral("tester")));
    }

    void citationsAreValidatedAgainstTheProvidedSources()
    {
        Fixture fixture;
        const QString libraryId = fixture.standardLibrary();
        const QString otherId = fixture.standardLibrary(QStringLiteral("other"));
        fixture.script->respond = [](const ModelRequest &request) {
            const QString release = label(request.input, QStringLiteral("docs/release.md"));
            return reply(page(QStringLiteral("Release process"), QStringLiteral("Body."), {
                // Valid.
                claim(QStringLiteral("a"), ClaimType::fact, QStringLiteral("Releases ship every second Tuesday."), 0.9, {cite(release, QStringLiteral("ship every second Tuesday"))}),
                // Quote not in the passage: citation dropped, fact dropped.
                claim(QStringLiteral("b"), ClaimType::fact, QStringLiteral("Releases ship daily."), 0.9, {cite(release, QStringLiteral("Releases ship daily"))}),
                // Unknown passage label.
                claim(QStringLiteral("c"), ClaimType::decision, QStringLiteral("The team chose Tuesdays."), 0.9, {cite(QStringLiteral("P99"), QStringLiteral("Tuesday"))}),
                // No citation at all.
                claim(QStringLiteral("d"), ClaimType::inference, QStringLiteral("The cadence suits the team."), 0.9, {}),
                // One good and one bad citation: kept with the good one.
                claim(QStringLiteral("e"), ClaimType::fact, QStringLiteral("The release manager signs builds."), 0.9,
                      {cite(release, QStringLiteral("signs the build with the hardware key")), cite(release, QStringLiteral("signs with a pen"))}),
                // Unresolved assumption: allowed without a source.
                claim(QStringLiteral("f"), ClaimType::assumption, QStringLiteral("The hardware key is kept in the office."), 0.4, {}),
                // Bad type and duplicate statement.
                claim(QStringLiteral("g"), QStringLiteral("rumour"), QStringLiteral("Something."), 0.9, {}),
                claim(QStringLiteral("h"), ClaimType::fact, QStringLiteral("releases ship every second tuesday"), 0.9, {cite(release, QStringLiteral("Releases ship"))}),
            }));
        };

        const QVariantMap job = fixture.runJob(libraryId, JobType::generatePage, {{QStringLiteral("topic"), QStringLiteral("Release process")}});
        QCOMPARE(job.value(QStringLiteral("status")).toString(), JobStatus::completed);
        const QVariantMap proposal = KnowledgeProposals::proposal(fixture.database, job.value(QStringLiteral("result")).toMap().value(QStringLiteral("proposalId")).toString()).value();
        const QVariantMap validation = proposal.value(QStringLiteral("validation")).toMap();
        QStringList kept;

        for (const QVariant &value : proposal.value(QStringLiteral("payload")).toMap().value(QStringLiteral("claims")).toList()) {
            kept.append(value.toMap().value(QStringLiteral("ref")).toString());
        }

        QCOMPARE(kept, (QStringList{QStringLiteral("a"), QStringLiteral("e"), QStringLiteral("f")}));
        QCOMPARE(validation.value(QStringLiteral("droppedClaims")).toList().size(), 5);
        QCOMPARE(validation.value(QStringLiteral("droppedCitations")).toList().size(), 3);
        const QVariantMap kept2 = proposal.value(QStringLiteral("payload")).toMap().value(QStringLiteral("claims")).toList().at(1).toMap();
        QCOMPARE(kept2.value(QStringLiteral("citations")).toList().size(), 1);

        // resolve(): the passage must be live text of this Library.
        const QString chunkId = fixture.database.prepare(QStringLiteral("SELECT id FROM library_chunks WHERE library_id = ? LIMIT 1")).bind(1, otherId).scalar().toString();
        const auto foreign = KnowledgeSources::resolve(fixture.database, libraryId, chunkId, QStringLiteral("Releases ship"));
        QVERIFY(!foreign.ok);
        QVERIFY(foreign.error.contains(QStringLiteral("not indexed text in this Library")));
        const auto unquoted = KnowledgeSources::resolve(fixture.database, otherId, chunkId, QString());
        QVERIFY(!unquoted.ok);

        // A reply that is not JSON, or has nothing usable, is refused.
        KnowledgeGeneration::Request request = KnowledgeGeneration::Request::fromVariant(libraryId, JobType::generatePage, {{QStringLiteral("topic"), QStringLiteral("Release process")}});
        const auto evidence = KnowledgeGeneration::selectEvidence(fixture.database, request);
        QVERIFY_THROWS_EXCEPTION(CoreError, (void)KnowledgeGeneration::parseResponse(fixture.database, request, evidence, QStringLiteral("not json"), {}, {}));
        const QString empty = QString::fromUtf8(QJsonDocument(page(QStringLiteral("T"), QString(), {
            claim(QStringLiteral("x"), ClaimType::fact, QStringLiteral("Uncited."), 0.9, {}),
        })).toJson());
        QVERIFY_THROWS_EXCEPTION(CoreError, (void)KnowledgeGeneration::parseResponse(fixture.database, request, evidence, empty, {}, {}));

        // The database refuses anything but an original Library source as a
        // citation: a Wiki record can never cite itself.
        const QString pageId = KnowledgeProposals::createManualPage(fixture.database, libraryId, QStringLiteral("Notes"), PageType::topic, QStringLiteral("x"), QStringLiteral("tester"));
        const QString claimId = KnowledgeProposals::addManualClaim(fixture.database, pageId, ClaimType::assumption, QStringLiteral("An assumption."), {}, QStringLiteral("tester"));
        QVERIFY_THROWS_EXCEPTION(CoreError, fixture.database.prepare(QStringLiteral(
            "INSERT INTO knowledge_citations (id, library_id, claim_id, source_type, relative_path, chunk_hash, quote, quote_key) "
            "VALUES ('x', ?, ?, 'wiki-claim', 'Wiki/notes.md', 'h', 'q', 'k')")).bind(1, libraryId).bind(2, claimId).run());
    }

    void relationshipsNeedEvidenceAndContradictionsAreContested()
    {
        Fixture fixture;
        const QString libraryId = fixture.standardLibrary();
        const QString pageId = fixture.approvedReleasePage(libraryId);
        const QString tuesday = fixture.claimByStatement(pageId, QStringLiteral("Releases ship every second Tuesday.")).value(QStringLiteral("id")).toString();
        const QString rollback = fixture.claimByStatement(pageId, QStringLiteral("A rollback takes at most ten minutes.")).value(QStringLiteral("id")).toString();

        // New evidence: a file that conflicts with an existing claim.
        fixture.write(QStringLiteral("library"), QStringLiteral("docs/schedule.md"), QStringLiteral("# Release schedule\n\nFrom March, releases ship every Thursday instead.\n"));
        fixture.scan(libraryId);
        fixture.script->respond = [](const ModelRequest &request) {
            const QString release = label(request.input, QStringLiteral("docs/release.md"));
            const QString schedule = label(request.input, QStringLiteral("docs/schedule.md"));
            // E1 is the first existing claim shown (the Tuesday claim).
            return reply(page(QStringLiteral("Release process"), QStringLiteral("Releases moved to Thursdays [P2]."), {
                claim(QStringLiteral("thursday"), ClaimType::fact, QStringLiteral("From March, releases ship every Thursday."), 0.8, {cite(schedule, QStringLiteral("releases ship every Thursday instead"))}),
                claim(QStringLiteral("rollback"), ClaimType::fact, QStringLiteral("A rollback takes at most ten minutes."), 0.8, {cite(release, QStringLiteral("within ten minutes"))}),
                claim(QStringLiteral("guess"), ClaimType::inference, QStringLiteral("Thursdays avoid weekend incidents."), 0.4, {}),
                claim(QStringLiteral("q"), ClaimType::question, QStringLiteral("Does the rollback window change?"), 0.4, {}),
            }, {
                QJsonObject{{QStringLiteral("from"), QStringLiteral("thursday")}, {QStringLiteral("type"), Relation::contradicts}, {QStringLiteral("to"), QStringLiteral("E1")}, {QStringLiteral("explanation"), QStringLiteral("New schedule.")}},
                QJsonObject{{QStringLiteral("from"), QStringLiteral("thursday")}, {QStringLiteral("type"), Relation::relatedTo}, {QStringLiteral("to"), QStringLiteral("rollback")}, {QStringLiteral("explanation"), QString()}},
                // Speculative: the question has no evidence.
                QJsonObject{{QStringLiteral("from"), QStringLiteral("q")}, {QStringLiteral("type"), Relation::dependsOn}, {QStringLiteral("to"), QStringLiteral("rollback")}, {QStringLiteral("explanation"), QString()}},
                // Unknown target.
                QJsonObject{{QStringLiteral("from"), QStringLiteral("thursday")}, {QStringLiteral("type"), Relation::supersedes}, {QStringLiteral("to"), QStringLiteral("E42")}, {QStringLiteral("explanation"), QString()}},
            }));
        };

        const QVariantMap job = fixture.runJob(libraryId, JobType::expandPage, {{QStringLiteral("targetPageId"), pageId}, {QStringLiteral("topic"), QStringLiteral("release schedule")}});
        QCOMPARE(job.value(QStringLiteral("status")).toString(), JobStatus::completed);
        QVERIFY(fixture.script->requests.last().input.contains(QStringLiteral("<current-claims>")));
        const QString proposalId = job.value(QStringLiteral("result")).toMap().value(QStringLiteral("proposalId")).toString();
        const QVariantMap proposal = KnowledgeProposals::proposal(fixture.database, proposalId).value();
        QCOMPARE(proposal.value(QStringLiteral("proposalType")).toString(), ProposalType::modify);
        QCOMPARE(proposal.value(QStringLiteral("payload")).toMap().value(QStringLiteral("relations")).toList().size(), 2);
        QCOMPARE(proposal.value(QStringLiteral("validation")).toMap().value(QStringLiteral("droppedRelations")).toList().size(), 2);
        // Expanding keeps existing claims.
        QVERIFY(proposal.value(QStringLiteral("payload")).toMap().value(QStringLiteral("retractClaimIds")).toList().isEmpty());

        QCOMPARE(KnowledgeProposals::approve(fixture.database, proposalId, QStringLiteral("tester")).value(QStringLiteral("status")).toString(), ProposalStatus::applied);
        const QString thursday = fixture.claimByStatement(pageId, QStringLiteral("From March, releases ship every Thursday.")).value(QStringLiteral("id")).toString();
        QCOMPARE(KnowledgeRepository::claim(fixture.database, tuesday)->value(QStringLiteral("verificationStatus")).toString(), Verification::contested);
        QCOMPARE(KnowledgeRepository::claim(fixture.database, thursday)->value(QStringLiteral("verificationStatus")).toString(), Verification::contested);
        QCOMPARE(KnowledgeRepository::claim(fixture.database, tuesday)->value(QStringLiteral("confidence")).toDouble(), 0.3);
        // The repeated rollback claim was not duplicated.
        QCOMPARE(fixture.count(QStringLiteral("SELECT COUNT(*) FROM knowledge_claims WHERE statement = 'A rollback takes at most ten minutes.'")), 1);

        const QVariantList relations = KnowledgeRepository::relations(fixture.database, QStringLiteral("claim"), tuesday);
        QCOMPARE(relations.size(), 1);
        QCOMPARE(relations.first().toMap().value(QStringLiteral("relationType")).toString(), Relation::contradicts);
        QCOMPARE(relations.first().toMap().value(QStringLiteral("direction")).toString(), QStringLiteral("in"));
        QCOMPARE(relations.first().toMap().value(QStringLiteral("otherId")).toString(), thursday);
        QCOMPARE(KnowledgeRepository::relations(fixture.database, QStringLiteral("claim"), rollback).size(), 1);

        // A person resolves the contradiction.
        KnowledgeProposals::reviewClaim(fixture.database, thursday, QStringLiteral("verify"), QStringLiteral("tester"));
        KnowledgeProposals::reviewClaim(fixture.database, tuesday, QStringLiteral("retract"), QStringLiteral("tester"));
        QCOMPARE(KnowledgeRepository::claim(fixture.database, thursday)->value(QStringLiteral("verificationStatus")).toString(), Verification::verified);
        QCOMPARE(KnowledgeRepository::claim(fixture.database, thursday)->value(QStringLiteral("confidence")).toDouble(), 0.95);

        // Relationship rules.
        QVERIFY_THROWS_EXCEPTION(CoreError, KnowledgeRepository::addRelation(fixture.database, libraryId, QStringLiteral("claim"), thursday, Relation::relatedTo, QStringLiteral("claim"), thursday, Origin::manual));
        QVERIFY_THROWS_EXCEPTION(CoreError, KnowledgeRepository::addRelation(fixture.database, libraryId, QStringLiteral("claim"), thursday, QStringLiteral("loves"), QStringLiteral("claim"), rollback, Origin::manual));
        QVERIFY(KnowledgeRepository::addRelation(fixture.database, libraryId, QStringLiteral("claim"), rollback, Relation::partOf, QStringLiteral("page"), pageId, Origin::manual));
        QVERIFY(!KnowledgeRepository::addRelation(fixture.database, libraryId, QStringLiteral("claim"), rollback, Relation::partOf, QStringLiteral("page"), pageId, Origin::manual));
    }

    void revisionsAreImmutableAndRestorable()
    {
        Fixture fixture;
        const QString libraryId = fixture.standardLibrary();
        const QString pageId = fixture.approvedReleasePage(libraryId);
        const QString revisionOne = KnowledgeRepository::revisions(fixture.database, pageId).first().toMap().value(QStringLiteral("id")).toString();

        // A manual edit is revision 2 and marks the page as edited by a person.
        QCOMPARE(KnowledgeProposals::editPage(fixture.database, pageId, QStringLiteral("Release process"), {}, QStringLiteral("Edited by hand."), 1, QStringLiteral("tester")), 2);
        auto page = KnowledgeRepository::pageRow(fixture.database, pageId).value();
        QCOMPARE(page.value(QStringLiteral("origin")).toString(), Origin::mixed);
        QVERIFY(!page.value(QStringLiteral("manuallyEditedAt")).isNull());

        // A stale base revision is refused, nothing overwritten.
        try {
            KnowledgeProposals::editPage(fixture.database, pageId, QStringLiteral("X"), {}, QStringLiteral("Lost update"), 1, QStringLiteral("tester"));
            QFAIL("expected a conflict");
        } catch (const CoreError &error) {
            QCOMPARE(error.details().value(QStringLiteral("code")).toString(), QStringLiteral("WIKI_REVISION_CONFLICT"));
        }

        const QString manualClaim = KnowledgeProposals::addManualClaim(fixture.database, pageId, ClaimType::decision, QStringLiteral("We keep Tuesday releases."), {}, QStringLiteral("tester"));
        QCOMPARE(KnowledgeRepository::pageRow(fixture.database, pageId)->value(QStringLiteral("revision")).toInt(), 3);
        QCOMPARE(KnowledgeRepository::claim(fixture.database, manualClaim)->value(QStringLiteral("confidence")).toDouble(), 0.5);

        // Revisions cannot be rewritten.
        QVERIFY_THROWS_EXCEPTION(CoreError, fixture.database.prepare(QStringLiteral("UPDATE knowledge_revisions SET content = 'x' WHERE id = ?")).bind(1, revisionOne).run());

        // Restore revision 1: content back, the later claim retracted (not deleted).
        QCOMPARE(KnowledgeProposals::restoreRevision(fixture.database, pageId, revisionOne, QStringLiteral("tester")), 4);
        page = KnowledgeRepository::pageRow(fixture.database, pageId).value();
        QCOMPARE(page.value(QStringLiteral("content")).toString(), QStringLiteral("Releases ship fortnightly [P1]."));
        QCOMPARE(KnowledgeRepository::claim(fixture.database, manualClaim)->value(QStringLiteral("verificationStatus")).toString(), Verification::retracted);
        QCOMPARE(KnowledgeRepository::claims(fixture.database, pageId).size(), 3);

        const QVariantList history = KnowledgeRepository::revisions(fixture.database, pageId);
        QCOMPARE(history.size(), 4);
        QCOMPARE(history.first().toMap().value(QStringLiteral("changeKind")).toString(), ChangeKind::restore);
        QCOMPARE(history.at(1).toMap().value(QStringLiteral("changeKind")).toString(), ChangeKind::manualEdit);
        QCOMPARE(history.first().toMap().value(QStringLiteral("previousRevisionId")).toString(), history.at(1).toMap().value(QStringLiteral("id")).toString());

        // Restoring the edited revision brings the hand-written text back.
        const QString revisionTwo = history.at(2).toMap().value(QStringLiteral("id")).toString();
        KnowledgeProposals::restoreRevision(fixture.database, pageId, revisionTwo, QStringLiteral("tester"));
        QCOMPARE(KnowledgeRepository::pageRow(fixture.database, pageId)->value(QStringLiteral("content")).toString(), QStringLiteral("Edited by hand."));

        // A revision of another page cannot be restored here.
        const QString otherPage = KnowledgeProposals::createManualPage(fixture.database, libraryId, QStringLiteral("Other"), PageType::topic, QStringLiteral("y"), QStringLiteral("tester"));
        QVERIFY_THROWS_EXCEPTION(CoreError, KnowledgeProposals::restoreRevision(fixture.database, otherPage, revisionOne, QStringLiteral("tester")));
    }

    void proposalsCanBeRejectedAndNeverOverwriteManualEdits()
    {
        Fixture fixture;
        const QString libraryId = fixture.standardLibrary();

        // Rejection: nothing applied.
        QVariantMap job = fixture.runJob(libraryId, JobType::generatePage, {{QStringLiteral("topic"), QStringLiteral("Release process")}});
        QString proposalId = job.value(QStringLiteral("result")).toMap().value(QStringLiteral("proposalId")).toString();
        QCOMPARE(KnowledgeProposals::reject(fixture.database, proposalId, QStringLiteral("tester"), QStringLiteral("Not yet")).value(QStringLiteral("status")).toString(), ProposalStatus::rejected);
        QCOMPARE(fixture.count(QStringLiteral("SELECT COUNT(*) FROM knowledge_pages")), 0);
        QVERIFY_THROWS_EXCEPTION(CoreError, KnowledgeProposals::approve(fixture.database, proposalId, QStringLiteral("tester")));
        QCOMPARE(KnowledgeProposals::proposal(fixture.database, proposalId)->value(QStringLiteral("reviewNote")).toString(), QStringLiteral("Not yet"));

        // Asking again after a rejection makes a new proposal.
        job = fixture.runJob(libraryId, JobType::generatePage, {{QStringLiteral("topic"), QStringLiteral("Release process")}});
        QCOMPARE(fixture.script->requests.size(), 2);
        const QString pageId = KnowledgeProposals::approve(fixture.database, job.value(QStringLiteral("result")).toMap().value(QStringLiteral("proposalId")).toString(), QStringLiteral("tester"))
                                   .value(QStringLiteral("pageId"))
                                   .toString();

        // A refresh proposal is made against revision 1...
        fixture.script->respond = [](const ModelRequest &request) {
            const QString p = label(request.input, QStringLiteral("docs/release.md"));
            return reply(page(QStringLiteral("Release process"), QStringLiteral("AI rewrite."), {
                claim(QStringLiteral("c1"), ClaimType::fact, QStringLiteral("Releases ship every second Tuesday."), 0.9, {cite(p, QStringLiteral("every second Tuesday"))}),
            }));
        };
        job = fixture.runJob(libraryId, JobType::generatePage, {{QStringLiteral("targetPageId"), pageId}});
        proposalId = job.value(QStringLiteral("result")).toMap().value(QStringLiteral("proposalId")).toString();
        const QVariantMap proposal = KnowledgeProposals::proposal(fixture.database, proposalId).value();
        QCOMPARE(proposal.value(QStringLiteral("baseRevision")).toInt(), 1);
        // A full refresh would drop the AI claims it no longer makes (shown for review).
        QCOMPARE(proposal.value(QStringLiteral("payload")).toMap().value(QStringLiteral("retractClaimIds")).toList().size(), 2);
        QVERIFY(proposal.value(QStringLiteral("diff")).toString().contains(QStringLiteral("- Releases ship fortnightly [P1].")));

        // ...then the person edits the page by hand.
        KnowledgeProposals::editPage(fixture.database, pageId, QStringLiteral("Release process"), {}, QStringLiteral("My own words."), 1, QStringLiteral("tester"));

        // Approving the now-outdated proposal conflicts and changes nothing.
        const QVariantMap result = KnowledgeProposals::approve(fixture.database, proposalId, QStringLiteral("tester"));
        QCOMPARE(result.value(QStringLiteral("status")).toString(), ProposalStatus::conflicted);
        QCOMPARE(KnowledgeRepository::pageRow(fixture.database, pageId)->value(QStringLiteral("content")).toString(), QStringLiteral("My own words."));
        QCOMPARE(KnowledgeProposals::proposal(fixture.database, proposalId)->value(QStringLiteral("status")).toString(), ProposalStatus::conflicted);
        QCOMPARE(KnowledgeRepository::claims(fixture.database, pageId).size(), 3);

        // A fresh proposal sees the manual text and applies on top of it.
        job = fixture.runJob(libraryId, JobType::generatePage, {{QStringLiteral("targetPageId"), pageId}});
        QVERIFY(fixture.script->requests.last().input.contains(QStringLiteral("My own words.")));
        const QString fresh = job.value(QStringLiteral("result")).toMap().value(QStringLiteral("proposalId")).toString();
        QCOMPARE(KnowledgeProposals::approve(fixture.database, fresh, QStringLiteral("tester")).value(QStringLiteral("status")).toString(), ProposalStatus::applied);
        QCOMPARE(KnowledgeRepository::claims(fixture.database, pageId).size(), 1);
        QCOMPARE(KnowledgeRepository::claims(fixture.database, pageId, true).size(), 3);
    }

    void reconciliationFollowsMovedReindexedAndDeletedSources()
    {
        Fixture fixture;
        const QString libraryId = fixture.standardLibrary();
        // Two pages: release (docs/release.md) and budget (notes/budget.txt).
        const QString releaseId = fixture.approvedReleasePage(libraryId);
        fixture.script->respond = [](const ModelRequest &request) {
            const QString p = label(request.input, QStringLiteral("notes/budget.txt"));
            return reply(page(QStringLiteral("Budget"), QStringLiteral("Budget summary."), {
                claim(QStringLiteral("b1"), ClaimType::fact, QStringLiteral("The 2026 budget is forty thousand euros."), 0.9, {cite(p, QStringLiteral("budget for 2026 is forty thousand euros"))}),
            }));
        };
        const QVariantMap budgetJob = fixture.runJob(libraryId, JobType::generatePage, {{QStringLiteral("topic"), QStringLiteral("Budget")}});
        const QString budgetId = KnowledgeProposals::approve(fixture.database, budgetJob.value(QStringLiteral("result")).toMap().value(QStringLiteral("proposalId")).toString(), QStringLiteral("tester"))
                                     .value(QStringLiteral("pageId"))
                                     .toString();
        const QString tuesday = fixture.claimByStatement(releaseId, QStringLiteral("Releases ship every second Tuesday.")).value(QStringLiteral("id")).toString();
        const QString rollback = fixture.claimByStatement(releaseId, QStringLiteral("A rollback takes at most ten minutes.")).value(QStringLiteral("id")).toString();
        const QString budget = fixture.claimByStatement(budgetId, QStringLiteral("The 2026 budget is forty thousand euros.")).value(QStringLiteral("id")).toString();
        const ActorRef system{Actor::system, QStringLiteral("test")};

        // Nothing changed: everything stays valid, no proposals.
        auto report = KnowledgeSources::reconcile(fixture.database, libraryId, system);
        QCOMPARE(report.checked, 3);
        QCOMPARE(report.unchanged, 3);
        QVERIFY(report.proposalIds.isEmpty());

        // 1. Re-indexed with new chunk ids: lines shift, text survives.
        fixture.write(QStringLiteral("library"), QStringLiteral("docs/release.md"), QStringLiteral("Intro line.\n\n") + releaseDoc);
        fixture.scan(libraryId);
        report = KnowledgeSources::reconcile(fixture.database, libraryId, system);
        QCOMPARE(report.staleClaimIds.size(), 0);
        QVERIFY(report.reanchored >= 1);
        QVariantMap citation = KnowledgeRepository::citations(fixture.database, tuesday).first().toMap();
        QCOMPARE(citation.value(QStringLiteral("status")).toString(), CitationStatus::reanchored);
        QCOMPARE(citation.value(QStringLiteral("startLine")).toInt(), 5);
        QCOMPARE(KnowledgeRepository::claim(fixture.database, tuesday)->value(QStringLiteral("verificationStatus")).toString(), Verification::supported);

        // 2. Moved inside the app: same file id, new path.
        const QString budgetFile = fixture.fileId(libraryId, QStringLiteral("notes/budget.txt"));
        LibraryFileRepository::updateFileLocation(fixture.database, libraryId, budgetFile, QStringLiteral("finance/budget.txt"), QStringLiteral("budget.txt"), QStringLiteral(".txt"));
        QDir().mkpath(fixture.root(QStringLiteral("library/finance")));
        QVERIFY(QFile::rename(fixture.root(QStringLiteral("library/notes/budget.txt")), fixture.root(QStringLiteral("library/finance/budget.txt"))));
        report = KnowledgeSources::reconcile(fixture.database, libraryId, system);
        citation = KnowledgeRepository::citations(fixture.database, budget).first().toMap();
        QCOMPARE(citation.value(QStringLiteral("status")).toString(), CitationStatus::moved);
        QCOMPARE(citation.value(QStringLiteral("relativePath")).toString(), QStringLiteral("finance/budget.txt"));
        QCOMPARE(citation.value(QStringLiteral("fileId")).toString(), budgetFile);

        // 3. Renamed outside the app: new file id, same text.
        QVERIFY(QFile::rename(fixture.root(QStringLiteral("library/finance/budget.txt")), fixture.root(QStringLiteral("library/finance/budget-2026.txt"))));
        fixture.scan(libraryId);
        report = KnowledgeSources::reconcile(fixture.database, libraryId, system);
        citation = KnowledgeRepository::citations(fixture.database, budget).first().toMap();
        QCOMPARE(citation.value(QStringLiteral("status")).toString(), CitationStatus::moved);
        QCOMPARE(citation.value(QStringLiteral("relativePath")).toString(), QStringLiteral("finance/budget-2026.txt"));
        QVERIFY(citation.value(QStringLiteral("fileId")).toString() != budgetFile);
        QCOMPARE(KnowledgeRepository::claim(fixture.database, budget)->value(QStringLiteral("verificationStatus")).toString(), Verification::supported);

        // 4. Modified: the quoted rollback sentence is rewritten. The page was
        //    also edited by hand, which reconciliation must not touch.
        const int revision = KnowledgeRepository::pageRow(fixture.database, releaseId)->value(QStringLiteral("revision")).toInt();
        KnowledgeProposals::editPage(fixture.database, releaseId, QStringLiteral("Release process"), {}, QStringLiteral("Hand-written body."), revision, QStringLiteral("tester"));
        fixture.write(QStringLiteral("library"), QStringLiteral("docs/release.md"),
                      QStringLiteral("Intro line.\n\n# Release process\n\nReleases ship every second Tuesday.\nThe release manager signs the build with the hardware key.\n\nRollbacks are no longer supported.\n"));
        fixture.scan(libraryId);
        report = KnowledgeSources::reconcile(fixture.database, libraryId, system);
        QCOMPARE(report.staleClaimIds, QStringList{rollback});
        QCOMPARE(report.modified, 1);
        QCOMPARE(report.proposalIds.size(), 1);
        QCOMPARE(KnowledgeRepository::claim(fixture.database, rollback)->value(QStringLiteral("verificationStatus")).toString(), Verification::stale);
        QCOMPARE(KnowledgeRepository::claim(fixture.database, rollback)->value(QStringLiteral("confidence")).toDouble(), 0.15);
        QCOMPARE(KnowledgeRepository::claim(fixture.database, tuesday)->value(QStringLiteral("verificationStatus")).toString(), Verification::supported);
        auto releasePage = KnowledgeRepository::pageRow(fixture.database, releaseId).value();
        QCOMPARE(releasePage.value(QStringLiteral("reviewStatus")).toString(), ReviewStatus::needsReview);
        QCOMPARE(releasePage.value(QStringLiteral("content")).toString(), QStringLiteral("Hand-written body."));
        // Stale, not deleted.
        QCOMPARE(KnowledgeRepository::claims(fixture.database, releaseId).size(), 3);

        // Reconciling again does not pile up proposals.
        const QString reconcileId = report.proposalIds.first();
        report = KnowledgeSources::reconcile(fixture.database, libraryId, system);
        QCOMPARE(report.proposalIds, QStringList{reconcileId});
        QCOMPARE(fixture.count(QStringLiteral("SELECT COUNT(*) FROM knowledge_proposals WHERE proposal_type = 'reconcile' AND status = 'pending'")), 1);

        // Stale claims are not retrieved.
        for (const auto &hit : KnowledgeRetrieval::search(fixture.database, libraryId, QStringLiteral("rollback minutes"), {}, 10).hits) {
            QVERIFY(hit.entryId != rollback);
        }

        // Approving the reconciliation retracts the stale claim, as a revision.
        QCOMPARE(KnowledgeProposals::approve(fixture.database, reconcileId, QStringLiteral("tester")).value(QStringLiteral("status")).toString(), ProposalStatus::applied);
        QCOMPARE(KnowledgeRepository::claim(fixture.database, rollback)->value(QStringLiteral("verificationStatus")).toString(), Verification::retracted);
        QCOMPARE(KnowledgeRepository::pageRow(fixture.database, releaseId)->value(QStringLiteral("reviewStatus")).toString(), ReviewStatus::reviewed);
        QCOMPARE(KnowledgeRepository::revisions(fixture.database, releaseId).first().toMap().value(QStringLiteral("changeKind")).toString(), ChangeKind::reconcile);

        // 5. Deleted: the budget file disappears; then comes back.
        QVERIFY(QFile::remove(fixture.root(QStringLiteral("library/finance/budget-2026.txt"))));
        fixture.scan(libraryId);
        report = KnowledgeSources::reconcile(fixture.database, libraryId, system);
        QCOMPARE(report.deleted, 1);
        QCOMPARE(report.staleClaimIds, QStringList{budget});
        QCOMPARE(KnowledgeRepository::citations(fixture.database, budget).first().toMap().value(QStringLiteral("status")).toString(), CitationStatus::deleted);
        const QString budgetReconcile = report.proposalIds.first();
        // The person keeps it as stale for now.
        KnowledgeProposals::reject(fixture.database, budgetReconcile, QStringLiteral("tester"));
        report = KnowledgeSources::reconcile(fixture.database, libraryId, system);
        QVERIFY(report.proposalIds.isEmpty()); // already answered; not asked again
        QCOMPARE(KnowledgeRepository::claim(fixture.database, budget)->value(QStringLiteral("verificationStatus")).toString(), Verification::stale);

        fixture.write(QStringLiteral("library"), QStringLiteral("archive/budget.txt"), budgetDoc);
        fixture.scan(libraryId);
        report = KnowledgeSources::reconcile(fixture.database, libraryId, system);
        QCOMPARE(report.recoveredClaimIds, QStringList{budget});
        QCOMPARE(KnowledgeRepository::claim(fixture.database, budget)->value(QStringLiteral("verificationStatus")).toString(), Verification::supported);
        QCOMPARE(KnowledgeRepository::citations(fixture.database, budget).first().toMap().value(QStringLiteral("relativePath")).toString(), QStringLiteral("archive/budget.txt"));

        // The reconcile job type runs the same through the queue.
        const QVariantMap job = fixture.runJob(libraryId, JobType::reconcileSources, {});
        QCOMPARE(job.value(QStringLiteral("status")).toString(), JobStatus::completed);
        QCOMPARE(job.value(QStringLiteral("result")).toMap().value(QStringLiteral("checked")).toInt(), report.checked);
    }

    void repeatedProcessingDoesNotDuplicateOrInflateConfidence()
    {
        Fixture fixture;
        const QString libraryId = fixture.standardLibrary();
        fixture.startRunner();

        // The same request queued twice is one job.
        const QVariantMap request{{QStringLiteral("topic"), QStringLiteral("Release process")}};
        const QVariantMap first = fixture.database.transaction([&]() { return KnowledgeJobs::enqueue(fixture.database, libraryId, JobType::generatePage, request); });
        const QVariantMap second = fixture.database.transaction([&]() { return KnowledgeJobs::enqueue(fixture.database, libraryId, JobType::generatePage, request); });
        QCOMPARE(first.value(QStringLiteral("id")), second.value(QStringLiteral("id")));
        fixture.runner->poke();
        QVERIFY(fixture.settle());
        QCOMPARE(fixture.script->requests.size(), 1);

        // Running it again with unchanged sources: no model call, no new proposal.
        QVariantMap again = fixture.runJob(libraryId, JobType::generatePage, request);
        QCOMPARE(again.value(QStringLiteral("status")).toString(), JobStatus::completed);
        QCOMPARE(again.value(QStringLiteral("result")).toMap().value(QStringLiteral("unchanged")).toBool(), true);
        QCOMPARE(fixture.script->requests.size(), 1);
        QCOMPARE(fixture.count(QStringLiteral("SELECT COUNT(*) FROM knowledge_proposals")), 1);

        // After approval the same request revises the same page (no second
        // page), and with unchanged sources it is still skipped.
        const QString proposalId = KnowledgeJobs::job(fixture.database, first.value(QStringLiteral("id")).toString())->value(QStringLiteral("result")).toMap().value(QStringLiteral("proposalId")).toString();
        const QString pageId = KnowledgeProposals::approve(fixture.database, proposalId, QStringLiteral("tester")).value(QStringLiteral("pageId")).toString();
        again = fixture.runJob(libraryId, JobType::generatePage, request);
        QCOMPARE(fixture.script->requests.size(), 2); // the page's revision changed the fingerprint
        const QString revise = again.value(QStringLiteral("result")).toMap().value(QStringLiteral("proposalId")).toString();
        QCOMPARE(KnowledgeProposals::proposal(fixture.database, revise)->value(QStringLiteral("proposalType")).toString(), ProposalType::modify);
        QCOMPARE(KnowledgeProposals::proposal(fixture.database, revise)->value(QStringLiteral("targetPageId")).toString(), pageId);
        const double before = fixture.claimByStatement(pageId, QStringLiteral("Releases ship every second Tuesday.")).value(QStringLiteral("confidence")).toDouble();

        // Approving the same summary again: same claims, same citations, same confidence.
        KnowledgeProposals::approve(fixture.database, revise, QStringLiteral("tester"));
        QCOMPARE(fixture.count(QStringLiteral("SELECT COUNT(*) FROM knowledge_pages")), 1);
        QCOMPARE(fixture.count(QStringLiteral("SELECT COUNT(*) FROM knowledge_claims")), 3);
        QCOMPARE(fixture.count(QStringLiteral("SELECT COUNT(*) FROM knowledge_citations")), 2);
        QCOMPARE(fixture.claimByStatement(pageId, QStringLiteral("Releases ship every second Tuesday.")).value(QStringLiteral("confidence")).toDouble(), before);

        // Even a different quote of the same passage is one source.
        KnowledgeRepository::CitationDraft extra = KnowledgeSources::resolve(fixture.database, libraryId,
            KnowledgeRepository::citations(fixture.database, fixture.claimByStatement(pageId, QStringLiteral("Releases ship every second Tuesday.")).value(QStringLiteral("id")).toString())
                .first().toMap().value(QStringLiteral("chunkId")).toString(),
            QStringLiteral("every second Tuesday")).citation;
        KnowledgeRepository::ClaimDraft repeat{ClaimType::fact, QStringLiteral("Releases ship every second Tuesday."), 1.0, Origin::ai, 0, {extra}};
        const auto write = KnowledgeRepository::upsertClaim(fixture.database, libraryId, pageId, repeat);
        QVERIFY(!write.created);
        QCOMPARE(write.citationsAdded, 1);
        QCOMPARE(KnowledgeRepository::claim(fixture.database, write.claimId)->value(QStringLiteral("confidence")).toDouble(), before);

        // A second, independent passage does raise it, up to the cap.
        const QString otherChunk = fixture.database.prepare(QStringLiteral("SELECT c.id FROM library_chunks c INNER JOIN library_files f ON f.id = c.library_file_id WHERE f.relative_path = 'notes/budget.txt'")).scalar().toString();
        repeat.citations = {KnowledgeSources::resolve(fixture.database, libraryId, otherChunk, QStringLiteral("Spending is reviewed monthly")).citation};
        KnowledgeRepository::upsertClaim(fixture.database, libraryId, pageId, repeat);
        QCOMPARE(KnowledgeRepository::claim(fixture.database, write.claimId)->value(QStringLiteral("confidence")).toDouble(), 0.7);

        // The model's own confidence only lowers.
        QCOMPARE(KnowledgeRepository::confidenceFor(Verification::supported, ClaimType::fact, Origin::ai, 1, 0.2), 0.2);
        QCOMPARE(KnowledgeRepository::confidenceFor(Verification::supported, ClaimType::fact, Origin::ai, 9, 1.0), 0.85);
        QCOMPARE(KnowledgeRepository::confidenceFor(Verification::unverified, ClaimType::fact, Origin::ai, 0, 1.0), 0.25);
    }

    void interruptedAndCancelledJobsRecover()
    {
        Fixture fixture;
        const QString libraryId = fixture.standardLibrary();

        // A job the previous process left running.
        const QVariantMap job = fixture.database.transaction([&]() {
            const QVariantMap queued = KnowledgeJobs::enqueue(fixture.database, libraryId, JobType::generatePage, {{QStringLiteral("topic"), QStringLiteral("Release process")}});
            KnowledgeJobs::update(fixture.database, queued.value(QStringLiteral("id")).toString(), {{QStringLiteral("status"), JobStatus::running}, {QStringLiteral("stage"), QStringLiteral("generating")}, {QStringLiteral("attemptCount"), 1}});
            return queued;
        });
        fixture.startRunner();
        QVERIFY(fixture.settle());
        const QVariantMap recovered = KnowledgeJobs::job(fixture.database, job.value(QStringLiteral("id")).toString()).value();
        QCOMPARE(recovered.value(QStringLiteral("status")).toString(), JobStatus::completed);
        QCOMPARE(recovered.value(QStringLiteral("attemptCount")).toInt(), 2);
        QCOMPARE(fixture.count(QStringLiteral("SELECT COUNT(*) FROM knowledge_proposals")), 1);

        // Cancelled while the model is writing: the late reply is dropped.
        fixture.script->hang = true;
        const QVariantMap slow = fixture.database.transaction([&]() {
            return KnowledgeJobs::enqueue(fixture.database, libraryId, JobType::generatePage, {{QStringLiteral("topic"), QStringLiteral("Budget")}});
        });
        const QString slowId = slow.value(QStringLiteral("id")).toString();
        fixture.runner->poke();
        QTRY_COMPARE(fixture.runner->activeJobId(), slowId);
        QTRY_VERIFY(fixture.script->requests.size() == 2);
        fixture.runner->cancelJob(slowId);
        QVERIFY(fixture.settle());
        QCOMPARE(KnowledgeJobs::job(fixture.database, slowId)->value(QStringLiteral("status")).toString(), JobStatus::cancelled);
        QCOMPARE(fixture.count(QStringLiteral("SELECT COUNT(*) FROM knowledge_proposals")), 1);

        // A cancelled job can be retried.
        fixture.script->hang = false;
        QVERIFY(fixture.database.transaction([&]() { return KnowledgeJobs::retry(fixture.database, slowId); }));
        fixture.runner->poke();
        QVERIFY(fixture.settle());
        QCOMPARE(KnowledgeJobs::job(fixture.database, slowId)->value(QStringLiteral("status")).toString(), JobStatus::completed);

        // Reserved librarian types are refused until implemented.
        QVERIFY_THROWS_EXCEPTION(CoreError, KnowledgeJobs::enqueue(fixture.database, libraryId, JobType::investigateGap, {}));
    }

    void providerFailuresAndOfflineBehaviour()
    {
        Fixture fixture;
        const QString libraryId = fixture.standardLibrary();
        int calls = 0;

        // Offline twice, then back: the job retries with backoff and succeeds.
        fixture.script->respond = [&calls](const ModelRequest &request) {
            return ++calls <= 2 ? failure(ModelResult::Error::Network, QStringLiteral("offline")) : releasePage(request);
        };
        QVariantMap job = fixture.runJob(libraryId, JobType::generatePage, {{QStringLiteral("topic"), QStringLiteral("Release process")}});
        QCOMPARE(job.value(QStringLiteral("status")).toString(), JobStatus::completed);
        QCOMPARE(job.value(QStringLiteral("attemptCount")).toInt(), 3);

        // Always offline: fails after the attempt budget, no proposal.
        fixture.script->respond = [](const ModelRequest &) { return failure(ModelResult::Error::Network, QStringLiteral("offline")); };
        job = fixture.runJob(libraryId, JobType::generatePage, {{QStringLiteral("topic"), QStringLiteral("Budget")}});
        QCOMPARE(job.value(QStringLiteral("status")).toString(), JobStatus::failed);
        QCOMPARE(job.value(QStringLiteral("errorCode")).toString(), QStringLiteral("network"));
        QCOMPARE(job.value(QStringLiteral("attemptCount")).toInt(), 3);

        // No key: fails at once with a message saying what to do.
        fixture.script->respond = [](const ModelRequest &) { return failure(ModelResult::Error::NotConfigured, QStringLiteral("Add an OpenAI API key")); };
        job = fixture.runJob(libraryId, JobType::generatePage, {{QStringLiteral("topic"), QStringLiteral("Spending")}});
        QCOMPARE(job.value(QStringLiteral("status")).toString(), JobStatus::failed);
        QCOMPARE(job.value(QStringLiteral("errorCode")).toString(), QStringLiteral("not-configured"));
        QCOMPARE(job.value(QStringLiteral("attemptCount")).toInt(), 1);

        // Malformed replies are retried, then fail.
        fixture.script->respond = [](const ModelRequest &) {
            ModelResult result;
            result.text = QStringLiteral("I think the budget is fine.");
            return result;
        };
        job = fixture.runJob(libraryId, JobType::generatePage, {{QStringLiteral("topic"), QStringLiteral("finance group")}});
        QCOMPARE(job.value(QStringLiteral("status")).toString(), JobStatus::failed);
        QCOMPARE(job.value(QStringLiteral("errorCode")).toString(), QStringLiteral("WIKI_INVALID_RESPONSE"));

        // Nothing in the Library: fails without calling the model.
        const int before = static_cast<int>(fixture.script->requests.size());
        job = fixture.runJob(libraryId, JobType::generatePage, {{QStringLiteral("topic"), QStringLiteral("zebras")}});
        QCOMPARE(job.value(QStringLiteral("errorCode")).toString(), QStringLiteral("WIKI_NO_EVIDENCE"));
        QCOMPARE(fixture.script->requests.size(), before);
        QCOMPARE(fixture.count(QStringLiteral("SELECT COUNT(*) FROM knowledge_proposals")), 1);

        // The OpenAI client: request shape, parsing, and no network without a key.
        ModelRequest request;
        request.model = QStringLiteral("gpt-5-mini");
        request.instructions = QStringLiteral("i");
        request.input = QStringLiteral("u");
        request.schemaName = QStringLiteral("s");
        request.schema = KnowledgeGeneration::responseSchema();
        const QJsonObject body = OpenAIKnowledgeModel::requestBody(request);
        QCOMPARE(body.value(QStringLiteral("store")).toBool(), false);
        QCOMPARE(body.value(QStringLiteral("text")).toObject().value(QStringLiteral("format")).toObject().value(QStringLiteral("type")).toString(), QStringLiteral("json_schema"));
        QCOMPARE(body.value(QStringLiteral("reasoning")).toObject().value(QStringLiteral("effort")).toString(), QStringLiteral("low"));
        const ModelResult parsed = OpenAIKnowledgeModel::parseResponse("{\"model\":\"gpt-5-mini\",\"output\":[{\"type\":\"reasoning\"},{\"type\":\"message\",\"content\":[{\"type\":\"output_text\",\"text\":\"{\\\"a\\\":1}\"}]}],\"usage\":{\"input_tokens\":5,\"output_tokens\":7}}");
        QVERIFY(parsed.ok());
        QCOMPARE(parsed.text, QStringLiteral("{\"a\":1}"));
        QCOMPARE(parsed.outputTokens, 7);
        QCOMPARE(OpenAIKnowledgeModel::parseResponse("{\"output\":[{\"type\":\"message\",\"content\":[{\"type\":\"refusal\",\"refusal\":\"no\"}]}]}").error, ModelResult::Error::InvalidRequest);
        QCOMPARE(OpenAIKnowledgeModel::parseResponse("{\"status\":\"incomplete\",\"incomplete_details\":{\"reason\":\"max_output_tokens\"}}").error, ModelResult::Error::InvalidRequest);

        QNetworkAccessManager network;
        OpenAIKnowledgeModel model(&network, []() { return AIProvider::OpenAIConfig{}; });
        ModelResult noKey;
        bool answered = false;
        model.generate(request, [&](const ModelResult &result) { noKey = result; answered = true; });
        QTRY_VERIFY(answered);
        QCOMPARE(noKey.error, ModelResult::Error::NotConfigured);
    }

    void librariesAreIsolated()
    {
        Fixture fixture;
        const QString first = fixture.standardLibrary(QStringLiteral("first"));
        const QString second = fixture.standardLibrary(QStringLiteral("second"));
        const QString pageA = fixture.approvedReleasePage(first);
        QVERIFY(!pageA.isEmpty());

        // The same topic in the other Library makes its own page from its own files.
        const QVariantMap job = fixture.runJob(second, JobType::generatePage, {{QStringLiteral("topic"), QStringLiteral("Release process")}});
        const QVariantMap proposal = KnowledgeProposals::proposal(fixture.database, job.value(QStringLiteral("result")).toMap().value(QStringLiteral("proposalId")).toString()).value();
        QCOMPARE(proposal.value(QStringLiteral("proposalType")).toString(), ProposalType::create);

        for (const QVariant &value : proposal.value(QStringLiteral("evidence")).toList()) {
            const QString chunk = value.toMap().value(QStringLiteral("chunkId")).toString();
            QCOMPARE(fixture.database.prepare(QStringLiteral("SELECT library_id FROM library_chunks WHERE id = ?")).bind(1, chunk).scalar().toString(), second);
        }

        const QString pageB = KnowledgeProposals::approve(fixture.database, proposal.value(QStringLiteral("id")).toString(), QStringLiteral("tester")).value(QStringLiteral("pageId")).toString();

        // Search and lists stay inside their Library.
        for (const auto &hit : KnowledgeRetrieval::search(fixture.database, second, QStringLiteral("release Tuesday"), {}, 20).hits) {
            QCOMPARE(hit.libraryId, second);
        }

        QCOMPARE(KnowledgeRepository::pages(fixture.database, first).size(), 1);
        QCOMPARE(KnowledgeRepository::pages(fixture.database, second).size(), 1);

        // No relationships across Libraries.
        QVERIFY_THROWS_EXCEPTION(CoreError, KnowledgeRepository::addRelation(fixture.database, first, QStringLiteral("page"), pageA, Relation::relatedTo, QStringLiteral("page"), pageB, Origin::manual));
        QVERIFY_THROWS_EXCEPTION(CoreError, KnowledgeProposals::proposeMerge(fixture.database, pageA, pageB, {}, {Actor::user, QStringLiteral("tester")}));
        // A page of another Library cannot be the target of a job.
        const QVariantMap foreign = fixture.runJob(first, JobType::expandPage, {{QStringLiteral("targetPageId"), pageB}});
        QCOMPARE(foreign.value(QStringLiteral("status")).toString(), JobStatus::failed);

        // Deleting a Library removes its knowledge and nothing else.
        fixture.database.exec(QStringLiteral("DELETE FROM libraries WHERE id = '") + first + QLatin1Char('\''));
        QCOMPARE(fixture.count(QStringLiteral("SELECT COUNT(*) FROM knowledge_pages")), 1);
        QCOMPARE(fixture.count(QStringLiteral("SELECT COUNT(*) FROM knowledge_search WHERE library_id = '") + first + QLatin1Char('\'')), 0);
        QVERIFY(fixture.count(QStringLiteral("SELECT COUNT(*) FROM knowledge_claims")) > 0);
    }

    void wikiReachesContextAssemblyV3AsLabelledInterpretation()
    {
        Fixture fixture;
        const QString libraryId = fixture.standardLibrary();
        const QString otherId = fixture.standardLibrary(QStringLiteral("other"));
        const QString pageId = fixture.approvedReleasePage(libraryId);
        fixture.approvedReleasePage(otherId);

        // Keyword retrieval through the v3 adapter.
        const ContextSources::Retrieval wiki = ContextSources::wikiKnowledge(fixture.database, libraryId, QStringLiteral("When do releases ship?"), {}, Retrieval::HybridPolicy{}, 8);
        QVERIFY(!wiki.candidates.isEmpty());
        const Context::SearchCandidate top = wiki.candidates.first();
        QCOMPARE(top.kind, Context::Kind::WikiKnowledge);
        QCOMPARE(top.ref.sourceType, QStringLiteral("wiki-claim"));
        QCOMPARE(top.ref.libraryId, libraryId);
        QCOMPARE(top.ref.relativePath, QStringLiteral("Wiki/release-process.md"));
        QVERIFY(top.ref.fileId.isEmpty());
        QCOMPARE(top.ref.producer, QStringLiteral("wiki-fts"));
        QCOMPARE(top.ref.attributes.value(QStringLiteral("status")).toString(), Verification::supported);
        QCOMPARE(top.ref.attributes.value(QStringLiteral("confidence")).toString(), QStringLiteral("0.60"));
        QVERIFY(top.ref.attributes.value(QStringLiteral("original-sources")).toString().startsWith(QStringLiteral("docs/release.md:")));
        QVERIFY(top.content.startsWith(QStringLiteral("Wiki claim on “Release process”")));
        QVERIFY(top.content.contains(QStringLiteral("Original sources: docs/release.md")));
        QCOMPARE(wiki.diagnostics.value(QStringLiteral("mode")).toString(), QStringLiteral("lexical"));

        // Assembled with the original evidence under the saved v3 defaults.
        const QVariantMap config = ContextCompilers::parseConfig(QStringLiteral("context-assembly"), 3, {{QStringLiteral("totalTokens"), 32000}, {QStringLiteral("responseTokenReserve"), 4000}});
        QCOMPARE(config.value(QStringLiteral("wikiTokens")).toInt(), 2000);
        const ContextSources::Retrieval library = ContextSources::libraryEvidence(fixture.database, libraryId, QStringLiteral("When do releases ship?"), 8);
        Context::ContextRequest request;
        request.chatId = QStringLiteral("chat");
        request.instructions = QStringLiteral("You are Archivist.");
        request.currentRequest.id = QStringLiteral("current");
        request.currentRequest.kind = Context::Kind::CurrentRequest;
        request.currentRequest.trust = Context::Trust::UserIntent;
        request.currentRequest.role = QStringLiteral("user");
        request.currentRequest.content = QStringLiteral("When do releases ship?");
        request.policy = Context::policyFromConfig(config);
        request.candidates = library.candidates + wiki.candidates;
        // Another Library's Wiki stays out.
        Context::SearchCandidate foreign = ContextSources::wikiKnowledge(fixture.database, otherId, QStringLiteral("releases ship"), {}, Retrieval::HybridPolicy{}, 1).candidates.first();
        request.candidates.append(foreign);
        request.scope.restricted = true;
        request.scope.libraryIds = {libraryId};

        const Context::CompiledContext compiled = Context::assemble(request, Context::CharacterEstimator());
        QVERIFY(compiled.includedCount(Context::Kind::WikiKnowledge) > 0);
        QVERIFY(compiled.includedCount(Context::Kind::LibraryEvidence) > 0);
        int libraryPosition = -1;
        int wikiPosition = -1;
        QString envelope;

        for (const auto &message : compiled.messages) {
            if (message.content.contains(QStringLiteral("archivist-reference-material"))) {
                envelope = message.content;
            }
        }

        libraryPosition = static_cast<int>(envelope.indexOf(QStringLiteral("kind=\"library-evidence\"")));
        wikiPosition = static_cast<int>(envelope.indexOf(QStringLiteral("kind=\"wiki-knowledge\"")));
        QVERIFY(libraryPosition >= 0);
        QVERIFY(wikiPosition > libraryPosition); // original sources first, interpretation after
        QVERIFY(envelope.contains(QStringLiteral("trust=\"untrusted\" library=\"library\" wiki-page=\"Wiki/release-process.md\" wiki-record=\"wiki-claim\"")));
        QVERIFY(envelope.contains(QStringLiteral("status=\"supported\"")));
        QVERIFY(envelope.contains(QStringLiteral("original-sources=\"docs/release.md:")));

        bool foreignOmitted = false;

        for (const auto &outcome : compiled.outcomes) {
            if (outcome.item.ref.libraryId == otherId) {
                QVERIFY(!outcome.included);
                QCOMPARE(outcome.reason, Context::Omission::outOfScope);
                foreignOmitted = true;
            }

            if (outcome.included && outcome.item.kind == Context::Kind::WikiKnowledge) {
                QCOMPARE(outcome.item.trust, Context::Trust::Untrusted);
            }
        }

        QVERIFY(foreignOmitted);
        const QVariantMap manifest = compiled.manifest({}, 0);
        QVERIFY(JsonValues::toJsonString(manifest).contains(QStringLiteral("\"wiki-claim\"")));

        // A v3 config can turn the Wiki off or size it.
        QVERIFY_THROWS_EXCEPTION(CoreError, (void)ContextCompilers::parseConfig(QStringLiteral("context-assembly"), 3, {
            {QStringLiteral("totalTokens"), 32000}, {QStringLiteral("responseTokenReserve"), 4000}, {QStringLiteral("wikiRetrieval"), QStringLiteral("sometimes")}}));
        QCOMPARE(Context::policyFromConfig({{QStringLiteral("wikiTokens"), 0}}).forKind(Context::Kind::WikiKnowledge).tokenCap, 0);

        // Semantic: Wiki records are embedded through the shared indexer and
        // found by meaning (no shared keyword with the query).
        Retrieval::EmbeddingIndexer indexer(fixture.database, []() -> std::unique_ptr<Retrieval::EmbeddingClient> { return std::make_unique<FakeEmbeddings>(); }, {}, testEmbeddingConfig());
        indexer.enable(libraryId);
        QTRY_VERIFY_WITH_TIMEOUT(indexer.idle() && fixture.count(QStringLiteral("SELECT COUNT(*) FROM embedding_sources WHERE source_type = 'wiki-claim'")) == 3, 5000);
        QCOMPARE(fixture.count(QStringLiteral("SELECT COUNT(*) FROM embedding_sources WHERE source_type = 'wiki-page'")), 1);
        const auto semantic = KnowledgeRetrieval::search(fixture.database, libraryId, QStringLiteral("revert"), conceptVector(QStringLiteral("revert undo")), 5, Retrieval::HybridPolicy{}, testEmbeddingConfig());
        QCOMPARE(semantic.diagnostics.value(QStringLiteral("mode")).toString(), QStringLiteral("hybrid"));
        QVERIFY(!semantic.hits.isEmpty());
        QCOMPARE(semantic.hits.first().text, QStringLiteral("A rollback takes at most ten minutes."));
        QVERIFY(semantic.hits.first().semanticRank > 0);

        // A retracted claim's vector source is cleaned up by the next sync.
        const QString rollback = fixture.claimByStatement(pageId, QStringLiteral("A rollback takes at most ten minutes.")).value(QStringLiteral("id")).toString();
        KnowledgeProposals::reviewClaim(fixture.database, rollback, QStringLiteral("retract"), QStringLiteral("tester"));
        indexer.sync(libraryId);
        QTRY_VERIFY_WITH_TIMEOUT(indexer.idle(), 5000);
        QCOMPARE(fixture.count(QStringLiteral("SELECT COUNT(*) FROM embedding_sources WHERE source_type = 'wiki-claim'")), 2);
        // The page text was re-embedded? No: page text did not change.
        QCOMPARE(fixture.count(QStringLiteral("SELECT COUNT(*) FROM embedding_sources WHERE source_type = 'wiki-page'")), 1);
    }

    // The app's handle: reads and review actions on its own connection,
    // failures as {ok: false, code} instead of exceptions. No job is run here
    // (that would reach the real provider).
    void serviceReportsResultsAndErrors()
    {
        Fixture fixture;
        const QString libraryId = fixture.standardLibrary();
        Knowledge::KnowledgeService service;
        QVERIFY(!service.createPage(libraryId, QStringLiteral("x"), PageType::topic, QStringLiteral("x")).value(QStringLiteral("ok")).toBool());
        QVERIFY(service.start(fixture.directory.filePath(QStringLiteral("knowledge.db"))));
        QSignalSpy changed(&service, &Knowledge::KnowledgeService::changed);

        const QVariantMap created = service.createPage(libraryId, QStringLiteral("Glossary"), PageType::topic, QStringLiteral("Terms."));
        QVERIFY(created.value(QStringLiteral("ok")).toBool());
        const QString pageId = created.value(QStringLiteral("pageId")).toString();
        QCOMPARE(changed.size(), 1);
        QCOMPARE(changed.first().first().toString(), libraryId);
        QCOMPARE(service.pages(libraryId).size(), 1);
        QCOMPARE(service.page(pageId).value(QStringLiteral("title")).toString(), QStringLiteral("Glossary"));
        QCOMPARE(service.overview(libraryId).value(QStringLiteral("pages")).toInt(), 1);

        const QVariantMap stale = service.editPage(pageId, QStringLiteral("Glossary"), PageType::topic, QStringLiteral("New."), 99);
        QVERIFY(!stale.value(QStringLiteral("ok")).toBool());
        QCOMPARE(stale.value(QStringLiteral("code")).toString(), QStringLiteral("WIKI_REVISION_CONFLICT"));
        QVERIFY(service.editPage(pageId, QStringLiteral("Glossary"), PageType::topic, QStringLiteral("New."), 1).value(QStringLiteral("ok")).toBool());
        QCOMPARE(service.revisions(pageId).size(), 2);

        const QVariantMap unknown = service.approve(QStringLiteral("missing"));
        QVERIFY(!unknown.value(QStringLiteral("ok")).toBool());
        QVERIFY(service.page(QStringLiteral("missing")).isEmpty());
        service.stop();
        QVERIFY(!service.ready());
    }

    void mergeAndDeprecationAreReviewed()
    {
        Fixture fixture;
        const QString libraryId = fixture.standardLibrary();
        const QString release = fixture.approvedReleasePage(libraryId);
        const QString notes = KnowledgeProposals::createManualPage(fixture.database, libraryId, QStringLiteral("Release notes"), PageType::topic, QStringLiteral("Notes."), QStringLiteral("tester"));
        KnowledgeProposals::addManualClaim(fixture.database, notes, ClaimType::decision, QStringLiteral("We publish notes with every release."), {}, QStringLiteral("tester"));
        KnowledgeProposals::addManualClaim(fixture.database, notes, ClaimType::question, QStringLiteral("Who holds the hardware key during holidays?"), {}, QStringLiteral("tester"));

        const auto merge = KnowledgeProposals::proposeMerge(fixture.database, notes, release, QStringLiteral("Same subject."), {Actor::user, QStringLiteral("tester")});
        QVERIFY(merge.created);
        QCOMPARE(KnowledgeProposals::approve(fixture.database, merge.proposalId, QStringLiteral("tester")).value(QStringLiteral("status")).toString(), ProposalStatus::applied);
        QCOMPARE(KnowledgeRepository::pageRow(fixture.database, notes)->value(QStringLiteral("status")).toString(), PageStatus::merged);
        QCOMPARE(KnowledgeRepository::pageRow(fixture.database, notes)->value(QStringLiteral("mergedIntoPageId")).toString(), release);
        // The new claim moved; the duplicate question was folded into the existing one.
        QCOMPARE(KnowledgeRepository::claims(fixture.database, release).size(), 4);
        QCOMPARE(KnowledgeRepository::pages(fixture.database, libraryId).size(), 1);
        QCOMPARE(KnowledgeRepository::relations(fixture.database, QStringLiteral("page"), release).first().toMap().value(QStringLiteral("relationType")).toString(), Relation::supersedes);

        const auto deprecate = KnowledgeProposals::proposeDeprecation(fixture.database, release, QStringLiteral("Process retired."), {Actor::user, QStringLiteral("tester")});
        // Proposing it twice is one proposal.
        QCOMPARE(KnowledgeProposals::proposeDeprecation(fixture.database, release, QStringLiteral("Process retired."), {Actor::user, QStringLiteral("tester")}).proposalId, deprecate.proposalId);
        KnowledgeProposals::approve(fixture.database, deprecate.proposalId, QStringLiteral("tester"));
        QCOMPARE(KnowledgeRepository::pageRow(fixture.database, release)->value(QStringLiteral("status")).toString(), PageStatus::deprecated);
        QVERIFY(KnowledgeRepository::pages(fixture.database, libraryId).isEmpty());
        QCOMPARE(KnowledgeRepository::pages(fixture.database, libraryId, true).size(), 2);
        QVERIFY(KnowledgeRetrieval::search(fixture.database, libraryId, QStringLiteral("release"), {}, 10).hits.isEmpty());
        QVERIFY(!KnowledgeRetrieval::hasKnowledge(fixture.database, libraryId));

        // Restoring an earlier revision brings the page back.
        const QVariantList history = KnowledgeRepository::revisions(fixture.database, release);
        KnowledgeProposals::restoreRevision(fixture.database, release, history.at(1).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("tester"));
        QCOMPARE(KnowledgeRepository::pageRow(fixture.database, release)->value(QStringLiteral("status")).toString(), PageStatus::active);
        const QVariantMap overview = KnowledgeRepository::overview(fixture.database, libraryId);
        QCOMPARE(overview.value(QStringLiteral("pages")).toInt(), 1);
        QCOMPARE(overview.value(QStringLiteral("pendingProposals")).toInt(), 0);
    }
};

QTEST_MAIN(CoreKnowledgeTest)
#include "core_knowledge_test.moc"

#include "Core/Domains/Agents/agent_defaults.h"
#include "Core/Domains/Agents/agent_repository.h"
#include "Core/Domains/Agents/context_compiler_catalog.h"
#include "Core/Support/core_values.h"
#include "Core/core_error.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

using namespace CoreValues;

class CoreAgentsTest final : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_directory;
    CoreDatabase m_database;
    QString m_cachePath;

    template<typename Work>
    QString errorFrom(Work &&work)
    {
        try {
            work();
        } catch (const CoreError &error) {
            return error.message();
        }

        return {};
    }

    QString insertChat()
    {
        const QString id = newId();
        m_database.prepare(QStringLiteral("INSERT INTO chats (id, title, type) VALUES (?, 'Chat', 'standard')"))
            .bind(1, id)
            .run();
        return id;
    }

private slots:
    void init()
    {
        QVERIFY(m_directory.isValid());
        m_database.open(m_directory.filePath(newId() + QStringLiteral(".db")));
        m_database.runMigrations();

        // A model cache like the AI runtime writes: one model that allows
        // temperature with an output cap, one conservative discovered model.
        m_cachePath = m_directory.filePath(newId() + QStringLiteral(".json"));
        QFile cache(m_cachePath);
        QVERIFY(cache.open(QIODevice::WriteOnly));
        cache.write(R"({"version":1,"providers":{"openai":{"provider":"openai","models":[
            {"provider":"openai","modelId":"gpt-test","supportedControls":["maxOutputTokens","temperature","topP"],"maximumOutputTokens":1000},
            {"provider":"openai","modelId":"gpt-plain","supportedControls":["maxOutputTokens"],"maximumOutputTokens":null}
        ]}}})");
    }

    void cleanup()
    {
        m_database.close();
    }

    void listsTheBuiltInDefaultAgent()
    {
        const QVariantList agents = AgentRepository::activeAgents(m_database);
        QCOMPARE(agents.size(), 1);

        const QVariantMap agent = agents.first().toMap();
        QCOMPARE(agent.value(QStringLiteral("id")).toString(), AgentDefaults::defaultAgentId);
        QVERIFY(agent.value(QStringLiteral("isBuiltIn")).toBool());
        QCOMPARE(agent.value(QStringLiteral("archivedAt")).typeId(), QMetaType::Nullptr);
        QCOMPARE(
            agent.value(QStringLiteral("context")).toMap().value(QStringLiteral("compiler")).toMap().value(QStringLiteral("id")).toString(),
            QStringLiteral("recent-history")
        );
        QVERIFY(agent.value(QStringLiteral("generation")).toMap().contains(QStringLiteral("model")));
    }

    void createsWithDefaultsAndTrimmedInput()
    {
        QVariantMap input;
        input.insert(QStringLiteral("name"), QStringLiteral("  Researcher  "));
        input.insert(QStringLiteral("description"), QStringLiteral("   "));
        input.insert(QStringLiteral("identity"), QVariantMap{{QStringLiteral("voice"), QStringLiteral(" Dry ")}});
        input.insert(
            QStringLiteral("profession"),
            QVariantMap{{QStringLiteral("expertise"), QVariantList{QStringLiteral(" SQL "), QStringLiteral("Qt")}}}
        );
        input.insert(QStringLiteral("unknownKey"), 42);

        const QVariantMap agent = AgentRepository::createAgent(m_database, input, m_cachePath);

        QVERIFY(isUuid(agent.value(QStringLiteral("id")).toString()));
        QCOMPARE(agent.value(QStringLiteral("name")).toString(), QStringLiteral("Researcher"));
        QCOMPARE(agent.value(QStringLiteral("description")).typeId(), QMetaType::Nullptr);
        QCOMPARE(agent.value(QStringLiteral("systemInstructions")).toString(), AgentDefaults::systemInstructions());
        QVERIFY(!agent.value(QStringLiteral("isBuiltIn")).toBool());

        const QVariantMap identity = agent.value(QStringLiteral("identity")).toMap();
        QCOMPARE(identity.value(QStringLiteral("voice")).toString(), QStringLiteral("Dry"));
        QCOMPARE(identity.value(QStringLiteral("temperament")).toString(), QStringLiteral("Patient and direct."));
        QCOMPARE(identity.value(QStringLiteral("backstory")).typeId(), QMetaType::Nullptr);

        const QVariantMap profession = agent.value(QStringLiteral("profession")).toMap();
        QCOMPARE(profession.value(QStringLiteral("expertise")).toList(), (QVariantList{QStringLiteral("SQL"), QStringLiteral("Qt")}));
        QCOMPARE(profession.value(QStringLiteral("responsibilities")).toList().size(), 3);

        const QVariantMap generation = agent.value(QStringLiteral("generation")).toMap();
        QCOMPARE(generation.value(QStringLiteral("model")).toString(), QStringLiteral("gpt-5-mini"));
        QCOMPARE(generation.value(QStringLiteral("maxOutputTokens")).toInt(), 4000);

        // Built-in first, then most recently updated.
        const QVariantList agents = AgentRepository::activeAgents(m_database);
        QCOMPARE(agents.size(), 2);
        QCOMPARE(agents.at(0).toMap().value(QStringLiteral("id")).toString(), AgentDefaults::defaultAgentId);
    }

    void rejectsInvalidInput()
    {
        QCOMPARE(
            errorFrom([&] { AgentRepository::createAgent(m_database, {{QStringLiteral("name"), QStringLiteral("  ")}}, m_cachePath); }),
            QStringLiteral("Invalid Agent data.")
        );
        QCOMPARE(
            errorFrom([&] {
                AgentRepository::createAgent(
                    m_database,
                    {{QStringLiteral("outputContract"), QVariantMap{{QStringLiteral("verbosity"), QStringLiteral("loud")}}}},
                    m_cachePath
                );
            }),
            QStringLiteral("Invalid Agent data.")
        );
        QCOMPARE(
            errorFrom([&] {
                AgentRepository::createAgent(
                    m_database,
                    {{QStringLiteral("generation"), QVariantMap{{QStringLiteral("maxOutputTokens"), 1.5}}}},
                    m_cachePath
                );
            }),
            QStringLiteral("Invalid Agent data.")
        );
        QCOMPARE(
            errorFrom([&] { AgentRepository::updateAgent(m_database, AgentDefaults::defaultAgentId, {}, m_cachePath); }),
            QStringLiteral("Invalid Agent data.")
        );
        QCOMPARE(
            errorFrom([&] { AgentRepository::updateAgent(m_database, QStringLiteral("nope"), {{QStringLiteral("name"), QStringLiteral("x")}}); }),
            QStringLiteral("Invalid Agent ID.")
        );
        QCOMPARE(
            errorFrom([&] { AgentRepository::updateAgent(m_database, newId(), {{QStringLiteral("name"), QStringLiteral("x")}}, m_cachePath); }),
            QStringLiteral("Agent not found.")
        );
    }

    void validatesGenerationAgainstTheModelCache()
    {
        auto generation = [&](const QVariantMap &settings) {
            return errorFrom([&] {
                AgentRepository::createAgent(m_database, {{QStringLiteral("generation"), settings}}, m_cachePath);
            });
        };

        QCOMPARE(
            generation({{QStringLiteral("model"), QStringLiteral("missing")}}),
            QStringLiteral("Model \"missing\" is not registered for provider \"openai\".")
        );
        QCOMPARE(
            generation({{QStringLiteral("model"), QStringLiteral("gpt-plain")}, {QStringLiteral("temperature"), 0.5}}),
            QStringLiteral("Model \"gpt-plain\" does not support the \"temperature\" control.")
        );
        QCOMPARE(
            generation({{QStringLiteral("model"), QStringLiteral("gpt-test")}, {QStringLiteral("maxOutputTokens"), 2000}}),
            QStringLiteral("Model \"gpt-test\" supports at most 1000 output tokens.")
        );
        QCOMPARE(
            generation({
                {QStringLiteral("model"), QStringLiteral("gpt-test")},
                {QStringLiteral("temperature"), 0.5},
                {QStringLiteral("topP"), 0.5},
            }),
            QStringLiteral("Set either temperature or top-p, not both.")
        );
        // The shipped verified model works even without a cache entry.
        QCOMPARE(generation({{QStringLiteral("maxOutputTokens"), 2000}}), QString());

        const QVariantMap agent = AgentRepository::createAgent(
            m_database,
            {{QStringLiteral("generation"), QVariantMap{{QStringLiteral("model"), QStringLiteral("gpt-test")}, {QStringLiteral("temperature"), 0.7}}}},
            m_cachePath
        );
        QCOMPARE(agent.value(QStringLiteral("generation")).toMap().value(QStringLiteral("temperature")).toDouble(), 0.7);
    }

    void validatesContextCompilerConfig()
    {
        auto context = [&](const QString &id, int version, const QVariantMap &config) {
            return errorFrom([&] {
                AgentRepository::createAgent(
                    m_database,
                    {{QStringLiteral("context"),
                      QVariantMap{
                          {QStringLiteral("compiler"), QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("version"), version}}},
                          {QStringLiteral("config"), config},
                      }}},
                    m_cachePath
                );
            });
        };

        QCOMPARE(
            context(QStringLiteral("nope"), 1, {}),
            QStringLiteral("Context Compiler \"nope:v1\" is not registered.")
        );
        QCOMPARE(
            context(QStringLiteral("recent-history"), 1, {{QStringLiteral("totalTokens"), 500}, {QStringLiteral("responseTokenReserve"), 4000}}),
            QStringLiteral(
                "Invalid configuration for Context Compiler \"recent-history:v1\". "
                "totalTokens: Too small: expected number to be >=1000 "
                "responseTokenReserve: Response token reserve must be smaller than total tokens."
            )
        );
        QCOMPARE(
            context(QStringLiteral("recent-history"), 1, {{QStringLiteral("totalTokens"), QStringLiteral("big")}, {QStringLiteral("responseTokenReserve"), 4000}}),
            QStringLiteral(
                "Invalid configuration for Context Compiler \"recent-history:v1\". "
                "totalTokens: Invalid input: expected number, received string"
            )
        );

        const QVariantMap balanced{
            {QStringLiteral("totalTokens"), 32000},
            {QStringLiteral("responseTokenReserve"), 4000},
            {QStringLiteral("recentHistoryTokens"), 18000},
            {QStringLiteral("retrievalTokens"), 10000},
            {QStringLiteral("retrievalResultLimit"), 16},
            {QStringLiteral("searchScope"), QStringLiteral("current-chat")},
            {QStringLiteral("minimumSearchTermLength"), 3},
        };
        QCOMPARE(context(QStringLiteral("auto-balanced"), 2, balanced), QString());

        QVariantMap overBudget = balanced;
        overBudget.insert(QStringLiteral("retrievalTokens"), 20000);
        QCOMPARE(
            context(QStringLiteral("auto-balanced"), 1, overBudget),
            QStringLiteral(
                "Invalid configuration for Context Compiler \"auto-balanced:v1\". "
                "retrievalTokens: Recent-history and retrieval budgets cannot exceed the available provider input budget."
            )
        );
    }

    void updatesByMergingNestedObjects()
    {
        const QVariantMap created = AgentRepository::createAgent(
            m_database,
            {{QStringLiteral("name"), QStringLiteral("Editor")}, {QStringLiteral("doctrine"), QStringLiteral("Be brief")}},
            m_cachePath
        );
        const QString agentId = created.value(QStringLiteral("id")).toString();

        const QVariantMap updated = AgentRepository::updateAgent(
            m_database,
            agentId,
            {
                {QStringLiteral("identity"), QVariantMap{{QStringLiteral("personality"), QVariant::fromValue(nullptr)}}},
                {QStringLiteral("description"), QStringLiteral(" Edits things ")},
                {QStringLiteral("generation"), QVariantMap{{QStringLiteral("model"), QStringLiteral("gpt-plain")}}},
            },
            m_cachePath
        );

        QCOMPARE(updated.value(QStringLiteral("name")).toString(), QStringLiteral("Editor"));
        QCOMPARE(updated.value(QStringLiteral("doctrine")).toString(), QStringLiteral("Be brief"));
        QCOMPARE(updated.value(QStringLiteral("description")).toString(), QStringLiteral("Edits things"));

        const QVariantMap identity = updated.value(QStringLiteral("identity")).toMap();
        QCOMPARE(identity.value(QStringLiteral("personality")).typeId(), QMetaType::Nullptr);
        QCOMPARE(identity.value(QStringLiteral("voice")).toString(), QStringLiteral("Clear, useful, and conversational."));

        const QVariantMap generation = updated.value(QStringLiteral("generation")).toMap();
        QCOMPARE(generation.value(QStringLiteral("model")).toString(), QStringLiteral("gpt-plain"));
        QCOMPARE(generation.value(QStringLiteral("maxOutputTokens")).toInt(), 4000);

        const QVariantMap cleared = AgentRepository::updateAgent(
            m_database,
            agentId,
            {{QStringLiteral("doctrine"), QVariant::fromValue(nullptr)}},
            m_cachePath
        );
        QCOMPARE(cleared.value(QStringLiteral("doctrine")).typeId(), QMetaType::Nullptr);
    }

    void duplicatesArchivesRestoresAndDeletes()
    {
        const QVariantMap source = AgentRepository::createAgent(
            m_database,
            {{QStringLiteral("name"), QStringLiteral("Scout")}, {QStringLiteral("systemInstructions"), QStringLiteral("Look around.")}},
            m_cachePath
        );
        const QString sourceId = source.value(QStringLiteral("id")).toString();

        const QVariantMap copy = AgentRepository::duplicateAgent(m_database, sourceId);
        QCOMPARE(copy.value(QStringLiteral("name")).toString(), QStringLiteral("Scout Copy"));
        QCOMPARE(copy.value(QStringLiteral("systemInstructions")).toString(), QStringLiteral("Look around."));
        QVERIFY(copy.value(QStringLiteral("id")).toString() != sourceId);

        // Built-ins cannot be archived or deleted.
        QCOMPARE(
            errorFrom([&] { AgentRepository::archiveAgent(m_database, AgentDefaults::defaultAgentId); }),
            QStringLiteral("The built-in Archivist Default Agent cannot be archived.")
        );
        QCOMPARE(
            errorFrom([&] { AgentRepository::deleteAgent(m_database, AgentDefaults::defaultAgentId); }),
            QStringLiteral("The built-in Archivist Default Agent cannot be deleted.")
        );

        // An Agent attached to a Chat cannot be archived.
        const QString chatId = insertChat();
        m_database.prepare(QStringLiteral("INSERT INTO chat_agents (chat_id, agent_id) VALUES (?, ?)"))
            .bind(1, chatId)
            .bind(2, sourceId)
            .run();
        QCOMPARE(
            errorFrom([&] { AgentRepository::archiveAgent(m_database, sourceId); }),
            QStringLiteral("This Agent is attached to one or more Chats. Detach it before archiving.")
        );

        const QString copyId = copy.value(QStringLiteral("id")).toString();
        const QVariantMap archived = AgentRepository::archiveAgent(m_database, copyId);
        QVERIFY(!isNullish(archived.value(QStringLiteral("archivedAt"))));
        QCOMPARE(AgentRepository::archivedAgents(m_database).size(), 1);
        QCOMPARE(
            errorFrom([&] { AgentRepository::archiveAgent(m_database, copyId); }),
            QStringLiteral("Agent is already archived.")
        );

        const QVariantMap restored = AgentRepository::restoreAgent(m_database, copyId);
        QCOMPARE(restored.value(QStringLiteral("archivedAt")).typeId(), QMetaType::Nullptr);
        QCOMPARE(
            errorFrom([&] { AgentRepository::restoreAgent(m_database, copyId); }),
            QStringLiteral("Agent is already active.")
        );

        // Deleting moves Chats that used the Agent to the default Agent.
        m_database.prepare(QStringLiteral("UPDATE chats SET agent_id = ? WHERE id = ?")).bind(1, sourceId).bind(2, chatId).run();
        QCOMPARE(AgentRepository::deleteAgent(m_database, sourceId), 1);
        QVERIFY(!AgentRepository::agentById(m_database, sourceId));
        QCOMPARE(
            m_database.prepare(QStringLiteral("SELECT agent_id FROM chats WHERE id = ?")).bind(1, chatId).scalar().toString(),
            AgentDefaults::defaultAgentId
        );
        QCOMPARE(
            m_database.prepare(QStringLiteral("SELECT COUNT(*) FROM chat_agents WHERE chat_id = ? AND agent_id = ?"))
                .bind(1, chatId)
                .bind(2, AgentDefaults::defaultAgentId)
                .scalar()
                .toInt(),
            1
        );
    }

    void fallsBackToDefaultsForBrokenStoredJson()
    {
        const QVariantMap created = AgentRepository::createAgent(m_database, {}, m_cachePath);
        const QString agentId = created.value(QStringLiteral("id")).toString();
        m_database.prepare(QStringLiteral(
                         "UPDATE agents SET identity_config = 'not json', output_contract = '{\"verbosity\":\"loud\"}', "
                         "context_compiler_config = '[]' WHERE id = ?"
                     ))
            .bind(1, agentId)
            .run();

        const QVariantMap agent = *AgentRepository::agentById(m_database, agentId);
        QCOMPARE(agent.value(QStringLiteral("identity")).toMap(), AgentDefaults::identity());
        QCOMPARE(
            agent.value(QStringLiteral("outputContract")).toMap().value(QStringLiteral("verbosity")).toString(),
            QStringLiteral("balanced")
        );
        QCOMPARE(
            agent.value(QStringLiteral("context")).toMap().value(QStringLiteral("config")).toMap(),
            AgentDefaults::contextConfig()
        );
    }
};

QTEST_GUILESS_MAIN(CoreAgentsTest)
#include "core_agents_test.moc"

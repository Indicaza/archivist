#include "agent_repository.h"

#include "agent_defaults.h"
#include "agent_schemas.h"
#include "context_compiler_catalog.h"
#include "Core/Domains/Models/model_catalog_cache.h"
#include "Core/Support/core_values.h"
#include "Core/core_error.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

using namespace CoreValues;

namespace
{
const QString agentColumns = QStringLiteral(
    "id, name, description, identity_config, profession_config, doctrine, output_contract, "
    "system_instructions, generation_config, context_compiler_id, context_compiler_version, "
    "context_compiler_config, is_built_in, archived_at, created_at, updated_at"
);

const QString nowSql = QStringLiteral("strftime('%Y-%m-%dT%H:%M:%fZ', 'now')");

QVariant parseJson(const QVariant &text)
{
    const QJsonDocument document = QJsonDocument::fromJson(text.toString().toUtf8());

    if (document.isObject()) {
        return document.object().toVariantMap();
    }

    return {};
}

QString toJson(const QVariantMap &value)
{
    return QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(value)).toJson(QJsonDocument::Compact));
}

QVariantMap parsed(
    const QVariant &text,
    std::optional<QVariantMap> (*schema)(const QVariant &, bool),
    QVariantMap (*fallback)()
)
{
    const auto result = schema(parseJson(text), false);
    return result ? *result : fallback();
}

QVariantMap mapAgent(const QVariantMap &row)
{
    QVariant config = parseJson(row.value(QStringLiteral("context_compiler_config")));

    if (!config.isValid()) {
        config = AgentDefaults::contextConfig();
    }

    return {
        {QStringLiteral("id"), row.value(QStringLiteral("id"))},
        {QStringLiteral("name"), row.value(QStringLiteral("name"))},
        {QStringLiteral("description"), nullable(row.value(QStringLiteral("description")))},
        {QStringLiteral("identity"),
         parsed(row.value(QStringLiteral("identity_config")), &AgentSchemas::identity, &AgentDefaults::identity)},
        {QStringLiteral("profession"),
         parsed(row.value(QStringLiteral("profession_config")), &AgentSchemas::profession, &AgentDefaults::profession)},
        {QStringLiteral("doctrine"), nullable(row.value(QStringLiteral("doctrine")))},
        {QStringLiteral("outputContract"),
         parsed(
             row.value(QStringLiteral("output_contract")),
             &AgentSchemas::outputContract,
             &AgentDefaults::outputContract
         )},
        {QStringLiteral("systemInstructions"), nullable(row.value(QStringLiteral("system_instructions")))},
        {QStringLiteral("generation"),
         parsed(row.value(QStringLiteral("generation_config")), &AgentSchemas::generation, &AgentDefaults::generation)},
        {QStringLiteral("context"),
         QVariantMap{
             {QStringLiteral("compiler"),
              QVariantMap{
                  {QStringLiteral("id"), row.value(QStringLiteral("context_compiler_id"))},
                  {QStringLiteral("version"), row.value(QStringLiteral("context_compiler_version")).toInt()},
              }},
             {QStringLiteral("config"), config},
         }},
        {QStringLiteral("isBuiltIn"), row.value(QStringLiteral("is_built_in")).toInt() == 1},
        {QStringLiteral("archivedAt"), nullable(row.value(QStringLiteral("archived_at")))},
        {QStringLiteral("createdAt"), row.value(QStringLiteral("created_at"))},
        {QStringLiteral("updatedAt"), row.value(QStringLiteral("updated_at"))},
    };
}

QVariantList loadAgents(CoreDatabase &database, const QString &where, const QString &orderBy)
{
    QVariantList agents;

    for (const QVariantMap &row :
         database.prepare(QStringLiteral("SELECT %1 FROM agents WHERE %2 ORDER BY %3").arg(agentColumns, where, orderBy))
             .all()) {
        agents.append(mapAgent(row));
    }

    return agents;
}

void requireAgentId(const QString &agentId)
{
    if (!isUuid(agentId)) {
        throw CoreError(QStringLiteral("Invalid Agent ID."));
    }
}

QVariantMap requireAgent(CoreDatabase &database, const QString &agentId)
{
    const auto agent = AgentRepository::agentById(database, agentId);

    if (!agent) {
        throw CoreError(QStringLiteral("Agent not found."));
    }

    return *agent;
}

// {...current, ...update}: keys present in `update` replace current ones.
QVariantMap merged(const QVariantMap &current, const QVariant &update)
{
    QVariantMap result = current;
    const QVariantMap changes = update.toMap();

    for (auto change = changes.constBegin(); change != changes.constEnd(); ++change) {
        result.insert(change.key(), change.value());
    }

    return result;
}

// `value?.trim() || fallback`, for already-trimmed values.
QVariant textOr(const QVariant &value, const QVariant &fallback)
{
    return isNullish(value) ? fallback : value;
}

void validateContext(const QVariantMap &input)
{
    if (!input.contains(QStringLiteral("context"))) {
        return;
    }

    const QVariantMap context = input.value(QStringLiteral("context")).toMap();
    const QVariantMap compiler = context.value(QStringLiteral("compiler")).toMap();

    ContextCompilerCatalog::validateConfig(
        compiler.value(QStringLiteral("id")).toString(),
        compiler.value(QStringLiteral("version")).toInt(),
        context.value(QStringLiteral("config")).toMap()
    );
}

bool present(const QVariant &value)
{
    return value.isValid() && !AgentSchemas::isNull(value);
}

// Port of AgentController.validateGenerationSettings.
void validateGeneration(
    const QVariantMap &input,
    const QVariantMap &currentGeneration,
    const QString &modelCachePath
)
{
    if (!input.contains(QStringLiteral("generation"))) {
        return;
    }

    const QVariantMap generation = input.value(QStringLiteral("generation")).toMap();
    const QString provider = generation.value(
        QStringLiteral("provider"),
        currentGeneration.value(QStringLiteral("provider"), QStringLiteral("openai"))
    ).toString();
    const QString model = generation.value(
        QStringLiteral("model"),
        currentGeneration.value(QStringLiteral("model"), QStringLiteral("gpt-5-mini"))
    ).toString();

    const auto definition = ModelCatalogCache::definition(modelCachePath, provider, model);

    if (!definition) {
        throw CoreError(QStringLiteral("Model \"%1\" is not registered for provider \"%2\".").arg(model, provider));
    }

    const QVariantMap values = merged(currentGeneration, generation);
    const QStringList supported = toStringList(definition->value(QStringLiteral("supportedControls")));

    for (const QString &control : {
             QStringLiteral("temperature"),
             QStringLiteral("maxOutputTokens"),
             QStringLiteral("topP"),
             QStringLiteral("frequencyPenalty"),
             QStringLiteral("presencePenalty"),
         }) {
        if (present(values.value(control)) && !supported.contains(control)) {
            throw CoreError(QStringLiteral("Model \"%1\" does not support the \"%2\" control.").arg(model, control));
        }
    }

    const QVariant maximum = definition->value(QStringLiteral("maximumOutputTokens"));
    const QVariant requested = values.value(QStringLiteral("maxOutputTokens"));

    if (present(requested) && present(maximum) && requested.toDouble() > maximum.toDouble()) {
        throw CoreError(QStringLiteral("Model \"%1\" supports at most %2 output tokens.")
                            .arg(model)
                            .arg(maximum.toLongLong()));
    }

    if (present(values.value(QStringLiteral("temperature"))) && present(values.value(QStringLiteral("topP")))) {
        throw CoreError(QStringLiteral("Set either temperature or top-p, not both."));
    }
}

QString cachePath(CoreDatabase &database, const QString &modelCachePath)
{
    return modelCachePath.isEmpty() ? ModelCatalogCache::cachePathForDatabase(database.path()) : modelCachePath;
}

QVariantMap normalizedInput(const QVariantMap &input)
{
    const auto normalized = AgentSchemas::agentInput(input);

    if (!normalized) {
        throw CoreError(QStringLiteral("Invalid Agent data."));
    }

    return *normalized;
}

// The insert half of createAgent, shared with duplicateAgent (which skips the
// controller's validation, like the Node route did).
QVariantMap insertAgent(CoreDatabase &database, const QVariantMap &input)
{
    const QString agentId = newId();
    const QVariantMap context = input.contains(QStringLiteral("context"))
        ? input.value(QStringLiteral("context")).toMap()
        : AgentDefaults::context();
    const QVariantMap compiler = context.value(QStringLiteral("compiler")).toMap();

    database
        .prepare(QStringLiteral(
            "INSERT INTO agents (id, name, description, identity_config, profession_config, doctrine, "
            "output_contract, system_instructions, generation_config, context_compiler_id, "
            "context_compiler_version, context_compiler_config, is_built_in) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0)"
        ))
        .bind(1, agentId)
        .bind(2, textOr(input.value(QStringLiteral("name")), QStringLiteral("New Agent")))
        .bind(3, textOr(input.value(QStringLiteral("description")), QVariant()))
        .bind(4, toJson(merged(AgentDefaults::identity(), input.value(QStringLiteral("identity")))))
        .bind(5, toJson(merged(AgentDefaults::profession(), input.value(QStringLiteral("profession")))))
        .bind(6, textOr(input.value(QStringLiteral("doctrine")), QVariant()))
        .bind(7, toJson(merged(AgentDefaults::outputContract(), input.value(QStringLiteral("outputContract")))))
        .bind(
            8,
            textOr(input.value(QStringLiteral("systemInstructions")), AgentDefaults::systemInstructions())
        )
        .bind(9, toJson(merged(AgentDefaults::generation(), input.value(QStringLiteral("generation")))))
        .bind(10, compiler.value(QStringLiteral("id")))
        .bind(11, compiler.value(QStringLiteral("version")))
        .bind(12, toJson(context.value(QStringLiteral("config")).toMap()))
        .run();

    const auto agent = AgentRepository::agentById(database, agentId);

    if (!agent) {
        throw CoreError(QStringLiteral("The newly created Agent could not be loaded."));
    }

    return *agent;
}
}

namespace AgentRepository
{
QVariantList activeAgents(CoreDatabase &database)
{
    return loadAgents(
        database,
        QStringLiteral("archived_at IS NULL"),
        QStringLiteral("is_built_in DESC, updated_at DESC, created_at DESC")
    );
}

QVariantList archivedAgents(CoreDatabase &database)
{
    return loadAgents(database, QStringLiteral("archived_at IS NOT NULL"), QStringLiteral("archived_at DESC"));
}

std::optional<QVariantMap> agentById(CoreDatabase &database, const QString &agentId)
{
    const auto row =
        database.prepare(QStringLiteral("SELECT %1 FROM agents WHERE id = ?").arg(agentColumns)).bind(1, agentId).one();

    if (!row) {
        return std::nullopt;
    }

    return mapAgent(*row);
}

QVariantMap createAgent(CoreDatabase &database, const QVariantMap &input, const QString &modelCachePath)
{
    const QVariantMap normalized = normalizedInput(input);

    validateContext(normalized);
    validateGeneration(normalized, {}, cachePath(database, modelCachePath));

    return insertAgent(database, normalized);
}

QVariantMap updateAgent(
    CoreDatabase &database,
    const QString &agentId,
    const QVariantMap &input,
    const QString &modelCachePath
)
{
    requireAgentId(agentId);
    const QVariantMap normalized = normalizedInput(input);

    if (normalized.isEmpty()) {
        throw CoreError(QStringLiteral("Invalid Agent data."));
    }

    validateContext(normalized);
    const QVariantMap current = requireAgent(database, agentId);
    validateGeneration(normalized, current.value(QStringLiteral("generation")).toMap(), cachePath(database, modelCachePath));

    // Missing keys keep the current value; a present null or blank clears it.
    auto keepOr = [&](const QString &key) {
        return normalized.contains(key) ? textOr(normalized.value(key), QVariant()) : nullable(current.value(key));
    };

    const QVariantMap context = normalized.contains(QStringLiteral("context"))
        ? normalized.value(QStringLiteral("context")).toMap()
        : current.value(QStringLiteral("context")).toMap();
    const QVariantMap compiler = context.value(QStringLiteral("compiler")).toMap();

    database
        .prepare(QStringLiteral(
            "UPDATE agents SET name = ?, description = ?, identity_config = ?, profession_config = ?, "
            "doctrine = ?, output_contract = ?, system_instructions = ?, generation_config = ?, "
            "context_compiler_id = ?, context_compiler_version = ?, context_compiler_config = ?, "
            "updated_at = %1 WHERE id = ?"
        ).arg(nowSql))
        .bind(1, normalized.value(QStringLiteral("name"), current.value(QStringLiteral("name"))))
        .bind(2, keepOr(QStringLiteral("description")))
        .bind(3, toJson(merged(current.value(QStringLiteral("identity")).toMap(), normalized.value(QStringLiteral("identity")))))
        .bind(
            4,
            toJson(merged(current.value(QStringLiteral("profession")).toMap(), normalized.value(QStringLiteral("profession"))))
        )
        .bind(5, keepOr(QStringLiteral("doctrine")))
        .bind(
            6,
            toJson(merged(
                current.value(QStringLiteral("outputContract")).toMap(),
                normalized.value(QStringLiteral("outputContract"))
            ))
        )
        .bind(7, keepOr(QStringLiteral("systemInstructions")))
        .bind(
            8,
            toJson(merged(current.value(QStringLiteral("generation")).toMap(), normalized.value(QStringLiteral("generation"))))
        )
        .bind(9, compiler.value(QStringLiteral("id")))
        .bind(10, compiler.value(QStringLiteral("version")))
        .bind(11, toJson(context.value(QStringLiteral("config")).toMap()))
        .bind(12, agentId)
        .run();

    const auto agent = agentById(database, agentId);

    if (!agent) {
        throw CoreError(QStringLiteral("The updated Agent could not be loaded."));
    }

    return *agent;
}

QVariantMap duplicateAgent(CoreDatabase &database, const QString &agentId, const QVariantMap &input)
{
    requireAgentId(agentId);
    const auto normalized = AgentSchemas::duplicateInput(input);

    if (!normalized) {
        throw CoreError(QStringLiteral("Invalid Agent duplication data."));
    }

    const QVariantMap source = requireAgent(database, agentId);
    QVariantMap copy;

    copy.insert(
        QStringLiteral("name"),
        normalized->value(
            QStringLiteral("name"),
            QStringLiteral("%1 Copy").arg(source.value(QStringLiteral("name")).toString())
        )
    );

    for (const QString &key : {
             QStringLiteral("description"),
             QStringLiteral("identity"),
             QStringLiteral("profession"),
             QStringLiteral("doctrine"),
             QStringLiteral("outputContract"),
             QStringLiteral("systemInstructions"),
             QStringLiteral("generation"),
             QStringLiteral("context"),
         }) {
        copy.insert(key, source.value(key));
    }

    return insertAgent(database, copy);
}

QVariantMap archiveAgent(CoreDatabase &database, const QString &agentId)
{
    requireAgentId(agentId);
    const QVariantMap agent = requireAgent(database, agentId);

    if (agent.value(QStringLiteral("isBuiltIn")).toBool()) {
        throw CoreError(QStringLiteral("The built-in Archivist Default Agent cannot be archived."));
    }

    if (!isNullish(agent.value(QStringLiteral("archivedAt")))) {
        throw CoreError(QStringLiteral("Agent is already archived."));
    }

    const qlonglong assignedChatCount =
        database.prepare(QStringLiteral("SELECT COUNT(DISTINCT chat_id) FROM chat_agents WHERE agent_id = ?"))
            .bind(1, agentId)
            .scalar()
            .toLongLong();

    if (assignedChatCount > 0) {
        throw CoreError(QStringLiteral("This Agent is attached to one or more Chats. Detach it before archiving."));
    }

    database.prepare(QStringLiteral("UPDATE agents SET archived_at = %1, updated_at = %1 WHERE id = ?").arg(nowSql))
        .bind(1, agentId)
        .run();

    const auto archived = agentById(database, agentId);

    if (!archived) {
        throw CoreError(QStringLiteral("The archived Agent could not be loaded."));
    }

    return *archived;
}

QVariantMap restoreAgent(CoreDatabase &database, const QString &agentId)
{
    requireAgentId(agentId);
    const QVariantMap agent = requireAgent(database, agentId);

    if (isNullish(agent.value(QStringLiteral("archivedAt")))) {
        throw CoreError(QStringLiteral("Agent is already active."));
    }

    database.prepare(QStringLiteral("UPDATE agents SET archived_at = NULL, updated_at = %1 WHERE id = ?").arg(nowSql))
        .bind(1, agentId)
        .run();

    const auto restored = agentById(database, agentId);

    if (!restored) {
        throw CoreError(QStringLiteral("The restored Agent could not be loaded."));
    }

    return *restored;
}

int deleteAgent(CoreDatabase &database, const QString &agentId)
{
    requireAgentId(agentId);
    const QVariantMap agent = requireAgent(database, agentId);

    if (agent.value(QStringLiteral("isBuiltIn")).toBool() || agentId == AgentDefaults::defaultAgentId) {
        throw CoreError(QStringLiteral("The built-in Archivist Default Agent cannot be deleted."));
    }

    database
        .prepare(QStringLiteral(
            "INSERT OR IGNORE INTO chat_agents (chat_id, agent_id, position) "
            "SELECT id, ?, 0 FROM chats WHERE agent_id = ?"
        ))
        .bind(1, AgentDefaults::defaultAgentId)
        .bind(2, agentId)
        .run();

    database
        .prepare(QStringLiteral("UPDATE chats SET agent_id = ?, updated_at = %1 WHERE agent_id = ?").arg(nowSql))
        .bind(1, AgentDefaults::defaultAgentId)
        .bind(2, agentId)
        .run();
    const int reassignedChatCount = database.changes();

    database.prepare(QStringLiteral("DELETE FROM agents WHERE id = ?")).bind(1, agentId).run();

    return reassignedChatCount;
}
}

#include "chat_repository.h"

#include "Core/Domains/Agents/agent_defaults.h"
#include "Core/Domains/Agents/agent_repository.h"
#include "Core/Domains/Libraries/library_file_repository.h"
#include "Core/Domains/Libraries/library_repository.h"
#include "Core/Support/core_values.h"
#include "Core/Support/js_compat.h"
#include "Core/Support/transactions.h"
#include "Core/core_error.h"

#include <QJsonDocument>
#include <QJsonObject>

using namespace CoreValues;

namespace
{
const QString nowSql = QStringLiteral("strftime('%Y-%m-%dT%H:%M:%fZ', 'now')");

const QString chatSelect = QStringLiteral(
    "SELECT chats.id, chats.library_id, libraries.name AS library_name, chats.title, chats.agent_id, "
    "chats.archived_at, chats.created_at, chats.updated_at "
    "FROM chats LEFT JOIN libraries ON libraries.id = chats.library_id "
);

const QString messageColumns = QStringLiteral("id, chat_id, role, content, status, created_at, updated_at");

const QString attachmentSelect = QStringLiteral(
    "SELECT chat_file_attachments.id, chat_file_attachments.chat_id, library_files.library_id, "
    "libraries.name AS library_name, library_files.id AS file_id, library_files.name AS file_name, "
    "library_files.relative_path, library_files.extension, library_files.size_bytes, "
    "library_files.status AS file_status, chat_file_attachments.created_at "
    "FROM chat_file_attachments "
    "INNER JOIN library_files ON library_files.id = chat_file_attachments.library_file_id "
    "INNER JOIN libraries ON libraries.id = library_files.library_id "
);

QString defaultAgentId()
{
    return AgentDefaults::defaultAgentId;
}

void requireUuid(const QString &value, const QString &message)
{
    if (!isUuid(value)) {
        throw CoreError(message);
    }
}

QVariantList chatAgentIds(CoreDatabase &database, const QString &chatId)
{
    QVariantList ids;
    const auto rows = database
        .prepare(QStringLiteral(
            "SELECT chat_agents.agent_id FROM chat_agents "
            "INNER JOIN agents ON agents.id = chat_agents.agent_id "
            "WHERE chat_agents.chat_id = ? AND agents.archived_at IS NULL "
            "ORDER BY chat_agents.position ASC, chat_agents.created_at ASC"
        ))
        .bind(1, chatId)
        .all();

    for (const QVariantMap &row : rows) {
        ids.append(row.value(QStringLiteral("agent_id")));
    }

    return ids;
}

QVariantMap mapChat(CoreDatabase &database, const QVariantMap &row)
{
    const QString id = row.value(QStringLiteral("id")).toString();
    const QString agentId = row.value(QStringLiteral("agent_id")).toString();

    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("libraryId"), nullable(row.value(QStringLiteral("library_id")))},
        {QStringLiteral("libraryName"), nullable(row.value(QStringLiteral("library_name")))},
        {QStringLiteral("title"), row.value(QStringLiteral("title"))},
        {QStringLiteral("agentId"), agentId.isEmpty() ? defaultAgentId() : agentId},
        {QStringLiteral("agentIds"), chatAgentIds(database, id)},
        {QStringLiteral("archivedAt"), nullable(row.value(QStringLiteral("archived_at")))},
        {QStringLiteral("createdAt"), row.value(QStringLiteral("created_at"))},
        {QStringLiteral("updatedAt"), row.value(QStringLiteral("updated_at"))},
    };
}

QVariantMap mapMessage(const QVariantMap &row)
{
    return {
        {QStringLiteral("id"), row.value(QStringLiteral("id"))},
        {QStringLiteral("chatId"), row.value(QStringLiteral("chat_id"))},
        {QStringLiteral("role"), row.value(QStringLiteral("role"))},
        {QStringLiteral("content"), row.value(QStringLiteral("content"))},
        {QStringLiteral("status"), row.value(QStringLiteral("status"))},
        {QStringLiteral("createdAt"), row.value(QStringLiteral("created_at"))},
        {QStringLiteral("updatedAt"), row.value(QStringLiteral("updated_at"))},
    };
}

QVariantMap mapAttachment(const QVariantMap &row)
{
    return {
        {QStringLiteral("id"), row.value(QStringLiteral("id"))},
        {QStringLiteral("chatId"), row.value(QStringLiteral("chat_id"))},
        {QStringLiteral("libraryId"), row.value(QStringLiteral("library_id"))},
        {QStringLiteral("libraryName"), row.value(QStringLiteral("library_name"))},
        {QStringLiteral("fileId"), row.value(QStringLiteral("file_id"))},
        {QStringLiteral("fileName"), row.value(QStringLiteral("file_name"))},
        {QStringLiteral("relativePath"), row.value(QStringLiteral("relative_path"))},
        {QStringLiteral("extension"), row.value(QStringLiteral("extension"))},
        {QStringLiteral("sizeBytes"), row.value(QStringLiteral("size_bytes"))},
        {QStringLiteral("fileStatus"), row.value(QStringLiteral("file_status"))},
        {QStringLiteral("createdAt"), row.value(QStringLiteral("created_at"))},
    };
}

QVariantList mapChats(CoreDatabase &database, const QList<QVariantMap> &rows)
{
    QVariantList chats;
    chats.reserve(rows.size());

    for (const QVariantMap &row : rows) {
        chats.append(mapChat(database, row));
    }

    return chats;
}

void requireActiveAgent(CoreDatabase &database, const QString &agentId)
{
    const auto agent = AgentRepository::agentById(database, agentId);

    if (!agent) {
        throw CoreError(QStringLiteral("Agent not found."));
    }

    if (!isNullish(agent->value(QStringLiteral("archivedAt")))) {
        throw CoreError(QStringLiteral("This Agent is archived. Restore it before assigning or using it."));
    }
}

void insertChatAgent(CoreDatabase &database, const QString &chatId, const QString &agentId)
{
    database
        .prepare(QStringLiteral(
            "INSERT OR IGNORE INTO chat_agents (chat_id, agent_id, position) VALUES (?, ?, "
            "COALESCE((SELECT MAX(position) + 1 FROM chat_agents WHERE chat_id = ?), 0))"
        ))
        .bind(1, chatId)
        .bind(2, agentId)
        .bind(3, chatId)
        .run();
}

void setSelectedChatId(CoreDatabase &database, const QVariant &chatId)
{
    database
        .prepare(QStringLiteral("UPDATE app_settings SET selected_chat_id = ?, updated_at = %1 WHERE id = 1").arg(nowSql))
        .bind(1, chatId)
        .run();
}

QVariant fallbackChatId(CoreDatabase &database, const QVariant &libraryId, const QString &excludedChatId)
{
    const auto row = database
        .prepare(QStringLiteral(
            "SELECT id FROM chats WHERE library_id IS ? AND type = 'standard' AND archived_at IS NULL "
            "AND id != ? ORDER BY updated_at DESC, created_at DESC LIMIT 1"
        ))
        .bind(1, isNullish(libraryId) ? QVariant() : libraryId)
        .bind(2, excludedChatId)
        .one();

    return row ? row->value(QStringLiteral("id")) : QVariant::fromValue(nullptr);
}

// After archiving or deleting, move the selection off the Chat if needed.
QVariant reselectAfterRemoving(CoreDatabase &database, const QVariantMap &chat)
{
    const QString chatId = chat.value(QStringLiteral("id")).toString();
    QVariant selected = ChatRepository::selectedChatId(database);

    if (selected.toString() == chatId) {
        selected = fallbackChatId(database, chat.value(QStringLiteral("libraryId")), chatId);
        setSelectedChatId(database, isNullish(selected) ? QVariant() : selected);
    }

    return selected;
}

QVariantMap requireChat(CoreDatabase &database, const QString &chatId)
{
    requireUuid(chatId, QStringLiteral("Invalid Chat ID."));
    const auto chat = ChatRepository::chatById(database, chatId);

    if (!chat) {
        throw CoreError(QStringLiteral("Chat not found."));
    }

    return *chat;
}

QVariantMap reloadChat(CoreDatabase &database, const QString &chatId, const QString &failure)
{
    const auto chat = ChatRepository::chatById(database, chatId);

    if (!chat) {
        throw CoreError(failure);
    }

    return *chat;
}

// z.string().trim().min(1).max(120).optional()
std::optional<QString> optionalTitle(const QVariantMap &input, const QString &invalidMessage)
{
    if (!input.contains(QStringLiteral("title")) || !input.value(QStringLiteral("title")).isValid()) {
        return std::nullopt;
    }

    const QVariant value = input.value(QStringLiteral("title"));

    if (value.typeId() != QMetaType::QString) {
        throw CoreError(invalidMessage);
    }

    const QString title = JsCompat::trimmed(value.toString());

    if (title.isEmpty() || title.size() > 120) {
        throw CoreError(invalidMessage);
    }

    return title;
}

std::optional<QString> optionalUuid(const QVariantMap &input, const QString &key, const QString &invalidMessage)
{
    if (!input.contains(key) || !input.value(key).isValid()) {
        return std::nullopt;
    }

    const QVariant value = input.value(key);

    if (value.typeId() != QMetaType::QString || !isUuid(value.toString())) {
        throw CoreError(invalidMessage);
    }

    return value.toString();
}

void touchChat(CoreDatabase &database, const QString &chatId)
{
    database.prepare(QStringLiteral("UPDATE chats SET updated_at = %1 WHERE id = ?").arg(nowSql)).bind(1, chatId).run();
}
}

namespace ChatRepository
{
QVariantList activeChats(CoreDatabase &database)
{
    return mapChats(
        database,
        database
            .prepare(chatSelect
                + QStringLiteral("WHERE chats.type = 'standard' AND chats.archived_at IS NULL "
                                 "ORDER BY chats.updated_at DESC, chats.created_at DESC"))
            .all()
    );
}

QVariantList archivedChats(CoreDatabase &database)
{
    return mapChats(
        database,
        database
            .prepare(chatSelect
                + QStringLiteral("WHERE chats.type = 'standard' AND chats.archived_at IS NOT NULL "
                                 "ORDER BY chats.archived_at DESC"))
            .all()
    );
}

std::optional<QVariantMap> chatById(CoreDatabase &database, const QString &chatId)
{
    const auto row = database
        .prepare(chatSelect + QStringLiteral("WHERE chats.id = ? AND chats.type = 'standard'"))
        .bind(1, chatId)
        .one();

    if (!row) {
        return std::nullopt;
    }

    return mapChat(database, *row);
}

QVariantMap requireActiveChat(CoreDatabase &database, const QString &chatId)
{
    const QVariantMap chat = requireChat(database, chatId);

    if (!isNullish(chat.value(QStringLiteral("archivedAt")))) {
        throw CoreError(QStringLiteral("This chat is archived. Restore it before continuing the conversation."));
    }

    return chat;
}

QVariantMap createChat(CoreDatabase &database, const QVariantMap &input)
{
    const QString invalid = QStringLiteral("Invalid Chat data.");
    const QString libraryId = input.value(QStringLiteral("libraryId")).toString();

    if (input.value(QStringLiteral("libraryId")).typeId() != QMetaType::QString || !isUuid(libraryId)) {
        throw CoreError(invalid);
    }

    const QString title = optionalTitle(input, invalid).value_or(QStringLiteral("New Chat"));
    const QString agentId = optionalUuid(input, QStringLiteral("agentId"), invalid).value_or(defaultAgentId());
    const auto library = LibraryRepository::libraryById(database, libraryId);

    if (!library) {
        throw CoreError(QStringLiteral("Library not found."));
    }

    if (!isNullish(library->value(QStringLiteral("archivedAt")))) {
        throw CoreError(QStringLiteral("Chats cannot be created in an archived Library."));
    }

    requireActiveAgent(database, agentId);

    const QString chatId = newId();
    database
        .prepare(QStringLiteral("INSERT INTO chats (id, library_id, title, type, agent_id) VALUES (?, ?, ?, 'standard', ?)"))
        .bind(1, chatId)
        .bind(2, libraryId)
        .bind(3, title)
        .bind(4, agentId)
        .run();

    insertChatAgent(database, chatId, agentId);
    setSelectedChatId(database, chatId);
    return reloadChat(database, chatId, QStringLiteral("The newly created Chat could not be loaded."));
}

QVariantMap updateChat(CoreDatabase &database, const QString &chatId, const QVariantMap &input)
{
    requireUuid(chatId, QStringLiteral("Invalid Chat ID."));
    const QString invalid = QStringLiteral("Invalid Chat data.");

    if (input.isEmpty()) {
        throw CoreError(invalid);
    }

    const auto title = optionalTitle(input, invalid);
    const auto requestedAgentId = optionalUuid(input, QStringLiteral("agentId"), invalid);
    const QVariantMap chat = requireChat(database, chatId);
    const QString agentId = requestedAgentId.value_or(chat.value(QStringLiteral("agentId")).toString());

    requireActiveAgent(database, agentId);
    insertChatAgent(database, chatId, agentId);
    database
        .prepare(QStringLiteral("UPDATE chats SET title = ?, agent_id = ?, updated_at = %1 WHERE id = ?").arg(nowSql))
        .bind(1, title.value_or(chat.value(QStringLiteral("title")).toString()))
        .bind(2, agentId)
        .bind(3, chatId)
        .run();

    return reloadChat(database, chatId, QStringLiteral("The updated Chat could not be loaded."));
}

QVariantMap attachAgent(CoreDatabase &database, const QString &chatId, const QString &agentId)
{
    requireUuid(chatId, QStringLiteral("Invalid Chat ID."));
    requireUuid(agentId, QStringLiteral("Invalid Chat Agent."));
    requireActiveChat(database, chatId);
    requireActiveAgent(database, agentId);
    insertChatAgent(database, chatId, agentId);
    return reloadChat(database, chatId, QStringLiteral("The updated Chat Agent roster could not be loaded."));
}

QVariantMap detachAgent(CoreDatabase &database, const QString &chatId, const QString &agentId)
{
    if (!isUuid(chatId) || !isUuid(agentId)) {
        throw CoreError(QStringLiteral("Invalid Chat Agent ID."));
    }

    const QVariantMap chat = requireActiveChat(database, chatId);

    if (chat.value(QStringLiteral("agentId")).toString() == agentId) {
        throw CoreError(QStringLiteral("Select another active Agent before detaching this one."));
    }

    database
        .prepare(QStringLiteral("DELETE FROM chat_agents WHERE chat_id = ? AND agent_id = ?"))
        .bind(1, chatId)
        .bind(2, agentId)
        .run();

    if (database.changes() == 0) {
        throw CoreError(QStringLiteral("This Agent is not attached to the Chat."));
    }

    return reloadChat(database, chatId, QStringLiteral("The updated Chat Agent roster could not be loaded."));
}

QVariantMap archiveChat(CoreDatabase &database, const QString &chatId)
{
    const QVariantMap chat = requireChat(database, chatId);

    if (!isNullish(chat.value(QStringLiteral("archivedAt")))) {
        throw CoreError(QStringLiteral("Chat is already archived."));
    }

    database
        .prepare(QStringLiteral("UPDATE chats SET archived_at = %1, updated_at = %1 WHERE id = ?").arg(nowSql))
        .bind(1, chatId)
        .run();

    const QVariant selected = reselectAfterRemoving(database, chat);

    return {
        {QStringLiteral("chat"), reloadChat(database, chatId, QStringLiteral("The archived chat could not be loaded."))},
        {QStringLiteral("selectedChatId"), nullable(selected)},
    };
}

QVariantMap restoreChat(CoreDatabase &database, const QString &chatId)
{
    const QVariantMap chat = requireChat(database, chatId);

    if (isNullish(chat.value(QStringLiteral("archivedAt")))) {
        throw CoreError(QStringLiteral("Chat is already active."));
    }

    database
        .prepare(QStringLiteral("UPDATE chats SET archived_at = NULL, updated_at = %1 WHERE id = ?").arg(nowSql))
        .bind(1, chatId)
        .run();

    return reloadChat(database, chatId, QStringLiteral("The restored chat could not be loaded."));
}

QVariant deleteChat(CoreDatabase &database, const QString &chatId)
{
    const QVariantMap chat = requireChat(database, chatId);
    database.prepare(QStringLiteral("DELETE FROM chats WHERE id = ?")).bind(1, chatId).run();
    return nullable(reselectAfterRemoving(database, chat));
}

QVariant selectChat(CoreDatabase &database, const QString &chatId)
{
    if (!chatId.isEmpty()) {
        requireUuid(chatId, QStringLiteral("Invalid selected Chat."));
        requireActiveChat(database, chatId);
    }

    setSelectedChatId(database, chatId.isEmpty() ? QVariant() : QVariant(chatId));
    return selectedChatId(database);
}

QVariant selectedChatId(CoreDatabase &database)
{
    const auto row = database.prepare(QStringLiteral("SELECT selected_chat_id FROM app_settings WHERE id = 1")).one();

    if (!row) {
        throw CoreError(QStringLiteral("Archivist app settings could not be loaded."));
    }

    return nullable(row->value(QStringLiteral("selected_chat_id")));
}

QVariantList messages(CoreDatabase &database, const QString &chatId)
{
    requireActiveChat(database, chatId);
    QVariantList result;
    const auto rows = database
        .prepare(QStringLiteral("SELECT %1 FROM messages WHERE chat_id = ? ORDER BY created_at ASC, rowid ASC").arg(messageColumns))
        .bind(1, chatId)
        .all();

    for (const QVariantMap &row : rows) {
        result.append(mapMessage(row));
    }

    return result;
}

QVariantMap messagePage(CoreDatabase &database, const QString &chatId, int limit, const QString &beforeMessageId)
{
    requireUuid(chatId, QStringLiteral("Invalid Chat ID."));

    if (limit < 1 || limit > 250 || (!beforeMessageId.isEmpty() && !isUuid(beforeMessageId))) {
        throw CoreError(QStringLiteral("Invalid message page query."));
    }

    requireActiveChat(database, chatId);
    QVariant beforeRowId;

    if (!beforeMessageId.isEmpty()) {
        const auto cursor = database
            .prepare(QStringLiteral("SELECT rowid FROM messages WHERE id = ? AND chat_id = ?"))
            .bind(1, beforeMessageId)
            .bind(2, chatId)
            .one();

        if (!cursor) {
            throw CoreError(QStringLiteral("Message cursor not found for this Chat."));
        }

        beforeRowId = cursor->value(QStringLiteral("rowid"));
    }

    const auto rows = database
        .prepare(QStringLiteral(
                     "SELECT %1 FROM messages WHERE chat_id = ? AND (? IS NULL OR rowid < ?) ORDER BY rowid DESC LIMIT ?"
        )
                     .arg(messageColumns))
        .bind(1, chatId)
        .bind(2, beforeRowId)
        .bind(3, beforeRowId)
        .bind(4, limit + 1)
        .all();

    const bool hasMore = rows.size() > limit;
    QVariantList page;

    for (qsizetype index = std::min<qsizetype>(rows.size(), limit) - 1; index >= 0; --index) {
        page.append(mapMessage(rows.at(index)));
    }

    return {
        {QStringLiteral("messages"), page},
        {
            QStringLiteral("pagination"),
            QVariantMap{
                {QStringLiteral("hasMore"), hasMore},
                {
                    QStringLiteral("nextBeforeMessageId"),
                    hasMore && !page.isEmpty() ? page.first().toMap().value(QStringLiteral("id")) : QVariant::fromValue(nullptr),
                },
            },
        },
    };
}

QVariantMap createMessage(CoreDatabase &database, const QString &chatId, const QVariantMap &input)
{
    requireUuid(chatId, QStringLiteral("Invalid Chat ID."));
    static const QStringList roles{QStringLiteral("user"), QStringLiteral("assistant"), QStringLiteral("system")};
    static const QStringList statuses{
        QStringLiteral("streaming"), QStringLiteral("complete"), QStringLiteral("cancelled"), QStringLiteral("failed"),
    };
    const QString role = input.value(QStringLiteral("role")).toString();
    const QString content = JsCompat::trimmed(input.value(QStringLiteral("content")).toString());
    const QVariant statusValue = input.value(QStringLiteral("status"));
    const QString status = statusValue.isValid() ? statusValue.toString() : QStringLiteral("complete");

    if (!roles.contains(role) || input.value(QStringLiteral("content")).typeId() != QMetaType::QString
        || content.isEmpty() || content.size() > 100000 || !statuses.contains(status)) {
        throw CoreError(QStringLiteral("Invalid message data."));
    }

    requireActiveChat(database, chatId);
    const QString messageId = newId();
    database
        .prepare(QStringLiteral("INSERT INTO messages (id, chat_id, role, content, status) VALUES (?, ?, ?, ?, ?)"))
        .bind(1, messageId)
        .bind(2, chatId)
        .bind(3, role)
        .bind(4, content)
        .bind(5, status)
        .run();
    touchChat(database, chatId);

    const auto message = messageById(database, messageId);

    if (!message) {
        throw CoreError(QStringLiteral("The newly created message could not be loaded."));
    }

    return *message;
}

std::optional<QVariantMap> messageById(CoreDatabase &database, const QString &messageId)
{
    const auto row = database
        .prepare(QStringLiteral("SELECT %1 FROM messages WHERE id = ?").arg(messageColumns))
        .bind(1, messageId)
        .one();

    return row ? std::optional<QVariantMap>(mapMessage(*row)) : std::nullopt;
}

QVariantMap insertMessage(CoreDatabase &database, const QString &chatId, const QString &role, const QString &content, const QString &status)
{
    requireActiveChat(database, chatId);
    const QString messageId = newId();

    Transactions::join(database, [&]() {
        database
            .prepare(QStringLiteral("INSERT INTO messages (id, chat_id, role, content, status) VALUES (?, ?, ?, ?, ?)"))
            .bind(1, messageId)
            .bind(2, chatId)
            .bind(3, role)
            .bind(4, JsCompat::trimmed(content))
            .bind(5, status)
            .run();
        touchChat(database, chatId);
        return QVariant();
    });

    const auto message = messageById(database, messageId);

    if (!message) {
        throw CoreError(QStringLiteral("The newly created message could not be loaded."));
    }

    return *message;
}

QVariantMap updateMessage(CoreDatabase &database, const QString &messageId, const QVariant &content, const QVariant &status)
{
    const auto current = messageById(database, messageId);

    if (!current) {
        throw CoreError(QStringLiteral("Message not found."));
    }

    database
        .prepare(QStringLiteral(
            "UPDATE messages SET content = ?, status = ?, updated_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') WHERE id = ?"
        ))
        .bind(1, content.isValid() ? content : current->value(QStringLiteral("content")))
        .bind(2, status.isValid() ? status : current->value(QStringLiteral("status")))
        .bind(3, messageId)
        .run();

    const auto updated = messageById(database, messageId);

    if (!updated) {
        throw CoreError(QStringLiteral("The updated message could not be loaded."));
    }

    return *updated;
}

QString createContextRun(CoreDatabase &database, const QVariantMap &input)
{
    const QString id = newId();
    const QVariantMap snapshot{
        {QStringLiteral("compiler"), input.value(QStringLiteral("compiler"))},
        {QStringLiteral("manifest"), input.value(QStringLiteral("manifest"))},
        {QStringLiteral("warnings"), input.value(QStringLiteral("warnings"))},
        {QStringLiteral("sources"), input.value(QStringLiteral("sources"))},
    };

    database
        .prepare(QStringLiteral(
            "INSERT INTO context_runs (id, chat_id, user_message_id, assistant_message_id, provider, model, agent_id, snapshot_json) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?)"
        ))
        .bind(1, id)
        .bind(2, input.value(QStringLiteral("chatId")))
        .bind(3, input.value(QStringLiteral("userMessageId")))
        .bind(4, input.value(QStringLiteral("assistantMessageId")))
        .bind(5, input.value(QStringLiteral("provider")))
        .bind(6, input.value(QStringLiteral("model")))
        .bind(7, input.value(QStringLiteral("agentId")))
        .bind(8, QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(snapshot)).toJson(QJsonDocument::Compact)))
        .run();

    return id;
}

QVariantList attachments(CoreDatabase &database, const QString &chatId)
{
    requireActiveChat(database, chatId);
    QVariantList result;
    const auto rows = database
        .prepare(attachmentSelect
            + QStringLiteral("WHERE chat_file_attachments.chat_id = ? "
                             "ORDER BY chat_file_attachments.created_at ASC, chat_file_attachments.rowid ASC"))
        .bind(1, chatId)
        .all();

    for (const QVariantMap &row : rows) {
        result.append(mapAttachment(row));
    }

    return result;
}

QVariantMap createAttachment(CoreDatabase &database, const QString &chatId, const QString &libraryId, const QString &fileId)
{
    requireUuid(chatId, QStringLiteral("Invalid Chat ID."));

    if (!isUuid(libraryId) || !isUuid(fileId)) {
        throw CoreError(QStringLiteral("Invalid Chat attachment."));
    }

    requireActiveChat(database, chatId);
    const auto library = LibraryRepository::libraryById(database, libraryId);

    if (!library) {
        throw CoreError(QStringLiteral("Library not found."));
    }

    if (!isNullish(library->value(QStringLiteral("archivedAt")))) {
        throw CoreError(QStringLiteral("Files from archived Libraries cannot be attached."));
    }

    const auto file = LibraryFileRepository::fileById(database, libraryId, fileId);

    if (!file) {
        throw CoreError(QStringLiteral("Library file not found."));
    }

    const QString status = file->value(QStringLiteral("status")).toString();

    if (status != QStringLiteral("available")) {
        throw CoreError(QStringLiteral("This file is marked %1. Rescan the Library before attaching it.").arg(status));
    }

    auto loadAttachment = [&](const QString &attachmentId) {
        const auto row = database
            .prepare(attachmentSelect + QStringLiteral("WHERE chat_file_attachments.id = ? AND chat_file_attachments.chat_id = ?"))
            .bind(1, attachmentId)
            .bind(2, chatId)
            .one();

        if (!row) {
            throw CoreError(QStringLiteral("The Chat attachment could not be loaded."));
        }

        return mapAttachment(*row);
    };

    const auto existing = database
        .prepare(QStringLiteral("SELECT id FROM chat_file_attachments WHERE chat_id = ? AND library_file_id = ?"))
        .bind(1, chatId)
        .bind(2, fileId)
        .one();

    if (existing) {
        return loadAttachment(existing->value(QStringLiteral("id")).toString());
    }

    const qint64 count = database
        .prepare(QStringLiteral("SELECT COUNT(*) FROM chat_file_attachments WHERE chat_id = ?"))
        .bind(1, chatId)
        .scalar()
        .toLongLong();

    if (count >= maximumFileAttachments) {
        throw CoreError(QStringLiteral("A Chat can have at most %1 attached files.").arg(maximumFileAttachments));
    }

    const QString attachmentId = newId();
    database
        .prepare(QStringLiteral("INSERT INTO chat_file_attachments (id, chat_id, library_file_id) VALUES (?, ?, ?)"))
        .bind(1, attachmentId)
        .bind(2, chatId)
        .bind(3, fileId)
        .run();

    return loadAttachment(attachmentId);
}

void deleteAttachment(CoreDatabase &database, const QString &chatId, const QString &attachmentId)
{
    if (!isUuid(chatId) || !isUuid(attachmentId)) {
        throw CoreError(QStringLiteral("Invalid Chat attachment ID."));
    }

    requireActiveChat(database, chatId);
    database
        .prepare(QStringLiteral("DELETE FROM chat_file_attachments WHERE id = ? AND chat_id = ?"))
        .bind(1, attachmentId)
        .bind(2, chatId)
        .run();

    if (database.changes() != 1) {
        throw CoreError(QStringLiteral("Chat attachment not found."));
    }
}

QVariantMap contextRun(CoreDatabase &database, const QString &chatId, const QString &assistantMessageId)
{
    if (!isUuid(chatId) || !isUuid(assistantMessageId)) {
        throw CoreError(QStringLiteral("Invalid Chat message ID."));
    }

    requireActiveChat(database, chatId);
    const auto row = database
        .prepare(QStringLiteral(
            "SELECT id, chat_id, user_message_id, assistant_message_id, provider, model, agent_id, snapshot_json, created_at "
            "FROM context_runs WHERE chat_id = ? AND assistant_message_id = ? LIMIT 1"
        ))
        .bind(1, chatId)
        .bind(2, assistantMessageId)
        .one();

    if (!row) {
        throw CoreError(QStringLiteral("No stored context is available for this response."));
    }

    const QString id = row->value(QStringLiteral("id")).toString();
    const QJsonDocument snapshot = QJsonDocument::fromJson(row->value(QStringLiteral("snapshot_json")).toString().toUtf8());
    const QJsonObject object = snapshot.object();

    if (!snapshot.isObject() || !object.value(QStringLiteral("compiler")).isObject()
        || !object.value(QStringLiteral("manifest")).isObject() || !object.value(QStringLiteral("warnings")).isArray()
        || !object.value(QStringLiteral("sources")).isArray()) {
        throw CoreError(QStringLiteral("Context run %1 contains an invalid snapshot.").arg(id));
    }

    QVariantMap result = object.toVariantMap();
    result.insert(QStringLiteral("id"), id);
    result.insert(QStringLiteral("chatId"), row->value(QStringLiteral("chat_id")));
    result.insert(QStringLiteral("userMessageId"), row->value(QStringLiteral("user_message_id")));
    result.insert(QStringLiteral("assistantMessageId"), row->value(QStringLiteral("assistant_message_id")));
    result.insert(QStringLiteral("provider"), row->value(QStringLiteral("provider")));
    result.insert(QStringLiteral("model"), row->value(QStringLiteral("model")));
    result.insert(QStringLiteral("agentId"), row->value(QStringLiteral("agent_id")));
    result.insert(QStringLiteral("createdAt"), row->value(QStringLiteral("created_at")));
    return result;
}
}

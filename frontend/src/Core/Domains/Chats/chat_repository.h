#pragma once

#include "Core/Database/core_database.h"

#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <optional>

// Native port of backend/src/api/chats (Chat.ts, ChatAttachment.ts and the
// controller validation) plus the read side of ContextRun.ts. Results keep
// the old JSON shape so ChatStore and QML see the same data.
namespace ChatRepository
{
constexpr int maximumFileAttachments = 8;

[[nodiscard]] QVariantList activeChats(CoreDatabase &database);
[[nodiscard]] QVariantList archivedChats(CoreDatabase &database);
[[nodiscard]] std::optional<QVariantMap> chatById(CoreDatabase &database, const QString &chatId);
// Throws "Chat not found." or the archived-chat message.
QVariantMap requireActiveChat(CoreDatabase &database, const QString &chatId);

// input: {libraryId, title?, agentId?}. Also selects the new Chat.
QVariantMap createChat(CoreDatabase &database, const QVariantMap &input);
// input: {title?, agentId?}; at least one is required.
QVariantMap updateChat(CoreDatabase &database, const QString &chatId, const QVariantMap &input);
QVariantMap attachAgent(CoreDatabase &database, const QString &chatId, const QString &agentId);
QVariantMap detachAgent(CoreDatabase &database, const QString &chatId, const QString &agentId);
// Returns {chat, selectedChatId}.
QVariantMap archiveChat(CoreDatabase &database, const QString &chatId);
QVariantMap restoreChat(CoreDatabase &database, const QString &chatId);
// Returns the selected Chat id after the delete (null when none).
QVariant deleteChat(CoreDatabase &database, const QString &chatId);
// chatId may be empty to clear the selection. Returns the stored selection.
QVariant selectChat(CoreDatabase &database, const QString &chatId);
[[nodiscard]] QVariant selectedChatId(CoreDatabase &database);

[[nodiscard]] QVariantList messages(CoreDatabase &database, const QString &chatId);
// Returns {messages, pagination {hasMore, nextBeforeMessageId}}.
[[nodiscard]] QVariantMap messagePage(CoreDatabase &database, const QString &chatId, int limit, const QString &beforeMessageId = {});
QVariantMap createMessage(CoreDatabase &database, const QString &chatId, const QVariantMap &input);
[[nodiscard]] std::optional<QVariantMap> messageById(CoreDatabase &database, const QString &messageId);
// The model-level createMessage the AI runtime uses: trims content and skips
// request validation, so a streaming assistant message can start empty.
QVariantMap insertMessage(CoreDatabase &database, const QString &chatId, const QString &role, const QString &content, const QString &status);
// Missing content or status keeps the current value. Throws "Message not found.".
QVariantMap updateMessage(CoreDatabase &database, const QString &messageId, const QVariant &content, const QVariant &status);

[[nodiscard]] QVariantList attachments(CoreDatabase &database, const QString &chatId);
QVariantMap createAttachment(CoreDatabase &database, const QString &chatId, const QString &libraryId, const QString &fileId);
void deleteAttachment(CoreDatabase &database, const QString &chatId, const QString &attachmentId);

// createContextRun: input {chatId, userMessageId, assistantMessageId,
// provider, model, agentId, compiler, manifest, warnings, sources}. Returns the id.
QString createContextRun(CoreDatabase &database, const QVariantMap &input);

// The stored context snapshot for an assistant message.
[[nodiscard]] QVariantMap contextRun(CoreDatabase &database, const QString &chatId, const QString &assistantMessageId);
}

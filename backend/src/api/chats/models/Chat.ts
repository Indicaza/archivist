import { ARCHIVIST_DEFAULT_AGENT_ID } from "../../agents/models/AgentDefaults.js";
import { requireActiveAgent } from "../../agents/models/Agent.js";
import { database } from "../../../database/database.js";
import { AppError } from "../../../errors/app-error.js";
import { getLibraryById } from "../../libraries/models/Library.js";
import type {
  ArchiveChatResult,
  Chat,
  ChatMessage,
  ChatMessagePage,
  CreateChatInput,
  CreateMessageInput,
  DeleteChatResult,
  UpdateChatInput,
} from "../types/ChatTypes.js";

type ChatRow = {
  id: string;
  library_id: string | null;
  library_name: string | null;
  title: string;
  agent_id: string | null;
  archived_at: string | null;
  created_at: string;
  updated_at: string;
};

type MessageRow = {
  id: string;
  chat_id: string;
  role: ChatMessage["role"];
  content: string;
  status: ChatMessage["status"];
  created_at: string;
  updated_at: string;
};

type MessageCursorRow = {
  rowid: number;
};

type AppSettingsRow = {
  selected_chat_id: string | null;
};

function mapChat(row: ChatRow): Chat {
  return {
    id: row.id,
    libraryId: row.library_id,
    libraryName: row.library_name,
    title: row.title,
    agentId: row.agent_id ?? ARCHIVIST_DEFAULT_AGENT_ID,
    archivedAt: row.archived_at,
    createdAt: row.created_at,
    updatedAt: row.updated_at,
  };
}

function mapMessage(row: MessageRow): ChatMessage {
  return {
    id: row.id,
    chatId: row.chat_id,
    role: row.role,
    content: row.content,
    status: row.status,
    createdAt: row.created_at,
    updatedAt: row.updated_at,
  };
}

function deriveChatTitle(input?: string): string {
  return input?.trim() || "New Chat";
}

function getSelectedChatId(): string | null {
  const row = database
    .prepare(
      `
        SELECT selected_chat_id
        FROM app_settings
        WHERE id = 1
      `,
    )
    .get() as AppSettingsRow | undefined;

  if (!row) {
    throw new Error("Archivist app settings could not be loaded.");
  }

  return row.selected_chat_id;
}

function setSelectedChatId(chatId: string | null): void {
  database
    .prepare(
      `
        UPDATE app_settings
        SET
          selected_chat_id = ?,
          updated_at = strftime(
            '%Y-%m-%dT%H:%M:%fZ',
            'now'
          )
        WHERE id = 1
      `,
    )
    .run(chatId);
}

function findFallbackChatId(
  libraryId: string | null,
  excludedChatId?: string,
): string | null {
  const row = database
    .prepare(
      `
        SELECT id
        FROM chats
        WHERE library_id IS ?
          AND type = 'standard'
          AND archived_at IS NULL
          AND (? IS NULL OR id != ?)
        ORDER BY updated_at DESC, created_at DESC
        LIMIT 1
      `,
    )
    .get(libraryId, excludedChatId ?? null, excludedChatId ?? null) as
    | { id: string }
    | undefined;

  return row?.id ?? null;
}

export function getAllChats(): Chat[] {
  const rows = database
    .prepare(
      `
        SELECT
          chats.id,
          chats.library_id,
          libraries.name AS library_name,
          chats.title,
          chats.agent_id,
          chats.archived_at,
          chats.created_at,
          chats.updated_at
        FROM chats
        LEFT JOIN libraries
          ON libraries.id = chats.library_id
        WHERE chats.type = 'standard'
          AND chats.archived_at IS NULL
        ORDER BY chats.updated_at DESC, chats.created_at DESC
      `,
    )
    .all() as ChatRow[];

  return rows.map(mapChat);
}

export function getArchivedChats(): Chat[] {
  const rows = database
    .prepare(
      `
        SELECT
          chats.id,
          chats.library_id,
          libraries.name AS library_name,
          chats.title,
          chats.agent_id,
          chats.archived_at,
          chats.created_at,
          chats.updated_at
        FROM chats
        LEFT JOIN libraries
          ON libraries.id = chats.library_id
        WHERE chats.type = 'standard'
          AND chats.archived_at IS NOT NULL
        ORDER BY chats.archived_at DESC
      `,
    )
    .all() as ChatRow[];

  return rows.map(mapChat);
}

export function getChatById(chatId: string): Chat | null {
  const row = database
    .prepare(
      `
        SELECT
          chats.id,
          chats.library_id,
          libraries.name AS library_name,
          chats.title,
          chats.agent_id,
          chats.archived_at,
          chats.created_at,
          chats.updated_at
        FROM chats
        LEFT JOIN libraries
          ON libraries.id = chats.library_id
        WHERE chats.id = ?
          AND chats.type = 'standard'
      `,
    )
    .get(chatId) as ChatRow | undefined;

  return row ? mapChat(row) : null;
}

export function requireActiveChat(chatId: string): Chat {
  const chat = getChatById(chatId);

  if (!chat) {
    throw new AppError(404, "Chat not found.");
  }

  if (chat.archivedAt) {
    throw new AppError(
      409,
      "This chat is archived. Restore it before continuing the conversation.",
    );
  }

  return chat;
}

export function createChat(input: CreateChatInput): Chat {
  const chatId = crypto.randomUUID();
  const title = deriveChatTitle(input.title);
  const agentId = input.agentId ?? ARCHIVIST_DEFAULT_AGENT_ID;
  const library = getLibraryById(input.libraryId);

  if (!library) {
    throw new AppError(404, "Library not found.");
  }

  if (library.archivedAt) {
    throw new AppError(409, "Chats cannot be created in an archived Library.");
  }

  requireActiveAgent(agentId);

  const createTransaction = database.transaction(() => {
    database
      .prepare(
        `
          INSERT INTO chats (
            id,
            library_id,
            title,
            type,
            agent_id
          )
          VALUES (?, ?, ?, 'standard', ?)
        `,
      )
      .run(chatId, library.id, title, agentId);

    setSelectedChatId(chatId);

    const chat = getChatById(chatId);

    if (!chat) {
      throw new Error("The newly created Chat could not be loaded.");
    }

    return chat;
  });

  return createTransaction();
}

export function updateChat(chatId: string, input: UpdateChatInput): Chat {
  const chat = getChatById(chatId);

  if (!chat) {
    throw new AppError(404, "Chat not found.");
  }

  const title = input.title === undefined ? chat.title : input.title.trim();

  const agentId = input.agentId ?? chat.agentId;

  requireActiveAgent(agentId);

  database
    .prepare(
      `
        UPDATE chats
        SET
          title = ?,
          agent_id = ?,
          updated_at = strftime(
            '%Y-%m-%dT%H:%M:%fZ',
            'now'
          )
        WHERE id = ?
      `,
    )
    .run(title, agentId, chatId);

  const updatedChat = getChatById(chatId);

  if (!updatedChat) {
    throw new Error("The updated Chat could not be loaded.");
  }

  return updatedChat;
}

export function archiveChat(chatId: string): ArchiveChatResult {
  const chat = getChatById(chatId);

  if (!chat) {
    throw new AppError(404, "Chat not found.");
  }

  if (chat.archivedAt) {
    throw new AppError(409, "Chat is already archived.");
  }

  const archiveTransaction = database.transaction(() => {
    database
      .prepare(
        `
          UPDATE chats
          SET
            archived_at = strftime(
              '%Y-%m-%dT%H:%M:%fZ',
              'now'
            ),
            updated_at = strftime(
              '%Y-%m-%dT%H:%M:%fZ',
              'now'
            )
          WHERE id = ?
        `,
      )
      .run(chatId);

    let selectedChatId = getSelectedChatId();

    if (selectedChatId === chatId) {
      selectedChatId = findFallbackChatId(chat.libraryId, chatId);
      setSelectedChatId(selectedChatId);
    }

    const archivedChat = getChatById(chatId);

    if (!archivedChat) {
      throw new Error("The archived chat could not be loaded.");
    }

    return {
      chat: archivedChat,
      selectedChatId,
    };
  });

  return archiveTransaction();
}

export function restoreChat(chatId: string): Chat {
  const chat = getChatById(chatId);

  if (!chat) {
    throw new AppError(404, "Chat not found.");
  }

  if (!chat.archivedAt) {
    throw new AppError(409, "Chat is already active.");
  }

  database
    .prepare(
      `
        UPDATE chats
        SET
          archived_at = NULL,
          updated_at = strftime(
            '%Y-%m-%dT%H:%M:%fZ',
            'now'
          )
        WHERE id = ?
      `,
    )
    .run(chatId);

  const restoredChat = getChatById(chatId);

  if (!restoredChat) {
    throw new Error("The restored chat could not be loaded.");
  }

  return restoredChat;
}

export function deleteChat(chatId: string): DeleteChatResult {
  const chat = getChatById(chatId);

  if (!chat) {
    throw new AppError(404, "Chat not found.");
  }

  const deleteTransaction = database.transaction(() => {
    database
      .prepare(
        `
          DELETE FROM chats
          WHERE id = ?
        `,
      )
      .run(chatId);

    let selectedChatId = getSelectedChatId();

    if (selectedChatId === chatId) {
      selectedChatId = findFallbackChatId(chat.libraryId, chatId);
      setSelectedChatId(selectedChatId);
    }

    return {
      selectedChatId,
    };
  });

  return deleteTransaction();
}

export function getMessagesByChatId(chatId: string): ChatMessage[] {
  requireActiveChat(chatId);

  const rows = database
    .prepare(
      `
        SELECT
          id,
          chat_id,
          role,
          content,
          status,
          created_at,
          updated_at
        FROM messages
        WHERE chat_id = ?
        ORDER BY created_at ASC, rowid ASC
      `,
    )
    .all(chatId) as MessageRow[];

  return rows.map(mapMessage);
}

export function getMessagePageByChatId(
  chatId: string,
  input: {
    limit: number;
    beforeMessageId?: string;
  },
): ChatMessagePage {
  requireActiveChat(chatId);

  let beforeRowId: number | null = null;

  if (input.beforeMessageId) {
    const cursor = database
      .prepare(
        `
          SELECT rowid
          FROM messages
          WHERE id = ?
            AND chat_id = ?
        `,
      )
      .get(input.beforeMessageId, chatId) as MessageCursorRow | undefined;

    if (!cursor) {
      throw new AppError(400, "Message cursor not found for this Chat.");
    }

    beforeRowId = cursor.rowid;
  }

  const rows = database
    .prepare(
      `
        SELECT
          id,
          chat_id,
          role,
          content,
          status,
          created_at,
          updated_at
        FROM messages
        WHERE chat_id = ?
          AND (? IS NULL OR rowid < ?)
        ORDER BY rowid DESC
        LIMIT ?
      `,
    )
    .all(chatId, beforeRowId, beforeRowId, input.limit + 1) as MessageRow[];

  const hasMore = rows.length > input.limit;
  const pageRows = rows.slice(0, input.limit).reverse();
  const messages = pageRows.map(mapMessage);

  return {
    messages,
    hasMore,
    nextBeforeMessageId:
      hasMore && messages.length > 0 ? messages[0].id : null,
  };
}

export function createMessage(
  chatId: string,
  input: CreateMessageInput,
): ChatMessage {
  requireActiveChat(chatId);

  const messageId = crypto.randomUUID();

  const createTransaction = database.transaction(() => {
    database
      .prepare(
        `
          INSERT INTO messages (
            id,
            chat_id,
            role,
            content,
            status
          )
          VALUES (?, ?, ?, ?, ?)
        `,
      )
      .run(
        messageId,
        chatId,
        input.role,
        input.content.trim(),
        input.status ?? "complete",
      );

    database
      .prepare(
        `
          UPDATE chats
          SET updated_at = strftime(
            '%Y-%m-%dT%H:%M:%fZ',
            'now'
          )
          WHERE id = ?
        `,
      )
      .run(chatId);

    const row = database
      .prepare(
        `
          SELECT
            id,
            chat_id,
            role,
            content,
            status,
            created_at,
            updated_at
          FROM messages
          WHERE id = ?
        `,
      )
      .get(messageId) as MessageRow | undefined;

    if (!row) {
      throw new Error("The newly created message could not be loaded.");
    }

    return mapMessage(row);
  });

  return createTransaction();
}

export function selectChat(chatId: string | null): string | null {
  if (chatId !== null) {
    requireActiveChat(chatId);
  }

  setSelectedChatId(chatId);

  return getSelectedChatId();
}

import { database } from "../../../database/database.js";
import { AppError } from "../../../errors/app-error.js";
import { getLibraryById } from "../../libraries/models/Library.js";
import { getLibraryFileById } from "../../libraries/models/LibraryFile.js";
import { requireActiveChat } from "./Chat.js";
import type {
  ChatFileAttachment,
  CreateChatFileAttachmentInput,
} from "../types/ChatTypes.js";

export const maximumChatFileAttachments = 8;

type ChatFileAttachmentRow = {
  id: string;
  chat_id: string;
  library_id: string;
  library_name: string;
  file_id: string;
  file_name: string;
  relative_path: string;
  extension: string;
  size_bytes: number;
  file_status: ChatFileAttachment["fileStatus"];
  created_at: string;
};

function mapChatFileAttachment(
  row: ChatFileAttachmentRow,
): ChatFileAttachment {
  return {
    id: row.id,
    chatId: row.chat_id,
    libraryId: row.library_id,
    libraryName: row.library_name,
    fileId: row.file_id,
    fileName: row.file_name,
    relativePath: row.relative_path,
    extension: row.extension,
    sizeBytes: row.size_bytes,
    fileStatus: row.file_status,
    createdAt: row.created_at,
  };
}

function getChatFileAttachmentById(
  chatId: string,
  attachmentId: string,
): ChatFileAttachment | null {
  const row = database
    .prepare(
      `
        SELECT
          chat_file_attachments.id,
          chat_file_attachments.chat_id,
          library_files.library_id,
          libraries.name AS library_name,
          library_files.id AS file_id,
          library_files.name AS file_name,
          library_files.relative_path,
          library_files.extension,
          library_files.size_bytes,
          library_files.status AS file_status,
          chat_file_attachments.created_at
        FROM chat_file_attachments
        INNER JOIN library_files
          ON library_files.id = chat_file_attachments.library_file_id
        INNER JOIN libraries
          ON libraries.id = library_files.library_id
        WHERE chat_file_attachments.id = ?
          AND chat_file_attachments.chat_id = ?
      `,
    )
    .get(attachmentId, chatId) as ChatFileAttachmentRow | undefined;

  return row ? mapChatFileAttachment(row) : null;
}

export function getChatFileAttachments(chatId: string): ChatFileAttachment[] {
  requireActiveChat(chatId);

  const rows = database
    .prepare(
      `
        SELECT
          chat_file_attachments.id,
          chat_file_attachments.chat_id,
          library_files.library_id,
          libraries.name AS library_name,
          library_files.id AS file_id,
          library_files.name AS file_name,
          library_files.relative_path,
          library_files.extension,
          library_files.size_bytes,
          library_files.status AS file_status,
          chat_file_attachments.created_at
        FROM chat_file_attachments
        INNER JOIN library_files
          ON library_files.id = chat_file_attachments.library_file_id
        INNER JOIN libraries
          ON libraries.id = library_files.library_id
        WHERE chat_file_attachments.chat_id = ?
        ORDER BY
          chat_file_attachments.created_at ASC,
          chat_file_attachments.rowid ASC
      `,
    )
    .all(chatId) as ChatFileAttachmentRow[];

  return rows.map(mapChatFileAttachment);
}

export function createChatFileAttachment(
  chatId: string,
  input: CreateChatFileAttachmentInput,
): ChatFileAttachment {
  requireActiveChat(chatId);

  const library = getLibraryById(input.libraryId);

  if (!library) {
    throw new AppError(404, "Library not found.");
  }

  if (library.archivedAt) {
    throw new AppError(409, "Files from archived Libraries cannot be attached.");
  }

  const file = getLibraryFileById(input.libraryId, input.fileId);

  if (!file) {
    throw new AppError(404, "Library file not found.");
  }

  if (file.status !== "available") {
    throw new AppError(
      409,
      `This file is marked ${file.status}. Rescan the Library before attaching it.`,
    );
  }

  const existingRow = database
    .prepare(
      `
        SELECT id
        FROM chat_file_attachments
        WHERE chat_id = ?
          AND library_file_id = ?
      `,
    )
    .get(chatId, input.fileId) as { id: string } | undefined;

  if (existingRow) {
    const existingAttachment = getChatFileAttachmentById(
      chatId,
      existingRow.id,
    );

    if (!existingAttachment) {
      throw new Error("The existing Chat attachment could not be loaded.");
    }

    return existingAttachment;
  }

  const currentCount = database
    .prepare(
      `
        SELECT COUNT(*) AS count
        FROM chat_file_attachments
        WHERE chat_id = ?
      `,
    )
    .get(chatId) as { count: number };

  if (currentCount.count >= maximumChatFileAttachments) {
    throw new AppError(
      409,
      `A Chat can have at most ${maximumChatFileAttachments} attached files.`,
    );
  }

  const attachmentId = crypto.randomUUID();

  database
    .prepare(
      `
        INSERT INTO chat_file_attachments (
          id,
          chat_id,
          library_file_id
        )
        VALUES (?, ?, ?)
      `,
    )
    .run(attachmentId, chatId, input.fileId);

  const attachment = getChatFileAttachmentById(chatId, attachmentId);

  if (!attachment) {
    throw new Error("The new Chat attachment could not be loaded.");
  }

  return attachment;
}

export function deleteChatFileAttachment(
  chatId: string,
  attachmentId: string,
): void {
  requireActiveChat(chatId);

  const result = database
    .prepare(
      `
        DELETE FROM chat_file_attachments
        WHERE id = ?
          AND chat_id = ?
      `,
    )
    .run(attachmentId, chatId);

  if (result.changes !== 1) {
    throw new AppError(404, "Chat attachment not found.");
  }
}

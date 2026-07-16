import type { RequestHandler } from "express";
import { AppError } from "../../../errors/app-error.js";
import {
  archiveChat,
  createChat,
  createMessage,
  deleteChat,
  getAllChats,
  getArchivedChats,
  getChatById,
  getMessagesByChatId,
  restoreChat,
  selectChat,
  updateChat,
} from "../models/Chat.js";
import {
  chatIdParamsSchema,
  createChatSchema,
  createMessageSchema,
  updateChatSchema,
} from "../schemas/ChatSchemas.js";

function parseChatId(params: unknown): string {
  const parsed = chatIdParamsSchema.safeParse(params);

  if (!parsed.success) {
    throw new AppError(400, "Invalid Chat ID.", parsed.error.flatten());
  }

  return parsed.data.chatId;
}

export const getChats: RequestHandler = (_request, response) => {
  response.json({
    ok: true,
    chats: getAllChats(),
  });
};

export const getArchivedChatList: RequestHandler = (_request, response) => {
  response.json({
    ok: true,
    chats: getArchivedChats(),
  });
};

export const getChat: RequestHandler = (request, response) => {
  const chatId = parseChatId(request.params);
  const chat = getChatById(chatId);

  if (!chat) {
    throw new AppError(404, "Chat not found.");
  }

  response.json({
    ok: true,
    chat,
  });
};

export const postChat: RequestHandler = (request, response) => {
  const body = createChatSchema.safeParse(request.body);

  if (!body.success) {
    throw new AppError(400, "Invalid Chat data.", body.error.flatten());
  }

  response.status(201).json({
    ok: true,
    chat: createChat(body.data),
  });
};

export const patchChat: RequestHandler = (request, response) => {
  const chatId = parseChatId(request.params);
  const body = updateChatSchema.safeParse(request.body);

  if (!body.success) {
    throw new AppError(400, "Invalid Chat data.", body.error.flatten());
  }

  response.json({
    ok: true,
    chat: updateChat(chatId, body.data),
  });
};

export const postArchiveChat: RequestHandler = (request, response) => {
  const chatId = parseChatId(request.params);

  response.json({
    ok: true,
    ...archiveChat(chatId),
  });
};

export const postRestoreChat: RequestHandler = (request, response) => {
  const chatId = parseChatId(request.params);

  response.json({
    ok: true,
    chat: restoreChat(chatId),
  });
};

export const removeChat: RequestHandler = (request, response) => {
  const chatId = parseChatId(request.params);

  response.json({
    ok: true,
    ...deleteChat(chatId),
  });
};

export const getChatMessages: RequestHandler = (request, response) => {
  const chatId = parseChatId(request.params);

  response.json({
    ok: true,
    messages: getMessagesByChatId(chatId),
  });
};

export const postChatMessage: RequestHandler = (request, response) => {
  const chatId = parseChatId(request.params);
  const body = createMessageSchema.safeParse(request.body);

  if (!body.success) {
    throw new AppError(400, "Invalid message data.", body.error.flatten());
  }

  response.status(201).json({
    ok: true,
    message: createMessage(chatId, body.data),
  });
};

export const patchSelectedChat: RequestHandler = (request, response) => {
  const body = chatIdParamsSchema
    .extend({
      chatId: chatIdParamsSchema.shape.chatId.nullable(),
    })
    .safeParse(request.body);

  if (!body.success) {
    throw new AppError(400, "Invalid selected Chat.", body.error.flatten());
  }

  response.json({
    ok: true,
    selectedChatId: selectChat(body.data.chatId),
  });
};

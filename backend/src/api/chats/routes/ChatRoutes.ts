import { Router } from "express";
import {
  getArchivedChatList,
  getChat,
  getChatMessages,
  getChats,
  patchChat,
  patchSelectedChat,
  postArchiveChat,
  postChat,
  postChatMessage,
  postRestoreChat,
  removeChat,
} from "../controllers/ChatController.js";

export const chatRouter = Router();

chatRouter.get("/", getChats);
chatRouter.get("/archived", getArchivedChatList);
chatRouter.post("/", postChat);
chatRouter.patch("/selected", patchSelectedChat);

chatRouter.get("/:chatId", getChat);
chatRouter.patch("/:chatId", patchChat);
chatRouter.delete("/:chatId", removeChat);

chatRouter.post("/:chatId/archive", postArchiveChat);
chatRouter.post("/:chatId/restore", postRestoreChat);

chatRouter.get("/:chatId/messages", getChatMessages);
chatRouter.post("/:chatId/messages", postChatMessage);

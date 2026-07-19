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
  postChatResponse,
  postRestoreChat,
  removeChat,
} from "../controllers/ChatController.js";

export const chatRouter = Router();

/* =========================================================
   COLLECTION ROUTES

   Fixed route names must stay above /:chatId.
   Otherwise Express treats "archived" as a Chat ID.
   ========================================================= */

chatRouter.get("/", getChats);
chatRouter.get("/archived", getArchivedChatList);

chatRouter.post("/", postChat);

chatRouter.patch("/selected", patchSelectedChat);

chatRouter.post("/:chatId/archive", postArchiveChat);
chatRouter.post("/:chatId/restore", postRestoreChat);

/* =========================================================
   INDIVIDUAL CHAT ROUTES
   ========================================================= */

chatRouter.get("/:chatId", getChat);

chatRouter.patch("/:chatId", patchChat);

chatRouter.delete("/:chatId", removeChat);

chatRouter.get("/:chatId/messages", getChatMessages);

chatRouter.post("/:chatId/messages", postChatMessage);

chatRouter.post("/:chatId/respond", postChatResponse);

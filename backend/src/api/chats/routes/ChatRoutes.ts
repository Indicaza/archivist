import { Router } from "express";
import {
  getArchivedChatList,
  getChatAttachmentList,
  getChat,
  getChatMessages,
  getChats,
  patchChat,
  patchSelectedChat,
  postArchiveChat,
  postChatAttachment,
  postChat,
  postChatMessage,
  postChatResponse,
  postRestoreChat,
  removeChat,
  removeChatAttachment,
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

chatRouter.get("/:chatId/attachments", getChatAttachmentList);
chatRouter.post("/:chatId/attachments", postChatAttachment);
chatRouter.delete(
  "/:chatId/attachments/:attachmentId",
  removeChatAttachment,
);

/* =========================================================
   INDIVIDUAL CHAT ROUTES
   ========================================================= */

chatRouter.get("/:chatId", getChat);

chatRouter.patch("/:chatId", patchChat);

chatRouter.delete("/:chatId", removeChat);

chatRouter.get("/:chatId/messages", getChatMessages);

chatRouter.post("/:chatId/messages", postChatMessage);

chatRouter.post("/:chatId/respond", postChatResponse);

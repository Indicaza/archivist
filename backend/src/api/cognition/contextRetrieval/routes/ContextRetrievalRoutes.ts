import { Router } from "express";
import {
  searchChatMessages,
  searchLibraryText,
} from "../controllers/ContextRetrievalController.js";

export const contextRetrievalRouter = Router();

contextRetrievalRouter.get("/messages", searchChatMessages);
contextRetrievalRouter.get("/library-files", searchLibraryText);

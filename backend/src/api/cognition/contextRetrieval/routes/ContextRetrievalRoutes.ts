import { Router } from "express";
import { searchChatMessages } from "../controllers/ContextRetrievalController.js";

export const contextRetrievalRouter = Router();

contextRetrievalRouter.get("/messages", searchChatMessages);

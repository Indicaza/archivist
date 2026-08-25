import { Router } from "express";
import {
  getAIEditProposal,
  getAIEditProposalsForChat,
  postApproveAIEditProposal,
  postRejectAIEditProposal,
  postUndoAIEditProposal,
} from "../controllers/AIEditController.js";

export const aiEditRouter = Router();

aiEditRouter.get("/chats/:chatId/proposals", getAIEditProposalsForChat);
aiEditRouter.get("/proposals/:proposalId", getAIEditProposal);
aiEditRouter.post("/proposals/:proposalId/approve", postApproveAIEditProposal);
aiEditRouter.post("/proposals/:proposalId/reject", postRejectAIEditProposal);
aiEditRouter.post("/proposals/:proposalId/undo", postUndoAIEditProposal);

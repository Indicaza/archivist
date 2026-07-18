import { Router } from "express";
import {
  getAgent,
  getAgents,
  getArchivedAgentList,
  patchAgent,
  postAgent,
  postArchiveAgent,
  postDuplicateAgent,
  postRestoreAgent,
  removeAgent,
} from "../controllers/AgentController.js";

export const agentRouter = Router();

agentRouter.get("/", getAgents);
agentRouter.get("/archived", getArchivedAgentList);

agentRouter.post("/", postAgent);

agentRouter.get("/:agentId", getAgent);
agentRouter.patch("/:agentId", patchAgent);
agentRouter.delete("/:agentId", removeAgent);

agentRouter.post("/:agentId/duplicate", postDuplicateAgent);
agentRouter.post("/:agentId/archive", postArchiveAgent);
agentRouter.post("/:agentId/restore", postRestoreAgent);

import { Router } from "express";
import {
  getAIRun,
  getAIRunEvents,
  postCancelAIRun,
} from "../controllers/AIRunController.js";

export const aiRunRouter = Router();

aiRunRouter.get("/:runId", getAIRun);
aiRunRouter.get("/:runId/events", getAIRunEvents);
aiRunRouter.post("/:runId/cancel", postCancelAIRun);

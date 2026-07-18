import { Router } from "express";
import {
  getAIModels,
  getAIProviderHealth,
  refreshAIModels,
} from "../controllers/AIModelController.js";

export const aiModelRouter = Router();

aiModelRouter.get("/models", getAIModels);

aiModelRouter.post("/models/refresh", refreshAIModels);

aiModelRouter.get("/providers", getAIProviderHealth);

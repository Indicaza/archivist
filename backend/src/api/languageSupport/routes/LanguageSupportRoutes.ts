import { Router } from "express";
import {
  getLanguageSupportConfig,
  getLanguageSupportSessions,
  postLanguageSupportSession,
} from "../controllers/LanguageSupportController.js";

export const languageSupportRouter = Router();

languageSupportRouter.get(
  "/config",
  getLanguageSupportConfig,
);
languageSupportRouter.get(
  "/sessions",
  getLanguageSupportSessions,
);
languageSupportRouter.post(
  "/sessions",
  postLanguageSupportSession,
);

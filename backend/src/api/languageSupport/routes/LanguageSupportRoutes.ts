import { Router } from "express";
import {
  getLanguageSupportConfig,
  getLanguageSupportEvents,
  getLanguageSupportSessions,
  postLanguageSupportEvents,
  postLanguageSupportSession,
} from "../controllers/LanguageSupportController.js";

export const languageSupportRouter = Router();

languageSupportRouter.get(
  "/config",
  getLanguageSupportConfig,
);
languageSupportRouter.get(
  "/events",
  getLanguageSupportEvents,
);
languageSupportRouter.post(
  "/events",
  postLanguageSupportEvents,
);
languageSupportRouter.get(
  "/sessions",
  getLanguageSupportSessions,
);
languageSupportRouter.post(
  "/sessions",
  postLanguageSupportSession,
);

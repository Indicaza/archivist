import { Router } from "express";
import {
  getCurrentAppState,
  patchSelectedLibrary,
} from "../controllers/AppStateController.js";

export const appStateRouter = Router();

appStateRouter.get("/", getCurrentAppState);
appStateRouter.patch(
  "/selected-library",
  patchSelectedLibrary,
);

import { Router } from "express";
import {
  getCurrentAppState,
  patchSelectedCollection,
  patchSelectedLibrary,
} from "../controllers/AppStateController.js";

export const appStateRouter = Router();

appStateRouter.get("/", getCurrentAppState);
appStateRouter.patch(
  "/selected-collection",
  patchSelectedCollection,
);
appStateRouter.patch(
  "/selected-library",
  patchSelectedLibrary,
);

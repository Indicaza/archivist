import { Router } from "express";
import {
  getArchivedLibraryList,
  getLibraries,
  getLibrary,
  patchLibrary,
  postArchiveLibrary,
  postLibrary,
  postRestoreLibrary,
} from "../controllers/LibraryController.js";

export const libraryRouter = Router();

libraryRouter.get("/", getLibraries);
libraryRouter.get("/archived", getArchivedLibraryList);
libraryRouter.post("/", postLibrary);
libraryRouter.get("/:libraryId", getLibrary);
libraryRouter.patch("/:libraryId", patchLibrary);
libraryRouter.post("/:libraryId/archive", postArchiveLibrary);
libraryRouter.post("/:libraryId/restore", postRestoreLibrary);

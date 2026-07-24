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
import {
  getLibraryFileContent,
  getLibraryFiles,
  patchLibraryFileLocation,
  postLibraryScan,
} from "../controllers/LibraryFileController.js";

export const libraryRouter = Router();

libraryRouter.get("/", getLibraries);
libraryRouter.get("/archived", getArchivedLibraryList);
libraryRouter.post("/", postLibrary);
libraryRouter.get("/:libraryId/files", getLibraryFiles);
libraryRouter.patch(
  "/:libraryId/files/:fileId",
  patchLibraryFileLocation,
);
libraryRouter.get(
  "/:libraryId/files/:fileId/content",
  getLibraryFileContent,
);
libraryRouter.post("/:libraryId/scan", postLibraryScan);
libraryRouter.get("/:libraryId", getLibrary);
libraryRouter.patch("/:libraryId", patchLibrary);
libraryRouter.post("/:libraryId/archive", postArchiveLibrary);
libraryRouter.post("/:libraryId/restore", postRestoreLibrary);

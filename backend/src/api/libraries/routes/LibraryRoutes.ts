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
  getLibraryGitStatus,
  patchLibraryFileLocation,
  patchLibraryFileName,
  postLibraryEntry,
  postLibraryEntryReveal,
  postLibraryFileDuplicate,
  postLibraryScan,
  putLibraryFileContent,
} from "../controllers/LibraryFileController.js";

export const libraryRouter = Router();

libraryRouter.get("/", getLibraries);
libraryRouter.get("/archived", getArchivedLibraryList);
libraryRouter.post("/", postLibrary);
libraryRouter.get("/:libraryId/git-status", getLibraryGitStatus);
libraryRouter.get("/:libraryId/files", getLibraryFiles);
libraryRouter.post("/:libraryId/files", postLibraryEntry);
libraryRouter.post("/:libraryId/reveal", postLibraryEntryReveal);
libraryRouter.patch(
  "/:libraryId/files/:fileId",
  patchLibraryFileLocation,
);
libraryRouter.patch(
  "/:libraryId/files/:fileId/name",
  patchLibraryFileName,
);
libraryRouter.post(
  "/:libraryId/files/:fileId/duplicate",
  postLibraryFileDuplicate,
);
libraryRouter.get(
  "/:libraryId/files/:fileId/content",
  getLibraryFileContent,
);
libraryRouter.put(
  "/:libraryId/files/:fileId/content",
  putLibraryFileContent,
);
libraryRouter.post("/:libraryId/scan", postLibraryScan);
libraryRouter.get("/:libraryId", getLibrary);
libraryRouter.patch("/:libraryId", patchLibrary);
libraryRouter.post("/:libraryId/archive", postArchiveLibrary);
libraryRouter.post("/:libraryId/restore", postRestoreLibrary);

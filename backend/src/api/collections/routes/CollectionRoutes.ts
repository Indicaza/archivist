import { Router } from "express";
import {
  getArchivedCollectionList,
  getCollection,
  getCollections,
  getCollectionWorkspaceScope,
  patchCollection,
  postArchiveCollection,
  postCollection,
  postRestoreCollection,
} from "../controllers/CollectionController.js";

export const collectionRouter = Router();

collectionRouter.get("/", getCollections);
collectionRouter.get("/archived", getArchivedCollectionList);
collectionRouter.post("/", postCollection);
collectionRouter.get("/:collectionId/scope", getCollectionWorkspaceScope);
collectionRouter.get("/:collectionId", getCollection);
collectionRouter.patch("/:collectionId", patchCollection);
collectionRouter.post("/:collectionId/archive", postArchiveCollection);
collectionRouter.post("/:collectionId/restore", postRestoreCollection);

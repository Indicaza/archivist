import type { RequestHandler } from "express";
import { AppError } from "../../../errors/app-error.js";
import {
  archiveCollection,
  createCollection,
  getAllCollections,
  getArchivedCollections,
  getCollectionById,
  getCollectionScope,
  restoreCollection,
  updateCollection,
} from "../models/Collection.js";
import {
  collectionIdParamsSchema,
  createCollectionSchema,
  updateCollectionSchema,
} from "../schemas/CollectionSchemas.js";

function parseCollectionId(params: unknown): string {
  const parsed = collectionIdParamsSchema.safeParse(params);

  if (!parsed.success) {
    throw new AppError(
      400,
      "Invalid Collection ID.",
      parsed.error.flatten(),
    );
  }

  return parsed.data.collectionId;
}

export const getCollections: RequestHandler = (_request, response) => {
  response.json({
    ok: true,
    collections: getAllCollections(),
  });
};

export const getArchivedCollectionList: RequestHandler = (
  _request,
  response,
) => {
  response.json({
    ok: true,
    collections: getArchivedCollections(),
  });
};

export const getCollection: RequestHandler = (request, response) => {
  const collectionId = parseCollectionId(request.params);
  const collection = getCollectionById(collectionId);

  if (!collection) {
    throw new AppError(404, "Collection not found.");
  }

  response.json({
    ok: true,
    collection,
  });
};

export const getCollectionWorkspaceScope: RequestHandler = (
  request,
  response,
) => {
  const collectionId = parseCollectionId(request.params);

  response.json({
    ok: true,
    scope: getCollectionScope(collectionId),
  });
};

export const postCollection: RequestHandler = (request, response) => {
  const body = createCollectionSchema.safeParse(request.body);

  if (!body.success) {
    throw new AppError(
      400,
      "Invalid Collection data.",
      body.error.flatten(),
    );
  }

  response.status(201).json({
    ok: true,
    collection: createCollection(body.data),
  });
};

export const patchCollection: RequestHandler = (request, response) => {
  const collectionId = parseCollectionId(request.params);
  const body = updateCollectionSchema.safeParse(request.body);

  if (!body.success) {
    throw new AppError(
      400,
      "Invalid Collection data.",
      body.error.flatten(),
    );
  }

  response.json({
    ok: true,
    collection: updateCollection(collectionId, body.data),
  });
};

export const postArchiveCollection: RequestHandler = (
  request,
  response,
) => {
  const collectionId = parseCollectionId(request.params);

  response.json({
    ok: true,
    ...archiveCollection(collectionId),
  });
};

export const postRestoreCollection: RequestHandler = (
  request,
  response,
) => {
  const collectionId = parseCollectionId(request.params);

  response.json({
    ok: true,
    collection: restoreCollection(collectionId),
  });
};

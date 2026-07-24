import type { RequestHandler } from "express";
import { AppError } from "../../../errors/app-error.js";
import {
  getAppState,
  setSelectedCollection,
  setSelectedLibrary,
} from "../models/AppState.js";
import {
  updateSelectedCollectionSchema,
  updateSelectedLibrarySchema,
} from "../schemas/AppStateSchemas.js";

export const getCurrentAppState: RequestHandler = (
  _request,
  response,
) => {
  response.json({
    ok: true,
    appState: getAppState(),
  });
};

export const patchSelectedLibrary: RequestHandler = (
  request,
  response,
) => {
  const body = updateSelectedLibrarySchema.safeParse(request.body);

  if (!body.success) {
    throw new AppError(
      400,
      "Invalid selected Library.",
      body.error.flatten(),
    );
  }

  response.json({
    ok: true,
    appState: setSelectedLibrary(body.data.selectedLibraryId),
  });
};

export const patchSelectedCollection: RequestHandler = (
  request,
  response,
) => {
  const body = updateSelectedCollectionSchema.safeParse(request.body);

  if (!body.success) {
    throw new AppError(
      400,
      "Invalid selected Collection.",
      body.error.flatten(),
    );
  }

  response.json({
    ok: true,
    appState: setSelectedCollection(body.data.selectedCollectionId),
  });
};

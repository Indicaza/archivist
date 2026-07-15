import type { RequestHandler } from "express";
import { AppError } from "../../../errors/app-error.js";
import {
  getAppState,
  setSelectedLibrary,
} from "../models/AppState.js";
import { updateSelectedLibrarySchema } from "../schemas/AppStateSchemas.js";

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

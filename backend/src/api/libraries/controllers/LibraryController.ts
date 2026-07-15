import type { RequestHandler } from "express";
import { AppError } from "../../../errors/app-error.js";
import {
  archiveLibrary,
  createLibrary,
  getAllLibraries,
  getArchivedLibraries,
  getLibraryById,
  restoreLibrary,
  updateLibrary,
} from "../models/Library.js";
import {
  createLibrarySchema,
  libraryIdParamsSchema,
  updateLibrarySchema,
} from "../schemas/LibrarySchemas.js";

function parseLibraryId(params: unknown): string {
  const parsed = libraryIdParamsSchema.safeParse(params);

  if (!parsed.success) {
    throw new AppError(
      400,
      "Invalid Library ID.",
      parsed.error.flatten(),
    );
  }

  return parsed.data.libraryId;
}

export const getLibraries: RequestHandler = (_request, response) => {
  response.json({
    ok: true,
    libraries: getAllLibraries(),
  });
};

export const getArchivedLibraryList: RequestHandler = (
  _request,
  response,
) => {
  response.json({
    ok: true,
    libraries: getArchivedLibraries(),
  });
};

export const getLibrary: RequestHandler = (request, response) => {
  const libraryId = parseLibraryId(request.params);
  const library = getLibraryById(libraryId);

  if (!library) {
    throw new AppError(404, "Library not found.");
  }

  response.json({
    ok: true,
    library,
  });
};

export const postLibrary: RequestHandler = (request, response) => {
  const body = createLibrarySchema.safeParse(request.body);

  if (!body.success) {
    throw new AppError(
      400,
      "Invalid Library data.",
      body.error.flatten(),
    );
  }

  const result = createLibrary(body.data);

  response.status(201).json({
    ok: true,
    ...result,
  });
};

export const patchLibrary: RequestHandler = (request, response) => {
  const libraryId = parseLibraryId(request.params);
  const body = updateLibrarySchema.safeParse(request.body);

  if (!body.success) {
    throw new AppError(
      400,
      "Invalid Library data.",
      body.error.flatten(),
    );
  }

  response.json({
    ok: true,
    library: updateLibrary(libraryId, body.data),
  });
};

export const postArchiveLibrary: RequestHandler = (
  request,
  response,
) => {
  const libraryId = parseLibraryId(request.params);
  const result = archiveLibrary(libraryId);

  response.json({
    ok: true,
    ...result,
  });
};

export const postRestoreLibrary: RequestHandler = (
  request,
  response,
) => {
  const libraryId = parseLibraryId(request.params);

  response.json({
    ok: true,
    library: restoreLibrary(libraryId),
  });
};

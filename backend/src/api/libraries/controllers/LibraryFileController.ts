import type { RequestHandler } from "express";
import { AppError } from "../../../errors/app-error.js";
import { getLibraryById } from "../models/Library.js";
import { getLibraryFileCatalog } from "../models/LibraryFile.js";
import {
  libraryFileIdParamsSchema,
  libraryIdParamsSchema,
} from "../schemas/LibrarySchemas.js";
import { readLibraryFilePreview } from "../services/LibraryFileReader.js";
import { scanLibraryFiles } from "../services/LibraryFileScanner.js";

function parseLibraryId(params: unknown): string {
  const parsed = libraryIdParamsSchema.safeParse(params);

  if (!parsed.success) {
    throw new AppError(400, "Invalid Library ID.", parsed.error.flatten());
  }

  return parsed.data.libraryId;
}

function parseLibraryFileIds(params: unknown): {
  libraryId: string;
  fileId: string;
} {
  const parsed = libraryFileIdParamsSchema.safeParse(params);

  if (!parsed.success) {
    throw new AppError(400, "Invalid Library file ID.", parsed.error.flatten());
  }

  return parsed.data;
}

export const getLibraryFiles: RequestHandler = (request, response) => {
  const libraryId = parseLibraryId(request.params);
  const library = getLibraryById(libraryId);

  if (!library) {
    throw new AppError(404, "Library not found.");
  }

  response.json({
    ok: true,
    ...getLibraryFileCatalog(libraryId),
  });
};

export const getLibraryFileContent: RequestHandler = async (
  request,
  response,
) => {
  const { libraryId, fileId } = parseLibraryFileIds(request.params);

  response.json({
    ok: true,
    preview: await readLibraryFilePreview(libraryId, fileId),
  });
};

export const postLibraryScan: RequestHandler = async (request, response) => {
  const libraryId = parseLibraryId(request.params);
  const result = await scanLibraryFiles(libraryId);

  response.json({
    ok: true,
    ...result,
  });
};

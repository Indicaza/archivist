import type { RequestHandler } from "express";
import { z } from "zod";
import { getLibraryById } from "../../../libraries/models/Library.js";
import { chatMessageFtsTool } from "../../../../core/cognition/retrieval/tools/ChatMessageFtsTool.js";
import { libraryFileFtsTool } from "../../../../core/cognition/retrieval/tools/LibraryFileFtsTool.js";
import { AppError } from "../../../../errors/app-error.js";

const messageSearchQuerySchema = z.object({
  q: z.string().trim().min(1).max(1_000),
  chatId: z.string().trim().min(1).optional(),
  limit: z.coerce.number().int().min(1).max(50).optional(),
});

const librarySearchQuerySchema = z.object({
  q: z.string().trim().min(1).max(1_000),
  libraryId: z.string().uuid(),
  limit: z.coerce.number().int().min(1).max(50).optional(),
});

export const searchChatMessages: RequestHandler = (request, response) => {
  const query = messageSearchQuerySchema.safeParse(request.query);

  if (!query.success) {
    throw new AppError(
      400,
      "Invalid Chat-message search query.",
      query.error.flatten(),
    );
  }

  const result = chatMessageFtsTool.search({
    query: query.data.q,
    chatId: query.data.chatId,
    limit: query.data.limit,
  });

  response.json({
    ok: true,
    result,
  });
};

export const searchLibraryText: RequestHandler = (request, response) => {
  const query = librarySearchQuerySchema.safeParse(request.query);

  if (!query.success) {
    throw new AppError(
      400,
      "Invalid Library-text search query.",
      query.error.flatten(),
    );
  }

  const library = getLibraryById(query.data.libraryId);

  if (!library) {
    throw new AppError(404, "Library not found.");
  }

  if (library.archivedAt) {
    throw new AppError(409, "Archived Libraries cannot be searched.");
  }

  const result = libraryFileFtsTool.search({
    query: query.data.q,
    libraryId: query.data.libraryId,
    limit: query.data.limit,
  });

  response.json({
    ok: true,
    result,
  });
};

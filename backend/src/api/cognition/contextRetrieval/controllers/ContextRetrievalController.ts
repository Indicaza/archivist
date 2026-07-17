import type { RequestHandler } from "express";
import { z } from "zod";
import { chatMessageFtsTool } from "../../../../core/cognition/retrieval/tools/ChatMessageFtsTool.js";
import { AppError } from "../../../../errors/app-error.js";

const messageSearchQuerySchema = z.object({
  q: z.string().trim().min(1).max(1_000),
  chatId: z.string().trim().min(1).optional(),
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

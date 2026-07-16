import { z } from "zod";

export const chatIdParamsSchema = z.object({
  chatId: z.string().uuid(),
});

export const createChatSchema = z.object({
  title: z.string().trim().min(1).max(120).optional(),
});

export const updateChatSchema = z.object({
  title: z.string().trim().min(1).max(120),
});

export const createMessageSchema = z.object({
  role: z.enum(["user", "assistant", "system"]),

  content: z.string().trim().min(1).max(100_000),

  status: z.enum(["streaming", "complete", "cancelled", "failed"]).optional(),
});

export const completeChatTurnSchema = z.object({
  content: z
    .string()
    .trim()
    .min(1, "Message content is required.")
    .max(100_000),
});

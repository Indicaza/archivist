import { z } from "zod";

export const chatIdParamsSchema = z.object({
  chatId: z.string().uuid(),
});

export const createChatSchema = z.object({
  title: z.string().trim().min(1).max(120).optional(),
  agentId: z.string().uuid().optional(),
});

export const updateChatSchema = z
  .object({
    title: z.string().trim().min(1).max(120).optional(),
    agentId: z.string().uuid().optional(),
  })
  .refine((input) => Object.keys(input).length > 0, {
    message: "At least one Chat field must be supplied.",
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

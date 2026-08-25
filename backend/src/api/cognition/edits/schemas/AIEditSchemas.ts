import { z } from "zod";

export const aiEditProposalIdParamsSchema = z.object({
  proposalId: z.string().uuid(),
});

export const aiEditChatIdParamsSchema = z.object({
  chatId: z.string().uuid(),
});

export const aiEditProposalApprovalSchema = z.object({
  operationIds: z.array(z.string().uuid()).min(1).max(64).optional(),
}).strict();

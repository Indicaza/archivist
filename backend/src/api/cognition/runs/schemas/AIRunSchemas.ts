import { z } from "zod";

export const aiRunIdParamsSchema = z.object({
  runId: z.string().uuid(),
});

export const aiRunEventQuerySchema = z.object({
  after: z.coerce.number().int().min(0).optional(),
});

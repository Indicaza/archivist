import { z } from "zod";

const terminalDimensionSchema = z.coerce
  .number()
  .int()
  .min(2)
  .max(500);

export const terminalSocketQuerySchema = z.object({
  sessionId: z
    .string()
    .trim()
    .min(1)
    .max(80)
    .regex(/^[A-Za-z0-9_-]+$/),
  collectionId: z.string().uuid(),
  libraryId: z.string().uuid(),
  cols: terminalDimensionSchema.default(120),
  rows: terminalDimensionSchema.default(30),
});

export const terminalClientMessageSchema = z.discriminatedUnion(
  "type",
  [
    z.object({
      type: z.literal("input"),
      data: z.string().max(65536),
    }),
    z.object({
      type: z.literal("resize"),
      cols: terminalDimensionSchema,
      rows: terminalDimensionSchema,
    }),
    z.object({
      type: z.literal("kill"),
    }),
    z.object({
      type: z.literal("ping"),
      sentAt: z.number().finite(),
    }),
  ],
);

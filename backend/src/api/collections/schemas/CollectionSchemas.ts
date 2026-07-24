import { z } from "zod";

const uuidListSchema = z.array(z.string().uuid()).max(500);

export const collectionIdParamsSchema = z.object({
  collectionId: z.string().uuid(),
});

export const createCollectionSchema = z.object({
  name: z.string().trim().min(1).max(120),
  parentCollectionId: z.string().uuid().nullable().optional(),
  position: z.number().int().min(0).optional(),
  libraryIds: uuidListSchema.optional(),
  chatIds: uuidListSchema.optional(),
  agentIds: uuidListSchema.optional(),
  defaultAgentId: z.string().uuid().nullable().optional(),
});

export const updateCollectionSchema = z
  .object({
    name: z.string().trim().min(1).max(120).optional(),
    parentCollectionId: z.string().uuid().nullable().optional(),
    position: z.number().int().min(0).optional(),
    libraryIds: uuidListSchema.optional(),
    chatIds: uuidListSchema.optional(),
    agentIds: uuidListSchema.optional(),
    defaultAgentId: z.string().uuid().nullable().optional(),
  })
  .refine((value) => Object.keys(value).length > 0, {
    message: "At least one Collection field must be provided.",
  });

import { z } from "zod";

export const createLibrarySchema = z.object({
  rootPath: z.string().trim().min(1),
  name: z.string().trim().min(1).max(120).optional(),
  description: z.string().trim().max(500).optional(),
});

export const updateLibrarySchema = z
  .object({
    name: z.string().trim().min(1).max(120).optional(),
    description: z.string().trim().max(500).nullable().optional(),
  })
  .refine(
    (value) =>
      value.name !== undefined || value.description !== undefined,
    {
      message: "At least one Library field must be provided.",
    },
  );

export const libraryIdParamsSchema = z.object({
  libraryId: z.string().uuid(),
});

export const libraryFileIdParamsSchema = libraryIdParamsSchema.extend({
  fileId: z.string().uuid(),
});

export const moveLibraryFileSchema = z.object({
  targetDirectory: z.string().trim().max(2000),
});

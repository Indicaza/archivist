import { z } from "zod";

export const updateSelectedLibrarySchema = z.object({
  selectedLibraryId: z.string().uuid().nullable(),
});

export const updateSelectedCollectionSchema = z.object({
  selectedCollectionId: z.string().uuid().nullable(),
});

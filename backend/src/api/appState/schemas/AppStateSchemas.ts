import { z } from "zod";

export const updateSelectedLibrarySchema = z.object({
  selectedLibraryId: z.string().uuid().nullable(),
});

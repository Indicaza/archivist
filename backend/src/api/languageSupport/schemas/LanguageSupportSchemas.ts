import { z } from "zod";

const optionalPath = z
  .string()
  .trim()
  .min(1)
  .max(8_192)
  .optional();

export const languageSupportQuerySchema = z.object({
  libraryId: z.string().uuid().optional(),
  workspaceRoot: optionalPath,
  filePath: optionalPath,
  serverId: z
    .string()
    .trim()
    .min(1)
    .max(100)
    .optional(),
}).superRefine((value, context) => {
  if (
    (value.workspaceRoot || value.filePath)
    && !value.libraryId
  ) {
    context.addIssue({
      code: "custom",
      message:
        "libraryId is required when resolving a workspace.",
      path: ["libraryId"],
    });
  }
});

export const languageSupportSessionRequestSchema = z.object({
  libraryId: z.string().uuid(),
  serverId: z
    .string()
    .trim()
    .min(1)
    .max(100),
  workspaceRoot: optionalPath,
  filePath: optionalPath,
});

export const languageSupportSocketQuerySchema = z.object({
  sessionId: z.string().uuid(),
  token: z
    .string()
    .regex(/^[a-f0-9]{64}$/i),
});

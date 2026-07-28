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


const languageSupportDiagnosticEventDetailSchema = z.object({
  owner: z.string().trim().min(1).max(100).optional(),
  severity: z.enum([
    "error",
    "warning",
    "info",
    "hint",
  ]),
  line: z.number().int().min(1).max(10_000_000),
  column: z.number().int().min(1).max(10_000_000),
  message: z.string().trim().min(1).max(4_000),
}).strict();

export const languageSupportClientEventSchema = z.object({
  kind: z.enum([
    "document-classified",
    "initialized",
    "document-open",
    "diagnostics",
    "markers",
    "disconnected",
    "client-error",
  ]),
  provider: z.enum(["lsp", "monaco"]).optional(),
  serverId: z.string().trim().min(1).max(100).optional(),
  workspaceRoot: optionalPath,
  filePath: optionalPath,
  uri: optionalPath,
  monacoLanguageId: z.string().trim().min(1).max(100).optional(),
  lspLanguageId: z.string().trim().min(1).max(100).optional(),
  version: z.number().int().min(0).max(2_147_483_647).optional(),
  capabilities: z.array(
    z.string().trim().min(1).max(100),
  ).max(100).optional(),
  diagnostics: z.object({
    total: z.number().int().min(0).max(1_000_000),
    errors: z.number().int().min(0).max(1_000_000),
    warnings: z.number().int().min(0).max(1_000_000),
    info: z.number().int().min(0).max(1_000_000),
    hints: z.number().int().min(0).max(1_000_000),
    details: z.array(
      languageSupportDiagnosticEventDetailSchema,
    ).max(8).optional(),
  }).strict().optional(),
  message: z.string().trim().min(1).max(4_000).optional(),
}).strict();

export const languageSupportClientEventsRequestSchema = z.object({
  events: z.array(languageSupportClientEventSchema)
    .min(1)
    .max(50),
}).strict();

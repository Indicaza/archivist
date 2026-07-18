import { z } from "zod";

const nullableTrimmedString = (maximumLength: number) =>
  z.string().trim().max(maximumLength).nullable();

const stringListSchema = z.array(z.string().trim().min(1).max(500)).max(100);

export const agentIdParamsSchema = z.object({
  agentId: z.string().uuid(),
});

export const agentIdentityConfigSchema = z.object({
  personality: nullableTrimmedString(2_000),
  temperament: nullableTrimmedString(2_000),
  voice: nullableTrimmedString(2_000),
  backstory: nullableTrimmedString(10_000),
});

export const agentProfessionConfigSchema = z.object({
  jobTitle: nullableTrimmedString(500),
  mission: nullableTrimmedString(10_000),
  expertise: stringListSchema,
  responsibilities: stringListSchema,
  successCriteria: stringListSchema,
  limitations: stringListSchema,
});

export const agentOutputContractSchema = z.object({
  responseStyle: nullableTrimmedString(5_000),
  verbosity: z.enum(["concise", "balanced", "detailed"]),
  formattingRules: stringListSchema,
  codeOutputPreferences: stringListSchema,
  citationRequirements: nullableTrimmedString(5_000),
  followUpBehavior: nullableTrimmedString(5_000),
});

export const generationConfigSchema = z.object({
  provider: z.literal("openai"),

  model: z.string().trim().min(1).max(200),

  temperature: z.number().min(0).max(2).nullable(),

  maxOutputTokens: z.number().int().min(1).max(128_000).nullable(),

  topP: z.number().min(0).max(1).nullable(),

  frequencyPenalty: z.number().min(-2).max(2).nullable(),

  presencePenalty: z.number().min(-2).max(2).nullable(),
});

export const agentContextSettingsSchema = z.object({
  compiler: z.object({
    id: z.string().trim().min(1).max(100),
    version: z.number().int().positive(),
  }),

  config: z.record(z.string(), z.unknown()),
});

export const createAgentSchema = z.object({
  name: z.string().trim().min(1).max(120).optional(),

  description: nullableTrimmedString(5_000).optional(),

  identity: agentIdentityConfigSchema.partial().optional(),

  profession: agentProfessionConfigSchema.partial().optional(),

  doctrine: nullableTrimmedString(20_000).optional(),

  outputContract: agentOutputContractSchema.partial().optional(),

  systemInstructions: nullableTrimmedString(50_000).optional(),

  generation: generationConfigSchema.partial().optional(),

  context: agentContextSettingsSchema.optional(),
});

export const updateAgentSchema = createAgentSchema
  .partial()
  .refine((input) => Object.keys(input).length > 0, {
    message: "At least one Agent field must be supplied.",
  });

export const duplicateAgentSchema = z.object({
  name: z.string().trim().min(1).max(120).optional(),
});

import type { RequestHandler } from "express";
import { modelRegistry } from "../../../core/ai/ModelRegistry.js";
import { contextCompilerRegistry } from "../../../core/cognition/conscious/context/ContextCompilerRegistry.js";
import { AppError } from "../../../errors/app-error.js";
import {
  archiveAgent,
  createAgent,
  deleteAgent,
  duplicateAgent,
  getAgentById,
  getAllAgents,
  getArchivedAgents,
  requireAgent,
  restoreAgent,
  updateAgent,
} from "../models/Agent.js";
import {
  agentIdParamsSchema,
  createAgentSchema,
  duplicateAgentSchema,
  updateAgentSchema,
} from "../schemas/AgentSchemas.js";

function parseAgentId(params: unknown): string {
  const parsed = agentIdParamsSchema.safeParse(params);

  if (!parsed.success) {
    throw new AppError(400, "Invalid Agent ID.", parsed.error.flatten());
  }

  return parsed.data.agentId;
}

function validateContextSettings(
  context:
    | {
        compiler: {
          id: string;
          version: number;
        };
        config: Record<string, unknown>;
      }
    | undefined,
): void {
  if (!context) {
    return;
  }

  try {
    contextCompilerRegistry.parseConfig(context.compiler, context.config);
  } catch (error) {
    throw new AppError(
      400,
      error instanceof Error
        ? error.message
        : "Invalid Context Compiler configuration.",
    );
  }
}

function validateGenerationSettings(
  generation:
    | {
        provider?: "openai";
        model?: string;
        temperature?: number | null;
        maxOutputTokens?: number | null;
        topP?: number | null;
        frequencyPenalty?: number | null;
        presencePenalty?: number | null;
      }
    | undefined,
  currentGeneration?: {
    provider: "openai";
    model: string;
    temperature: number | null;
    maxOutputTokens: number | null;
    topP: number | null;
    frequencyPenalty: number | null;
    presencePenalty: number | null;
  },
): void {
  if (!generation) {
    return;
  }

  const provider =
    generation.provider ?? currentGeneration?.provider ?? "openai";

  const model = generation.model ?? currentGeneration?.model ?? "gpt-5-mini";

  const definition = modelRegistry.getDefinition(provider, model);

  const mergedGeneration = {
    ...currentGeneration,
    ...generation,
  };

  const controlValues = {
    temperature: mergedGeneration.temperature,
    maxOutputTokens: mergedGeneration.maxOutputTokens,
    topP: mergedGeneration.topP,
    frequencyPenalty: mergedGeneration.frequencyPenalty,
    presencePenalty: mergedGeneration.presencePenalty,
  };

  for (const [control, value] of Object.entries(controlValues)) {
    if (
      value !== null &&
      value !== undefined &&
      !definition.supportedControls.includes(
        control as keyof typeof controlValues,
      )
    ) {
      throw new AppError(
        400,
        `Model "${model}" does not support the "${control}" control.`,
      );
    }
  }

  if (
    mergedGeneration.maxOutputTokens !== null &&
    mergedGeneration.maxOutputTokens !== undefined &&
    definition.maximumOutputTokens !== null &&
    mergedGeneration.maxOutputTokens > definition.maximumOutputTokens
  ) {
    throw new AppError(
      400,
      `Model "${model}" supports at most ${definition.maximumOutputTokens} output tokens.`,
    );
  }

  if (
    mergedGeneration.temperature !== null &&
    mergedGeneration.temperature !== undefined &&
    mergedGeneration.topP !== null &&
    mergedGeneration.topP !== undefined
  ) {
    throw new AppError(400, "Set either temperature or top-p, not both.");
  }
}

export const getAgents: RequestHandler = (_request, response) => {
  response.json({
    ok: true,
    agents: getAllAgents(),
  });
};

export const getArchivedAgentList: RequestHandler = (_request, response) => {
  response.json({
    ok: true,
    agents: getArchivedAgents(),
  });
};

export const getAgent: RequestHandler = (request, response) => {
  const agentId = parseAgentId(request.params);
  const agent = getAgentById(agentId);

  if (!agent) {
    throw new AppError(404, "Agent not found.");
  }

  response.json({
    ok: true,
    agent,
  });
};

export const postAgent: RequestHandler = (request, response) => {
  const body = createAgentSchema.safeParse(request.body);

  if (!body.success) {
    throw new AppError(400, "Invalid Agent data.", body.error.flatten());
  }

  validateContextSettings(body.data.context);
  validateGenerationSettings(body.data.generation);

  response.status(201).json({
    ok: true,
    agent: createAgent(body.data),
  });
};

export const patchAgent: RequestHandler = (request, response) => {
  const agentId = parseAgentId(request.params);
  const body = updateAgentSchema.safeParse(request.body);

  if (!body.success) {
    throw new AppError(400, "Invalid Agent data.", body.error.flatten());
  }

  validateContextSettings(body.data.context);

  const currentAgent = requireAgent(agentId);

  validateGenerationSettings(body.data.generation, currentAgent.generation);

  response.json({
    ok: true,
    agent: updateAgent(agentId, body.data),
  });
};

export const postDuplicateAgent: RequestHandler = (request, response) => {
  const agentId = parseAgentId(request.params);
  const body = duplicateAgentSchema.safeParse(request.body);

  if (!body.success) {
    throw new AppError(
      400,
      "Invalid Agent duplication data.",
      body.error.flatten(),
    );
  }

  response.status(201).json({
    ok: true,
    agent: duplicateAgent(agentId, body.data),
  });
};

export const postArchiveAgent: RequestHandler = (request, response) => {
  const agentId = parseAgentId(request.params);

  response.json({
    ok: true,
    ...archiveAgent(agentId),
  });
};

export const postRestoreAgent: RequestHandler = (request, response) => {
  const agentId = parseAgentId(request.params);

  response.json({
    ok: true,
    agent: restoreAgent(agentId),
  });
};

export const removeAgent: RequestHandler = (request, response) => {
  const agentId = parseAgentId(request.params);

  response.json({
    ok: true,
    ...deleteAgent(agentId),
  });
};

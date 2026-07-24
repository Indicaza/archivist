import { database } from "../../../database/database.js";
import { AppError } from "../../../errors/app-error.js";
import {
  agentIdentityConfigSchema,
  agentOutputContractSchema,
  agentProfessionConfigSchema,
  generationConfigSchema,
} from "../schemas/AgentSchemas.js";
import type {
  Agent,
  AgentContextSettings,
  AgentIdentityConfig,
  AgentOutputContract,
  AgentProfessionConfig,
  ArchiveAgentResult,
  CreateAgentInput,
  DeleteAgentResult,
  DuplicateAgentInput,
  GenerationConfig,
  UpdateAgentInput,
} from "../types/AgentTypes.js";
import {
  ARCHIVIST_DEFAULT_AGENT_ID,
  DEFAULT_AGENT_CONTEXT,
  DEFAULT_AGENT_IDENTITY,
  DEFAULT_AGENT_OUTPUT_CONTRACT,
  DEFAULT_AGENT_PROFESSION,
  DEFAULT_AGENT_SYSTEM_INSTRUCTIONS,
  DEFAULT_GENERATION_CONFIG,
} from "./AgentDefaults.js";

type AgentRow = {
  id: string;
  name: string;
  description: string | null;
  identity_config: string;
  profession_config: string;
  doctrine: string | null;
  output_contract: string;
  system_instructions: string | null;
  generation_config: string;
  context_compiler_id: string;
  context_compiler_version: number;
  context_compiler_config: string;
  is_built_in: number;
  archived_at: string | null;
  created_at: string;
  updated_at: string;
};

type CountRow = {
  count: number;
};

function parseJson(value: string): unknown {
  try {
    return JSON.parse(value) as unknown;
  } catch {
    return null;
  }
}

function parseRecord(
  value: string,
  fallback: Record<string, unknown>,
): Record<string, unknown> {
  const parsed = parseJson(value);

  if (typeof parsed === "object" && parsed !== null && !Array.isArray(parsed)) {
    return parsed as Record<string, unknown>;
  }

  return { ...fallback };
}

function parseIdentityConfig(value: string): AgentIdentityConfig {
  const result = agentIdentityConfigSchema.safeParse(parseJson(value));

  return result.success ? result.data : { ...DEFAULT_AGENT_IDENTITY };
}

function parseProfessionConfig(value: string): AgentProfessionConfig {
  const result = agentProfessionConfigSchema.safeParse(parseJson(value));

  return result.success
    ? result.data
    : {
        ...DEFAULT_AGENT_PROFESSION,
        expertise: [...DEFAULT_AGENT_PROFESSION.expertise],
        responsibilities: [...DEFAULT_AGENT_PROFESSION.responsibilities],
        successCriteria: [...DEFAULT_AGENT_PROFESSION.successCriteria],
        limitations: [...DEFAULT_AGENT_PROFESSION.limitations],
      };
}

function parseOutputContract(value: string): AgentOutputContract {
  const result = agentOutputContractSchema.safeParse(parseJson(value));

  return result.success
    ? result.data
    : {
        ...DEFAULT_AGENT_OUTPUT_CONTRACT,
        formattingRules: [...DEFAULT_AGENT_OUTPUT_CONTRACT.formattingRules],
        codeOutputPreferences: [
          ...DEFAULT_AGENT_OUTPUT_CONTRACT.codeOutputPreferences,
        ],
      };
}

function parseGenerationConfig(value: string): GenerationConfig {
  const result = generationConfigSchema.safeParse(parseJson(value));

  return result.success ? result.data : { ...DEFAULT_GENERATION_CONFIG };
}

function mapAgent(row: AgentRow): Agent {
  return {
    id: row.id,
    name: row.name,
    description: row.description,
    identity: parseIdentityConfig(row.identity_config),
    profession: parseProfessionConfig(row.profession_config),
    doctrine: row.doctrine,
    outputContract: parseOutputContract(row.output_contract),
    systemInstructions: row.system_instructions,
    generation: parseGenerationConfig(row.generation_config),
    context: {
      compiler: {
        id: row.context_compiler_id,
        version: row.context_compiler_version,
      },
      config: parseRecord(
        row.context_compiler_config,
        DEFAULT_AGENT_CONTEXT.config,
      ),
    },
    isBuiltIn: row.is_built_in === 1,
    archivedAt: row.archived_at,
    createdAt: row.created_at,
    updatedAt: row.updated_at,
  };
}

function cloneIdentity(
  current: AgentIdentityConfig,
  update?: Partial<AgentIdentityConfig>,
): AgentIdentityConfig {
  return {
    ...current,
    ...update,
  };
}

function cloneProfession(
  current: AgentProfessionConfig,
  update?: Partial<AgentProfessionConfig>,
): AgentProfessionConfig {
  return {
    ...current,
    ...update,
    expertise: update?.expertise
      ? [...update.expertise]
      : [...current.expertise],
    responsibilities: update?.responsibilities
      ? [...update.responsibilities]
      : [...current.responsibilities],
    successCriteria: update?.successCriteria
      ? [...update.successCriteria]
      : [...current.successCriteria],
    limitations: update?.limitations
      ? [...update.limitations]
      : [...current.limitations],
  };
}

function cloneOutputContract(
  current: AgentOutputContract,
  update?: Partial<AgentOutputContract>,
): AgentOutputContract {
  return {
    ...current,
    ...update,
    formattingRules: update?.formattingRules
      ? [...update.formattingRules]
      : [...current.formattingRules],
    codeOutputPreferences: update?.codeOutputPreferences
      ? [...update.codeOutputPreferences]
      : [...current.codeOutputPreferences],
  };
}

function cloneGeneration(
  current: GenerationConfig,
  update?: Partial<GenerationConfig>,
): GenerationConfig {
  return {
    ...current,
    ...update,
  };
}

function getAssignedChatCount(agentId: string): number {
  const row = database
    .prepare(
      `
        SELECT COUNT(DISTINCT chat_id) AS count
        FROM chat_agents
        WHERE agent_id = ?
      `,
    )
    .get(agentId) as CountRow;

  return row.count;
}

export function getAllAgents(): Agent[] {
  const rows = database
    .prepare(
      `
        SELECT
          id,
          name,
          description,
          identity_config,
          profession_config,
          doctrine,
          output_contract,
          system_instructions,
          generation_config,
          context_compiler_id,
          context_compiler_version,
          context_compiler_config,
          is_built_in,
          archived_at,
          created_at,
          updated_at
        FROM agents
        WHERE archived_at IS NULL
        ORDER BY
          is_built_in DESC,
          updated_at DESC,
          created_at DESC
      `,
    )
    .all() as AgentRow[];

  return rows.map(mapAgent);
}

export function getArchivedAgents(): Agent[] {
  const rows = database
    .prepare(
      `
        SELECT
          id,
          name,
          description,
          identity_config,
          profession_config,
          doctrine,
          output_contract,
          system_instructions,
          generation_config,
          context_compiler_id,
          context_compiler_version,
          context_compiler_config,
          is_built_in,
          archived_at,
          created_at,
          updated_at
        FROM agents
        WHERE archived_at IS NOT NULL
        ORDER BY archived_at DESC
      `,
    )
    .all() as AgentRow[];

  return rows.map(mapAgent);
}

export function getAgentById(agentId: string): Agent | null {
  const row = database
    .prepare(
      `
        SELECT
          id,
          name,
          description,
          identity_config,
          profession_config,
          doctrine,
          output_contract,
          system_instructions,
          generation_config,
          context_compiler_id,
          context_compiler_version,
          context_compiler_config,
          is_built_in,
          archived_at,
          created_at,
          updated_at
        FROM agents
        WHERE id = ?
      `,
    )
    .get(agentId) as AgentRow | undefined;

  return row ? mapAgent(row) : null;
}

export function requireAgent(agentId: string): Agent {
  const agent = getAgentById(agentId);

  if (!agent) {
    throw new AppError(404, "Agent not found.");
  }

  return agent;
}

export function requireActiveAgent(agentId: string): Agent {
  const agent = requireAgent(agentId);

  if (agent.archivedAt) {
    throw new AppError(
      409,
      "This Agent is archived. Restore it before assigning or using it.",
    );
  }

  return agent;
}

export function createAgent(input: CreateAgentInput): Agent {
  const agentId = crypto.randomUUID();

  const name = input.name?.trim() || "New Agent";
  const description = input.description?.trim() || null;

  const identity = cloneIdentity(DEFAULT_AGENT_IDENTITY, input.identity);

  const profession = cloneProfession(
    DEFAULT_AGENT_PROFESSION,
    input.profession,
  );

  const outputContract = cloneOutputContract(
    DEFAULT_AGENT_OUTPUT_CONTRACT,
    input.outputContract,
  );

  const generation = cloneGeneration(
    DEFAULT_GENERATION_CONFIG,
    input.generation,
  );

  const context: AgentContextSettings = input.context ?? DEFAULT_AGENT_CONTEXT;

  database
    .prepare(
      `
        INSERT INTO agents (
          id,
          name,
          description,
          identity_config,
          profession_config,
          doctrine,
          output_contract,
          system_instructions,
          generation_config,
          context_compiler_id,
          context_compiler_version,
          context_compiler_config,
          is_built_in
        )
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0)
      `,
    )
    .run(
      agentId,
      name,
      description,
      JSON.stringify(identity),
      JSON.stringify(profession),
      input.doctrine?.trim() || null,
      JSON.stringify(outputContract),
      input.systemInstructions?.trim() || DEFAULT_AGENT_SYSTEM_INSTRUCTIONS,
      JSON.stringify(generation),
      context.compiler.id,
      context.compiler.version,
      JSON.stringify(context.config),
    );

  const agent = getAgentById(agentId);

  if (!agent) {
    throw new Error("The newly created Agent could not be loaded.");
  }

  return agent;
}

export function updateAgent(agentId: string, input: UpdateAgentInput): Agent {
  const currentAgent = requireAgent(agentId);

  const name = input.name === undefined ? currentAgent.name : input.name.trim();

  const description =
    input.description === undefined
      ? currentAgent.description
      : input.description?.trim() || null;

  const identity = cloneIdentity(currentAgent.identity, input.identity);

  const profession = cloneProfession(currentAgent.profession, input.profession);

  const doctrine =
    input.doctrine === undefined
      ? currentAgent.doctrine
      : input.doctrine?.trim() || null;

  const outputContract = cloneOutputContract(
    currentAgent.outputContract,
    input.outputContract,
  );

  const systemInstructions =
    input.systemInstructions === undefined
      ? currentAgent.systemInstructions
      : input.systemInstructions?.trim() || null;

  const generation = cloneGeneration(currentAgent.generation, input.generation);

  const context = input.context ?? currentAgent.context;

  database
    .prepare(
      `
        UPDATE agents
        SET
          name = ?,
          description = ?,
          identity_config = ?,
          profession_config = ?,
          doctrine = ?,
          output_contract = ?,
          system_instructions = ?,
          generation_config = ?,
          context_compiler_id = ?,
          context_compiler_version = ?,
          context_compiler_config = ?,
          updated_at = strftime(
            '%Y-%m-%dT%H:%M:%fZ',
            'now'
          )
        WHERE id = ?
      `,
    )
    .run(
      name,
      description,
      JSON.stringify(identity),
      JSON.stringify(profession),
      doctrine,
      JSON.stringify(outputContract),
      systemInstructions,
      JSON.stringify(generation),
      context.compiler.id,
      context.compiler.version,
      JSON.stringify(context.config),
      agentId,
    );

  const updatedAgent = getAgentById(agentId);

  if (!updatedAgent) {
    throw new Error("The updated Agent could not be loaded.");
  }

  return updatedAgent;
}

export function duplicateAgent(
  agentId: string,
  input: DuplicateAgentInput,
): Agent {
  const sourceAgent = requireAgent(agentId);

  return createAgent({
    name: input.name?.trim() || `${sourceAgent.name} Copy`,
    description: sourceAgent.description,
    identity: sourceAgent.identity,
    profession: sourceAgent.profession,
    doctrine: sourceAgent.doctrine,
    outputContract: sourceAgent.outputContract,
    systemInstructions: sourceAgent.systemInstructions,
    generation: sourceAgent.generation,
    context: sourceAgent.context,
  });
}

export function archiveAgent(agentId: string): ArchiveAgentResult {
  const agent = requireAgent(agentId);

  if (agent.isBuiltIn) {
    throw new AppError(
      409,
      "The built-in Archivist Default Agent cannot be archived.",
    );
  }

  if (agent.archivedAt) {
    throw new AppError(409, "Agent is already archived.");
  }

  const assignedChatCount = getAssignedChatCount(agentId);

  if (assignedChatCount > 0) {
    throw new AppError(
      409,
      "This Agent is attached to one or more Chats. Detach it before archiving.",
      {
        assignedChatCount,
      },
    );
  }

  database
    .prepare(
      `
        UPDATE agents
        SET
          archived_at = strftime(
            '%Y-%m-%dT%H:%M:%fZ',
            'now'
          ),
          updated_at = strftime(
            '%Y-%m-%dT%H:%M:%fZ',
            'now'
          )
        WHERE id = ?
      `,
    )
    .run(agentId);

  const archivedAgent = getAgentById(agentId);

  if (!archivedAgent) {
    throw new Error("The archived Agent could not be loaded.");
  }

  return {
    agent: archivedAgent,
  };
}

export function restoreAgent(agentId: string): Agent {
  const agent = requireAgent(agentId);

  if (!agent.archivedAt) {
    throw new AppError(409, "Agent is already active.");
  }

  database
    .prepare(
      `
        UPDATE agents
        SET
          archived_at = NULL,
          updated_at = strftime(
            '%Y-%m-%dT%H:%M:%fZ',
            'now'
          )
        WHERE id = ?
      `,
    )
    .run(agentId);

  const restoredAgent = getAgentById(agentId);

  if (!restoredAgent) {
    throw new Error("The restored Agent could not be loaded.");
  }

  return restoredAgent;
}

export function deleteAgent(agentId: string): DeleteAgentResult {
  const agent = requireAgent(agentId);

  if (agent.isBuiltIn || agent.id === ARCHIVIST_DEFAULT_AGENT_ID) {
    throw new AppError(
      409,
      "The built-in Archivist Default Agent cannot be deleted.",
    );
  }

  const deleteTransaction = database.transaction(() => {
    database
      .prepare(
        `
          INSERT OR IGNORE INTO chat_agents (
            chat_id,
            agent_id,
            position
          )
          SELECT
            id,
            ?,
            0
          FROM chats
          WHERE agent_id = ?
        `,
      )
      .run(ARCHIVIST_DEFAULT_AGENT_ID, agentId);

    const reassignmentResult = database
      .prepare(
        `
          UPDATE chats
          SET
            agent_id = ?,
            updated_at = strftime(
              '%Y-%m-%dT%H:%M:%fZ',
              'now'
            )
          WHERE agent_id = ?
        `,
      )
      .run(ARCHIVIST_DEFAULT_AGENT_ID, agentId);

    database
      .prepare(
        `
          DELETE FROM agents
          WHERE id = ?
        `,
      )
      .run(agentId);

    return {
      reassignedChatCount: reassignmentResult.changes,
    };
  });

  return deleteTransaction();
}

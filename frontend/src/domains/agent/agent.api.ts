import type {
  Agent,
  CreateAgentInput,
  DeleteAgentResult,
  DuplicateAgentInput,
  UpdateAgentInput,
} from "./agent.types";

const API_BASE_URL = "http://127.0.0.1:3333/api";

type ErrorResponse = {
  ok: false;
  error?: {
    message?: string;
    details?: Record<string, unknown>;
  };
};

type AgentsResponse = {
  ok: true;
  agents: Agent[];
};

type AgentResponse = {
  ok: true;
  agent: Agent;
};

type DeleteAgentResponse = {
  ok: true;
  reassignedChatCount: number;
};

async function request<T>(path: string, options?: RequestInit): Promise<T> {
  const response = await fetch(`${API_BASE_URL}${path}`, {
    ...options,
    headers: {
      "Content-Type": "application/json",
      ...options?.headers,
    },
  });

  if (!response.ok) {
    const body = (await response
      .json()
      .catch(() => null)) as ErrorResponse | null;

    throw new Error(
      body?.error?.message ??
        `Archivist API request failed with status ${response.status}.`,
    );
  }

  return (await response.json()) as T;
}

export async function fetchAgents(): Promise<Agent[]> {
  const response = await request<AgentsResponse>("/agents");

  return response.agents;
}

export async function fetchArchivedAgents(): Promise<Agent[]> {
  const response = await request<AgentsResponse>("/agents/archived");

  return response.agents;
}

export async function fetchAgent(agentId: string): Promise<Agent> {
  const response = await request<AgentResponse>(`/agents/${agentId}`);

  return response.agent;
}

export async function addAgent(input: CreateAgentInput = {}): Promise<Agent> {
  const response = await request<AgentResponse>("/agents", {
    method: "POST",
    body: JSON.stringify(input),
  });

  return response.agent;
}

export async function editAgent(
  agentId: string,
  input: UpdateAgentInput,
): Promise<Agent> {
  const response = await request<AgentResponse>(`/agents/${agentId}`, {
    method: "PATCH",
    body: JSON.stringify(input),
  });

  return response.agent;
}

export async function duplicateAgent(
  agentId: string,
  input: DuplicateAgentInput = {},
): Promise<Agent> {
  const response = await request<AgentResponse>(
    `/agents/${agentId}/duplicate`,
    {
      method: "POST",
      body: JSON.stringify(input),
    },
  );

  return response.agent;
}

export async function archiveAgent(agentId: string): Promise<Agent> {
  const response = await request<AgentResponse>(`/agents/${agentId}/archive`, {
    method: "POST",
  });

  return response.agent;
}

export async function restoreAgent(agentId: string): Promise<Agent> {
  const response = await request<AgentResponse>(`/agents/${agentId}/restore`, {
    method: "POST",
  });

  return response.agent;
}

export async function removeAgent(agentId: string): Promise<DeleteAgentResult> {
  const response = await request<DeleteAgentResponse>(`/agents/${agentId}`, {
    method: "DELETE",
  });

  return {
    reassignedChatCount: response.reassignedChatCount,
  };
}

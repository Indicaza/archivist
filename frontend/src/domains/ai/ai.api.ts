import type { ModelDefinition } from "./ai.types";

const API_BASE_URL = "http://127.0.0.1:3333/api";

type ErrorResponse = {
  ok: false;
  error?: {
    message?: string;
  };
};

type ModelListResponse = {
  ok: true;
  models: ModelDefinition[];
};

export async function fetchAIModels(): Promise<ModelDefinition[]> {
  const response = await fetch(`${API_BASE_URL}/ai/models`);

  if (!response.ok) {
    const body = (await response
      .json()
      .catch(() => null)) as ErrorResponse | null;

    throw new Error(
      body?.error?.message ??
        `Archivist API request failed with status ${response.status}.`,
    );
  }

  const body = (await response.json()) as ModelListResponse;

  return body.models;
}

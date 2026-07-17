import type { ContextCompilerDefinition } from "./contextCompiler.types";

const API_BASE_URL = "http://127.0.0.1:3333/api";

type ErrorResponse = {
  ok: false;
  error?: {
    message?: string;
  };
};

type ContextCompilerListResponse = {
  ok: true;
  compilers: ContextCompilerDefinition[];
};

export async function fetchContextCompilers(): Promise<
  ContextCompilerDefinition[]
> {
  const response = await fetch(`${API_BASE_URL}/cognition/context-compilers`);

  if (!response.ok) {
    const body = (await response
      .json()
      .catch(() => null)) as ErrorResponse | null;

    throw new Error(
      body?.error?.message ??
        `Archivist API request failed with status ${response.status}.`,
    );
  }

  const body = (await response.json()) as ContextCompilerListResponse;

  return body.compilers;
}

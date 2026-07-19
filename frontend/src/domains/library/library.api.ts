import type {
  AppState,
  Library,
  LibraryFileCatalog,
  LibraryFile,
  LibraryScan,
  LibraryScanIssue,
  ScanLibraryResult,
} from "./library.types";

const API_BASE_URL = "http://127.0.0.1:3333/api";

type ErrorResponse = {
  ok: false;
  error?: {
    message?: string;
    details?: {
      code?: string;
      libraryId?: string;
    };
  };
};

type LibrariesResponse = {
  ok: true;
  libraries: Library[];
};

type LibraryResponse = {
  ok: true;
  library: Library;
};

type CreateLibraryResponse = {
  ok: true;
  library: Library;
  selectedLibraryId: string | null;
};

type ArchiveLibraryResponse = {
  ok: true;
  library: Library;
  selectedLibraryId: string | null;
};

type AppStateResponse = {
  ok: true;
  appState: AppState;
};

type LibraryFilesResponse = {
  ok: true;
  files: LibraryFile[];
  latestScan: LibraryScan | null;
};

type ScanLibraryResponse = {
  ok: true;
  files: LibraryFile[];
  latestScan: LibraryScan | null;
  scan: LibraryScan;
  issues: LibraryScanIssue[];
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

export async function fetchLibraries(): Promise<Library[]> {
  const response = await request<LibrariesResponse>("/libraries");

  return response.libraries;
}

export async function fetchArchivedLibraries(): Promise<Library[]> {
  const response = await request<LibrariesResponse>("/libraries/archived");

  return response.libraries;
}

export async function addLibrary(
  rootPath: string,
): Promise<CreateLibraryResponse> {
  return request<CreateLibraryResponse>("/libraries", {
    method: "POST",
    body: JSON.stringify({
      rootPath,
    }),
  });
}

export async function editLibrary(
  libraryId: string,
  input: {
    name?: string;
    description?: string | null;
  },
): Promise<Library> {
  const response = await request<LibraryResponse>(`/libraries/${libraryId}`, {
    method: "PATCH",
    body: JSON.stringify(input),
  });

  return response.library;
}

export async function archiveLibrary(
  libraryId: string,
): Promise<ArchiveLibraryResponse> {
  return request<ArchiveLibraryResponse>(`/libraries/${libraryId}/archive`, {
    method: "POST",
  });
}

export async function restoreLibrary(libraryId: string): Promise<Library> {
  const response = await request<LibraryResponse>(
    `/libraries/${libraryId}/restore`,
    {
      method: "POST",
    },
  );

  return response.library;
}

export async function fetchLibraryFiles(
  libraryId: string,
): Promise<LibraryFileCatalog> {
  const response = await request<LibraryFilesResponse>(
    `/libraries/${libraryId}/files`,
  );

  return {
    files: response.files,
    latestScan: response.latestScan,
  };
}

export async function scanLibraryFiles(
  libraryId: string,
): Promise<ScanLibraryResult> {
  const response = await request<ScanLibraryResponse>(
    `/libraries/${libraryId}/scan`,
    {
      method: "POST",
    },
  );

  return {
    files: response.files,
    latestScan: response.latestScan,
    scan: response.scan,
    issues: response.issues,
  };
}

export async function fetchAppState(): Promise<AppState> {
  const response = await request<AppStateResponse>("/app-state");

  return response.appState;
}

export async function updateSelectedLibrary(
  selectedLibraryId: string | null,
): Promise<AppState> {
  const response = await request<AppStateResponse>(
    "/app-state/selected-library",
    {
      method: "PATCH",
      body: JSON.stringify({
        selectedLibraryId,
      }),
    },
  );

  return response.appState;
}

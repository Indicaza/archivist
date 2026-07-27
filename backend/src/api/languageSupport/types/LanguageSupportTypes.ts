export type LanguageServerAvailabilityState =
  | "available"
  | "missing"
  | "disabled";

export type LanguageServerSessionState =
  | "pending"
  | "connected";

export interface LanguageServerDefinition {
  id: string;
  displayName: string;
  languageIds: readonly string[];
  executableCandidates: readonly string[];
  args: readonly string[];
  rootMarkers: readonly string[];
  enabledByDefault: boolean;
}

export interface ResolvedLanguageWorkspace {
  libraryId: string;
  libraryRoot: string;
  workspaceRoot: string;
  filePath: string | null;
}

export interface LanguageServerAvailability {
  id: string;
  displayName: string;
  languageIds: readonly string[];
  state: LanguageServerAvailabilityState;
  executablePath: string | null;
  workspaceRoot: string | null;
}

export interface LanguageServerSessionDescriptor {
  sessionId: string;
  serverId: string;
  displayName: string;
  languageIds: readonly string[];
  state: LanguageServerSessionState;
  workspaceRoot: string;
  filePath: string | null;
  socketUrl: string;
  expiresAt: string;
}

export interface LanguageServerSessionSummary {
  sessionId: string;
  serverId: string;
  displayName: string;
  state: LanguageServerSessionState;
  workspaceRoot: string;
  filePath: string | null;
  processId: number | null;
  connectedAt: string | null;
  expiresAt: string;
}

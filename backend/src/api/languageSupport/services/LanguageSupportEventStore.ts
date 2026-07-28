export type LanguageSupportClientEventKind =
  | "document-classified"
  | "initialized"
  | "document-open"
  | "diagnostics"
  | "markers"
  | "disconnected"
  | "client-error";

export interface LanguageSupportDiagnosticEventDetail {
  owner?: string;
  severity: "error" | "warning" | "info" | "hint";
  line: number;
  column: number;
  message: string;
}

export interface LanguageSupportClientEventInput {
  kind: LanguageSupportClientEventKind;
  provider?: "lsp" | "monaco";
  serverId?: string;
  workspaceRoot?: string;
  filePath?: string;
  uri?: string;
  monacoLanguageId?: string;
  lspLanguageId?: string;
  version?: number;
  capabilities?: string[];
  diagnostics?: {
    total: number;
    errors: number;
    warnings: number;
    info: number;
    hints: number;
    details?: LanguageSupportDiagnosticEventDetail[];
  };
  message?: string;
}

export interface LanguageSupportClientEvent
  extends LanguageSupportClientEventInput {
  sequence: number;
  recordedAt: string;
}

const maximumEvents = 500;

class LanguageSupportEventStore {
  private readonly events: LanguageSupportClientEvent[] = [];
  private nextSequence = 1;

  append(inputs: readonly LanguageSupportClientEventInput[]): void {
    const recordedAt = new Date().toISOString();

    for (const input of inputs) {
      this.events.push({
        ...input,
        sequence: this.nextSequence,
        recordedAt,
      });
      this.nextSequence += 1;
    }

    if (this.events.length > maximumEvents) {
      this.events.splice(
        0,
        this.events.length - maximumEvents,
      );
    }
  }

  list(limit = 250): readonly LanguageSupportClientEvent[] {
    const normalizedLimit = Math.max(
      1,
      Math.min(maximumEvents, Math.floor(limit)),
    );

    return this.events.slice(-normalizedLimit);
  }
}

export const languageSupportEventStore =
  new LanguageSupportEventStore();

export type LanguageSupportTelemetryKind =
  | "document-classified"
  | "initialized"
  | "document-open"
  | "diagnostics"
  | "markers"
  | "disconnected"
  | "client-error";

export interface LanguageSupportTelemetryDiagnostic {
  owner?: string;
  severity: "error" | "warning" | "info" | "hint";
  line: number;
  column: number;
  message: string;
}

export interface LanguageSupportTelemetryEvent {
  kind: LanguageSupportTelemetryKind;
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
    details?: LanguageSupportTelemetryDiagnostic[];
  };
  message?: string;
}

const pendingEvents: LanguageSupportTelemetryEvent[] = [];
let flushTimer: number | null = null;
let flushing = false;

function scheduleFlush(delayMilliseconds = 120): void {
  if (flushTimer !== null) {
    return;
  }

  flushTimer = window.setTimeout(() => {
    flushTimer = null;
    void flushLanguageSupportTelemetry();
  }, delayMilliseconds);
}

export function recordLanguageSupportTelemetry(
  event: LanguageSupportTelemetryEvent,
): void {
  pendingEvents.push(event);

  if (pendingEvents.length > 200) {
    pendingEvents.splice(0, pendingEvents.length - 200);
  }

  scheduleFlush();
}

export async function flushLanguageSupportTelemetry(): Promise<void> {
  if (flushing || pendingEvents.length === 0) {
    return;
  }

  flushing = true;
  const events = pendingEvents.splice(0, 50);

  try {
    await fetch("/api/language-support/events", {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
      },
      body: JSON.stringify({ events }),
      keepalive: true,
    });
  } catch {
    // Telemetry is diagnostic-only and must never affect editing.
  } finally {
    flushing = false;

    if (pendingEvents.length > 0) {
      scheduleFlush(20);
    }
  }
}

window.addEventListener("beforeunload", () => {
  void flushLanguageSupportTelemetry();
});

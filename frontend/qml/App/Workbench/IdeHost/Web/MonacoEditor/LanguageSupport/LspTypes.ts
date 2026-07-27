export type JsonRpcId = number | string;

export interface JsonRpcErrorShape {
  code: number;
  message: string;
  data?: unknown;
}

export interface JsonRpcRequest {
  jsonrpc: "2.0";
  id: JsonRpcId;
  method: string;
  params?: unknown;
}

export interface JsonRpcNotification {
  jsonrpc: "2.0";
  method: string;
  params?: unknown;
}

export interface JsonRpcResponse {
  jsonrpc: "2.0";
  id: JsonRpcId;
  result?: unknown;
  error?: JsonRpcErrorShape;
}

export type JsonRpcMessage =
  | JsonRpcRequest
  | JsonRpcNotification
  | JsonRpcResponse;

export interface LspPosition {
  line: number;
  character: number;
}

export interface LspRange {
  start: LspPosition;
  end: LspPosition;
}

export interface LspLocation {
  uri: string;
  range: LspRange;
}

export interface LspLocationLink {
  originSelectionRange?: LspRange;
  targetUri: string;
  targetRange: LspRange;
  targetSelectionRange: LspRange;
}

export interface LspMarkupContent {
  kind: "plaintext" | "markdown" | string;
  value: string;
}

export interface LspMarkedString {
  language: string;
  value: string;
}

export type LspHoverContent =
  | string
  | LspMarkedString
  | LspMarkupContent;

export interface LspHover {
  contents: LspHoverContent | LspHoverContent[];
  range?: LspRange;
}

export interface LspTextEdit {
  range: LspRange;
  newText: string;
}

export interface LspInsertReplaceEdit {
  insert: LspRange;
  replace: LspRange;
  newText: string;
}

export interface LspCompletionItem {
  label: string;
  labelDetails?: {
    detail?: string;
    description?: string;
  };
  kind?: number;
  tags?: number[];
  detail?: string;
  documentation?: string | LspMarkupContent;
  deprecated?: boolean;
  preselect?: boolean;
  sortText?: string;
  filterText?: string;
  insertText?: string;
  insertTextFormat?: number;
  textEdit?: LspTextEdit | LspInsertReplaceEdit;
  additionalTextEdits?: LspTextEdit[];
  commitCharacters?: string[];
}

export interface LspCompletionList {
  isIncomplete?: boolean;
  items: LspCompletionItem[];
}

export interface LspDiagnosticRelatedInformation {
  location: LspLocation;
  message: string;
}

export interface LspDiagnostic {
  range: LspRange;
  severity?: number;
  code?: string | number;
  source?: string;
  message: string;
  tags?: number[];
  relatedInformation?: LspDiagnosticRelatedInformation[];
}

export interface LspPublishDiagnosticsParams {
  uri: string;
  diagnostics: LspDiagnostic[];
}

export interface LspParameterInformation {
  label: string | [number, number];
  documentation?: string | LspMarkupContent;
}

export interface LspSignatureInformation {
  label: string;
  documentation?: string | LspMarkupContent;
  parameters?: LspParameterInformation[];
  activeParameter?: number;
}

export interface LspSignatureHelp {
  signatures: LspSignatureInformation[];
  activeSignature?: number;
  activeParameter?: number;
}

export interface LanguageServerSessionDescriptor {
  sessionId: string;
  serverId: string;
  displayName: string;
  languageIds: string[];
  state: "pending";
  workspaceRoot: string;
  filePath: string | null;
  socketUrl: string;
  expiresAt: string;
}

export function isRecord(
  value: unknown,
): value is Record<string, unknown> {
  return typeof value === "object"
    && value !== null
    && !Array.isArray(value);
}

export function isJsonRpcResponse(
  message: JsonRpcMessage,
): message is JsonRpcResponse {
  return "id" in message && !("method" in message);
}

export function isJsonRpcRequest(
  message: JsonRpcMessage,
): message is JsonRpcRequest {
  return "id" in message && "method" in message;
}

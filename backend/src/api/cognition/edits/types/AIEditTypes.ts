export type AIEditProposalStatus =
  | "proposed"
  | "approved"
  | "partially_approved"
  | "rejected"
  | "stale"
  | "executing"
  | "completed"
  | "failed"
  | "undone";

export type AIEditOperationType =
  | "create_file"
  | "patch_file"
  | "rename_file"
  | "move_file"
  | "create_directory";

export type AIEditOperationStatus =
  | "proposed"
  | "approved"
  | "rejected"
  | "completed"
  | "failed"
  | "undone";

export type AIEditOperation = {
  id: string;
  proposalId: string;
  ordinal: number;
  type: AIEditOperationType;
  status: AIEditOperationStatus;
  sourcePath: string | null;
  destinationPath: string | null;
  expectedHash: string | null;
  beforeContent: string | null;
  afterContent: string | null;
  afterHash: string | null;
  createdAt: string;
  completedAt: string | null;
  errorCode: string | null;
  errorMessage: string | null;
};

export type AIEditProposal = {
  id: string;
  runId: string;
  chatId: string;
  libraryId: string;
  agentId: string;
  provider: string;
  model: string;
  skillId: string | null;
  summary: string;
  status: AIEditProposalStatus;
  operations: AIEditOperation[];
  createdAt: string;
  reviewedAt: string | null;
  completedAt: string | null;
  errorCode: string | null;
  errorMessage: string | null;
};

export type AIEditOperationDraft = {
  type: AIEditOperationType;
  sourcePath?: string | null;
  destinationPath?: string | null;
  expectedHash?: string | null;
  beforeContent?: string | null;
  afterContent?: string | null;
};

export type CreateAIEditProposalInput = {
  runId: string;
  chatId: string;
  libraryId: string;
  agentId: string;
  provider: string;
  model: string;
  skillId: string | null;
  summary: string;
  operations: AIEditOperationDraft[];
};

export type AppendAIEditProposalOperationsInput = {
  proposalId: string;
  summary: string;
  operations: AIEditOperationDraft[];
};

export type ProposeAIEditInput = {
  runId: string;
  summary: string;
  skillId?: string | null;
  operations: AIEditOperationDraft[];
};

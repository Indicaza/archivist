import { database } from "../../../../database/database.js";
import { AppError } from "../../../../errors/app-error.js";
import type {
  AIEditOperation,
  AIEditOperationStatus,
  AIEditOperationType,
  AIEditProposal,
  AIEditProposalStatus,
  AppendAIEditProposalOperationsInput,
  CreateAIEditProposalInput,
} from "../types/AIEditTypes.js";

type AIEditProposalRow = {
  id: string;
  run_id: string;
  chat_id: string;
  library_id: string;
  agent_id: string;
  provider: string;
  model: string;
  skill_id: string | null;
  summary: string;
  status: AIEditProposalStatus;
  created_at: string;
  reviewed_at: string | null;
  completed_at: string | null;
  error_code: string | null;
  error_message: string | null;
};

type AIEditOperationRow = {
  id: string;
  proposal_id: string;
  ordinal: number;
  operation_type: AIEditOperationType;
  status: AIEditOperationStatus;
  source_path: string | null;
  destination_path: string | null;
  expected_hash: string | null;
  before_content: string | null;
  after_content: string | null;
  after_hash: string | null;
  created_at: string;
  completed_at: string | null;
  error_code: string | null;
  error_message: string | null;
};

const proposalSelect = `
  SELECT
    id,
    run_id,
    chat_id,
    library_id,
    agent_id,
    provider,
    model,
    skill_id,
    summary,
    status,
    created_at,
    reviewed_at,
    completed_at,
    error_code,
    error_message
  FROM ai_edit_proposals
`;

const operationSelect = `
  SELECT
    id,
    proposal_id,
    ordinal,
    operation_type,
    status,
    source_path,
    destination_path,
    expected_hash,
    before_content,
    after_content,
    after_hash,
    created_at,
    completed_at,
    error_code,
    error_message
  FROM ai_edit_operations
`;

const operationSummarySelect = `
  SELECT
    id,
    proposal_id,
    ordinal,
    operation_type,
    status,
    source_path,
    destination_path,
    expected_hash,
    NULL AS before_content,
    NULL AS after_content,
    after_hash,
    created_at,
    completed_at,
    error_code,
    error_message
  FROM ai_edit_operations
`;

function mapOperation(row: AIEditOperationRow): AIEditOperation {
  return {
    id: row.id,
    proposalId: row.proposal_id,
    ordinal: row.ordinal,
    type: row.operation_type,
    status: row.status,
    sourcePath: row.source_path,
    destinationPath: row.destination_path,
    expectedHash: row.expected_hash,
    beforeContent: row.before_content,
    afterContent: row.after_content,
    afterHash: row.after_hash,
    createdAt: row.created_at,
    completedAt: row.completed_at,
    errorCode: row.error_code,
    errorMessage: row.error_message,
  };
}

function listOperations(proposalId: string): AIEditOperation[] {
  const rows = database
    .prepare(
      `${operationSelect}
       WHERE proposal_id = ?
       ORDER BY ordinal ASC`,
    )
    .all(proposalId) as AIEditOperationRow[];

  return rows.map(mapOperation);
}

function listOperationSummaries(proposalId: string): AIEditOperation[] {
  const rows = database
    .prepare(
      `${operationSummarySelect}
       WHERE proposal_id = ?
       ORDER BY ordinal ASC`,
    )
    .all(proposalId) as AIEditOperationRow[];

  return rows.map(mapOperation);
}

function mapProposal(
  row: AIEditProposalRow,
  operations = listOperations(row.id),
): AIEditProposal {
  return {
    id: row.id,
    runId: row.run_id,
    chatId: row.chat_id,
    libraryId: row.library_id,
    agentId: row.agent_id,
    provider: row.provider,
    model: row.model,
    skillId: row.skill_id,
    summary: row.summary,
    status: row.status,
    operations,
    createdAt: row.created_at,
    reviewedAt: row.reviewed_at,
    completedAt: row.completed_at,
    errorCode: row.error_code,
    errorMessage: row.error_message,
  };
}

export function getAIEditProposalById(
  proposalId: string,
): AIEditProposal | null {
  const row = database
    .prepare(`${proposalSelect} WHERE id = ?`)
    .get(proposalId) as AIEditProposalRow | undefined;

  return row ? mapProposal(row) : null;
}

export function requireAIEditProposal(
  proposalId: string,
): AIEditProposal {
  const proposal = getAIEditProposalById(proposalId);

  if (!proposal) {
    throw new AppError(404, "AI edit proposal not found.");
  }

  return proposal;
}

export function listAIEditProposalsByRunId(
  runId: string,
): AIEditProposal[] {
  const rows = database
    .prepare(
      `${proposalSelect}
       WHERE run_id = ?
       ORDER BY created_at ASC, rowid ASC`,
    )
    .all(runId) as AIEditProposalRow[];

  return rows.map((row) => mapProposal(row));
}

export function listAIEditProposalsByChatId(
  chatId: string,
): AIEditProposal[] {
  const rows = database
    .prepare(
      `${proposalSelect}
       WHERE chat_id = ?
       ORDER BY created_at ASC, rowid ASC`,
    )
    .all(chatId) as AIEditProposalRow[];

  return rows.map((row) =>
    mapProposal(row, listOperationSummaries(row.id))
  );
}

export function getLatestCompletedAIEditProposalByLibraryId(
  libraryId: string,
): AIEditProposal | null {
  const row = database
    .prepare(
      `${proposalSelect}
       WHERE library_id = ?
         AND status = 'completed'
       ORDER BY completed_at DESC, rowid DESC
       LIMIT 1`,
    )
    .get(libraryId) as AIEditProposalRow | undefined;

  return row ? mapProposal(row) : null;
}

export function createAIEditProposal(
  input: CreateAIEditProposalInput,
): AIEditProposal {
  const create = database.transaction(() => {
    const proposalId = crypto.randomUUID();

    database
      .prepare(
        `
          INSERT INTO ai_edit_proposals (
            id,
            run_id,
            chat_id,
            library_id,
            agent_id,
            provider,
            model,
            skill_id,
            summary,
            status
          )
          VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, 'proposed')
        `,
      )
      .run(
        proposalId,
        input.runId,
        input.chatId,
        input.libraryId,
        input.agentId,
        input.provider,
        input.model,
        input.skillId,
        input.summary,
      );

    const insertOperation = database.prepare(
      `
        INSERT INTO ai_edit_operations (
          id,
          proposal_id,
          ordinal,
          operation_type,
          status,
          source_path,
          destination_path,
          expected_hash,
          before_content,
          after_content
        )
        VALUES (?, ?, ?, ?, 'proposed', ?, ?, ?, ?, ?)
      `,
    );

    input.operations.forEach((operation, index) => {
      insertOperation.run(
        crypto.randomUUID(),
        proposalId,
        index + 1,
        operation.type,
        operation.sourcePath ?? null,
        operation.destinationPath ?? null,
        operation.expectedHash ?? null,
        operation.beforeContent ?? null,
        operation.afterContent ?? null,
      );
    });

    return requireAIEditProposal(proposalId);
  });

  return create();
}

export function appendAIEditProposalOperations(
  input: AppendAIEditProposalOperationsInput,
): AIEditProposal {
  const append = database.transaction(() => {
    const proposal = requireAIEditProposal(input.proposalId);

    if (proposal.status !== "proposed") {
      throw new Error(
        `AI edit proposal ${input.proposalId} is no longer open for changes.`,
      );
    }

    const nextOrdinal =
      proposal.operations.reduce(
        (highest, operation) => Math.max(highest, operation.ordinal),
        0,
      ) + 1;
    const insertOperation = database.prepare(
      `
        INSERT INTO ai_edit_operations (
          id,
          proposal_id,
          ordinal,
          operation_type,
          status,
          source_path,
          destination_path,
          expected_hash,
          before_content,
          after_content
        )
        VALUES (?, ?, ?, ?, 'proposed', ?, ?, ?, ?, ?)
      `,
    );

    input.operations.forEach((operation, index) => {
      insertOperation.run(
        crypto.randomUUID(),
        proposal.id,
        nextOrdinal + index,
        operation.type,
        operation.sourcePath ?? null,
        operation.destinationPath ?? null,
        operation.expectedHash ?? null,
        operation.beforeContent ?? null,
        operation.afterContent ?? null,
      );
    });

    database
      .prepare(
        `
          UPDATE ai_edit_proposals
          SET summary = ?
          WHERE id = ?
            AND status = 'proposed'
        `,
      )
      .run(input.summary, proposal.id);

    return requireAIEditProposal(proposal.id);
  });

  return append();
}

function assertNoOtherExecutingAIEditProposal(
  libraryId: string,
  proposalId: string,
): void {
  const row = database
    .prepare(
      `
        SELECT id
        FROM ai_edit_proposals
        WHERE library_id = ?
          AND status = 'executing'
          AND id != ?
        LIMIT 1
      `,
    )
    .get(libraryId, proposalId) as { id: string } | undefined;

  if (row) {
    throw new AppError(
      409,
      "This Library already has an AI edit transaction in progress.",
      {
        proposalId: row.id,
      },
    );
  }
}

function requireProposedAIEditProposal(
  proposalId: string,
): AIEditProposal {
  const proposal = requireAIEditProposal(proposalId);

  if (proposal.status !== "proposed") {
    throw new AppError(
      409,
      "This AI edit proposal has already been reviewed.",
    );
  }

  return proposal;
}

export function beginAIEditProposalExecution(
  proposalId: string,
  selectedOperationIds?: string[],
): AIEditProposal {
  const begin = database.transaction(() => {
    const proposal = requireProposedAIEditProposal(proposalId);

    assertNoOtherExecutingAIEditProposal(
      proposal.libraryId,
      proposal.id,
    );

    const allOperationIds = proposal.operations.map((operation) => operation.id);
    const requestedIds = selectedOperationIds ?? allOperationIds;
    const uniqueRequestedIds = Array.from(new Set(requestedIds));

    if (requestedIds.length === 0) {
      throw new AppError(
        400,
        "At least one edit operation must be approved.",
      );
    }

    if (uniqueRequestedIds.length !== requestedIds.length) {
      throw new AppError(
        400,
        "Approved edit operation IDs must be unique.",
      );
    }

    const knownOperationIds = new Set(allOperationIds);

    for (const operationId of uniqueRequestedIds) {
      if (!knownOperationIds.has(operationId)) {
        throw new AppError(
          400,
          "Approved edit operations must belong to this proposal.",
        );
      }
    }

    const selectedIds = new Set(uniqueRequestedIds);
    const updateOperation = database.prepare(
      `
        UPDATE ai_edit_operations
        SET
          status = ?,
          error_code = NULL,
          error_message = NULL
        WHERE id = ?
          AND proposal_id = ?
          AND status = 'proposed'
      `,
    );

    for (const operation of proposal.operations) {
      const status: AIEditOperationStatus = selectedIds.has(operation.id)
        ? "approved"
        : "rejected";
      updateOperation.run(status, operation.id, proposal.id);
    }

    database
      .prepare(
        `
          UPDATE ai_edit_proposals
          SET
            status = 'executing',
            reviewed_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now'),
            error_code = NULL,
            error_message = NULL
          WHERE id = ?
            AND status = 'proposed'
        `,
      )
      .run(proposal.id);

    return requireAIEditProposal(proposal.id);
  });

  return begin();
}

export function rejectAIEditProposal(
  proposalId: string,
): AIEditProposal {
  const reject = database.transaction(() => {
    const proposal = requireProposedAIEditProposal(proposalId);

    database
      .prepare(
        `
          UPDATE ai_edit_operations
          SET
            status = 'rejected',
            error_code = NULL,
            error_message = NULL
          WHERE proposal_id = ?
            AND status = 'proposed'
        `,
      )
      .run(proposal.id);

    database
      .prepare(
        `
          UPDATE ai_edit_proposals
          SET
            status = 'rejected',
            reviewed_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now'),
            completed_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now'),
            error_code = NULL,
            error_message = NULL
          WHERE id = ?
            AND status = 'proposed'
        `,
      )
      .run(proposal.id);

    return requireAIEditProposal(proposal.id);
  });

  return reject();
}

function markApprovedOperationsFailed(
  proposalId: string,
  errorCode: string,
  errorMessage: string,
): void {
  database
    .prepare(
      `
        UPDATE ai_edit_operations
        SET
          status = 'failed',
          completed_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now'),
          error_code = ?,
          error_message = ?
        WHERE proposal_id = ?
          AND status = 'approved'
      `,
    )
    .run(errorCode, errorMessage, proposalId);
}

export function markAIEditProposalStale(
  proposalId: string,
  errorMessage: string,
): AIEditProposal {
  const mark = database.transaction(() => {
    const proposal = requireAIEditProposal(proposalId);

    if (proposal.status !== "executing") {
      throw new AppError(
        409,
        "Only executing AI edit proposals can become stale.",
      );
    }

    markApprovedOperationsFailed(
      proposal.id,
      "proposal_stale",
      errorMessage,
    );

    database
      .prepare(
        `
          UPDATE ai_edit_proposals
          SET
            status = 'stale',
            completed_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now'),
            error_code = 'proposal_stale',
            error_message = ?
          WHERE id = ?
            AND status = 'executing'
        `,
      )
      .run(errorMessage, proposal.id);

    return requireAIEditProposal(proposal.id);
  });

  return mark();
}

export function markAIEditProposalFailed(
  proposalId: string,
  errorCode: string,
  errorMessage: string,
): AIEditProposal {
  const mark = database.transaction(() => {
    const proposal = requireAIEditProposal(proposalId);

    if (proposal.status !== "executing") {
      throw new AppError(
        409,
        "Only executing AI edit proposals can fail execution.",
      );
    }

    markApprovedOperationsFailed(
      proposal.id,
      errorCode,
      errorMessage,
    );

    database
      .prepare(
        `
          UPDATE ai_edit_proposals
          SET
            status = 'failed',
            completed_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now'),
            error_code = ?,
            error_message = ?
          WHERE id = ?
            AND status = 'executing'
        `,
      )
      .run(errorCode, errorMessage, proposal.id);

    return requireAIEditProposal(proposal.id);
  });

  return mark();
}

export function completeAIEditProposalExecution(
  proposalId: string,
  afterHashes: Map<string, string | null>,
): AIEditProposal {
  const complete = database.transaction(() => {
    const proposal = requireAIEditProposal(proposalId);

    if (proposal.status !== "executing") {
      throw new AppError(
        409,
        "Only executing AI edit proposals can complete.",
      );
    }

    const approvedOperations = proposal.operations.filter(
      (operation) => operation.status === "approved",
    );

    if (approvedOperations.length === 0) {
      throw new AppError(
        409,
        "This AI edit proposal has no approved operations to complete.",
      );
    }

    const updateOperation = database.prepare(
      `
        UPDATE ai_edit_operations
        SET
          status = 'completed',
          after_hash = ?,
          completed_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now'),
          error_code = NULL,
          error_message = NULL
        WHERE id = ?
          AND proposal_id = ?
          AND status = 'approved'
      `,
    );

    for (const operation of approvedOperations) {
      if (!afterHashes.has(operation.id)) {
        throw new Error(
          `Missing final hash record for AI edit operation ${operation.id}.`,
        );
      }

      updateOperation.run(
        afterHashes.get(operation.id) ?? null,
        operation.id,
        proposal.id,
      );
    }

    database
      .prepare(
        `
          UPDATE ai_edit_proposals
          SET
            status = 'completed',
            completed_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now'),
            error_code = NULL,
            error_message = NULL
          WHERE id = ?
            AND status = 'executing'
        `,
      )
      .run(proposal.id);

    return requireAIEditProposal(proposal.id);
  });

  return complete();
}

export function beginAIEditProposalUndo(
  proposalId: string,
): AIEditProposal {
  const begin = database.transaction(() => {
    const proposal = requireAIEditProposal(proposalId);

    if (proposal.status !== "completed") {
      throw new AppError(
        409,
        "Only completed AI edit transactions can be undone.",
      );
    }

    assertNoOtherExecutingAIEditProposal(
      proposal.libraryId,
      proposal.id,
    );

    const latestCompleted = getLatestCompletedAIEditProposalByLibraryId(
      proposal.libraryId,
    );

    if (latestCompleted?.id !== proposal.id) {
      throw new AppError(
        409,
        "Only the latest completed AI edit transaction can be undone.",
      );
    }

    const completedOperations = proposal.operations.filter(
      (operation) => operation.status === "completed",
    );

    if (completedOperations.length === 0) {
      throw new AppError(
        409,
        "This AI edit transaction has no completed operations to undo.",
      );
    }

    const result = database
      .prepare(
        `
          UPDATE ai_edit_proposals
          SET
            status = 'executing',
            error_code = NULL,
            error_message = NULL
          WHERE id = ?
            AND status = 'completed'
        `,
      )
      .run(proposal.id);

    if (result.changes !== 1) {
      throw new AppError(
        409,
        "This AI edit transaction is no longer available for undo.",
      );
    }

    return requireAIEditProposal(proposal.id);
  });

  return begin();
}

export function restoreAIEditProposalAfterUndoFailure(
  proposalId: string,
): AIEditProposal {
  const restore = database.transaction(() => {
    const proposal = requireAIEditProposal(proposalId);

    if (proposal.status !== "executing") {
      throw new AppError(
        409,
        "Only an executing AI edit undo can be restored.",
      );
    }

    const result = database
      .prepare(
        `
          UPDATE ai_edit_proposals
          SET
            status = 'completed',
            error_code = NULL,
            error_message = NULL
          WHERE id = ?
            AND status = 'executing'
        `,
      )
      .run(proposal.id);

    if (result.changes !== 1) {
      throw new Error("The AI edit undo state could not be restored.");
    }

    return requireAIEditProposal(proposal.id);
  });

  return restore();
}

export function markAIEditProposalUndoFailed(
  proposalId: string,
  errorCode: string,
  errorMessage: string,
): AIEditProposal {
  const mark = database.transaction(() => {
    const proposal = requireAIEditProposal(proposalId);

    if (proposal.status !== "executing") {
      throw new AppError(
        409,
        "Only an executing AI edit undo can fail.",
      );
    }

    database
      .prepare(
        `
          UPDATE ai_edit_operations
          SET
            status = 'failed',
            error_code = ?,
            error_message = ?
          WHERE proposal_id = ?
            AND status = 'completed'
        `,
      )
      .run(errorCode, errorMessage, proposal.id);

    database
      .prepare(
        `
          UPDATE ai_edit_proposals
          SET
            status = 'failed',
            error_code = ?,
            error_message = ?
          WHERE id = ?
            AND status = 'executing'
        `,
      )
      .run(errorCode, errorMessage, proposal.id);

    return requireAIEditProposal(proposal.id);
  });

  return mark();
}

export function completeAIEditProposalUndo(
  proposalId: string,
): AIEditProposal {
  const complete = database.transaction(() => {
    const proposal = requireAIEditProposal(proposalId);

    if (proposal.status !== "executing") {
      throw new AppError(
        409,
        "Only an executing AI edit transaction can complete undo.",
      );
    }

    const completedOperations = proposal.operations.filter(
      (operation) => operation.status === "completed",
    );

    if (completedOperations.length === 0) {
      throw new AppError(
        409,
        "This AI edit transaction has no completed operations to undo.",
      );
    }

    database
      .prepare(
        `
          UPDATE ai_edit_operations
          SET
            status = 'undone',
            error_code = NULL,
            error_message = NULL
          WHERE proposal_id = ?
            AND status = 'completed'
        `,
      )
      .run(proposal.id);

    database
      .prepare(
        `
          UPDATE ai_edit_proposals
          SET
            status = 'undone',
            error_code = NULL,
            error_message = NULL
          WHERE id = ?
            AND status = 'executing'
        `,
      )
      .run(proposal.id);

    return requireAIEditProposal(proposal.id);
  });

  return complete();
}

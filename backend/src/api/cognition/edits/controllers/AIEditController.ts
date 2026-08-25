import type { RequestHandler } from "express";
import { AppError } from "../../../../errors/app-error.js";
import {
  aiEditChatIdParamsSchema,
  aiEditProposalApprovalSchema,
  aiEditProposalIdParamsSchema,
} from "../schemas/AIEditSchemas.js";
import {
  approveAIEditProposalForReview,
  getAIEditProposalForReview,
  listAIEditProposalsForChatReview,
  rejectAIEditProposalForReview,
  undoAIEditProposalForReview,
} from "../services/AIEditTransactionService.js";

function parseChatId(params: unknown): string {
  const parsed = aiEditChatIdParamsSchema.safeParse(params);

  if (!parsed.success) {
    throw new AppError(
      400,
      "Invalid chat ID for AI edit proposals.",
      parsed.error.flatten(),
    );
  }

  return parsed.data.chatId;
}

function parseProposalId(params: unknown): string {
  const parsed = aiEditProposalIdParamsSchema.safeParse(params);

  if (!parsed.success) {
    throw new AppError(
      400,
      "Invalid AI edit proposal ID.",
      parsed.error.flatten(),
    );
  }

  return parsed.data.proposalId;
}

export const getAIEditProposalsForChat: RequestHandler = (
  request,
  response,
) => {
  const chatId = parseChatId(request.params);

  response.json({
    ok: true,
    proposals: listAIEditProposalsForChatReview(chatId),
  });
};

export const getAIEditProposal: RequestHandler = (request, response) => {
  const proposalId = parseProposalId(request.params);

  response.json({
    ok: true,
    proposal: getAIEditProposalForReview(proposalId),
  });
};

export const postApproveAIEditProposal: RequestHandler = async (
  request,
  response,
) => {
  const proposalId = parseProposalId(request.params);
  const parsed = aiEditProposalApprovalSchema.safeParse(
    request.body ?? {},
  );

  if (!parsed.success) {
    throw new AppError(
      400,
      "Invalid AI edit approval request.",
      parsed.error.flatten(),
    );
  }

  const result = await approveAIEditProposalForReview(
    proposalId,
    parsed.data.operationIds,
  );

  response.json({
    ok: true,
    ...result,
  });
};

export const postRejectAIEditProposal: RequestHandler = (
  request,
  response,
) => {
  const proposalId = parseProposalId(request.params);

  response.json({
    ok: true,
    proposal: rejectAIEditProposalForReview(proposalId),
  });
};

export const postUndoAIEditProposal: RequestHandler = async (
  request,
  response,
) => {
  const proposalId = parseProposalId(request.params);
  const result = await undoAIEditProposalForReview(proposalId);

  response.json({
    ok: true,
    ...result,
  });
};

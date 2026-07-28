import type { RequestHandler } from "express";
import { AppError } from "../../../errors/app-error.js";
import {
  languageSupportClientEventsRequestSchema,
  languageSupportQuerySchema,
  languageSupportSessionRequestSchema,
} from "../schemas/LanguageSupportSchemas.js";
import { languageSupportEventStore } from "../services/LanguageSupportEventStore.js";
import { languageSupportManager } from "../services/LanguageSupportManager.js";
import { languageSupportSocketServer } from "../services/LanguageSupportSocketServer.js";

function parseQuery(query: unknown): {
  libraryId?: string;
  workspaceRoot?: string;
  filePath?: string;
  serverId?: string;
} {
  const parsed = languageSupportQuerySchema.safeParse(
    query,
  );

  if (!parsed.success) {
    throw new AppError(
      400,
      "Invalid language-support query.",
      parsed.error.flatten(),
    );
  }

  return parsed.data;
}

export const getLanguageSupportConfig: RequestHandler = (request, response) => {
  const query = parseQuery(request.query);
  const input = {
    libraryId: query.libraryId,
    workspaceRoot: query.workspaceRoot,
    filePath: query.filePath,
  };

  if (query.serverId) {
    const server =
      languageSupportManager.describeServerById(
        query.serverId,
        input,
      );

    if (!server) {
      throw new AppError(
        404,
        "Language server is not registered.",
      );
    }

    response.json({
      ok: true,
      server,
    });
    return;
  }

  response.json({
    ok: true,
    servers:
      languageSupportManager.describeServers(input),
  });
};


export const getLanguageSupportEvents: RequestHandler = (
  request,
  response,
) => {
  const requestedLimit = Number(request.query.limit ?? 250);
  const limit = Number.isFinite(requestedLimit)
    ? requestedLimit
    : 250;

  response.json({
    ok: true,
    events: languageSupportEventStore.list(limit),
  });
};

export const postLanguageSupportEvents: RequestHandler = (
  request,
  response,
) => {
  const parsed =
    languageSupportClientEventsRequestSchema.safeParse(
      request.body,
    );

  if (!parsed.success) {
    throw new AppError(
      400,
      "Invalid language-support client events.",
      parsed.error.flatten(),
    );
  }

  languageSupportEventStore.append(parsed.data.events);
  response.status(202).json({ ok: true });
};

export const getLanguageSupportSessions: RequestHandler = (
  _request,
  response,
) => {
  response.json({
    ok: true,
    sessions: languageSupportSocketServer.listSessions(),
  });
};

export const postLanguageSupportSession: RequestHandler = (
  request,
  response,
) => {
  const parsed =
    languageSupportSessionRequestSchema.safeParse(
      request.body,
    );

  if (!parsed.success) {
    throw new AppError(
      400,
      "Invalid language-support session request.",
      parsed.error.flatten(),
    );
  }

  const session = languageSupportSocketServer.createSession(
    parsed.data,
  );

  console.log(
    `[Language Support:${session.serverId}] session requested`
    + ` session=${session.sessionId}`
    + ` workspace=${session.workspaceRoot}`
    + ` file=${session.filePath || "none"}`,
  );

  response.status(201).json({
    ok: true,
    session,
  });
};

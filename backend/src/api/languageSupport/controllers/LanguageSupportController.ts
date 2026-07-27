import type { RequestHandler } from "express";
import { AppError } from "../../../errors/app-error.js";
import {
  languageSupportQuerySchema,
  languageSupportSessionRequestSchema,
} from "../schemas/LanguageSupportSchemas.js";
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
    `[Language Support] Session requested for ${session.displayName} in ${session.workspaceRoot}`,
  );

  response.status(201).json({
    ok: true,
    session,
  });
};

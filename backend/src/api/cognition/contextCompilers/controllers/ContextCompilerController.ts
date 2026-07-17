import type { RequestHandler } from "express";
import { contextCompilerRegistry } from "../../../../core/cognition/conscious/context/ContextCompilerRegistry.js";

export const getContextCompilers: RequestHandler = (_request, response) => {
  response.json({
    ok: true,
    compilers: contextCompilerRegistry.list(),
  });
};

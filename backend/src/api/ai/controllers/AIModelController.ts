import type { RequestHandler } from "express";
import { modelCatalog } from "../../../core/ai/ModelCatalog.js";

export const getAIModels: RequestHandler = async (_request, response, next) => {
  try {
    const models = await modelCatalog.listModels();

    response.json({
      ok: true,
      models,
    });
  } catch (error) {
    next(error);
  }
};

export const refreshAIModels: RequestHandler = async (
  _request,
  response,
  next,
) => {
  try {
    const models = await modelCatalog.refreshModels({
      force: true,
    });

    response.json({
      ok: true,
      models,
    });
  } catch (error) {
    next(error);
  }
};

export const getAIProviderHealth: RequestHandler = async (
  _request,
  response,
  next,
) => {
  try {
    const providers = await modelCatalog.getProviderHealth();

    response.json({
      ok: true,
      providers,
    });
  } catch (error) {
    next(error);
  }
};

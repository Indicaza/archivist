import type { ErrorRequestHandler } from "express";
import { AppError } from "../errors/app-error.js";

export const errorHandler: ErrorRequestHandler = (
  error,
  _request,
  response,
  _next,
) => {
  if (error instanceof AppError) {
    response.status(error.statusCode).json({
      ok: false,
      error: {
        message: error.message,
        details: error.details,
      },
    });

    return;
  }

  console.error(error);

  response.status(500).json({
    ok: false,
    error: {
      message: "An unexpected server error occurred.",
    },
  });
};

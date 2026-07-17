import { Router } from "express";
import { getContextCompilers } from "../controllers/ContextCompilerController.js";

export const contextCompilerRouter = Router();

contextCompilerRouter.get("/", getContextCompilers);

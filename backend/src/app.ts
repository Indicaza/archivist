import cors from "cors";
import path from "node:path";
import { fileURLToPath } from "node:url";
import express from "express";
import { agentRouter } from "./api/agents/routes/AgentRoutes.js";
import { aiModelRouter } from "./api/ai/routes/AIModelRoutes.js";
import { appStateRouter } from "./api/appState/routes/AppStateRoutes.js";
import { chatRouter } from "./api/chats/routes/ChatRoutes.js";
import { collectionRouter } from "./api/collections/routes/CollectionRoutes.js";
import { contextCompilerRouter } from "./api/cognition/contextCompilers/routes/ContextCompilerRoutes.js";
import { contextRetrievalRouter } from "./api/cognition/contextRetrieval/routes/ContextRetrievalRoutes.js";
import { aiRunRouter } from "./api/cognition/runs/routes/AIRunRoutes.js";
import { languageSupportRouter } from "./api/languageSupport/routes/LanguageSupportRoutes.js";
import { libraryRouter } from "./api/libraries/routes/LibraryRoutes.js";
import { errorHandler } from "./middleware/error-handler.js";

const currentDirectory = path.dirname(
  fileURLToPath(import.meta.url),
);
const ideDistributionPath = path.resolve(
  currentDirectory,
  "../../frontend/dist/ide",
);

export const app = express();

app.use(
  cors({
    origin: "http://127.0.0.1:5173",
  }),
);

app.use(express.json());
app.use(
  "/ide",
  express.static(ideDistributionPath, {
    index: "index.html",
    fallthrough: true,
  }),
);

app.get("/api/health", (_request, response) => {
  response.json({
    ok: true,
    app: "Archivist API",
  });
});

app.use("/api/agents", agentRouter);
app.use("/api/ai", aiModelRouter);
app.use("/api/libraries", libraryRouter);
app.use("/api/language-support", languageSupportRouter);
app.use("/api/chats", chatRouter);
app.use("/api/collections", collectionRouter);
app.use("/api/app-state", appStateRouter);
app.use("/api/cognition/context-compilers", contextCompilerRouter);
app.use("/api/cognition/search", contextRetrievalRouter);
app.use("/api/runs", aiRunRouter);

app.use(errorHandler);

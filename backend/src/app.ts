import cors from "cors";
import express from "express";
import { appStateRouter } from "./api/appState/routes/AppStateRoutes.js";
import { libraryRouter } from "./api/libraries/routes/LibraryRoutes.js";
import { errorHandler } from "./middleware/error-handler.js";

export const app = express();

app.use(
  cors({
    origin: "http://127.0.0.1:5173",
  }),
);

app.use(express.json());

app.get("/api/health", (_request, response) => {
  response.json({
    ok: true,
    app: "Archivist API",
  });
});

app.use("/api/libraries", libraryRouter);
app.use("/api/app-state", appStateRouter);

app.use(errorHandler);

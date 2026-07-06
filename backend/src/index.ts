import cors from "cors";
import express from "express";
import { z } from "zod";

const app = express();
const port = Number(process.env.PORT ?? 3333);

app.use(cors({ origin: "http://127.0.0.1:5173" }));
app.use(express.json());

app.get("/health", (_req, res) => {
  res.json({
    ok: true,
    app: "Archivist API",
  });
});

app.post("/libraries", (req, res) => {
  const schema = z.object({
    rootPath: z.string().min(1),
  });

  const parsed = schema.safeParse(req.body);

  if (!parsed.success) {
    res.status(400).json({
      ok: false,
      error: parsed.error.flatten(),
    });
    return;
  }

  res.json({
    ok: true,
    library: {
      id: crypto.randomUUID(),
      name: "New Library",
      rootPath: parsed.data.rootPath,
    },
  });
});

app.listen(port, "127.0.0.1", () => {
  console.log(`Archivist API listening at http://127.0.0.1:${port}`);
});

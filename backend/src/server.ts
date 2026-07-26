import "dotenv/config";
import { app } from "./app.js";
import { closeDatabase } from "./database/database.js";
import { recoverInterruptedLibraryScans } from "./api/libraries/models/LibraryFile.js";

const port = Number(process.env.PORT ?? 3333);
const recoveredScanCount = recoverInterruptedLibraryScans();

if (recoveredScanCount > 0) {
  console.warn(
    `[Library scan] Recovered ${recoveredScanCount} interrupted scan${
      recoveredScanCount === 1 ? "" : "s"
    }.`,
  );
}

export const server = app.listen(port, "127.0.0.1", () => {
  console.log(`Archivist API listening at http://127.0.0.1:${port}`);
});

function shutdown(): void {
  server.close(() => {
    closeDatabase();
    process.exit(0);
  });
}

process.on("SIGINT", shutdown);
process.on("SIGTERM", shutdown);

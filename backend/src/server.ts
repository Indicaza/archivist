import "dotenv/config";
import { app } from "./app.js";
import { closeDatabase } from "./database/database.js";
import { recoverInterruptedLibraryScans } from "./api/libraries/models/LibraryFile.js";
import { installTerminalSocketServer } from "./api/terminals/services/TerminalSocketServer.js";

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
const terminalSocketServer = installTerminalSocketServer(server);
let shuttingDown = false;

function shutdown(): void {
  if (shuttingDown) {
    return;
  }

  shuttingDown = true;
  terminalSocketServer.close();
  server.close(() => {
    closeDatabase();
    process.exit(0);
  });
}

process.on("SIGINT", shutdown);
process.on("SIGTERM", shutdown);

import Database from "better-sqlite3";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { runMigrations } from "./migrations.js";

const currentFilePath = fileURLToPath(import.meta.url);
const currentDirectory = path.dirname(currentFilePath);

const defaultDatabasePath = path.resolve(
  currentDirectory,
  "../../data/archivist.db",
);

export const databasePath =
  process.env.ARCHIVIST_DB_PATH?.trim() || defaultDatabasePath;

fs.mkdirSync(path.dirname(databasePath), {
  recursive: true,
});

export const database = new Database(databasePath);

database.pragma("foreign_keys = ON");
database.pragma("journal_mode = WAL");
database.pragma("busy_timeout = 5000");

runMigrations(database);

const schemaVersion = database.pragma("user_version", {
  simple: true,
}) as number;

function tableExists(tableName: string): boolean {
  return Boolean(
    database
      .prepare(
        `
          SELECT 1
          FROM sqlite_master
          WHERE type IN ('table', 'view')
            AND name = ?
          LIMIT 1
        `,
      )
      .get(tableName),
  );
}

const attachmentTableReady = tableExists("chat_file_attachments");
const libraryIndexReady =
  tableExists("library_documents") &&
  tableExists("library_chunks") &&
  tableExists("library_chunk_search");

console.info(
  `[Database] ${databasePath} · schema v${schemaVersion} · attachments ${
    attachmentTableReady ? "ready" : "missing"
  } · Library index ${libraryIndexReady ? "ready" : "missing"}`,
);

export function closeDatabase(): void {
  if (database.open) {
    database.close();
  }
}

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

const databasePath =
  process.env.ARCHIVIST_DB_PATH?.trim() || defaultDatabasePath;

fs.mkdirSync(path.dirname(databasePath), {
  recursive: true,
});

export const database = new Database(databasePath);

database.pragma("foreign_keys = ON");
database.pragma("journal_mode = WAL");
database.pragma("busy_timeout = 5000");

runMigrations(database);

export function closeDatabase(): void {
  if (database.open) {
    database.close();
  }
}

import type Database from "better-sqlite3";

type Migration = {
  version: number;
  migrate: (database: Database.Database) => void;
};

const migrations: Migration[] = [
  {
    version: 1,
    migrate(database) {
      database.exec(`
        CREATE TABLE libraries (
          id TEXT PRIMARY KEY,
          name TEXT NOT NULL CHECK (length(trim(name)) > 0),
          description TEXT,
          root_path TEXT NOT NULL UNIQUE,
          created_at TEXT NOT NULL DEFAULT (
            strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
          ),
          updated_at TEXT NOT NULL DEFAULT (
            strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
          )
        );

        CREATE TABLE chats (
          id TEXT PRIMARY KEY,
          library_id TEXT,
          title TEXT NOT NULL CHECK (length(trim(title)) > 0),
          type TEXT NOT NULL CHECK (
            type IN ('main', 'standard')
          ),
          created_at TEXT NOT NULL DEFAULT (
            strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
          ),
          updated_at TEXT NOT NULL DEFAULT (
            strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
          ),
          FOREIGN KEY (library_id)
            REFERENCES libraries(id)
            ON DELETE CASCADE
        );

        CREATE UNIQUE INDEX chats_one_main_per_library
          ON chats(library_id)
          WHERE type = 'main'
            AND library_id IS NOT NULL;

        CREATE INDEX chats_library_id_index
          ON chats(library_id);

        CREATE TABLE app_settings (
          id INTEGER PRIMARY KEY CHECK (id = 1),
          selected_library_id TEXT,
          created_at TEXT NOT NULL DEFAULT (
            strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
          ),
          updated_at TEXT NOT NULL DEFAULT (
            strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
          ),
          FOREIGN KEY (selected_library_id)
            REFERENCES libraries(id)
            ON DELETE SET NULL
        );

        INSERT INTO app_settings (
          id,
          selected_library_id
        )
        VALUES (
          1,
          NULL
        );
      `);
    },
  },
  {
    version: 2,
    migrate(database) {
      database.exec(`
        ALTER TABLE libraries
        ADD COLUMN archived_at TEXT;

        CREATE INDEX libraries_archived_at_index
          ON libraries(archived_at);
      `);
    },
  },
];

export function runMigrations(database: Database.Database): void {
  const currentVersion = database.pragma("user_version", {
    simple: true,
  }) as number;

  const pendingMigrations = migrations
    .filter((migration) => migration.version > currentVersion)
    .sort((first, second) => first.version - second.version);

  for (const migration of pendingMigrations) {
    const migrate = database.transaction(() => {
      migration.migrate(database);
      database.pragma(`user_version = ${migration.version}`);
    });

    migrate();
  }
}

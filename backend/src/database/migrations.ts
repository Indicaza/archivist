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
  {
    version: 3,
    migrate(database) {
      database.exec(`
        CREATE TABLE messages (
          id TEXT PRIMARY KEY,
          chat_id TEXT NOT NULL,
          role TEXT NOT NULL CHECK (
            role IN ('user', 'assistant', 'system')
          ),
          content TEXT NOT NULL,
          status TEXT NOT NULL DEFAULT 'complete' CHECK (
            status IN (
              'streaming',
              'complete',
              'cancelled',
              'failed'
            )
          ),
          created_at TEXT NOT NULL DEFAULT (
            strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
          ),
          updated_at TEXT NOT NULL DEFAULT (
            strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
          ),
          FOREIGN KEY (chat_id)
            REFERENCES chats(id)
            ON DELETE CASCADE
        );

        CREATE INDEX messages_chat_id_created_at_index
          ON messages(chat_id, created_at);

        ALTER TABLE app_settings
        ADD COLUMN selected_chat_id TEXT
          REFERENCES chats(id)
          ON DELETE SET NULL;
      `);
    },
  },
  {
    version: 4,
    migrate(database) {
      database.exec(`
        DELETE FROM chats
        WHERE type = 'main';

        UPDATE app_settings
        SET
          selected_chat_id = NULL,
          updated_at = strftime(
            '%Y-%m-%dT%H:%M:%fZ',
            'now'
          );
      `);
    },
  },
  {
    version: 5,
    migrate(database) {
      database.exec(`
        ALTER TABLE chats
        ADD COLUMN archived_at TEXT;

        CREATE INDEX chats_archived_at_index
          ON chats(archived_at);
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

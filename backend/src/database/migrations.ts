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
  {
    version: 6,
    migrate(database) {
      database.exec(`
        ALTER TABLE chats
        ADD COLUMN context_compiler_id TEXT NOT NULL
          DEFAULT 'recent-history';

        ALTER TABLE chats
        ADD COLUMN context_compiler_version INTEGER NOT NULL
          DEFAULT 1
          CHECK (context_compiler_version > 0);

        ALTER TABLE chats
        ADD COLUMN context_compiler_config TEXT NOT NULL
          DEFAULT '{"totalTokens":32000,"responseTokenReserve":4000}';
      `);
    },
  },
  {
    version: 7,
    migrate(database) {
      database.exec(`
        CREATE VIRTUAL TABLE message_search USING fts5(
          message_id UNINDEXED,
          chat_id UNINDEXED,
          role UNINDEXED,
          content,
          created_at UNINDEXED,
          tokenize = 'unicode61'
        );

        INSERT INTO message_search (
          message_id,
          chat_id,
          role,
          content,
          created_at
        )
        SELECT
          id,
          chat_id,
          role,
          content,
          created_at
        FROM messages
        WHERE status = 'complete'
          AND length(trim(content)) > 0;

        CREATE TRIGGER messages_search_after_insert
        AFTER INSERT ON messages
        WHEN
          new.status = 'complete'
          AND length(trim(new.content)) > 0
        BEGIN
          INSERT INTO message_search (
            message_id,
            chat_id,
            role,
            content,
            created_at
          )
          VALUES (
            new.id,
            new.chat_id,
            new.role,
            new.content,
            new.created_at
          );
        END;

        CREATE TRIGGER messages_search_after_update
        AFTER UPDATE OF
          chat_id,
          role,
          content,
          status,
          created_at
        ON messages
        BEGIN
          DELETE FROM message_search
          WHERE message_id = old.id;

          INSERT INTO message_search (
            message_id,
            chat_id,
            role,
            content,
            created_at
          )
          SELECT
            new.id,
            new.chat_id,
            new.role,
            new.content,
            new.created_at
          WHERE new.status = 'complete'
            AND length(trim(new.content)) > 0;
        END;

        CREATE TRIGGER messages_search_after_delete
        AFTER DELETE ON messages
        BEGIN
          DELETE FROM message_search
          WHERE message_id = old.id;
         END;
      `);
    },
  },
  {
    version: 8,
    migrate(database) {
      database.exec(`
        CREATE TABLE agents (
          id TEXT PRIMARY KEY,

          name TEXT NOT NULL CHECK (
            length(trim(name)) > 0
          ),

          description TEXT,

          identity_config TEXT NOT NULL
            DEFAULT '{}',

          profession_config TEXT NOT NULL
            DEFAULT '{}',

          doctrine TEXT,

          output_contract TEXT NOT NULL
            DEFAULT '{}',

          system_instructions TEXT,

          generation_config TEXT NOT NULL
            DEFAULT '{}',

          context_compiler_id TEXT NOT NULL
            DEFAULT 'recent-history',

          context_compiler_version INTEGER NOT NULL
            DEFAULT 1
            CHECK (context_compiler_version > 0),

          context_compiler_config TEXT NOT NULL
            DEFAULT '{"totalTokens":32000,"responseTokenReserve":4000}',

          is_built_in INTEGER NOT NULL
            DEFAULT 0
            CHECK (is_built_in IN (0, 1)),

          archived_at TEXT,

          created_at TEXT NOT NULL DEFAULT (
            strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
          ),

          updated_at TEXT NOT NULL DEFAULT (
            strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
          )
        );

        CREATE INDEX agents_archived_at_index
          ON agents(archived_at);

        CREATE INDEX agents_updated_at_index
          ON agents(updated_at);

        INSERT INTO agents (
          id,
          name,
          description,
          identity_config,
          profession_config,
          doctrine,
          output_contract,
          system_instructions,
          generation_config,
          context_compiler_id,
          context_compiler_version,
          context_compiler_config,
          is_built_in
        )
        VALUES (
          '00000000-0000-4000-8000-000000000001',

          'Archivist Default',

          'The built-in general-purpose Archivist Agent.',

          '{"personality":"Thoughtful, practical, observant, and calm.","temperament":"Patient and direct.","voice":"Clear, useful, and conversational.","backstory":null}',

          '{"jobTitle":"Local-first AI workspace assistant","mission":"Help the user understand information, maintain durable context, and perform useful computer work safely.","expertise":[],"responsibilities":["Answer the current request clearly.","Use supplied context without inventing missing information.","Preserve the distinction between current intent and retrieved evidence."],"successCriteria":["The response is accurate, useful, and grounded in available context.","The user''s current request remains the highest-priority instruction."],"limitations":["Do not claim to have inspected files, tools, or context that were not provided."]}',

          NULL,

          '{"responseStyle":"Clear, practical, and concise unless more depth is requested.","verbosity":"balanced","formattingRules":[],"codeOutputPreferences":[],"citationRequirements":null,"followUpBehavior":"Do not add unnecessary follow-up offers or questions after answering."}',

          'You are Archivist, a thoughtful local-first AI assistant. Give clear and useful answers. Do not claim to have inspected files or context that was not supplied. Prefer concise answers unless the user asks for depth.',

          '{"provider":"openai","model":"gpt-5-mini","temperature":null,"maxOutputTokens":4000,"topP":null,"frequencyPenalty":null,"presencePenalty":null}',

          'recent-history',
          1,
          '{"totalTokens":32000,"responseTokenReserve":4000}',
          1
        );

        ALTER TABLE chats
        ADD COLUMN agent_id TEXT
          REFERENCES agents(id)
          ON DELETE RESTRICT;

        UPDATE chats
        SET agent_id = '00000000-0000-4000-8000-000000000001'
        WHERE agent_id IS NULL;

        CREATE INDEX chats_agent_id_index
          ON chats(agent_id);
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

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
  {
    version: 9,
    migrate(database) {
      database.exec(`
        CREATE TABLE library_scans (
          id TEXT PRIMARY KEY,
          library_id TEXT NOT NULL,
          status TEXT NOT NULL DEFAULT 'running' CHECK (
            status IN ('running', 'complete', 'partial', 'failed')
          ),
          started_at TEXT NOT NULL DEFAULT (
            strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
          ),
          completed_at TEXT,
          discovered_file_count INTEGER NOT NULL DEFAULT 0 CHECK (
            discovered_file_count >= 0
          ),
          cataloged_file_count INTEGER NOT NULL DEFAULT 0 CHECK (
            cataloged_file_count >= 0
          ),
          ignored_entry_count INTEGER NOT NULL DEFAULT 0 CHECK (
            ignored_entry_count >= 0
          ),
          error_count INTEGER NOT NULL DEFAULT 0 CHECK (
            error_count >= 0
          ),
          error_message TEXT,
          FOREIGN KEY (library_id)
            REFERENCES libraries(id)
            ON DELETE CASCADE
        );

        CREATE INDEX library_scans_library_started_at_index
          ON library_scans(library_id, started_at DESC);

        CREATE UNIQUE INDEX library_scans_one_running_per_library
          ON library_scans(library_id)
          WHERE status = 'running';

        CREATE TABLE library_files (
          id TEXT PRIMARY KEY,
          library_id TEXT NOT NULL,
          relative_path TEXT NOT NULL CHECK (
            length(trim(relative_path)) > 0
          ),
          name TEXT NOT NULL CHECK (
            length(trim(name)) > 0
          ),
          extension TEXT NOT NULL CHECK (
            length(trim(extension)) > 0
          ),
          size_bytes INTEGER NOT NULL CHECK (
            size_bytes >= 0
          ),
          modified_at TEXT NOT NULL,
          status TEXT NOT NULL DEFAULT 'available' CHECK (
            status IN ('available', 'unreadable', 'missing')
          ),
          last_seen_scan_id TEXT,
          last_seen_at TEXT,
          created_at TEXT NOT NULL DEFAULT (
            strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
          ),
          updated_at TEXT NOT NULL DEFAULT (
            strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
          ),
          FOREIGN KEY (library_id)
            REFERENCES libraries(id)
            ON DELETE CASCADE,
          FOREIGN KEY (last_seen_scan_id)
            REFERENCES library_scans(id)
            ON DELETE SET NULL,
          UNIQUE (library_id, relative_path)
        );

        CREATE INDEX library_files_library_path_index
          ON library_files(library_id, relative_path);

        CREATE INDEX library_files_library_status_index
          ON library_files(library_id, status);
      `);
    },
  },
  {
    version: 10,
    migrate(database) {
      database.exec(`
        CREATE TABLE chat_file_attachments (
          id TEXT PRIMARY KEY,
          chat_id TEXT NOT NULL,
          library_file_id TEXT NOT NULL,
          created_at TEXT NOT NULL DEFAULT (
            strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
          ),
          FOREIGN KEY (chat_id)
            REFERENCES chats(id)
            ON DELETE CASCADE,
          FOREIGN KEY (library_file_id)
            REFERENCES library_files(id)
            ON DELETE CASCADE,
          UNIQUE (chat_id, library_file_id)
        );

        CREATE INDEX chat_file_attachments_chat_created_at_index
          ON chat_file_attachments(chat_id, created_at);
      `);
    },
  },
  {
    // Repair databases that reported schema version 10 without the attachment
    // table. CREATE IF NOT EXISTS keeps this safe for healthy databases.
    version: 11,
    migrate(database) {
      database.exec(`
        CREATE TABLE IF NOT EXISTS chat_file_attachments (
          id TEXT PRIMARY KEY,
          chat_id TEXT NOT NULL,
          library_file_id TEXT NOT NULL,
          created_at TEXT NOT NULL DEFAULT (
            strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
          ),
          FOREIGN KEY (chat_id)
            REFERENCES chats(id)
            ON DELETE CASCADE,
          FOREIGN KEY (library_file_id)
            REFERENCES library_files(id)
            ON DELETE CASCADE,
          UNIQUE (chat_id, library_file_id)
        );

        CREATE INDEX IF NOT EXISTS
          chat_file_attachments_chat_created_at_index
          ON chat_file_attachments(chat_id, created_at);
      `);
    },
  },
  {
    version: 12,
    migrate(database) {
      database.exec(`
        CREATE TABLE context_runs (
          id TEXT PRIMARY KEY,
          chat_id TEXT NOT NULL,
          user_message_id TEXT NOT NULL,
          assistant_message_id TEXT NOT NULL UNIQUE,
          provider TEXT NOT NULL,
          model TEXT NOT NULL,
          agent_id TEXT NOT NULL,
          snapshot_json TEXT NOT NULL CHECK (json_valid(snapshot_json)),
          created_at TEXT NOT NULL DEFAULT (
            strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
          ),
          FOREIGN KEY (chat_id)
            REFERENCES chats(id)
            ON DELETE CASCADE,
          FOREIGN KEY (user_message_id)
            REFERENCES messages(id)
            ON DELETE CASCADE,
          FOREIGN KEY (assistant_message_id)
            REFERENCES messages(id)
            ON DELETE CASCADE
        );

        CREATE INDEX context_runs_chat_created_at_index
          ON context_runs(chat_id, created_at DESC);
      `);
    },
  },

  {
    version: 13,
    migrate(database) {
      database.exec(`
        CREATE TABLE library_documents (
          library_file_id TEXT PRIMARY KEY,
          library_id TEXT NOT NULL,
          status TEXT NOT NULL CHECK (
            status IN ('indexed', 'empty', 'unavailable', 'failed')
          ),
          content_hash TEXT,
          source_modified_at TEXT NOT NULL,
          source_size_bytes INTEGER NOT NULL CHECK (
            source_size_bytes >= 0
          ),
          chunk_count INTEGER NOT NULL DEFAULT 0 CHECK (
            chunk_count >= 0
          ),
          error_message TEXT,
          extracted_at TEXT NOT NULL DEFAULT (
            strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
          ),
          updated_at TEXT NOT NULL DEFAULT (
            strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
          ),
          FOREIGN KEY (library_file_id)
            REFERENCES library_files(id)
            ON DELETE CASCADE,
          FOREIGN KEY (library_id)
            REFERENCES libraries(id)
            ON DELETE CASCADE
        );

        CREATE INDEX library_documents_library_status_index
          ON library_documents(library_id, status);

        CREATE TABLE library_chunks (
          id TEXT PRIMARY KEY,
          library_file_id TEXT NOT NULL,
          library_id TEXT NOT NULL,
          ordinal INTEGER NOT NULL CHECK (
            ordinal >= 0
          ),
          start_line INTEGER NOT NULL CHECK (
            start_line >= 1
          ),
          end_line INTEGER NOT NULL CHECK (
            end_line >= start_line
          ),
          content TEXT NOT NULL CHECK (
            length(trim(content)) > 0
          ),
          estimated_tokens INTEGER NOT NULL CHECK (
            estimated_tokens > 0
          ),
          content_hash TEXT NOT NULL,
          created_at TEXT NOT NULL DEFAULT (
            strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
          ),
          FOREIGN KEY (library_file_id)
            REFERENCES library_documents(library_file_id)
            ON DELETE CASCADE,
          FOREIGN KEY (library_id)
            REFERENCES libraries(id)
            ON DELETE CASCADE,
          UNIQUE (library_file_id, ordinal)
        );

        CREATE INDEX library_chunks_library_file_ordinal_index
          ON library_chunks(library_file_id, ordinal);

        CREATE INDEX library_chunks_library_index
          ON library_chunks(library_id);

        CREATE VIRTUAL TABLE library_chunk_search USING fts5(
          chunk_id UNINDEXED,
          library_id UNINDEXED,
          library_file_id UNINDEXED,
          relative_path,
          file_name,
          content,
          tokenize = 'unicode61'
        );

        CREATE TRIGGER library_chunk_search_after_insert
        AFTER INSERT ON library_chunks
        BEGIN
          INSERT INTO library_chunk_search (
            chunk_id,
            library_id,
            library_file_id,
            relative_path,
            file_name,
            content
          )
          SELECT
            new.id,
            new.library_id,
            new.library_file_id,
            library_files.relative_path,
            library_files.name,
            new.content
          FROM library_files
          WHERE library_files.id = new.library_file_id;
        END;

        CREATE TRIGGER library_chunk_search_after_update
        AFTER UPDATE OF
          library_id,
          library_file_id,
          content
        ON library_chunks
        BEGIN
          DELETE FROM library_chunk_search
          WHERE chunk_id = old.id;

          INSERT INTO library_chunk_search (
            chunk_id,
            library_id,
            library_file_id,
            relative_path,
            file_name,
            content
          )
          SELECT
            new.id,
            new.library_id,
            new.library_file_id,
            library_files.relative_path,
            library_files.name,
            new.content
          FROM library_files
          WHERE library_files.id = new.library_file_id;
        END;

        CREATE TRIGGER library_chunk_search_after_delete
        AFTER DELETE ON library_chunks
        BEGIN
          DELETE FROM library_chunk_search
          WHERE chunk_id = old.id;
        END;
      `);
    },
  },
  {
    version: 14,
    migrate(database) {
      database.exec(`
        CREATE TABLE collections (
          id TEXT PRIMARY KEY,
          parent_collection_id TEXT,
          name TEXT NOT NULL CHECK (
            length(trim(name)) > 0
          ),
          position INTEGER NOT NULL DEFAULT 0 CHECK (
            position >= 0
          ),
          archived_at TEXT,
          created_at TEXT NOT NULL DEFAULT (
            strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
          ),
          updated_at TEXT NOT NULL DEFAULT (
            strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
          ),
          CHECK (
            parent_collection_id IS NULL
            OR parent_collection_id != id
          ),
          FOREIGN KEY (parent_collection_id)
            REFERENCES collections(id)
            ON DELETE CASCADE
        );

        CREATE INDEX collections_parent_position_index
          ON collections(parent_collection_id, position, name);

        CREATE INDEX collections_archived_at_index
          ON collections(archived_at);

        CREATE TABLE collection_items (
          id TEXT PRIMARY KEY,
          collection_id TEXT NOT NULL,
          item_type TEXT NOT NULL CHECK (
            item_type IN ('library', 'chat')
          ),
          item_id TEXT NOT NULL,
          position INTEGER NOT NULL DEFAULT 0 CHECK (
            position >= 0
          ),
          created_at TEXT NOT NULL DEFAULT (
            strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
          ),
          FOREIGN KEY (collection_id)
            REFERENCES collections(id)
            ON DELETE CASCADE,
          UNIQUE (collection_id, item_type, item_id)
        );

        CREATE INDEX collection_items_collection_position_index
          ON collection_items(collection_id, item_type, position);

        CREATE INDEX collection_items_target_index
          ON collection_items(item_type, item_id);

        CREATE TABLE collection_agents (
          id TEXT PRIMARY KEY,
          collection_id TEXT NOT NULL,
          agent_id TEXT NOT NULL,
          position INTEGER NOT NULL DEFAULT 0 CHECK (
            position >= 0
          ),
          is_default INTEGER NOT NULL DEFAULT 0 CHECK (
            is_default IN (0, 1)
          ),
          created_at TEXT NOT NULL DEFAULT (
            strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
          ),
          FOREIGN KEY (collection_id)
            REFERENCES collections(id)
            ON DELETE CASCADE,
          FOREIGN KEY (agent_id)
            REFERENCES agents(id)
            ON DELETE CASCADE,
          UNIQUE (collection_id, agent_id)
        );

        CREATE INDEX collection_agents_collection_position_index
          ON collection_agents(collection_id, position);

        CREATE UNIQUE INDEX collection_agents_one_default
          ON collection_agents(collection_id)
          WHERE is_default = 1;

        CREATE TRIGGER collection_items_validate_library_insert
        BEFORE INSERT ON collection_items
        WHEN
          new.item_type = 'library'
          AND NOT EXISTS (
            SELECT 1
            FROM libraries
            WHERE id = new.item_id
          )
        BEGIN
          SELECT RAISE(ABORT, 'Collection Library not found');
        END;

        CREATE TRIGGER collection_items_validate_chat_insert
        BEFORE INSERT ON collection_items
        WHEN
          new.item_type = 'chat'
          AND NOT EXISTS (
            SELECT 1
            FROM chats
            WHERE id = new.item_id
          )
        BEGIN
          SELECT RAISE(ABORT, 'Collection Chat not found');
        END;

        CREATE TRIGGER collection_items_library_cleanup
        AFTER DELETE ON libraries
        BEGIN
          DELETE FROM collection_items
          WHERE item_type = 'library'
            AND item_id = old.id;
        END;

        CREATE TRIGGER collection_items_chat_cleanup
        AFTER DELETE ON chats
        BEGIN
          DELETE FROM collection_items
          WHERE item_type = 'chat'
            AND item_id = old.id;
        END;

        ALTER TABLE app_settings
        ADD COLUMN selected_collection_id TEXT
          REFERENCES collections(id)
          ON DELETE SET NULL;
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

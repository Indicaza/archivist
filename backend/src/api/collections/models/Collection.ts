import { database } from "../../../database/database.js";
import { AppError } from "../../../errors/app-error.js";
import type {
  ArchiveCollectionResult,
  Collection,
  CollectionItemType,
  CollectionScope,
  CreateCollectionInput,
  UpdateCollectionInput,
} from "../types/CollectionTypes.js";

type CollectionRow = {
  id: string;
  parent_collection_id: string | null;
  name: string;
  position: number;
  archived_at: string | null;
  created_at: string;
  updated_at: string;
};

type CollectionItemRow = {
  collection_id: string;
  item_type: CollectionItemType;
  item_id: string;
};

type CollectionAgentRow = {
  collection_id: string;
  agent_id: string;
  is_default: number;
};

type AppSettingsRow = {
  selected_collection_id: string | null;
};

type ExistsRow = {
  exists_flag: number;
};

function uniqueIds(ids: string[]): string[] {
  return [...new Set(ids)];
}

function placeholders(count: number): string {
  return Array.from({ length: count }, () => "?").join(", ");
}

function loadCollectionRows(includeArchived: boolean): CollectionRow[] {
  return database
    .prepare(
      `
        SELECT
          id,
          parent_collection_id,
          name,
          position,
          archived_at,
          created_at,
          updated_at
        FROM collections
        WHERE archived_at IS ${includeArchived ? "NOT NULL" : "NULL"}
        ORDER BY position ASC, name COLLATE NOCASE ASC
      `,
    )
    .all() as CollectionRow[];
}

function loadCollectionRow(collectionId: string): CollectionRow | null {
  const row = database
    .prepare(
      `
        SELECT
          id,
          parent_collection_id,
          name,
          position,
          archived_at,
          created_at,
          updated_at
        FROM collections
        WHERE id = ?
      `,
    )
    .get(collectionId) as CollectionRow | undefined;

  return row ?? null;
}

function loadItems(collectionIds: string[]): CollectionItemRow[] {
  if (collectionIds.length === 0) {
    return [];
  }

  return database
    .prepare(
      `
        SELECT
          collection_id,
          item_type,
          item_id
        FROM collection_items
        WHERE collection_id IN (${placeholders(collectionIds.length)})
        ORDER BY collection_id ASC, item_type ASC, position ASC, created_at ASC
      `,
    )
    .all(...collectionIds) as CollectionItemRow[];
}

function loadAgents(collectionIds: string[]): CollectionAgentRow[] {
  if (collectionIds.length === 0) {
    return [];
  }

  return database
    .prepare(
      `
        SELECT
          collection_id,
          agent_id,
          is_default
        FROM collection_agents
        WHERE collection_id IN (${placeholders(collectionIds.length)})
        ORDER BY collection_id ASC, position ASC, created_at ASC
      `,
    )
    .all(...collectionIds) as CollectionAgentRow[];
}

function collectionPath(
  row: CollectionRow,
  rowsById: Map<string, CollectionRow>,
): string {
  const names = [row.name];
  const seen = new Set([row.id]);
  let parentId = row.parent_collection_id;

  while (parentId) {
    if (seen.has(parentId)) {
      break;
    }

    seen.add(parentId);
    const parent = rowsById.get(parentId);

    if (!parent) {
      break;
    }

    names.unshift(parent.name);
    parentId = parent.parent_collection_id;
  }

  return names.join(" / ");
}

function mapCollections(rows: CollectionRow[]): Collection[] {
  const collectionIds = rows.map((row) => row.id);
  const items = loadItems(collectionIds);
  const agents = loadAgents(collectionIds);
  const rowsById = new Map(rows.map((row) => [row.id, row]));
  const itemsByCollection = new Map<string, CollectionItemRow[]>();
  const agentsByCollection = new Map<string, CollectionAgentRow[]>();

  for (const item of items) {
    const collectionItems = itemsByCollection.get(item.collection_id) ?? [];
    collectionItems.push(item);
    itemsByCollection.set(item.collection_id, collectionItems);
  }

  for (const agent of agents) {
    const collectionAgents = agentsByCollection.get(agent.collection_id) ?? [];
    collectionAgents.push(agent);
    agentsByCollection.set(agent.collection_id, collectionAgents);
  }

  return rows
    .map((row) => {
      const collectionItems = itemsByCollection.get(row.id) ?? [];
      const collectionAgents = agentsByCollection.get(row.id) ?? [];

      return {
        id: row.id,
        parentCollectionId: row.parent_collection_id,
        name: row.name,
        path: collectionPath(row, rowsById),
        position: row.position,
        libraryIds: collectionItems
          .filter((item) => item.item_type === "library")
          .map((item) => item.item_id),
        chatIds: collectionItems
          .filter((item) => item.item_type === "chat")
          .map((item) => item.item_id),
        agentIds: collectionAgents.map((agent) => agent.agent_id),
        defaultAgentId:
          collectionAgents.find((agent) => agent.is_default === 1)?.agent_id ??
          null,
        archivedAt: row.archived_at,
        createdAt: row.created_at,
        updatedAt: row.updated_at,
      };
    })
    .sort((left, right) => left.path.localeCompare(right.path));
}

function requireCollectionRow(collectionId: string): CollectionRow {
  const collection = loadCollectionRow(collectionId);

  if (!collection) {
    throw new AppError(404, "Collection not found.");
  }

  return collection;
}

function requireActiveCollectionRow(collectionId: string): CollectionRow {
  const collection = requireCollectionRow(collectionId);

  if (collection.archived_at) {
    throw new AppError(
      409,
      "This Collection is archived. Restore it before changing it.",
    );
  }

  return collection;
}

function validateParent(
  collectionId: string | null,
  parentCollectionId: string | null,
): void {
  if (parentCollectionId === null) {
    return;
  }

  if (parentCollectionId === collectionId) {
    throw new AppError(409, "A Collection cannot contain itself.");
  }

  requireActiveCollectionRow(parentCollectionId);

  if (collectionId === null) {
    return;
  }

  const row = database
    .prepare(
      `
        WITH RECURSIVE descendants(id) AS (
          SELECT id
          FROM collections
          WHERE parent_collection_id = ?

          UNION ALL

          SELECT collections.id
          FROM collections
          JOIN descendants
            ON collections.parent_collection_id = descendants.id
        )
        SELECT EXISTS(
          SELECT 1
          FROM descendants
          WHERE id = ?
        ) AS exists_flag
      `,
    )
    .get(collectionId, parentCollectionId) as ExistsRow;

  if (row.exists_flag) {
    throw new AppError(
      409,
      "A Collection cannot be moved beneath one of its descendants.",
    );
  }
}

function validateActiveTargets(
  targetType: CollectionItemType | "agent",
  ids: string[],
): void {
  const uniqueTargetIds = uniqueIds(ids);

  if (uniqueTargetIds.length === 0) {
    return;
  }

  const table =
    targetType === "library"
      ? "libraries"
      : targetType === "chat"
        ? "chats"
        : "agents";
  const rows = database
    .prepare(
      `
        SELECT id
        FROM ${table}
        WHERE id IN (${placeholders(uniqueTargetIds.length)})
          AND archived_at IS NULL
      `,
    )
    .all(...uniqueTargetIds) as { id: string }[];
  const foundIds = new Set(rows.map((row) => row.id));
  const missingIds = uniqueTargetIds.filter((id) => !foundIds.has(id));

  if (missingIds.length > 0) {
    const label =
      targetType === "library"
        ? "Library"
        : targetType === "chat"
          ? "Chat"
          : "Agent";

    throw new AppError(
      400,
      `One or more Collection ${label} references are unavailable.`,
      {
        ids: missingIds,
      },
    );
  }
}

function replaceItems(
  collectionId: string,
  itemType: CollectionItemType,
  ids: string[],
): void {
  const targetIds = uniqueIds(ids);
  validateActiveTargets(itemType, targetIds);

  database
    .prepare(
      `
        DELETE FROM collection_items
        WHERE collection_id = ?
          AND item_type = ?
      `,
    )
    .run(collectionId, itemType);

  const insert = database.prepare(
    `
      INSERT INTO collection_items (
        id,
        collection_id,
        item_type,
        item_id,
        position
      )
      VALUES (?, ?, ?, ?, ?)
    `,
  );

  targetIds.forEach((itemId, position) => {
    insert.run(crypto.randomUUID(), collectionId, itemType, itemId, position);
  });
}

function replaceAgents(
  collectionId: string,
  agentIds: string[],
  defaultAgentId: string | null,
): void {
  const targetAgentIds = uniqueIds(agentIds);
  validateActiveTargets("agent", targetAgentIds);

  if (defaultAgentId !== null && !targetAgentIds.includes(defaultAgentId)) {
    throw new AppError(
      400,
      "The default Agent must belong to the Collection roster.",
    );
  }

  database
    .prepare(
      `
        DELETE FROM collection_agents
        WHERE collection_id = ?
      `,
    )
    .run(collectionId);

  const insert = database.prepare(
    `
      INSERT INTO collection_agents (
        id,
        collection_id,
        agent_id,
        position,
        is_default
      )
      VALUES (?, ?, ?, ?, ?)
    `,
  );

  targetAgentIds.forEach((agentId, position) => {
    insert.run(
      crypto.randomUUID(),
      collectionId,
      agentId,
      position,
      agentId === defaultAgentId ? 1 : 0,
    );
  });
}

function selectedCollectionId(): string | null {
  const row = database
    .prepare(
      `
        SELECT selected_collection_id
        FROM app_settings
        WHERE id = 1
      `,
    )
    .get() as AppSettingsRow | undefined;

  if (!row) {
    throw new Error("Archivist app settings could not be loaded.");
  }

  return row.selected_collection_id;
}

function clearSelectedCollection(): void {
  database
    .prepare(
      `
        UPDATE app_settings
        SET
          selected_collection_id = NULL,
          updated_at = strftime(
            '%Y-%m-%dT%H:%M:%fZ',
            'now'
          )
        WHERE id = 1
      `,
    )
    .run();
}

export function getAllCollections(): Collection[] {
  return mapCollections([
    ...loadCollectionRows(false),
    ...loadCollectionRows(true),
  ]).filter((collection) => collection.archivedAt === null);
}

export function getArchivedCollections(): Collection[] {
  return mapCollections([
    ...loadCollectionRows(false),
    ...loadCollectionRows(true),
  ]).filter((collection) => collection.archivedAt !== null);
}

export function getCollectionById(collectionId: string): Collection | null {
  const row = loadCollectionRow(collectionId);

  if (!row) {
    return null;
  }

  const rows = [...loadCollectionRows(false), ...loadCollectionRows(true)];
  return mapCollections(rows).find((collection) => collection.id === collectionId) ?? null;
}

export function getCollectionScope(collectionId: string): CollectionScope {
  requireActiveCollectionRow(collectionId);

  const collectionRows = database
    .prepare(
      `
        WITH RECURSIVE scoped_collections(id, depth) AS (
          SELECT id, 0
          FROM collections
          WHERE id = ?
            AND archived_at IS NULL

          UNION ALL

          SELECT collections.id, scoped_collections.depth + 1
          FROM collections
          JOIN scoped_collections
            ON collections.parent_collection_id = scoped_collections.id
          WHERE collections.archived_at IS NULL
        )
        SELECT scoped_collections.id
        FROM scoped_collections
        JOIN collections
          ON collections.id = scoped_collections.id
        ORDER BY
          scoped_collections.depth ASC,
          collections.position ASC,
          collections.name COLLATE NOCASE ASC
      `,
    )
    .all(collectionId) as { id: string }[];
  const collectionIds = collectionRows.map((row) => row.id);

  if (collectionIds.length === 0) {
    throw new AppError(404, "Collection not found.");
  }

  const items = database
    .prepare(
      `
        SELECT
          collection_items.item_type,
          collection_items.item_id
        FROM collection_items
        JOIN collections
          ON collections.id = collection_items.collection_id
        WHERE collection_items.collection_id IN (
          ${placeholders(collectionIds.length)}
        )
        ORDER BY
          collections.position ASC,
          collection_items.position ASC,
          collection_items.created_at ASC
      `,
    )
    .all(...collectionIds) as Pick<CollectionItemRow, "item_type" | "item_id">[];
  const referencedLibraryIds = uniqueIds(
    items
      .filter((item) => item.item_type === "library")
      .map((item) => item.item_id),
  );
  const referencedChatIds = uniqueIds(
    items
      .filter((item) => item.item_type === "chat")
      .map((item) => item.item_id),
  );
  const activeLibraryIdSet = new Set(
    referencedLibraryIds.length === 0
      ? []
      : (
          database
            .prepare(
              `
                SELECT id
                FROM libraries
                WHERE id IN (${placeholders(referencedLibraryIds.length)})
                  AND archived_at IS NULL
              `,
            )
            .all(...referencedLibraryIds) as { id: string }[]
        ).map((row) => row.id),
  );
  const activeLibraryIds = referencedLibraryIds.filter((id) =>
    activeLibraryIdSet.has(id),
  );
  const activeDirectChatIdSet = new Set(
    referencedChatIds.length === 0
      ? []
      : (
          database
            .prepare(
              `
                SELECT id
                FROM chats
                WHERE id IN (${placeholders(referencedChatIds.length)})
                  AND archived_at IS NULL
              `,
            )
            .all(...referencedChatIds) as { id: string }[]
        ).map((row) => row.id),
  );
  const activeDirectChatIds = referencedChatIds.filter((id) =>
    activeDirectChatIdSet.has(id),
  );
  const libraryChatIds =
    activeLibraryIds.length === 0
      ? []
      : (
          database
            .prepare(
              `
                SELECT id
                FROM chats
                WHERE library_id IN (${placeholders(activeLibraryIds.length)})
                  AND type = 'standard'
                  AND archived_at IS NULL
                ORDER BY updated_at DESC, created_at DESC
              `,
            )
            .all(...activeLibraryIds) as { id: string }[]
        ).map((row) => row.id);
  const selectedCollection = getCollectionById(collectionId);

  if (!selectedCollection) {
    throw new AppError(404, "Collection not found.");
  }

  const activeRosterIds =
    selectedCollection.agentIds.length === 0
      ? []
      : (
          database
            .prepare(
              `
                SELECT id
                FROM agents
                WHERE id IN (${placeholders(selectedCollection.agentIds.length)})
                  AND archived_at IS NULL
              `,
            )
            .all(...selectedCollection.agentIds) as { id: string }[]
        ).map((row) => row.id);

  return {
    collectionIds,
    libraryIds: activeLibraryIds,
    directChatIds: activeDirectChatIds,
    chatIds: uniqueIds([...activeDirectChatIds, ...libraryChatIds]),
    agentIds: selectedCollection.agentIds.filter((id) =>
      activeRosterIds.includes(id),
    ),
    defaultAgentId:
      selectedCollection.defaultAgentId &&
      activeRosterIds.includes(selectedCollection.defaultAgentId)
        ? selectedCollection.defaultAgentId
        : null,
  };
}

export function createCollection(input: CreateCollectionInput): Collection {
  const collectionId = crypto.randomUUID();
  const parentCollectionId = input.parentCollectionId ?? null;
  const libraryIds = uniqueIds(input.libraryIds ?? []);
  const chatIds = uniqueIds(input.chatIds ?? []);
  const agentIds = uniqueIds(input.agentIds ?? []);
  const defaultAgentId = input.defaultAgentId ?? null;

  validateParent(null, parentCollectionId);

  const createTransaction = database.transaction(() => {
    database
      .prepare(
        `
          INSERT INTO collections (
            id,
            parent_collection_id,
            name,
            position
          )
          VALUES (?, ?, ?, ?)
        `,
      )
      .run(
        collectionId,
        parentCollectionId,
        input.name.trim(),
        input.position ?? 0,
      );

    replaceItems(collectionId, "library", libraryIds);
    replaceItems(collectionId, "chat", chatIds);
    replaceAgents(collectionId, agentIds, defaultAgentId);

    const collection = getCollectionById(collectionId);

    if (!collection) {
      throw new Error("The new Collection could not be loaded.");
    }

    return collection;
  });

  return createTransaction();
}

export function updateCollection(
  collectionId: string,
  input: UpdateCollectionInput,
): Collection {
  const current = getCollectionById(collectionId);

  if (!current) {
    throw new AppError(404, "Collection not found.");
  }

  if (current.archivedAt) {
    throw new AppError(
      409,
      "This Collection is archived. Restore it before editing it.",
    );
  }

  const parentCollectionId =
    input.parentCollectionId === undefined
      ? current.parentCollectionId
      : input.parentCollectionId;
  validateParent(collectionId, parentCollectionId);

  const nextAgentIds = uniqueIds(input.agentIds ?? current.agentIds);
  let nextDefaultAgentId =
    input.defaultAgentId === undefined
      ? current.defaultAgentId
      : input.defaultAgentId;

  if (
    input.agentIds !== undefined &&
    input.defaultAgentId === undefined &&
    nextDefaultAgentId !== null &&
    !nextAgentIds.includes(nextDefaultAgentId)
  ) {
    nextDefaultAgentId = null;
  }

  const updateTransaction = database.transaction(() => {
    database
      .prepare(
        `
          UPDATE collections
          SET
            parent_collection_id = ?,
            name = ?,
            position = ?,
            updated_at = strftime(
              '%Y-%m-%dT%H:%M:%fZ',
              'now'
            )
          WHERE id = ?
        `,
      )
      .run(
        parentCollectionId,
        input.name?.trim() ?? current.name,
        input.position ?? current.position,
        collectionId,
      );

    if (input.libraryIds !== undefined) {
      replaceItems(collectionId, "library", input.libraryIds);
    }

    if (input.chatIds !== undefined) {
      replaceItems(collectionId, "chat", input.chatIds);
    }

    if (
      input.agentIds !== undefined ||
      input.defaultAgentId !== undefined
    ) {
      replaceAgents(collectionId, nextAgentIds, nextDefaultAgentId);
    }

    const collection = getCollectionById(collectionId);

    if (!collection) {
      throw new Error("The updated Collection could not be loaded.");
    }

    return collection;
  });

  return updateTransaction();
}

export function archiveCollection(
  collectionId: string,
): ArchiveCollectionResult {
  const collection = requireActiveCollectionRow(collectionId);

  const archiveTransaction = database.transaction(() => {
    const descendantRows = database
      .prepare(
        `
          WITH RECURSIVE descendants(id) AS (
            SELECT id
            FROM collections
            WHERE id = ?

            UNION ALL

            SELECT collections.id
            FROM collections
            JOIN descendants
              ON collections.parent_collection_id = descendants.id
          )
          SELECT id
          FROM descendants
        `,
      )
      .all(collectionId) as { id: string }[];
    const descendantIds = descendantRows.map((row) => row.id);

    database
      .prepare(
        `
          UPDATE collections
          SET
            archived_at = strftime(
              '%Y-%m-%dT%H:%M:%fZ',
              'now'
            ),
            updated_at = strftime(
              '%Y-%m-%dT%H:%M:%fZ',
              'now'
            )
          WHERE id IN (${placeholders(descendantIds.length)})
        `,
      )
      .run(...descendantIds);

    if (
      selectedCollectionId() !== null &&
      descendantIds.includes(selectedCollectionId() as string)
    ) {
      clearSelectedCollection();
    }

    const archivedCollection = getCollectionById(collection.id);

    if (!archivedCollection) {
      throw new Error("The archived Collection could not be loaded.");
    }

    return {
      collection: archivedCollection,
      selectedCollectionId: selectedCollectionId(),
    };
  });

  return archiveTransaction();
}

export function restoreCollection(collectionId: string): Collection {
  const collection = requireCollectionRow(collectionId);

  if (!collection.archived_at) {
    throw new AppError(409, "Collection is not archived.");
  }

  if (collection.parent_collection_id) {
    const parent = requireCollectionRow(collection.parent_collection_id);

    if (parent.archived_at) {
      throw new AppError(
        409,
        "Restore the parent Collection before restoring this Collection.",
      );
    }
  }

  database
    .prepare(
      `
        UPDATE collections
        SET
          archived_at = NULL,
          updated_at = strftime(
            '%Y-%m-%dT%H:%M:%fZ',
            'now'
          )
        WHERE id = ?
      `,
    )
    .run(collectionId);

  const restoredCollection = getCollectionById(collectionId);

  if (!restoredCollection) {
    throw new Error("The restored Collection could not be loaded.");
  }

  return restoredCollection;
}

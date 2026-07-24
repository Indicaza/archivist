import { database } from "../../../database/database.js";
import { AppError } from "../../../errors/app-error.js";
import { getCollectionScope } from "../../collections/models/Collection.js";
import type { AppState } from "../types/AppStateTypes.js";

type AppStateRow = {
  selected_collection_id: string | null;
  selected_library_id: string | null;
  selected_chat_id: string | null;
};

type ExistsRow = {
  exists_flag: number;
};

export function getAppState(): AppState {
  const row = database
    .prepare(
      `
        SELECT
          selected_collection_id,
          selected_library_id,
          selected_chat_id
        FROM app_settings
        WHERE id = 1
      `,
    )
    .get() as AppStateRow | undefined;

  if (!row) {
    throw new Error("Archivist app settings could not be loaded.");
  }

  return {
    selectedCollectionId: row.selected_collection_id,
    selectedLibraryId: row.selected_library_id,
    selectedChatId: row.selected_chat_id,
  };
}

export function setSelectedCollection(
  selectedCollectionId: string | null,
): AppState {
  const current = getAppState();

  if (selectedCollectionId === null) {
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

    return getAppState();
  }

  const scope = getCollectionScope(selectedCollectionId);
  const selectedLibraryId =
    current.selectedLibraryId &&
    scope.libraryIds.includes(current.selectedLibraryId)
      ? current.selectedLibraryId
      : (scope.libraryIds[0] ?? null);
  const selectedChatId =
    current.selectedChatId && scope.chatIds.includes(current.selectedChatId)
      ? current.selectedChatId
      : (scope.directChatIds[0] ?? scope.chatIds[0] ?? null);

  database
    .prepare(
      `
        UPDATE app_settings
        SET
          selected_collection_id = ?,
          selected_library_id = ?,
          selected_chat_id = ?,
          updated_at = strftime(
            '%Y-%m-%dT%H:%M:%fZ',
            'now'
          )
        WHERE id = 1
      `,
    )
    .run(selectedCollectionId, selectedLibraryId, selectedChatId);

  return getAppState();
}

export function setSelectedLibrary(selectedLibraryId: string | null): AppState {
  if (selectedLibraryId !== null) {
    const row = database
      .prepare(
        `
          SELECT EXISTS(
            SELECT 1
            FROM libraries
            WHERE id = ?
          ) AS exists_flag
        `,
      )
      .get(selectedLibraryId) as ExistsRow;

    if (!row.exists_flag) {
      throw new AppError(404, "Library not found.");
    }
  }

  database
    .prepare(
      `
        UPDATE app_settings
        SET
          selected_library_id = ?,
          updated_at = strftime(
            '%Y-%m-%dT%H:%M:%fZ',
            'now'
          )
        WHERE id = 1
      `,
    )
    .run(selectedLibraryId);

  return getAppState();
}

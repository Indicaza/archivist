import { database } from "../../../database/database.js";
import { AppError } from "../../../errors/app-error.js";
import type { AppState } from "../types/AppStateTypes.js";

type AppStateRow = {
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
    selectedLibraryId: row.selected_library_id,
    selectedChatId: row.selected_chat_id,
  };
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

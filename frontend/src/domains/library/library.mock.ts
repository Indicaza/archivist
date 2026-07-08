// frontend/src/domains/library/library.mock.ts
import type { LibraryListItem } from "./library.types";

export const mockLibraries: LibraryListItem[] = [
  {
    id: "shard",
    name: "Shard Library",
    subtitle: "Worldbuilding vault",
    status: "active",
  },
  {
    id: "archivist",
    name: "Archivist",
    subtitle: "Design docs and app notes",
    status: "draft",
  },
];

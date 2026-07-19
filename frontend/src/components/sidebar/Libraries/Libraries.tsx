import { useEffect, useMemo, useState } from "react";
import { Archive, Pencil, Plus, Search, X } from "lucide-react";
import type { LibraryListItem } from "../../../domains/library/library.types";
import { LibraryFileTree } from "./LibraryFileTree/LibraryFileTree";
import styles from "./Libraries.module.css";

type LibrariesProps = {
  libraries: LibraryListItem[];
  archivedLibraries: LibraryListItem[];
  selectedLibraryId: string | null;
  loading: boolean;
  adding: boolean;
  onSelectLibrary: (libraryId: string) => void;
  onAddLibrary: () => void;
  onManageLibrary: (libraryId: string) => void;
  onManageArchivedLibrary: (libraryId: string) => void;
};

export function Libraries({
  libraries,
  archivedLibraries,
  selectedLibraryId,
  loading,
  adding,
  onSelectLibrary,
  onAddLibrary,
  onManageLibrary,
  onManageArchivedLibrary,
}: LibrariesProps) {
  const [searchOpen, setSearchOpen] = useState(false);
  const [search, setSearch] = useState("");
  const [debouncedSearch, setDebouncedSearch] = useState("");
  const [archivedOpen, setArchivedOpen] = useState(false);

  useEffect(() => {
    const timeoutId = window.setTimeout(() => {
      setDebouncedSearch(search.trim());
    }, 180);

    return () => {
      window.clearTimeout(timeoutId);
    };
  }, [search]);

  const selectedLibrary = useMemo(() => {
    return (
      libraries.find((library) => library.id === selectedLibraryId) ?? null
    );
  }, [libraries, selectedLibraryId]);

  const filteredArchivedLibraries = useMemo(() => {
    const query = debouncedSearch.toLowerCase();

    if (!query) {
      return archivedLibraries;
    }

    return archivedLibraries.filter((library) => {
      return `${library.name} ${library.rootPath} ${library.description ?? ""}`
        .toLowerCase()
        .includes(query);
    });
  }, [archivedLibraries, debouncedSearch]);

  function closeSearch() {
    setSearch("");
    setDebouncedSearch("");
    setSearchOpen(false);
  }

  return (
    <section className={styles.libraryExplorer}>
      <div className={styles.explorerToolbar}>
        <label className={styles.librarySelectWrap}>
          <select
            className={styles.librarySelect}
            value={selectedLibraryId ?? ""}
            onChange={(event) => onSelectLibrary(event.target.value)}
            disabled={loading || libraries.length === 0}
            aria-label="Active Library"
            title={selectedLibrary?.rootPath ?? "Choose a Library"}
          >
            {!selectedLibrary ? (
              <option value="">Choose a Library</option>
            ) : null}

            {libraries.map((library) => (
              <option key={library.id} value={library.id}>
                {library.name}
              </option>
            ))}
          </select>
        </label>

        <button
          className={`${styles.toolbarButton} ${
            searchOpen ? styles.toolbarButtonActive : ""
          }`}
          type="button"
          onClick={() => setSearchOpen((current) => !current)}
          aria-label="Search Library catalog"
          title="Search Library catalog"
        >
          <Search size={15} strokeWidth={2.1} />
        </button>

        <button
          className={styles.toolbarButton}
          type="button"
          onClick={onAddLibrary}
          disabled={adding}
          aria-label="Add Library"
          title={adding ? "Adding Library" : "Add Library"}
        >
          <Plus size={16} strokeWidth={2.2} />
        </button>

        <button
          className={styles.toolbarButton}
          type="button"
          onClick={() => selectedLibrary && onManageLibrary(selectedLibrary.id)}
          disabled={!selectedLibrary}
          aria-label="Manage active Library"
          title="Manage active Library"
        >
          <Pencil size={14} strokeWidth={2.1} />
        </button>

        <button
          className={`${styles.toolbarButton} ${
            archivedOpen ? styles.toolbarButtonActive : ""
          }`}
          type="button"
          onClick={() => setArchivedOpen((current) => !current)}
          aria-label="Archived Libraries"
          title="Archived Libraries"
        >
          <Archive size={14} strokeWidth={2.1} />

          {archivedLibraries.length > 0 ? (
            <span className={styles.toolbarBadge}>
              {archivedLibraries.length}
            </span>
          ) : null}
        </button>
      </div>

      {searchOpen ? (
        <div className={styles.searchBar}>
          <Search size={14} strokeWidth={2} />

          <input
            className={styles.searchInput}
            value={search}
            onChange={(event) => setSearch(event.target.value)}
            type="search"
            placeholder="Search file names and paths..."
            autoFocus
          />

          <button
            className={styles.searchCloseButton}
            type="button"
            onClick={closeSearch}
            aria-label="Close search"
            title="Close Search"
          >
            <X size={14} strokeWidth={2.1} />
          </button>
        </div>
      ) : null}

      <div className={styles.treeHost}>
        {loading ? (
          <div className={styles.emptyState}>Loading Libraries...</div>
        ) : selectedLibrary ? (
          <LibraryFileTree
            key={selectedLibrary.id}
            library={selectedLibrary}
            searchQuery={debouncedSearch}
          />
        ) : (
          <div className={styles.emptyState}>
            <span>No active Library selected.</span>
            <button type="button" onClick={onAddLibrary} disabled={adding}>
              {adding ? "Adding Library..." : "Add Library"}
            </button>
          </div>
        )}
      </div>

      {archivedOpen ? (
        <div className={styles.archivedDrawer}>
          <div className={styles.drawerHeader}>
            <span>Archived Libraries</span>
            <span>{filteredArchivedLibraries.length}</span>
          </div>

          {filteredArchivedLibraries.length > 0 ? (
            <div className={styles.archivedList}>
              {filteredArchivedLibraries.map((library) => (
                <button
                  key={library.id}
                  className={styles.archivedRow}
                  type="button"
                  onClick={() => onManageArchivedLibrary(library.id)}
                  title={library.rootPath}
                >
                  <Archive size={13} strokeWidth={2} />
                  <span>{library.name}</span>
                </button>
              ))}
            </div>
          ) : (
            <div className={styles.drawerEmpty}>No archived Libraries.</div>
          )}
        </div>
      ) : null}
    </section>
  );
}

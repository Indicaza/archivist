import { useEffect, useMemo, useState } from "react";
import {
  BookOpen,
  ChevronRight,
  FolderArchive,
  Library,
  Pencil,
  Plus,
} from "lucide-react";
import type { LibraryListItem } from "../../../domains/library/library.types";
import { SidebarButton } from "../SidebarButton/SidebarButton";
import { SidebarCard } from "../SidebarCard/SidebarCard";
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
  const [librariesOpen, setLibrariesOpen] = useState(true);
  const [allLibrariesOpen, setAllLibrariesOpen] = useState(true);
  const [archivedLibrariesOpen, setArchivedLibrariesOpen] = useState(false);

  const [search, setSearch] = useState("");
  const [debouncedSearch, setDebouncedSearch] = useState("");

  const [pressedLibraryId, setPressedLibraryId] = useState<string | null>(null);

  useEffect(() => {
    const timeoutId = window.setTimeout(() => {
      setDebouncedSearch(search.trim().toLowerCase());
    }, 200);

    return () => {
      window.clearTimeout(timeoutId);
    };
  }, [search]);

  const filteredLibraries = useMemo(() => {
    if (!debouncedSearch) {
      return libraries;
    }

    return libraries.filter((library) => {
      const haystack =
        `${library.name} ${library.subtitle} ${library.rootPath}`.toLowerCase();

      return haystack.includes(debouncedSearch);
    });
  }, [debouncedSearch, libraries]);

  const filteredArchivedLibraries = useMemo(() => {
    if (!debouncedSearch) {
      return archivedLibraries;
    }

    return archivedLibraries.filter((library) => {
      const haystack =
        `${library.name} ${library.subtitle} ${library.rootPath}`.toLowerCase();

      return haystack.includes(debouncedSearch);
    });
  }, [archivedLibraries, debouncedSearch]);

  function pressLibrary(
    libraryId: string,
    action: (libraryId: string) => void,
  ) {
    setPressedLibraryId(libraryId);
    action(libraryId);

    window.setTimeout(() => {
      setPressedLibraryId((current) =>
        current === libraryId ? null : current,
      );
    }, 280);
  }

  return (
    <section>
      <div
        className={styles.groupHeader}
        onClick={() => setLibrariesOpen((value) => !value)}
        role="button"
        tabIndex={0}
      >
        <ChevronRight
          size={16}
          strokeWidth={2.25}
          className={`${styles.caret} ${
            librariesOpen ? styles.caretOpen : styles.caretClosed
          }`}
        />

        <Library size={16} strokeWidth={2.1} />
        <span>Libraries</span>
      </div>

      <div
        className={`${styles.groupContent} ${
          librariesOpen ? styles.open : styles.closed
        }`}
      >
        <div className={styles.groupInner}>
          <div className={styles.actionsAreaTop}>
            <SidebarButton
              label={adding ? "Adding Library..." : "Add Library"}
              icon={<Plus size={16} strokeWidth={2.25} />}
              onClick={onAddLibrary}
            />
          </div>

          <div className={styles.searchWrap}>
            <input
              className={styles.searchInput}
              value={search}
              onChange={(event) => setSearch(event.target.value)}
              type="text"
              placeholder="Search libraries..."
              disabled={loading}
            />
          </div>

          <div
            className={`${styles.subHeader} ${styles.indent1}`}
            onClick={() => setAllLibrariesOpen((value) => !value)}
            role="button"
            tabIndex={0}
          >
            <ChevronRight
              size={16}
              strokeWidth={2.25}
              className={`${styles.caret} ${
                allLibrariesOpen ? styles.caretOpen : styles.caretClosed
              }`}
            />

            <BookOpen size={16} strokeWidth={2.1} />
            <span>All Libraries</span>
          </div>

          <div
            className={`${styles.subContent} ${styles.indent2} ${
              allLibrariesOpen ? styles.open : styles.closed
            }`}
          >
            {loading ? (
              <div className={styles.empty}>Loading Libraries...</div>
            ) : filteredLibraries.length ? (
              <ul className={styles.list}>
                {filteredLibraries.map((library) => {
                  const selected = library.id === selectedLibraryId;

                  const pressed = library.id === pressedLibraryId;

                  return (
                    <SidebarCard
                      key={library.id}
                      title={library.name}
                      subtitle={library.subtitle}
                      leading={
                        <span className={styles.libraryGlyph} aria-hidden>
                          {library.name.slice(0, 1)}
                        </span>
                      }
                      trailing={
                        <span
                          className={`${styles.statusDot} ${
                            styles[library.status]
                          }`}
                          title={selected ? "Selected" : "Available"}
                          aria-label={selected ? "Selected" : "Available"}
                        />
                      }
                      selected={selected}
                      pressed={pressed}
                      onClick={() => pressLibrary(library.id, onSelectLibrary)}
                      aria-current={selected ? "page" : undefined}
                      action={
                        <button
                          className={styles.manageButton}
                          type="button"
                          onClick={() => onManageLibrary(library.id)}
                          aria-label={`Manage ${library.name}`}
                          title="Manage Library"
                        >
                          <Pencil size={13} strokeWidth={2.2} />
                        </button>
                      }
                    />
                  );
                })}
              </ul>
            ) : (
              <div className={styles.empty}>
                {debouncedSearch
                  ? "No active Libraries match your search."
                  : "No active Libraries yet."}
              </div>
            )}
          </div>

          <div
            className={`${styles.subHeader} ${styles.indent1}`}
            onClick={() => setArchivedLibrariesOpen((value) => !value)}
            role="button"
            tabIndex={0}
          >
            <ChevronRight
              size={16}
              strokeWidth={2.25}
              className={`${styles.caret} ${
                archivedLibrariesOpen ? styles.caretOpen : styles.caretClosed
              }`}
            />

            <FolderArchive size={16} strokeWidth={2.1} />
            <span>Archived Libraries</span>

            {archivedLibraries.length ? (
              <span className={styles.archiveCount}>
                {archivedLibraries.length}
              </span>
            ) : null}
          </div>

          <div
            className={`${styles.subContent} ${styles.indent2} ${
              archivedLibrariesOpen ? styles.open : styles.closed
            }`}
          >
            {loading ? (
              <div className={styles.empty}>Loading archived Libraries...</div>
            ) : filteredArchivedLibraries.length ? (
              <ul className={styles.list}>
                {filteredArchivedLibraries.map((library) => {
                  const pressed = library.id === pressedLibraryId;

                  return (
                    <SidebarCard
                      key={library.id}
                      title={library.name}
                      subtitle={library.subtitle}
                      leading={
                        <span
                          className={`${styles.libraryGlyph} ${styles.archivedGlyph}`}
                          aria-hidden
                        >
                          {library.name.slice(0, 1)}
                        </span>
                      }
                      archived
                      pressed={pressed}
                      onClick={() =>
                        pressLibrary(library.id, onManageArchivedLibrary)
                      }
                      action={
                        <button
                          className={styles.manageButton}
                          type="button"
                          onClick={() => onManageArchivedLibrary(library.id)}
                          aria-label={`Manage archived Library ${library.name}`}
                          title="Manage Archived Library"
                        >
                          <Pencil size={13} strokeWidth={2.2} />
                        </button>
                      }
                    />
                  );
                })}
              </ul>
            ) : (
              <div className={styles.empty}>
                {debouncedSearch
                  ? "No archived Libraries match your search."
                  : "No archived Libraries."}
              </div>
            )}
          </div>
        </div>
      </div>
    </section>
  );
}

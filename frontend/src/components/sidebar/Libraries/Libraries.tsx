import { useEffect, useMemo, useState } from "react";
import { BookOpen, ChevronRight, Library, Plus } from "lucide-react";
import { SidebarButton } from "../SidebarButton/SidebarButton";
import type { LibraryListItem } from "../../../domains/library/library.types";
import styles from "./Libraries.module.css";

type LibrariesProps = {
  libraries: LibraryListItem[];
  selectedLibraryId: string | null;
  onSelectLibrary: (libraryId: string) => void;
  onAddLibrary: () => void;
};

export function Libraries({
  libraries,
  selectedLibraryId,
  onSelectLibrary,
  onAddLibrary,
}: LibrariesProps) {
  const [librariesOpen, setLibrariesOpen] = useState(true);
  const [allLibrariesOpen, setAllLibrariesOpen] = useState(true);
  const [search, setSearch] = useState("");
  const [debouncedSearch, setDebouncedSearch] = useState("");
  const [pressedLibraryId, setPressedLibraryId] = useState<string | null>(null);

  useEffect(() => {
    const timeoutId = window.setTimeout(() => {
      setDebouncedSearch(search.trim().toLowerCase());
    }, 200);

    return () => window.clearTimeout(timeoutId);
  }, [search]);

  const filteredLibraries = useMemo(() => {
    if (!debouncedSearch) return libraries;

    return libraries.filter((library) => {
      const haystack =
        `${library.name} ${library.subtitle} ${library.status}`.toLowerCase();

      return haystack.includes(debouncedSearch);
    });
  }, [debouncedSearch, libraries]);

  function selectLibrary(libraryId: string) {
    setPressedLibraryId(libraryId);
    onSelectLibrary(libraryId);

    window.setTimeout(() => {
      setPressedLibraryId((current) =>
        current === libraryId ? null : current,
      );
    }, 360);
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
              label="Add Library"
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
            {filteredLibraries.length ? (
              <ul className={styles.list}>
                {filteredLibraries.map((library) => {
                  const selected = library.id === selectedLibraryId;
                  const pressed = library.id === pressedLibraryId;

                  return (
                    <li key={library.id} className={styles.item}>
                      <button
                        className={`${styles.row} ${
                          selected ? styles.rowActive : ""
                        } ${pressed ? styles.rowPressed : ""}`}
                        type="button"
                        onClick={() => selectLibrary(library.id)}
                        aria-current={selected ? "page" : undefined}
                      >
                        <span className={styles.libraryGlyph} aria-hidden>
                          {library.name.slice(0, 1)}
                        </span>

                        <span className={styles.meta}>
                          <span className={styles.name}>{library.name}</span>
                          <span className={styles.sub}>{library.subtitle}</span>
                        </span>

                        <span
                          className={`${styles.statusDot} ${
                            styles[library.status]
                          }`}
                          title={library.status}
                          aria-label={library.status}
                        />
                      </button>
                    </li>
                  );
                })}
              </ul>
            ) : (
              <div className={styles.empty}>
                No Libraries match your search.
              </div>
            )}
          </div>
        </div>
      </div>
    </section>
  );
}

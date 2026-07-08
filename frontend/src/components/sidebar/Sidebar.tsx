// frontend/src/components/sidebar/Sidebar.tsx
import { useEffect, useState } from "react";
import styles from "./Sidebar.module.css";
import { Logo } from "../logo/Logo";
import { Chats } from "./Chats/Chats";
import { Libraries } from "./Libraries/Libraries";
import type { LibraryListItem } from "../../domains/library/library.types";
import { ChevronLeft, ChevronRight, UserRoundCog, Wrench } from "lucide-react";

type SidebarProps = {
  collapsed?: boolean;
  libraries: LibraryListItem[];
  selectedLibraryId: string | null;
  onSelectLibrary: (libraryId: string) => void;
  onToggle: () => void;
};

const SIDEBAR_PANEL_WIDTH = 300;
const SIDEBAR_COLLAPSED_WIDTH = 0;

export function Sidebar({
  collapsed = false,
  libraries,
  selectedLibraryId,
  onSelectLibrary,
  onToggle,
}: SidebarProps) {
  const [profilesOpen, setProfilesOpen] = useState(false);
  const [toolsOpen, setToolsOpen] = useState(false);

  useEffect(() => {
    const root = document.documentElement;

    root.style.setProperty(
      "--sidebar-width",
      `${collapsed ? SIDEBAR_COLLAPSED_WIDTH : SIDEBAR_PANEL_WIDTH}px`,
    );

    root.style.setProperty("--sidebar-panel-width", `${SIDEBAR_PANEL_WIDTH}px`);
  }, [collapsed]);

  return (
    <>
      {collapsed ? (
        <button
          className={styles.revealBtn}
          onClick={onToggle}
          aria-label="Open sidebar"
          title="Open sidebar"
          type="button"
        >
          <ChevronRight size={20} strokeWidth={2.25} />
        </button>
      ) : null}

      <aside
        className={`${styles.sidebarShell} ${
          collapsed ? styles.collapsed : ""
        }`}
        aria-hidden={collapsed}
      >
        <div className={styles.sidebarPanel}>
          <div className={styles.topRow}>
            <button
              className={styles.collapseBtn}
              onClick={onToggle}
              aria-label="Collapse sidebar"
              title="Collapse sidebar"
              type="button"
            >
              <ChevronLeft size={20} strokeWidth={2.25} />
            </button>

            <div className={styles.brand}>
              <Logo size="sm" />
              <span className={styles.title}>Archivist</span>
            </div>
          </div>

          <div className={styles.scrollArea}>
            <Libraries
              libraries={libraries}
              selectedLibraryId={selectedLibraryId}
              onSelectLibrary={onSelectLibrary}
              onAddLibrary={() => console.log("Add Library")}
            />

            <Chats />

            <div
              className={styles.groupHeader}
              onClick={() => setProfilesOpen((value) => !value)}
              role="button"
              tabIndex={0}
            >
              <ChevronRight
                size={16}
                strokeWidth={2.25}
                className={`${styles.caret} ${
                  profilesOpen ? styles.caretOpen : styles.caretClosed
                }`}
              />
              <UserRoundCog size={16} strokeWidth={2.1} />
              <span>Profiles</span>
            </div>

            <div
              className={`${styles.groupContent} ${
                profilesOpen ? styles.open : styles.closed
              }`}
            >
              <div className={styles.groupInner}>
                <div className={styles.empty}>
                  AI profiles will live here: Archivist, Continuity Keeper,
                  Worldbuilder.
                </div>
              </div>
            </div>

            <div
              className={styles.groupHeader}
              onClick={() => setToolsOpen((value) => !value)}
              role="button"
              tabIndex={0}
            >
              <ChevronRight
                size={16}
                strokeWidth={2.25}
                className={`${styles.caret} ${
                  toolsOpen ? styles.caretOpen : styles.caretClosed
                }`}
              />
              <Wrench size={16} strokeWidth={2.1} />
              <span>Tools</span>
            </div>

            <div
              className={`${styles.groupContent} ${
                toolsOpen ? styles.open : styles.closed
              }`}
            >
              <div className={styles.groupInner}>
                <div className={styles.empty}>
                  Scanner, proposals, codex export, and routines later.
                </div>
              </div>
            </div>
          </div>
        </div>
      </aside>
    </>
  );
}

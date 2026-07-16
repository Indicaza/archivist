import { useEffect, useState } from "react";
import { ChevronLeft, ChevronRight, UserRoundCog, Wrench } from "lucide-react";
import type { Chat } from "../../domains/chat/chat.types";
import type { LibraryListItem } from "../../domains/library/library.types";
import { Chats } from "./Chats/Chats";
import { Libraries } from "./Libraries/Libraries";
import styles from "./Sidebar.module.css";

type SidebarProps = {
  collapsed?: boolean;

  libraries: LibraryListItem[];
  archivedLibraries: LibraryListItem[];
  selectedLibraryId: string | null;
  loadingLibraries: boolean;
  addingLibrary: boolean;
  restoringLibraryId: string | null;
  onSelectLibrary: (libraryId: string) => void;
  onAddLibrary: () => void;
  onManageLibrary: (libraryId: string) => void;
  onRestoreLibrary: (libraryId: string) => void;

  chats: Chat[];
  archivedChats: Chat[];
  selectedChatId: string | null;
  loadingChats: boolean;
  addingChat: boolean;
  onAddChat: () => void;
  onSelectChat: (chatId: string) => void;
  onManageChat: (chatId: string) => void;
  onManageArchivedChat: (chatId: string) => void;

  onToggle: () => void;
};

const SIDEBAR_PANEL_WIDTH = 300;
const SIDEBAR_COLLAPSED_WIDTH = 0;

export function Sidebar({
  collapsed = false,

  libraries,
  archivedLibraries,
  selectedLibraryId,
  loadingLibraries,
  addingLibrary,
  restoringLibraryId,
  onSelectLibrary,
  onAddLibrary,
  onManageLibrary,
  onRestoreLibrary,

  chats,
  archivedChats,
  selectedChatId,
  loadingChats,
  addingChat,
  onAddChat,
  onSelectChat,
  onManageChat,
  onManageArchivedChat,

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
          </div>

          <div className={styles.scrollArea}>
            <Libraries
              libraries={libraries}
              archivedLibraries={archivedLibraries}
              selectedLibraryId={selectedLibraryId}
              loading={loadingLibraries}
              adding={addingLibrary}
              restoringLibraryId={restoringLibraryId}
              onSelectLibrary={onSelectLibrary}
              onAddLibrary={onAddLibrary}
              onManageLibrary={onManageLibrary}
              onRestoreLibrary={onRestoreLibrary}
            />

            <Chats
              chats={chats}
              archivedChats={archivedChats}
              selectedChatId={selectedChatId}
              loading={loadingChats}
              adding={addingChat}
              onAddChat={onAddChat}
              onSelectChat={onSelectChat}
              onManageChat={onManageChat}
              onManageArchivedChat={onManageArchivedChat}
            />

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

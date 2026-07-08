import { useEffect, useMemo, useState } from "react";
import styles from "./Sidebar.module.css";
import { Logo } from "../logo/Logo";
import { SidebarButton } from "./SidebarButton/SidebarButton";
import {
  BookOpen,
  ChevronLeft,
  ChevronRight,
  Library,
  MessageSquareText,
  Plus,
  Sparkles,
  UserRoundCog,
  Wrench,
} from "lucide-react";

type SidebarProps = {
  collapsed?: boolean;
  onToggle: () => void;
};

type LibraryListItem = {
  id: string;
  name: string;
  subtitle: string;
  status: "active" | "draft" | "offline";
};

type ChatListItem = {
  id: string;
  title: string;
  subtitle: string;
};

const SIDEBAR_PANEL_WIDTH = 300;
const SIDEBAR_COLLAPSED_WIDTH = 0;

const mockLibraries: LibraryListItem[] = [
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

const mockChats: ChatListItem[] = [
  {
    id: "main",
    title: "Main Timeline",
    subtitle: "Persistent Library chat",
  },
  {
    id: "lore-riffage",
    title: "Lore Riffage",
    subtitle: "Worldbuilding forge",
  },
  {
    id: "codex",
    title: "Morning Codex",
    subtitle: "Reading artifacts",
  },
];

export function Sidebar({ collapsed = false, onToggle }: SidebarProps) {
  const [librariesOpen, setLibrariesOpen] = useState(true);
  const [allLibrariesOpen, setAllLibrariesOpen] = useState(true);

  const [chatsOpen, setChatsOpen] = useState(true);
  const [allChatsOpen, setAllChatsOpen] = useState(true);

  const [profilesOpen, setProfilesOpen] = useState(false);
  const [toolsOpen, setToolsOpen] = useState(false);

  const [search, setSearch] = useState("");
  const [debouncedSearch, setDebouncedSearch] = useState("");

  useEffect(() => {
    const timeoutId = window.setTimeout(() => {
      setDebouncedSearch(search.trim().toLowerCase());
    }, 200);

    return () => window.clearTimeout(timeoutId);
  }, [search]);

  const filteredLibraries = useMemo(() => {
    if (!debouncedSearch) return mockLibraries;

    return mockLibraries.filter((library) => {
      const haystack =
        `${library.name} ${library.subtitle} ${library.status}`.toLowerCase();

      return haystack.includes(debouncedSearch);
    });
  }, [debouncedSearch]);

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
                    onClick={() => console.log("Add Library")}
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
                      {filteredLibraries.map((library) => (
                        <li
                          key={library.id}
                          className={styles.row}
                          onClick={() =>
                            console.log("Open library", library.id)
                          }
                        >
                          <div
                            className={styles.libraryGlyph}
                            aria-hidden="true"
                          >
                            {library.name.slice(0, 1)}
                          </div>

                          <div className={styles.meta}>
                            <div className={styles.name}>{library.name}</div>
                            <div className={styles.sub}>{library.subtitle}</div>
                          </div>

                          <span
                            className={`${styles.statusDot} ${
                              styles[library.status]
                            }`}
                            title={library.status}
                            aria-label={library.status}
                          />
                        </li>
                      ))}
                    </ul>
                  ) : (
                    <div className={styles.empty}>
                      No Libraries match your search.
                    </div>
                  )}
                </div>
              </div>
            </div>

            <div
              className={styles.groupHeader}
              onClick={() => setChatsOpen((value) => !value)}
              role="button"
              tabIndex={0}
            >
              <ChevronRight
                size={16}
                strokeWidth={2.25}
                className={`${styles.caret} ${
                  chatsOpen ? styles.caretOpen : styles.caretClosed
                }`}
              />
              <MessageSquareText size={16} strokeWidth={2.1} />
              <span>Chats</span>
            </div>

            <div
              className={`${styles.groupContent} ${
                chatsOpen ? styles.open : styles.closed
              }`}
            >
              <div className={styles.groupInner}>
                <div
                  className={`${styles.subHeader} ${styles.indent1}`}
                  onClick={() => setAllChatsOpen((value) => !value)}
                  role="button"
                  tabIndex={0}
                >
                  <ChevronRight
                    size={16}
                    strokeWidth={2.25}
                    className={`${styles.caret} ${
                      allChatsOpen ? styles.caretOpen : styles.caretClosed
                    }`}
                  />
                  <MessageSquareText size={16} strokeWidth={2.1} />
                  <span>All Chats</span>
                </div>

                <div
                  className={`${styles.subContent} ${styles.indent2} ${
                    allChatsOpen ? styles.open : styles.closed
                  }`}
                >
                  {mockChats.length ? (
                    <ul className={styles.list}>
                      {mockChats.map((chat) => (
                        <li
                          key={chat.id}
                          className={styles.row}
                          onClick={() => console.log("Open chat", chat.id)}
                        >
                          <div className={styles.chatGlyph} aria-hidden="true">
                            <Sparkles size={18} strokeWidth={2.15} />
                          </div>

                          <div className={styles.meta}>
                            <div className={styles.name}>{chat.title}</div>
                            <div className={styles.sub}>{chat.subtitle}</div>
                          </div>
                        </li>
                      ))}
                    </ul>
                  ) : (
                    <div className={styles.empty}>No chats yet.</div>
                  )}
                </div>
              </div>
            </div>

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

import {
  Bot,
  FolderOpen,
  Maximize2,
  MessageSquareText,
  Minimize2,
  Pencil,
} from "lucide-react";
import type { Agent } from "../../../../domains/agent/agent.types";
import type { Chat } from "../../../../domains/chat/chat.types";
import { useWorkbenchLayout } from "../../WorkbenchShell/WorkbenchLayoutContext";
import styles from "./ChatDockHeader.module.css";

export type ChatDockView = "chats" | "agents" | null;

type ChatDockHeaderProps = {
  selectedChat: Chat | null;
  activeAgent: Agent | null;
  selectedLibraryName: string | null;
  activeView: ChatDockView;
  onToggleView: (view: Exclude<ChatDockView, null>) => void;
  onManageChat: (chatId: string) => void;
};

export function ChatDockHeader({
  selectedChat,
  activeAgent,
  selectedLibraryName,
  activeView,
  onToggleView,
  onManageChat,
}: ChatDockHeaderProps) {
  const { dockMode, effectiveDockMode, leftOpen, toggleDockMode } =
    useWorkbenchLayout();

  const dockModeTitle =
    dockMode === "attached" && leftOpen
      ? "Center Chat Dock"
      : "Attach Chat Dock to Explorer";

  return (
    <div className={styles.header}>
      <div className={styles.identity} aria-label="Active Chat context">
        <strong
          className={styles.chatTitle}
          title={selectedChat?.title ?? "No active Chat"}
        >
          {selectedChat?.title ?? "Select a Chat"}
        </strong>

        <div className={styles.contextMeta}>
          <span
            className={styles.contextItem}
            title={selectedLibraryName ?? "Standalone workspace"}
          >
            <FolderOpen size={11} strokeWidth={2} />

            <span className={styles.contextLabel}>Library</span>

            <span className={styles.contextValue}>
              {selectedLibraryName ?? "Standalone"}
            </span>
          </span>

          <span className={styles.contextDivider} aria-hidden />

          <span
            className={styles.contextItem}
            title={activeAgent?.name ?? "No active Agent"}
          >
            <Bot size={11} strokeWidth={2} />

            <span className={styles.contextLabel}>Agent</span>

            <span className={styles.contextValue}>
              {activeAgent?.name ?? "None"}
            </span>
          </span>
        </div>
      </div>

      <nav className={styles.actions} aria-label="Chat dock controls">
        <button
          className={styles.iconButton}
          type="button"
          onClick={() => selectedChat && onManageChat(selectedChat.id)}
          disabled={!selectedChat}
          aria-label="Manage active Chat"
          title="Manage Chat"
        >
          <Pencil size={13} strokeWidth={2.1} />
        </button>

        <button
          className={styles.iconButton}
          type="button"
          onClick={toggleDockMode}
          aria-label={dockModeTitle}
          title={dockModeTitle}
        >
          {effectiveDockMode === "attached" ? (
            <Minimize2 size={13} strokeWidth={2.1} />
          ) : (
            <Maximize2 size={13} strokeWidth={2.1} />
          )}
        </button>

        <button
          className={`${styles.tab} ${
            activeView === "chats" ? styles.activeTab : ""
          }`}
          type="button"
          onClick={() => onToggleView("chats")}
          aria-expanded={activeView === "chats"}
          title="Browse Chats"
        >
          <MessageSquareText size={13} strokeWidth={2.1} />
          <span>Chats</span>
        </button>

        <button
          className={`${styles.tab} ${
            activeView === "agents" ? styles.activeTab : ""
          }`}
          type="button"
          onClick={() => onToggleView("agents")}
          aria-expanded={activeView === "agents"}
          title="Browse Agents"
        >
          <Bot size={13} strokeWidth={2.1} />
          <span>Agents</span>
        </button>
      </nav>
    </div>
  );
}

import { Bot, FolderOpen, MessageSquareText } from "lucide-react";
import type { Agent } from "../../domains/agent/agent.types";
import type { Chat } from "../../domains/chat/chat.types";
import styles from "./ChatPanelToolbar.module.css";

export type ChatDockView = "chats" | "agents" | null;

type ChatPanelToolbarProps = {
  selectedChat: Chat | null;
  activeAgent: Agent | null;
  selectedLibraryName: string | null;
  activeView: ChatDockView;
  onToggleView: (view: Exclude<ChatDockView, null>) => void;
};

export function ChatPanelToolbar({
  selectedChat,
  activeAgent,
  selectedLibraryName,
  activeView,
  onToggleView,
}: ChatPanelToolbarProps) {
  return (
    <div className={styles.toolbar}>
      <div className={styles.activeContext} aria-label="Active chat context">
        <div className={styles.contextCopy}>
          <strong title={selectedChat?.title ?? "No active Chat"}>
            {selectedChat?.title ?? "Select a Chat"}
          </strong>

          <span className={styles.contextMeta}>
            <span title={activeAgent?.name ?? "No active Agent"}>
              <Bot size={10} strokeWidth={2} />
              {activeAgent?.name ?? "No Agent"}
            </span>

            <i aria-hidden>·</i>

            <span title={selectedLibraryName ?? "Standalone workspace"}>
              <FolderOpen size={10} strokeWidth={2} />
              {selectedLibraryName ?? "Standalone"}
            </span>
          </span>
        </div>
      </div>

      <nav className={styles.panelTabs} aria-label="Chat dock browsers">
        <button
          className={`${styles.panelTab} ${
            activeView === "chats" ? styles.panelTabActive : ""
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
          className={`${styles.panelTab} ${
            activeView === "agents" ? styles.panelTabActive : ""
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

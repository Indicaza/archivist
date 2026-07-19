import { Bot, MessageSquareText, Pencil, Plus } from "lucide-react";
import type { Agent } from "../../domains/agent/agent.types";
import type { Chat } from "../../domains/chat/chat.types";
import styles from "./ChatPanelToolbar.module.css";

type ChatPanelToolbarProps = {
  chats: Chat[];
  selectedChat: Chat | null;
  activeAgent: Agent | null;
  addingChat: boolean;
  onSelectChat: (chatId: string) => void;
  onAddChat: () => void;
  onManageChat: (chatId: string) => void;
  onManageAgent: (agentId: string) => void;
};

export function ChatPanelToolbar({
  chats,
  selectedChat,
  activeAgent,
  addingChat,
  onSelectChat,
  onAddChat,
  onManageChat,
  onManageAgent,
}: ChatPanelToolbarProps) {
  return (
    <div className={styles.toolbar}>
      <label className={styles.selectWrap}>
        <MessageSquareText size={13} strokeWidth={2.1} />

        <select
          className={styles.select}
          value={selectedChat?.id ?? ""}
          onChange={(event) => onSelectChat(event.target.value)}
          aria-label="Active chat"
        >
          {!selectedChat ? <option value="">Choose a chat</option> : null}

          {chats.map((chat) => (
            <option key={chat.id} value={chat.id}>
              {chat.title}
            </option>
          ))}
        </select>
      </label>

      <button
        className={styles.iconButton}
        type="button"
        onClick={onAddChat}
        disabled={addingChat}
        aria-label="Create chat"
        title="Create Chat"
      >
        <Plus size={15} strokeWidth={2.2} />
      </button>

      <button
        className={styles.iconButton}
        type="button"
        onClick={() => selectedChat && onManageChat(selectedChat.id)}
        disabled={!selectedChat}
        aria-label="Manage active chat"
        title="Manage Chat"
      >
        <Pencil size={14} strokeWidth={2.1} />
      </button>

      <span className={styles.divider} />

      <button
        className={styles.agentButton}
        type="button"
        onClick={() => activeAgent && onManageAgent(activeAgent.id)}
        disabled={!activeAgent}
        title={activeAgent ? `Manage ${activeAgent.name}` : "No active Agent"}
      >
        <Bot size={14} strokeWidth={2.1} />
        <span>{activeAgent?.name ?? "No Agent"}</span>
      </button>
    </div>
  );
}

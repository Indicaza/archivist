import { useState } from "react";
import {
  Archive,
  ChevronRight,
  MessageSquareText,
  Pencil,
  Plus,
} from "lucide-react";
import type { Chat } from "../../../domains/chat/chat.types";
import styles from "./Chats.module.css";

type ChatsProps = {
  chats: Chat[];
  archivedChats: Chat[];
  selectedChatId: string | null;
  loading: boolean;
  adding: boolean;
  onAddChat: () => void;
  onSelectChat: (chatId: string) => void;
  onManageChat: (chatId: string) => void;
  onManageArchivedChat: (chatId: string) => void;
};

export function Chats({
  chats,
  archivedChats,
  selectedChatId,
  loading,
  adding,
  onAddChat,
  onSelectChat,
  onManageChat,
  onManageArchivedChat,
}: ChatsProps) {
  const [archivedOpen, setArchivedOpen] = useState(false);

  return (
    <section className={styles.section}>
      <div className={styles.toolbar}>
        <button
          className={styles.createButton}
          type="button"
          onClick={onAddChat}
          disabled={adding}
        >
          <Plus size={14} strokeWidth={2.2} />
          <span>{adding ? "Creating..." : "New Chat"}</span>
        </button>
      </div>

      <div className={styles.listViewport}>
        {loading ? (
          <div className={styles.empty}>Loading chats...</div>
        ) : chats.length > 0 ? (
          <ul className={styles.list}>
            {chats.map((chat) => {
              const selected = chat.id === selectedChatId;

              return (
                <li key={chat.id} className={styles.rowWrap}>
                  <button
                    className={`${styles.row} ${selected ? styles.selected : ""}`}
                    type="button"
                    onClick={() => onSelectChat(chat.id)}
                    title={chat.title}
                  >
                    <MessageSquareText size={13} strokeWidth={2} />
                    <span>{chat.title}</span>
                  </button>

                  <button
                    className={styles.manageButton}
                    type="button"
                    onClick={() => onManageChat(chat.id)}
                    aria-label={`Manage ${chat.title}`}
                    title="Manage Chat"
                  >
                    <Pencil size={12} strokeWidth={2.1} />
                  </button>
                </li>
              );
            })}
          </ul>
        ) : (
          <div className={styles.empty}>No chats yet.</div>
        )}
      </div>

      <button
        className={styles.archiveToggle}
        type="button"
        onClick={() => setArchivedOpen((current) => !current)}
      >
        <ChevronRight
          size={12}
          strokeWidth={2.2}
          className={archivedOpen ? styles.caretOpen : ""}
        />
        <Archive size={12} strokeWidth={2} />
        <span>Archived</span>
        <span className={styles.count}>{archivedChats.length}</span>
      </button>

      {archivedOpen ? (
        <div className={styles.archivedList}>
          {archivedChats.length > 0 ? (
            archivedChats.map((chat) => (
              <button
                key={chat.id}
                className={styles.archivedRow}
                type="button"
                onClick={() => onManageArchivedChat(chat.id)}
                title={chat.title}
              >
                <Archive size={12} strokeWidth={2} />
                <span>{chat.title}</span>
              </button>
            ))
          ) : (
            <div className={styles.archivedEmpty}>No archived chats.</div>
          )}
        </div>
      ) : null}
    </section>
  );
}

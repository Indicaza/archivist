import { useState } from "react";
import {
  ChevronRight,
  FolderArchive,
  MessageSquareText,
  Pencil,
  Plus,
  Sparkles,
} from "lucide-react";
import type { Chat } from "../../../domains/chat/chat.types";
import { SidebarButton } from "../SidebarButton/SidebarButton";
import { SidebarCard } from "../SidebarCard/SidebarCard";
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

function formatChatSubtitle(updatedAt: string): string {
  const updatedDate = new Date(updatedAt);

  if (Number.isNaN(updatedDate.getTime())) {
    return "Persistent conversation";
  }

  return `Updated ${updatedDate.toLocaleString([], {
    month: "short",
    day: "numeric",
    hour: "numeric",
    minute: "2-digit",
  })}`;
}

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
  const [chatsOpen, setChatsOpen] = useState(true);
  const [allChatsOpen, setAllChatsOpen] = useState(true);
  const [archivedChatsOpen, setArchivedChatsOpen] = useState(false);

  const [pressedChatId, setPressedChatId] = useState<string | null>(null);

  function pressChat(chatId: string, action: (chatId: string) => void) {
    setPressedChatId(chatId);
    action(chatId);

    window.setTimeout(() => {
      setPressedChatId((current) => (current === chatId ? null : current));
    }, 280);
  }

  return (
    <section>
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
          <div className={styles.actionsAreaTop}>
            <SidebarButton
              label={adding ? "Creating Chat..." : "New Chat"}
              icon={<Plus size={16} strokeWidth={2.25} />}
              onClick={onAddChat}
            />
          </div>

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
            {loading ? (
              <div className={styles.empty}>Loading chats...</div>
            ) : chats.length ? (
              <ul className={styles.list}>
                {chats.map((chat) => {
                  const selected = chat.id === selectedChatId;

                  const pressed = chat.id === pressedChatId;

                  return (
                    <SidebarCard
                      key={chat.id}
                      title={chat.title}
                      subtitle={formatChatSubtitle(chat.updatedAt)}
                      leading={
                        <span className={styles.chatGlyph} aria-hidden>
                          <Sparkles size={18} strokeWidth={2.15} />
                        </span>
                      }
                      selected={selected}
                      pressed={pressed}
                      onClick={() => pressChat(chat.id, onSelectChat)}
                      aria-current={selected ? "page" : undefined}
                      action={
                        <button
                          className={styles.manageButton}
                          type="button"
                          onClick={() => onManageChat(chat.id)}
                          aria-label={`Manage ${chat.title}`}
                          title="Manage Chat"
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
                No chats yet. Create one to begin.
              </div>
            )}
          </div>

          <div
            className={`${styles.subHeader} ${styles.indent1}`}
            onClick={() => setArchivedChatsOpen((value) => !value)}
            role="button"
            tabIndex={0}
          >
            <ChevronRight
              size={16}
              strokeWidth={2.25}
              className={`${styles.caret} ${
                archivedChatsOpen ? styles.caretOpen : styles.caretClosed
              }`}
            />

            <FolderArchive size={16} strokeWidth={2.1} />
            <span>Archived Chats</span>

            {archivedChats.length ? (
              <span className={styles.archiveCount}>
                {archivedChats.length}
              </span>
            ) : null}
          </div>

          <div
            className={`${styles.subContent} ${styles.indent2} ${
              archivedChatsOpen ? styles.open : styles.closed
            }`}
          >
            {loading ? (
              <div className={styles.empty}>Loading archived chats...</div>
            ) : archivedChats.length ? (
              <ul className={styles.list}>
                {archivedChats.map((chat) => {
                  const pressed = chat.id === pressedChatId;

                  return (
                    <SidebarCard
                      key={chat.id}
                      title={chat.title}
                      subtitle="Archived conversation"
                      leading={
                        <span
                          className={`${styles.chatGlyph} ${styles.archivedGlyph}`}
                          aria-hidden
                        >
                          <Sparkles size={18} strokeWidth={2.15} />
                        </span>
                      }
                      archived
                      pressed={pressed}
                      onClick={() => pressChat(chat.id, onManageArchivedChat)}
                      action={
                        <button
                          className={styles.manageButton}
                          type="button"
                          onClick={() => onManageArchivedChat(chat.id)}
                          aria-label={`Manage archived chat ${chat.title}`}
                          title="Manage Archived Chat"
                        >
                          <Pencil size={13} strokeWidth={2.2} />
                        </button>
                      }
                    />
                  );
                })}
              </ul>
            ) : (
              <div className={styles.empty}>No archived chats.</div>
            )}
          </div>
        </div>
      </div>
    </section>
  );
}

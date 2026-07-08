// frontend/src/components/sidebar/Chats/Chats.tsx
import { useState } from "react";
import { ChevronRight, MessageSquareText, Sparkles } from "lucide-react";
import styles from "./Chats.module.css";

type ChatListItem = {
  id: string;
  title: string;
  subtitle: string;
};

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

export function Chats() {
  const [chatsOpen, setChatsOpen] = useState(true);
  const [allChatsOpen, setAllChatsOpen] = useState(true);

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
                  <li key={chat.id}>
                    <button
                      className={styles.row}
                      type="button"
                      onClick={() => console.log("Open chat", chat.id)}
                    >
                      <span className={styles.chatGlyph} aria-hidden>
                        <Sparkles size={18} strokeWidth={2.15} />
                      </span>

                      <span className={styles.meta}>
                        <span className={styles.name}>{chat.title}</span>
                        <span className={styles.sub}>{chat.subtitle}</span>
                      </span>
                    </button>
                  </li>
                ))}
              </ul>
            ) : (
              <div className={styles.empty}>No chats yet.</div>
            )}
          </div>
        </div>
      </div>
    </section>
  );
}

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
  const [pressedChatId, setPressedChatId] = useState<string | null>(null);

  function selectChat(chatId: string) {
    setPressedChatId(chatId);
    console.log("Open chat", chatId);

    window.setTimeout(() => {
      setPressedChatId((current) => (current === chatId ? null : current));
    }, 360);
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
                {mockChats.map((chat) => {
                  const pressed = chat.id === pressedChatId;

                  return (
                    <li key={chat.id} className={styles.item}>
                      <button
                        className={`${styles.row} ${
                          pressed ? styles.rowPressed : ""
                        }`}
                        type="button"
                        onClick={() => selectChat(chat.id)}
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
                  );
                })}
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

// frontend/src/components/chat/ChatWindow.tsx
import {
  type FormEvent,
  type KeyboardEvent,
  type RefObject,
  useEffect,
  useMemo,
  useRef,
  useState,
} from "react";
import { SendHorizontal, Sparkles } from "lucide-react";
import type { LibraryListItem } from "../../domains/library/library.types";
import styles from "./ChatWindow.module.css";

type ChatRole = "user" | "assistant" | "system";

type MessageBlock =
  | {
      id: string;
      type: "markdown";
      content: string;
    }
  | {
      id: string;
      type: "contextPacket";
      title: string;
      lines?: string[];
      dynamic?: "activeLibrary";
    };

type ChatMessage = {
  id: string;
  role: ChatRole;
  blocks: MessageBlock[];
  createdAt: string;
};

type ChatWindowProps = {
  scrollContainerRef: RefObject<HTMLElement | null>;
  selectedLibrary: LibraryListItem | null;
};

const starterMessages: ChatMessage[] = [
  {
    id: "system-welcome",
    role: "system",
    createdAt: new Date().toISOString(),
    blocks: [
      {
        id: "system-welcome-block",
        type: "markdown",
        content:
          "Archivist shell online. This is still a mock chat, but the artifact stream is waking up.",
      },
    ],
  },
  {
    id: "assistant-intro",
    role: "assistant",
    createdAt: new Date().toISOString(),
    blocks: [
      {
        id: "assistant-intro-block",
        type: "markdown",
        content:
          "Choose a Library when you are ready. Soon I will be able to scan folders, read the wiki, show sources, and propose careful changes.",
      },
      {
        id: "assistant-context-block",
        type: "contextPacket",
        title: "Current Context",
        dynamic: "activeLibrary",
      },
    ],
  },
];

function isNearBottom(container: HTMLElement, threshold = 120): boolean {
  const distanceFromBottom =
    container.scrollHeight - container.scrollTop - container.clientHeight;

  return distanceFromBottom <= threshold;
}

function scrollToBottom(
  container: HTMLElement,
  behavior: ScrollBehavior = "smooth",
): void {
  container.scrollTo({
    top: container.scrollHeight,
    behavior,
  });
}

export function ChatWindow({
  scrollContainerRef,
  selectedLibrary,
}: ChatWindowProps) {
  const [messages, setMessages] = useState<ChatMessage[]>(starterMessages);
  const [input, setInput] = useState("");
  const [grown, setGrown] = useState(false);
  const [sending, setSending] = useState(false);
  const [pinnedToBottom, setPinnedToBottom] = useState(true);

  const textareaRef = useRef<HTMLTextAreaElement | null>(null);
  const forceScrollRef = useRef(false);

  const hasMessages = messages.length > 0;

  const activeLibraryLines = useMemo(() => {
    return [
      `Library: ${selectedLibrary ? selectedLibrary.name : "none selected"}`,
      `Status: ${selectedLibrary ? selectedLibrary.status : "idle"}`,
      "Profile: Archivist",
      "Mode: Main Timeline",
      "Provider: mock",
    ];
  }, [selectedLibrary]);

  const canSend = useMemo(() => {
    return input.trim().length > 0 && !sending && Boolean(selectedLibrary);
  }, [input, sending, selectedLibrary]);

  function resizeTextarea() {
    const textarea = textareaRef.current;
    if (!textarea) return;

    textarea.style.height = "auto";
    textarea.style.height = `${textarea.scrollHeight}px`;
    setGrown(textarea.scrollHeight > 48);
  }

  async function send() {
    const text = input.trim();
    if (!text || sending || !selectedLibrary) return;

    const userMessage: ChatMessage = {
      id: crypto.randomUUID(),
      role: "user",
      createdAt: new Date().toISOString(),
      blocks: [
        {
          id: crypto.randomUUID(),
          type: "markdown",
          content: text,
        },
      ],
    };

    forceScrollRef.current = true;
    setPinnedToBottom(true);
    setMessages((current) => [...current, userMessage]);
    setInput("");
    setSending(true);

    window.setTimeout(() => {
      const assistantMessage: ChatMessage = {
        id: crypto.randomUUID(),
        role: "assistant",
        createdAt: new Date().toISOString(),
        blocks: [
          {
            id: crypto.randomUUID(),
            type: "markdown",
            content: `Mock response received inside ${selectedLibrary.name}. Later this will route through the Context Compiler, AI Harness, source cards, proposals, and tool calls.`,
          },
          {
            id: crypto.randomUUID(),
            type: "contextPacket",
            title: "Context Used",
            lines: activeLibraryLines,
          },
        ],
      };

      setMessages((current) => [...current, assistantMessage]);
      setSending(false);
    }, 450);
  }

  function onSubmit(event: FormEvent<HTMLFormElement>) {
    event.preventDefault();
    void send();
  }

  function onKeyDown(event: KeyboardEvent<HTMLTextAreaElement>) {
    if (event.key === "Enter" && !event.shiftKey) {
      event.preventDefault();
      void send();
    }
  }

  function getContextLines(block: MessageBlock): string[] {
    if (block.type !== "contextPacket") return [];

    if (block.dynamic === "activeLibrary") {
      return activeLibraryLines;
    }

    return block.lines ?? [];
  }

  useEffect(() => {
    const container = scrollContainerRef.current;
    if (!container) return;

    function onScroll() {
      if (!container) return;
      setPinnedToBottom(isNearBottom(container));
    }

    container.addEventListener("scroll", onScroll, { passive: true });
    onScroll();

    return () => container.removeEventListener("scroll", onScroll);
  }, [scrollContainerRef]);

  useEffect(() => {
    resizeTextarea();
  }, [input]);

  useEffect(() => {
    const container = scrollContainerRef.current;
    if (!container) return;

    window.requestAnimationFrame(() => {
      if (forceScrollRef.current) {
        scrollToBottom(container, "smooth");
        forceScrollRef.current = false;
        return;
      }

      if (pinnedToBottom) {
        scrollToBottom(container, "smooth");
      }
    });
  }, [messages.length, sending, pinnedToBottom, scrollContainerRef]);

  return (
    <section className={styles.chatWindow}>
      <div className={styles.chatContent}>
        <div className={styles.messages} aria-live="polite">
          {!hasMessages ? (
            <div className={styles.emptyState}>
              <Sparkles size={24} strokeWidth={2} />
              <span>Select or create a Library to begin.</span>
            </div>
          ) : (
            messages.map((message) => (
              <article
                key={message.id}
                className={`${styles.message} ${styles[message.role]}`}
              >
                <div className={styles.messageMeta}>
                  <span className={styles.roleLabel}>
                    {message.role === "assistant"
                      ? "Archivist"
                      : message.role === "user"
                        ? "You"
                        : "System"}
                  </span>
                </div>

                <div className={styles.blocks}>
                  {message.blocks.map((block) => {
                    if (block.type === "contextPacket") {
                      return (
                        <div key={block.id} className={styles.contextPacket}>
                          <div className={styles.contextTitle}>
                            {block.title}
                          </div>
                          <ul className={styles.contextList}>
                            {getContextLines(block).map((line) => (
                              <li key={line}>{line}</li>
                            ))}
                          </ul>
                        </div>
                      );
                    }

                    return (
                      <div key={block.id} className={styles.markdownBlock}>
                        {block.content}
                      </div>
                    );
                  })}
                </div>
              </article>
            ))
          )}

          {sending ? (
            <article className={`${styles.message} ${styles.assistant}`}>
              <div className={styles.messageMeta}>
                <span className={styles.roleLabel}>Archivist</span>
              </div>
              <div className={styles.blocks}>
                <div className={styles.typing}>Thinking...</div>
              </div>
            </article>
          ) : null}
        </div>

        <form className={styles.composer} onSubmit={onSubmit}>
          <div
            className={`${styles.composerInner} ${grown ? styles.grown : ""}`}
          >
            <textarea
              ref={textareaRef}
              className={styles.composerInput}
              value={input}
              onChange={(event) => setInput(event.target.value)}
              onKeyDown={onKeyDown}
              placeholder={
                selectedLibrary
                  ? `Message ${selectedLibrary.name}...`
                  : "Choose a Library..."
              }
              autoComplete="off"
              rows={1}
              disabled={sending || !selectedLibrary}
            />

            <button
              className={styles.composerButton}
              type="submit"
              disabled={!canSend}
              aria-label="Send"
              title="Send"
            >
              <SendHorizontal size={20} strokeWidth={2.35} />
            </button>
          </div>
        </form>
      </div>
    </section>
  );
}

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
import { addMessage, fetchMessages } from "../../domains/chat/chat.api";
import type { Chat, ChatMessage } from "../../domains/chat/chat.types";
import styles from "./ChatWindow.module.css";

type ChatWindowProps = {
  scrollContainerRef: RefObject<HTMLElement | null>;
  selectedChat: Chat | null;
  onChatActivity: (chatId: string) => void;
};

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
  selectedChat,
  onChatActivity,
}: ChatWindowProps) {
  const [messages, setMessages] = useState<ChatMessage[]>([]);
  const [input, setInput] = useState("");
  const [grown, setGrown] = useState(false);
  const [loadingMessages, setLoadingMessages] = useState(false);
  const [sending, setSending] = useState(false);
  const [pinnedToBottom, setPinnedToBottom] = useState(true);

  const textareaRef = useRef<HTMLTextAreaElement | null>(null);
  const forceScrollRef = useRef(false);

  const hasMessages = messages.length > 0;

  const canSend = useMemo(() => {
    return (
      input.trim().length > 0 &&
      !sending &&
      !loadingMessages &&
      Boolean(selectedChat)
    );
  }, [input, loadingMessages, selectedChat, sending]);

  function resizeTextarea() {
    const textarea = textareaRef.current;

    if (!textarea) {
      return;
    }

    textarea.style.height = "auto";
    textarea.style.height = `${textarea.scrollHeight}px`;
    setGrown(textarea.scrollHeight > 48);
  }

  async function send() {
    const text = input.trim();

    if (!text || sending || !selectedChat) {
      return;
    }

    setSending(true);
    setInput("");
    forceScrollRef.current = true;
    setPinnedToBottom(true);

    try {
      const message = await addMessage(selectedChat.id, {
        role: "user",
        content: text,
      });

      setMessages((current) => [...current, message]);
      onChatActivity(selectedChat.id);
    } catch (error) {
      setInput(text);

      window.alert(
        error instanceof Error
          ? error.message
          : "Archivist could not save the message.",
      );
    } finally {
      setSending(false);
    }
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

  useEffect(() => {
    let cancelled = false;

    async function loadMessages() {
      if (!selectedChat) {
        setMessages([]);
        setLoadingMessages(false);
        return;
      }

      setLoadingMessages(true);
      setMessages([]);

      try {
        const loadedMessages = await fetchMessages(selectedChat.id);

        if (cancelled) {
          return;
        }

        setMessages(loadedMessages);
        forceScrollRef.current = true;
        setPinnedToBottom(true);
      } catch (error) {
        if (cancelled) {
          return;
        }

        window.alert(
          error instanceof Error
            ? error.message
            : "Archivist could not load the conversation.",
        );
      } finally {
        if (!cancelled) {
          setLoadingMessages(false);
        }
      }
    }

    void loadMessages();

    return () => {
      cancelled = true;
    };
  }, [selectedChat]);

  useEffect(() => {
    const currentContainer = scrollContainerRef.current;

    if (!currentContainer) {
      return;
    }

    const container: HTMLElement = currentContainer;

    function handleScroll() {
      setPinnedToBottom(isNearBottom(container));
    }

    container.addEventListener("scroll", handleScroll, {
      passive: true,
    });

    handleScroll();

    return () => {
      container.removeEventListener("scroll", handleScroll);
    };
  }, [scrollContainerRef]);

  useEffect(() => {
    resizeTextarea();
  }, [input]);

  useEffect(() => {
    const currentContainer = scrollContainerRef.current;

    if (!currentContainer) {
      return;
    }

    const container: HTMLElement = currentContainer;

    const animationFrameId = window.requestAnimationFrame(() => {
      if (forceScrollRef.current) {
        scrollToBottom(container, "smooth");
        forceScrollRef.current = false;
        return;
      }

      if (pinnedToBottom) {
        scrollToBottom(container, "smooth");
      }
    });

    return () => {
      window.cancelAnimationFrame(animationFrameId);
    };
  }, [loadingMessages, messages, pinnedToBottom, scrollContainerRef, sending]);

  return (
    <section className={styles.chatWindow}>
      <div className={styles.chatContent}>
        <div className={styles.messages} aria-live="polite">
          {!selectedChat ? (
            <div className={styles.emptyState}>
              <Sparkles size={24} strokeWidth={2} />
              <span>Select or create a chat to begin.</span>
            </div>
          ) : loadingMessages ? (
            <div className={styles.emptyState}>
              <Sparkles size={24} strokeWidth={2} />
              <span>Loading {selectedChat.title}...</span>
            </div>
          ) : !hasMessages ? (
            <div className={styles.emptyState}>
              <Sparkles size={24} strokeWidth={2} />
              <span>
                {selectedChat.title} is empty. Send the first message.
              </span>
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
                  <div className={styles.markdownBlock}>{message.content}</div>
                </div>
              </article>
            ))
          )}

          {sending ? (
            <article className={`${styles.message} ${styles.user}`}>
              <div className={styles.messageMeta}>
                <span className={styles.roleLabel}>You</span>
              </div>

              <div className={styles.blocks}>
                <div className={styles.typing}>Saving message...</div>
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
                selectedChat
                  ? `Message ${selectedChat.title}...`
                  : "Choose or create a chat..."
              }
              autoComplete="off"
              rows={1}
              disabled={sending || loadingMessages || !selectedChat}
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

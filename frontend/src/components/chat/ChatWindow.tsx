import {
  type CSSProperties,
  type FormEvent,
  type KeyboardEvent,
  type ReactNode,
  useEffect,
  useMemo,
  useRef,
  useState,
} from "react";
import {
  Code2,
  List,
  Plus,
  SendHorizontal,
  Sparkles,
  TextQuote,
} from "lucide-react";
import { fetchMessages, respondToChat } from "../../domains/chat/chat.api";
import type { Chat, ChatMessage } from "../../domains/chat/chat.types";
import styles from "./ChatWindow.module.css";

type ChatWindowProps = {
  selectedChat: Chat | null;
  toolbar: ReactNode;
  controlPanel: ReactNode | null;
  controlPanelLabel: string | null;
  controlPanelWidth?: number;
  controlPanelActionLabel?: string | null;
  controlPanelActionBusy?: boolean;
  onControlPanelAction?: (() => void) | null;
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

function createOptimisticUserMessage(
  chatId: string,
  content: string,
): ChatMessage {
  const now = new Date().toISOString();

  return {
    id: `optimistic-${crypto.randomUUID()}`,
    chatId,
    role: "user",
    content,
    status: "streaming",
    createdAt: now,
    updatedAt: now,
  };
}

export function ChatWindow({
  selectedChat,
  toolbar,
  controlPanel,
  controlPanelLabel,
  controlPanelWidth = 176,
  controlPanelActionLabel = null,
  controlPanelActionBusy = false,
  onControlPanelAction = null,
  onChatActivity,
}: ChatWindowProps) {
  const [messages, setMessages] = useState<ChatMessage[]>([]);
  const [input, setInput] = useState("");
  const [loadingMessages, setLoadingMessages] = useState(false);
  const [sending, setSending] = useState(false);
  const [pinnedToBottom, setPinnedToBottom] = useState(true);

  const textareaRef = useRef<HTMLTextAreaElement | null>(null);
  const messagesContainerRef = useRef<HTMLDivElement | null>(null);
  const forceScrollRef = useRef(false);
  const activeRequestChatIdRef = useRef<string | null>(null);

  const selectedChatId = selectedChat?.id ?? null;
  const selectedChatTitle = selectedChat?.title ?? null;
  const hasMessages = messages.length > 0;

  const canSend = useMemo(() => {
    return (
      input.trim().length > 0 &&
      !sending &&
      !loadingMessages &&
      Boolean(selectedChatId)
    );
  }, [input, loadingMessages, selectedChatId, sending]);

  async function send() {
    const text = input.trim();
    const chat = selectedChat;

    if (!text || sending || !chat) {
      return;
    }

    const optimisticMessage = createOptimisticUserMessage(chat.id, text);

    activeRequestChatIdRef.current = chat.id;

    setSending(true);
    setInput("");
    setMessages((current) => [...current, optimisticMessage]);

    forceScrollRef.current = true;
    setPinnedToBottom(true);

    try {
      const result = await respondToChat(chat.id, text);

      if (activeRequestChatIdRef.current !== chat.id) {
        return;
      }

      setMessages((current) => [
        ...current.filter((message) => message.id !== optimisticMessage.id),
        result.userMessage,
        result.assistantMessage,
      ]);

      onChatActivity(chat.id);
    } catch (error) {
      if (activeRequestChatIdRef.current === chat.id) {
        setMessages((current) =>
          current.filter((message) => message.id !== optimisticMessage.id),
        );

        setInput(text);

        window.alert(
          error instanceof Error
            ? error.message
            : "Archivist could not complete the response.",
        );
      }
    } finally {
      if (activeRequestChatIdRef.current === chat.id) {
        activeRequestChatIdRef.current = null;
        setSending(false);
      }
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

  function insertTemplate(prefix: string, suffix = "", fallback = "") {
    const textarea = textareaRef.current;
    const selectionStart = textarea?.selectionStart ?? input.length;
    const selectionEnd = textarea?.selectionEnd ?? input.length;
    const selectedText = input.slice(selectionStart, selectionEnd) || fallback;
    const replacement = `${prefix}${selectedText}${suffix}`;
    const nextInput = `${input.slice(0, selectionStart)}${replacement}${input.slice(
      selectionEnd,
    )}`;
    const nextCursor = selectionStart + prefix.length + selectedText.length;

    setInput(nextInput);

    window.requestAnimationFrame(() => {
      textarea?.focus();
      textarea?.setSelectionRange(nextCursor, nextCursor);
    });
  }

  useEffect(() => {
    let cancelled = false;

    activeRequestChatIdRef.current = null;
    setSending(false);

    async function loadMessages() {
      if (!selectedChatId) {
        setMessages([]);
        setLoadingMessages(false);
        return;
      }

      setLoadingMessages(true);
      setMessages([]);

      try {
        const loadedMessages = await fetchMessages(selectedChatId);

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
  }, [selectedChatId]);

  useEffect(() => {
    const container = messagesContainerRef.current;

    if (!container) {
      return;
    }

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
  }, []);

  useEffect(() => {
    const container = messagesContainerRef.current;

    if (!container) {
      return;
    }

    const animationFrameId = window.requestAnimationFrame(() => {
      if (forceScrollRef.current) {
        scrollToBottom(container, "auto");
        forceScrollRef.current = false;
        return;
      }

      if (pinnedToBottom) {
        scrollToBottom(container, "auto");
      }
    });

    return () => {
      window.cancelAnimationFrame(animationFrameId);
    };
  }, [loadingMessages, messages, pinnedToBottom, sending]);

  return (
    <>
      <section className={styles.chatWindow}>
        <div
          ref={messagesContainerRef}
          className={styles.messagesViewport}
          aria-live="polite"
        >
          <div className={styles.messages}>
            {!selectedChatId ? (
              <div className={styles.emptyState}>
                <Sparkles size={22} strokeWidth={2} />
                <span>Select or create a chat to begin.</span>
              </div>
            ) : loadingMessages ? (
              <div className={styles.emptyState}>
                <Sparkles size={22} strokeWidth={2} />
                <span>Loading {selectedChatTitle}...</span>
              </div>
            ) : !hasMessages && !sending ? (
              <div className={styles.emptyState}>
                <Sparkles size={22} strokeWidth={2} />
                <span>
                  {selectedChatTitle} is empty. Send the first message.
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
                    <div className={styles.markdownBlock}>
                      {message.content}
                    </div>
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
        </div>
      </section>

      <section className={styles.composerPanel} aria-label="Chat input panel">
        <header className={styles.composerHeader}>
          <div className={styles.composerToolbar}>{toolbar}</div>
        </header>

        <div
          className={`${styles.composerBody} ${
            controlPanel ? styles.composerBodyWithPanel : ""
          }`}
          style={
            controlPanel
              ? ({
                  "--chat-control-panel-width": `${controlPanelWidth}px`,
                } as CSSProperties)
              : undefined
          }
        >
          <form className={styles.composer} onSubmit={onSubmit}>
            <div className={styles.composerInner}>
              <textarea
                ref={textareaRef}
                className={styles.composerInput}
                value={input}
                onChange={(event) => setInput(event.target.value)}
                onKeyDown={onKeyDown}
                placeholder={
                  selectedChatTitle
                    ? `Message ${selectedChatTitle}...`
                    : "Choose or create a chat..."
                }
                autoComplete="off"
                rows={4}
                disabled={sending || loadingMessages || !selectedChatId}
              />

              <div className={styles.composerFooter}>
                <div className={styles.writingTools} aria-label="Writing tools">
                  <button
                    type="button"
                    onClick={() => insertTemplate("- ")}
                    disabled={!selectedChatId || sending}
                    aria-label="Insert list item"
                    title="Insert List Item"
                  >
                    <List size={14} strokeWidth={2} />
                  </button>

                  <button
                    type="button"
                    onClick={() => insertTemplate("> ")}
                    disabled={!selectedChatId || sending}
                    aria-label="Insert quote"
                    title="Insert Quote"
                  >
                    <TextQuote size={14} strokeWidth={2} />
                  </button>

                  <button
                    type="button"
                    onClick={() => insertTemplate("```\n", "\n```", "code")}
                    disabled={!selectedChatId || sending}
                    aria-label="Insert code block"
                    title="Insert Code Block"
                  >
                    <Code2 size={14} strokeWidth={2} />
                  </button>
                </div>

                <span className={styles.keyboardHint}>
                  Enter to send · Shift+Enter for newline
                </span>

                <button
                  className={styles.composerButton}
                  type="submit"
                  disabled={!canSend}
                  aria-label="Send"
                  title="Send"
                >
                  <SendHorizontal size={16} strokeWidth={2.35} />
                  <span>Send</span>
                </button>
              </div>
            </div>
          </form>

          {controlPanel ? (
            <aside
              className={styles.controlPanel}
              aria-label={controlPanelLabel ?? "Chat controls"}
            >
              <div className={styles.controlPanelHeader}>
                <strong>{controlPanelLabel ?? "Chat controls"}</strong>

                {onControlPanelAction && controlPanelActionLabel ? (
                  <button
                    className={styles.controlPanelAction}
                    type="button"
                    onClick={onControlPanelAction}
                    disabled={controlPanelActionBusy}
                    aria-label={controlPanelActionLabel}
                    title={controlPanelActionLabel}
                  >
                    <Plus size={12} strokeWidth={2.2} />
                  </button>
                ) : null}
              </div>

              <div className={styles.controlPanelContent}>{controlPanel}</div>
            </aside>
          ) : null}
        </div>
      </section>
    </>
  );
}

import {
  Check,
  Copy,
  MoreHorizontal,
  Pencil,
  RotateCcw,
  ThumbsDown,
  ThumbsUp,
} from "lucide-react";
import { useEffect, useRef, useState } from "react";
import type { MessageRole } from "../../../../../domains/chat/chat.types";
import styles from "./ChatMessageActions.module.css";

type ChatMessageActionsProps = {
  role: MessageRole;
  content: string;
};

type Rating = "up" | "down" | null;

async function copyText(content: string): Promise<void> {
  if (navigator.clipboard?.writeText) {
    await navigator.clipboard.writeText(content);
    return;
  }

  const textarea = document.createElement("textarea");

  textarea.value = content;
  textarea.style.position = "fixed";
  textarea.style.opacity = "0";

  document.body.appendChild(textarea);
  textarea.select();
  document.execCommand("copy");
  textarea.remove();
}

function emitPlaceholderAction(action: string): void {
  window.dispatchEvent(
    new CustomEvent("archivist:chat-message-action", {
      detail: {
        action,
      },
    }),
  );
}

export function ChatMessageActions({ role, content }: ChatMessageActionsProps) {
  const [copied, setCopied] = useState(false);
  const [rating, setRating] = useState<Rating>(null);
  const copiedTimerRef = useRef<number | null>(null);

  useEffect(() => {
    return () => {
      if (copiedTimerRef.current !== null) {
        window.clearTimeout(copiedTimerRef.current);
      }
    };
  }, []);

  async function handleCopy() {
    try {
      await copyText(content);

      setCopied(true);

      if (copiedTimerRef.current !== null) {
        window.clearTimeout(copiedTimerRef.current);
      }

      copiedTimerRef.current = window.setTimeout(() => {
        setCopied(false);
      }, 1600);
    } catch {
      setCopied(false);
    }
  }

  return (
    <div className={styles.actions} aria-label="Message actions">
      <button
        className={styles.actionButton}
        type="button"
        onClick={() => void handleCopy()}
        aria-label={copied ? "Copied message" : "Copy message"}
        title={copied ? "Copied" : "Copy"}
      >
        {copied ? (
          <Check size={13} strokeWidth={2.2} />
        ) : (
          <Copy size={13} strokeWidth={2} />
        )}

        <span className={styles.actionLabel}>{copied ? "Copied" : "Copy"}</span>
      </button>

      {role === "assistant" ? (
        <>
          <button
            className={`${styles.actionButton} ${
              rating === "up" ? styles.active : ""
            }`}
            type="button"
            onClick={() =>
              setRating((current) => (current === "up" ? null : "up"))
            }
            aria-pressed={rating === "up"}
            aria-label="Rate response positively"
            title="Good response"
          >
            <ThumbsUp size={13} strokeWidth={2} />
          </button>

          <button
            className={`${styles.actionButton} ${
              rating === "down" ? styles.active : ""
            }`}
            type="button"
            onClick={() =>
              setRating((current) => (current === "down" ? null : "down"))
            }
            aria-pressed={rating === "down"}
            aria-label="Rate response negatively"
            title="Poor response"
          >
            <ThumbsDown size={13} strokeWidth={2} />
          </button>

          <button
            className={styles.actionButton}
            type="button"
            onClick={() => emitPlaceholderAction("retry")}
            aria-label="Retry response"
            title="Retry response · Coming soon"
          >
            <RotateCcw size={13} strokeWidth={2} />
          </button>
        </>
      ) : null}

      {role === "user" ? (
        <button
          className={styles.actionButton}
          type="button"
          onClick={() => emitPlaceholderAction("edit")}
          aria-label="Edit message"
          title="Edit message · Coming soon"
        >
          <Pencil size={13} strokeWidth={2} />
        </button>
      ) : null}

      <button
        className={styles.actionButton}
        type="button"
        onClick={() => emitPlaceholderAction("more")}
        aria-label="More message actions"
        title="More actions · Coming soon"
      >
        <MoreHorizontal size={14} strokeWidth={2} />
      </button>
    </div>
  );
}

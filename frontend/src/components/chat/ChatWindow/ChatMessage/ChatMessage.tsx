import { Bot, CircleAlert, Sparkles, UserRound } from "lucide-react";
import type { ReactNode } from "react";
import type { ChatMessage as ChatMessageType } from "../../../../domains/chat/chat.types";
import { ChatMessageActions } from "./ChatMessageActions/ChatMessageActions";
import { MessageContent } from "./MessageContent/MessageContent";
import styles from "./ChatMessage.module.css";

type ChatMessageProps = {
  message: ChatMessageType;
  thinking?: boolean;
};

type RolePresentation = {
  label: string;
  icon: ReactNode;
};

function getRolePresentation(role: ChatMessageType["role"]): RolePresentation {
  if (role === "assistant") {
    return {
      label: "Archivist",
      icon: <Sparkles size={13} strokeWidth={2} />,
    };
  }

  if (role === "user") {
    return {
      label: "You",
      icon: <UserRound size={13} strokeWidth={2} />,
    };
  }

  return {
    label: "System",
    icon: <CircleAlert size={13} strokeWidth={2} />,
  };
}

function formatMessageTime(value: string): {
  short: string;
  full: string;
} {
  const date = new Date(value);

  if (Number.isNaN(date.getTime())) {
    return {
      short: "",
      full: value,
    };
  }

  return {
    short: date.toLocaleTimeString([], {
      hour: "numeric",
      minute: "2-digit",
    }),
    full: date.toLocaleString(),
  };
}

function getStatusLabel(
  message: ChatMessageType,
  thinking: boolean,
): string | null {
  if (thinking) {
    return "Thinking";
  }

  if (message.status === "complete") {
    return null;
  }

  if (message.status === "streaming") {
    return message.role === "user" ? "Sending" : "Streaming";
  }

  if (message.status === "failed") {
    return "Failed";
  }

  return "Cancelled";
}

export function ChatMessage({ message, thinking = false }: ChatMessageProps) {
  const presentation = getRolePresentation(message.role);
  const time = formatMessageTime(message.createdAt);
  const statusLabel = getStatusLabel(message, thinking);

  return (
    <article
      className={`${styles.message} ${styles[message.role]}`}
      data-message-role={message.role}
    >
      <div className={styles.frame}>
        <header className={styles.header}>
          <span className={styles.avatar} aria-hidden>
            {presentation.icon}
          </span>

          <div className={styles.identity}>
            <strong>{presentation.label}</strong>

            {time.short ? (
              <time dateTime={message.createdAt} title={time.full}>
                {time.short}
              </time>
            ) : null}
          </div>

          {statusLabel ? (
            <span
              className={`${styles.status} ${
                message.status === "failed" ? styles.failed : ""
              }`}
            >
              {statusLabel}
            </span>
          ) : null}
        </header>

        <div className={styles.surface}>
          {thinking ? (
            <div className={styles.thinking}>
              <Bot size={15} strokeWidth={1.9} />

              <span>Archivist is thinking</span>

              <span className={styles.thinkingDots} aria-hidden>
                <i />
                <i />
                <i />
              </span>
            </div>
          ) : (
            <MessageContent content={message.content} />
          )}
        </div>

        {!thinking ? (
          <footer className={styles.footer}>
            <ChatMessageActions role={message.role} content={message.content} />
          </footer>
        ) : null}
      </div>
    </article>
  );
}

import { LoaderCircle, MessageSquarePlus, Sparkles } from "lucide-react";
import styles from "./ChatEmptyState.module.css";

type ChatEmptyStateMode = "unselected" | "loading" | "empty";

type ChatEmptyStateProps = {
  mode: ChatEmptyStateMode;
  chatTitle?: string | null;
};

export function ChatEmptyState({
  mode,
  chatTitle = null,
}: ChatEmptyStateProps) {
  const loading = mode === "loading";

  const title =
    mode === "unselected"
      ? "Choose your workspace conversation"
      : loading
        ? `Opening ${chatTitle ?? "conversation"}`
        : `${chatTitle ?? "This Chat"} is ready`;

  const description =
    mode === "unselected"
      ? "Select an existing Chat or create a new one from the command deck below."
      : loading
        ? "Restoring persistent history and preparing the active context."
        : "Send the first message. Archivist will preserve the conversation from here.";

  return (
    <div className={styles.state}>
      <div className={styles.card}>
        <div
          className={`${styles.icon} ${loading ? styles.loadingIcon : ""}`}
          aria-hidden
        >
          {loading ? (
            <LoaderCircle size={23} strokeWidth={1.9} />
          ) : mode === "unselected" ? (
            <MessageSquarePlus size={23} strokeWidth={1.9} />
          ) : (
            <Sparkles size={23} strokeWidth={1.9} />
          )}
        </div>

        <span className={styles.eyebrow}>Archivist Workspace</span>

        <strong>{title}</strong>

        <p>{description}</p>

        <div className={styles.decorativeLine} aria-hidden>
          <span />
          <i />
          <span />
        </div>
      </div>
    </div>
  );
}

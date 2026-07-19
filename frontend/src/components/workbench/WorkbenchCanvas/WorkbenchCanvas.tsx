import { Bot, FolderOpen, MessageSquareText, Sparkles } from "lucide-react";
import type { Agent } from "../../../domains/agent/agent.types";
import type { Chat } from "../../../domains/chat/chat.types";
import type { LibraryListItem } from "../../../domains/library/library.types";
import styles from "./WorkbenchCanvas.module.css";

type WorkbenchCanvasProps = {
  selectedLibrary: LibraryListItem | null;
  selectedChat: Chat | null;
  activeAgent: Agent | null;
};

export function WorkbenchCanvas({
  selectedLibrary,
  selectedChat,
  activeAgent,
}: WorkbenchCanvasProps) {
  return (
    <section className={styles.canvas}>
      <header className={styles.header}>
        <div className={styles.breadcrumbs}>
          <span>Archivist</span>
          <span className={styles.separator}>/</span>
          <strong>{selectedLibrary?.name ?? "No Library"}</strong>
        </div>

        <div className={styles.statuses}>
          <span className={styles.statusChip}>
            <MessageSquareText size={12} strokeWidth={2} />
            {selectedChat?.title ?? "No Chat"}
          </span>

          <span className={styles.statusChip}>
            <Bot size={12} strokeWidth={2} />
            {activeAgent?.name ?? "No Agent"}
          </span>
        </div>
      </header>

      <div className={styles.stage}>
        <div className={styles.emptyState}>
          <span className={styles.iconWrap}>
            {selectedLibrary ? (
              <FolderOpen size={28} strokeWidth={1.75} />
            ) : (
              <Sparkles size={28} strokeWidth={1.75} />
            )}
          </span>

          <div className={styles.copy}>
            <h2>{selectedLibrary ? selectedLibrary.name : "Workbench"}</h2>
            <p>
              {selectedLibrary
                ? "Select a file from the Library Explorer. File previews, diffs, search results, and tools will open here."
                : "Choose or create a Library to begin building your workspace."}
            </p>
          </div>

          {selectedLibrary ? (
            <div className={styles.path}>{selectedLibrary.rootPath}</div>
          ) : null}
        </div>
      </div>
    </section>
  );
}

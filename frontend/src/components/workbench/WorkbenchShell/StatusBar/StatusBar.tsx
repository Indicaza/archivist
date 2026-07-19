import { Bot, Database, HardDrive, MessageSquareText } from "lucide-react";
import { useWorkbenchLayout } from "../WorkbenchLayoutContext";
import styles from "./StatusBar.module.css";

type StatusBarProps = {
  libraryName: string | null;
  chatTitle: string | null;
  agentName: string | null;
};

export function StatusBar({
  libraryName,
  chatTitle,
  agentName,
}: StatusBarProps) {
  const { effectiveDockMode, leftOpen } = useWorkbenchLayout();

  return (
    <footer className={styles.statusBar} aria-label="Workspace status">
      <div className={styles.group}>
        <span className={styles.readyDot} aria-hidden />
        <span>Ready</span>
        <span className={styles.localStatus}>
          <HardDrive size={10} strokeWidth={2} />
          Local
        </span>
      </div>

      <div className={`${styles.group} ${styles.contextGroup}`}>
        <span title={libraryName ?? "No Library"}>
          <Database size={10} strokeWidth={2} />
          {libraryName ?? "No Library"}
        </span>

        <span title={chatTitle ?? "No Chat"}>
          <MessageSquareText size={10} strokeWidth={2} />
          {chatTitle ?? "No Chat"}
        </span>

        <span title={agentName ?? "No Agent"}>
          <Bot size={10} strokeWidth={2} />
          {agentName ?? "No Agent"}
        </span>

        <span>{leftOpen ? "Explorer open" : "Explorer hidden"}</span>
        <span>
          {effectiveDockMode === "attached" ? "Dock attached" : "Dock centered"}
        </span>
      </div>
    </footer>
  );
}

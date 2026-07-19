import { PanelLeftClose } from "lucide-react";
import type { WorkbenchDockView } from "../WorkbenchShell";
import styles from "./ExplorerDock.module.css";

type ExplorerDockProps = {
  views: WorkbenchDockView[];
  activeViewId: string;
  onClose: () => void;
};

export function ExplorerDock({
  views,
  activeViewId,
  onClose,
}: ExplorerDockProps) {
  const activeView =
    views.find((view) => view.id === activeViewId) ?? views[0] ?? null;

  if (!activeView) {
    return null;
  }

  return (
    <aside
      className={styles.dock}
      aria-label="Library workspace sidebar"
      data-workbench-explorer
    >
      <header className={styles.header}>
        <span className={styles.title}>{activeView.title}</span>

        <button
          className={styles.closeButton}
          type="button"
          onClick={onClose}
          aria-label={`Collapse ${activeView.title}`}
          title={`Collapse ${activeView.title}`}
        >
          <PanelLeftClose size={15} strokeWidth={2} />
        </button>
      </header>

      <div className={styles.content}>{activeView.content}</div>
    </aside>
  );
}

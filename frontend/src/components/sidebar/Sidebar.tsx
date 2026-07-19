import type { ReactNode } from "react";
import { PanelLeftClose } from "lucide-react";
import styles from "./Sidebar.module.css";

export type SidebarView = {
  id: string;
  title: string;
  icon: ReactNode;
  content: ReactNode;
};

type SidebarProps = {
  views: SidebarView[];
  activeViewId: string;
  onActivateView: (viewId: string) => void;
  onClosePanel: () => void;
};

export function Sidebar({
  views,
  activeViewId,
  onActivateView,
  onClosePanel,
}: SidebarProps) {
  const activeView =
    views.find((view) => view.id === activeViewId) ?? views[0] ?? null;

  if (!activeView) {
    return null;
  }

  return (
    <aside className={styles.sidebar} aria-label="Library workspace sidebar">
      <header className={styles.header}>
        <span className={styles.title}>{activeView.title}</span>

        <div className={styles.headerActions}>
          <nav className={styles.viewSwitches} aria-label="Explorer views">
            {views.map((view) => {
              const active = view.id === activeView.id;

              return (
                <button
                  key={view.id}
                  className={`${styles.headerButton} ${
                    active ? styles.headerButtonActive : ""
                  }`}
                  type="button"
                  onClick={() => onActivateView(view.id)}
                  aria-label={view.title}
                  aria-pressed={active}
                  title={view.title}
                >
                  {view.icon}
                </button>
              );
            })}
          </nav>

          <button
            className={styles.headerButton}
            type="button"
            onClick={onClosePanel}
            aria-label={`Collapse ${activeView.title}`}
            title={`Collapse ${activeView.title}`}
          >
            <PanelLeftClose size={15} strokeWidth={2} />
          </button>
        </div>
      </header>

      <div className={styles.scrollArea}>{activeView.content}</div>
    </aside>
  );
}

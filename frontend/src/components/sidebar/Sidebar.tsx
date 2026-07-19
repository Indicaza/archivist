import type { ReactNode } from "react";
import { PanelLeftClose, PanelRightClose } from "lucide-react";
import styles from "./Sidebar.module.css";

export type SidebarView = {
  id: string;
  title: string;
  icon: ReactNode;
  content: ReactNode;
};

type SidebarProps = {
  side: "left" | "right";
  views: SidebarView[];
  activeViewId: string;
  open: boolean;
  onActivateView: (viewId: string) => void;
  onClosePanel: () => void;
};

export function Sidebar({
  side,
  views,
  activeViewId,
  open,
  onActivateView,
  onClosePanel,
}: SidebarProps) {
  const activeView =
    views.find((view) => view.id === activeViewId) ?? views[0] ?? null;
  const isLeft = side === "left";

  return (
    <aside
      className={`${styles.sidebar} ${styles[side]} ${
        open ? styles.open : styles.closed
      }`}
      aria-label={`${side} activity sidebar`}
    >
      <nav className={styles.activityRail} aria-label={`${side} activities`}>
        <div className={styles.activityGroup}>
          {views.map((view) => {
            const active = view.id === activeView?.id;

            return (
              <button
                key={view.id}
                className={`${styles.activityButton} ${
                  active ? styles.activityButtonActive : ""
                }`}
                type="button"
                onClick={() => onActivateView(view.id)}
                aria-label={view.title}
                aria-pressed={active && open}
                title={view.title}
              >
                {view.icon}
              </button>
            );
          })}
        </div>
      </nav>

      {open && activeView ? (
        <section className={styles.panel} aria-label={activeView.title}>
          <header className={styles.header}>
            <span className={styles.title}>{activeView.title}</span>

            <button
              className={styles.headerButton}
              type="button"
              onClick={onClosePanel}
              aria-label={`Collapse ${activeView.title}`}
              title={`Collapse ${activeView.title}`}
            >
              {isLeft ? (
                <PanelLeftClose size={17} strokeWidth={2} />
              ) : (
                <PanelRightClose size={17} strokeWidth={2} />
              )}
            </button>
          </header>

          <div className={styles.scrollArea}>{activeView.content}</div>
        </section>
      ) : null}
    </aside>
  );
}

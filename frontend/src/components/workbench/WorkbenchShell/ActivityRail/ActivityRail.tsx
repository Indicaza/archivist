import type { WorkbenchDockView } from "../WorkbenchShell";
import styles from "./ActivityRail.module.css";

type ActivityRailProps = {
  views: WorkbenchDockView[];
  activeViewId: string;
  panelOpen: boolean;
  onActivateView: (viewId: string) => void;
};

export function ActivityRail({
  views,
  activeViewId,
  panelOpen,
  onActivateView,
}: ActivityRailProps) {
  return (
    <nav className={styles.rail} aria-label="Workspace navigation">
      <ul className={styles.list}>
        {views.map((view) => {
          const active = panelOpen && view.id === activeViewId;

          return (
            <li key={view.id} className={styles.item}>
              <button
                className={`${styles.button} ${active ? styles.active : ""}`}
                type="button"
                onClick={() => onActivateView(view.id)}
                aria-label={view.title}
                aria-pressed={active}
                title={view.title}
              >
                {view.icon}
              </button>
            </li>
          );
        })}
      </ul>
    </nav>
  );
}

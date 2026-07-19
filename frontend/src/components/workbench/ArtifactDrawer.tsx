import { type ReactNode, useEffect, useState } from "react";
import { PanelRightClose, PanelRightOpen, Sparkles } from "lucide-react";
import styles from "./ArtifactDrawer.module.css";

type ArtifactDrawerProps = {
  children: ReactNode;
};

const STORAGE_KEY = "archivist.workbench.artifacts.open";

function readStoredOpenState(): boolean {
  try {
    return window.localStorage.getItem(STORAGE_KEY) === "true";
  } catch {
    return false;
  }
}

export function ArtifactDrawer({ children }: ArtifactDrawerProps) {
  const [open, setOpen] = useState(readStoredOpenState);

  useEffect(() => {
    window.localStorage.setItem(STORAGE_KEY, String(open));
  }, [open]);

  return (
    <aside
      className={`${styles.drawer} ${open ? styles.open : styles.closed}`}
      aria-label="Chat artifacts"
    >
      <button
        className={styles.toggleButton}
        type="button"
        onClick={() => setOpen((current) => !current)}
        aria-expanded={open}
        aria-label={open ? "Close Chat Artifacts" : "Open Chat Artifacts"}
        title={open ? "Close Chat Artifacts" : "Open Chat Artifacts"}
      >
        {open ? (
          <PanelRightClose size={16} strokeWidth={2} />
        ) : (
          <PanelRightOpen size={16} strokeWidth={2} />
        )}
      </button>

      {open ? (
        <section className={styles.panel}>
          <header className={styles.header}>
            <Sparkles size={14} strokeWidth={2} />
            <span>Chat Artifacts</span>
          </header>

          <div className={styles.content}>{children}</div>
        </section>
      ) : null}
    </aside>
  );
}

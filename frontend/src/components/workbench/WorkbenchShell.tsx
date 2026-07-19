import { type ReactNode, useEffect, useRef, useState } from "react";
import { PanelLeftOpen } from "lucide-react";
import { Sidebar, type SidebarView } from "../sidebar/Sidebar";
import { ArtifactDrawer } from "./ArtifactDrawer";
import styles from "./WorkbenchShell.module.css";

export type WorkbenchDockView = SidebarView;

type WorkbenchShellProps = {
  leftViews: WorkbenchDockView[];
  workspace: ReactNode;
  artifactPanel: ReactNode;
};

type StoredLayout = {
  activeLeftViewId: string;
  leftOpen: boolean;
};

const STORAGE_KEY = "archivist.workbench.layout.v5";

function readStoredLayout(): StoredLayout | null {
  try {
    const rawValue = window.localStorage.getItem(STORAGE_KEY);

    if (!rawValue) {
      return null;
    }

    return JSON.parse(rawValue) as StoredLayout;
  } catch {
    return null;
  }
}

export function WorkbenchShell({
  leftViews,
  workspace,
  artifactPanel,
}: WorkbenchShellProps) {
  const storedLayout = useRef(readStoredLayout());

  const [activeLeftViewId, setActiveLeftViewId] = useState(
    storedLayout.current?.activeLeftViewId ?? leftViews[0]?.id ?? "",
  );
  const [leftOpen, setLeftOpen] = useState(
    storedLayout.current?.leftOpen ?? true,
  );

  useEffect(() => {
    const nextLayout: StoredLayout = {
      activeLeftViewId,
      leftOpen,
    };

    window.localStorage.setItem(STORAGE_KEY, JSON.stringify(nextLayout));
  }, [activeLeftViewId, leftOpen]);

  function activateLeftView(viewId: string) {
    if (viewId === activeLeftViewId && leftOpen) {
      return;
    }

    setActiveLeftViewId(viewId);
    setLeftOpen(true);
  }

  return (
    <div
      className={`${styles.shell} ${
        leftOpen ? styles.leftOpen : styles.leftClosed
      }`}
    >
      {leftOpen ? (
        <Sidebar
          views={leftViews}
          activeViewId={activeLeftViewId}
          onActivateView={activateLeftView}
          onClosePanel={() => setLeftOpen(false)}
        />
      ) : (
        <button
          className={styles.restoreExplorerButton}
          type="button"
          onClick={() => setLeftOpen(true)}
          aria-label="Open Library Explorer"
          title="Open Library Explorer"
        >
          <PanelLeftOpen size={17} strokeWidth={2} />
        </button>
      )}

      {workspace}

      <ArtifactDrawer>{artifactPanel}</ArtifactDrawer>
    </div>
  );
}

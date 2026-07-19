import { type ReactNode, useEffect, useMemo, useRef, useState } from "react";
import { ActivityRail } from "./ActivityRail/ActivityRail";
import { ExplorerDock } from "./ExplorerDock/ExplorerDock";
import {
  type WorkbenchDockMode,
  WorkbenchLayoutProvider,
} from "./WorkbenchLayoutContext";
import { ArtifactDrawer } from "../ArtifactDrawer/ArtifactDrawer";
import styles from "./WorkbenchShell.module.css";

export type WorkbenchDockView = {
  id: string;
  title: string;
  icon: ReactNode;
  content: ReactNode;
};

type WorkbenchShellProps = {
  leftViews: WorkbenchDockView[];
  workspace: ReactNode;
  artifactPanel: ReactNode;
  statusBar: ReactNode;
};

type StoredLayout = {
  activeLeftViewId: string;
  leftOpen: boolean;
  dockMode: WorkbenchDockMode;
};

const STORAGE_KEY = "archivist.workbench.layout.v8";

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
  statusBar,
}: WorkbenchShellProps) {
  const storedLayout = useRef(readStoredLayout());

  const [activeLeftViewId, setActiveLeftViewId] = useState(
    storedLayout.current?.activeLeftViewId ?? leftViews[0]?.id ?? "",
  );
  const [leftOpen, setLeftOpen] = useState(
    storedLayout.current?.leftOpen ?? true,
  );
  const [dockMode, setDockMode] = useState<WorkbenchDockMode>(
    storedLayout.current?.dockMode ?? "attached",
  );

  const effectiveDockMode: WorkbenchDockMode =
    leftOpen && dockMode === "attached" ? "attached" : "centered";

  useEffect(() => {
    const nextLayout: StoredLayout = {
      activeLeftViewId,
      leftOpen,
      dockMode,
    };

    window.localStorage.setItem(STORAGE_KEY, JSON.stringify(nextLayout));
  }, [activeLeftViewId, dockMode, leftOpen]);

  function activateLeftView(viewId: string) {
    if (viewId === activeLeftViewId && leftOpen) {
      setLeftOpen(false);
      return;
    }

    setActiveLeftViewId(viewId);
    setLeftOpen(true);
  }

  const layoutContext = useMemo(
    () => ({
      leftOpen,
      dockMode,
      effectiveDockMode,
      toggleDockMode: () => {
        setDockMode((current) =>
          current === "attached" ? "centered" : "attached",
        );
      },
    }),
    [dockMode, effectiveDockMode, leftOpen],
  );

  return (
    <WorkbenchLayoutProvider value={layoutContext}>
      <div
        className={`${styles.shell} ${
          leftOpen ? styles.leftOpen : styles.leftClosed
        } ${
          effectiveDockMode === "attached"
            ? styles.dockAttached
            : styles.dockCentered
        }`}
      >
        <ActivityRail
          views={leftViews}
          activeViewId={activeLeftViewId}
          panelOpen={leftOpen}
          onActivateView={activateLeftView}
        />

        {leftOpen ? (
          <ExplorerDock
            views={leftViews}
            activeViewId={activeLeftViewId}
            onClose={() => setLeftOpen(false)}
          />
        ) : null}

        {workspace}

        <ArtifactDrawer>{artifactPanel}</ArtifactDrawer>

        {statusBar}
      </div>
    </WorkbenchLayoutProvider>
  );
}

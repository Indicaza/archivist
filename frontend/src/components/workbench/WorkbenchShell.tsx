import { type ReactNode, useEffect, useRef, useState } from "react";
import { Sidebar, type SidebarView } from "../sidebar/Sidebar";
import styles from "./WorkbenchShell.module.css";

export type WorkbenchDockView = SidebarView;

type WorkbenchShellProps = {
  leftViews: WorkbenchDockView[];
  rightViews: WorkbenchDockView[];
  workspace: ReactNode;
};

type StoredLayout = {
  activeLeftViewId: string;
  activeRightViewId: string;
  leftOpen: boolean;
  rightOpen: boolean;
};

const STORAGE_KEY = "archivist.workbench.layout.v2";

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
  rightViews,
  workspace,
}: WorkbenchShellProps) {
  const storedLayout = useRef(readStoredLayout());

  const [activeLeftViewId, setActiveLeftViewId] = useState(
    storedLayout.current?.activeLeftViewId ?? leftViews[0]?.id ?? "",
  );
  const [activeRightViewId, setActiveRightViewId] = useState(
    storedLayout.current?.activeRightViewId ?? rightViews[0]?.id ?? "",
  );
  const [leftOpen, setLeftOpen] = useState(
    storedLayout.current?.leftOpen ?? true,
  );
  const [rightOpen, setRightOpen] = useState(
    storedLayout.current?.rightOpen ?? true,
  );

  useEffect(() => {
    const nextLayout: StoredLayout = {
      activeLeftViewId,
      activeRightViewId,
      leftOpen,
      rightOpen,
    };

    window.localStorage.setItem(STORAGE_KEY, JSON.stringify(nextLayout));
  }, [activeLeftViewId, activeRightViewId, leftOpen, rightOpen]);

  function activateLeftView(viewId: string) {
    if (viewId === activeLeftViewId) {
      setLeftOpen((current) => !current);
      return;
    }

    setActiveLeftViewId(viewId);
    setLeftOpen(true);
  }

  function activateRightView(viewId: string) {
    if (viewId === activeRightViewId) {
      setRightOpen((current) => !current);
      return;
    }

    setActiveRightViewId(viewId);
    setRightOpen(true);
  }

  return (
    <div
      className={`${styles.shell} ${
        leftOpen ? styles.leftOpen : styles.leftClosed
      } ${rightOpen ? styles.rightOpen : styles.rightClosed}`}
    >
      <Sidebar
        side="left"
        views={leftViews}
        activeViewId={activeLeftViewId}
        open={leftOpen}
        onActivateView={activateLeftView}
        onClosePanel={() => setLeftOpen(false)}
      />

      {workspace}

      <Sidebar
        side="right"
        views={rightViews}
        activeViewId={activeRightViewId}
        open={rightOpen}
        onActivateView={activateRightView}
        onClosePanel={() => setRightOpen(false)}
      />
    </div>
  );
}

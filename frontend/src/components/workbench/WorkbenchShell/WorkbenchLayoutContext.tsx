import { createContext, useContext } from "react";

export type WorkbenchDockMode = "attached" | "centered";

type WorkbenchLayoutContextValue = {
  leftOpen: boolean;
  dockMode: WorkbenchDockMode;
  effectiveDockMode: WorkbenchDockMode;
  toggleDockMode: () => void;
};

const WorkbenchLayoutContext =
  createContext<WorkbenchLayoutContextValue | null>(null);

export const WorkbenchLayoutProvider = WorkbenchLayoutContext.Provider;

export function useWorkbenchLayout(): WorkbenchLayoutContextValue {
  const context = useContext(WorkbenchLayoutContext);

  if (!context) {
    throw new Error(
      "useWorkbenchLayout must be used inside a WorkbenchLayoutProvider.",
    );
  }

  return context;
}

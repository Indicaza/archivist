// frontend/src/App.tsx
import { useMemo, useRef, useState } from "react";
import { ChatWindow } from "./components/chat/ChatWindow";
import { Sidebar } from "./components/sidebar/Sidebar";
import { Topbar } from "./components/topbar/Topbar";
import { mockLibraries } from "./domains/library/library.mock";
import "./App.css";

export function App() {
  const [sidebarCollapsed, setSidebarCollapsed] = useState(false);
  const [selectedLibraryId, setSelectedLibraryId] = useState(
    mockLibraries[0]?.id ?? null,
  );

  const appMainRef = useRef<HTMLElement | null>(null);

  const selectedLibrary = useMemo(() => {
    return (
      mockLibraries.find((library) => library.id === selectedLibraryId) ?? null
    );
  }, [selectedLibraryId]);

  return (
    <>
      <Sidebar
        collapsed={sidebarCollapsed}
        libraries={mockLibraries}
        selectedLibraryId={selectedLibraryId}
        onSelectLibrary={setSelectedLibraryId}
        onToggle={() => setSidebarCollapsed((value) => !value)}
      />

      <Topbar selectedLibrary={selectedLibrary} />

      <main ref={appMainRef} className="app-main">
        <div className="app-main-inner">
          <ChatWindow
            scrollContainerRef={appMainRef}
            selectedLibrary={selectedLibrary}
          />
        </div>
      </main>
    </>
  );
}

export default App;

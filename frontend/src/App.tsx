// frontend/src/App.tsx
import { useRef, useState } from "react";
import { ChatWindow } from "./components/chat/ChatWindow";
import { Sidebar } from "./components/sidebar/Sidebar";
import { Topbar } from "./components/topbar/Topbar";
import "./App.css";

export function App() {
  const [sidebarCollapsed, setSidebarCollapsed] = useState(false);
  const appMainRef = useRef<HTMLElement | null>(null);

  return (
    <>
      <Sidebar
        collapsed={sidebarCollapsed}
        onToggle={() => setSidebarCollapsed((value) => !value)}
      />

      <Topbar />

      <main ref={appMainRef} className="app-main">
        <div className="app-main-inner">
          <ChatWindow scrollContainerRef={appMainRef} />
        </div>
      </main>
    </>
  );
}

export default App;

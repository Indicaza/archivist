import { useState } from "react";
import { Sidebar } from "./components/sidebar/Sidebar";
import { Topbar } from "./components/topbar/Topbar";
import "./App.css";

export function App() {
  const [sidebarCollapsed, setSidebarCollapsed] = useState(false);

  return (
    <>
      <Sidebar
        collapsed={sidebarCollapsed}
        onToggle={() => setSidebarCollapsed((value) => !value)}
      />

      <Topbar />

      <main className="app-main">
        <div className="app-main-inner">Archivist workspace goes here.</div>
      </main>
    </>
  );
}

export default App;

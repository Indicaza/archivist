import { useEffect, useState } from "react";
import "./App.css";

declare global {
  interface Window {
    archivist?: {
      selectFolder: () => Promise<string | null>;
    };
  }
}

type HealthResponse = {
  ok: boolean;
  app: string;
};

function App() {
  const [health, setHealth] = useState<HealthResponse | null>(null);
  const [selectedFolder, setSelectedFolder] = useState<string | null>(null);

  useEffect(() => {
    fetch("http://127.0.0.1:3333/health")
      .then((res) => res.json())
      .then(setHealth)
      .catch(() => setHealth(null));
  }, []);

  async function handleSelectFolder() {
    const folder = await window.archivist?.selectFolder();
    setSelectedFolder(folder ?? null);
  }

  return (
    <main className="app-shell">
      <section className="hero">
        <p className="eyebrow">Local-first AI librarian</p>
        <h1>The Archivist</h1>
        <p>
          Turn messy folders into organized, searchable, AI-readable libraries.
        </p>

        <div className="status-card">
          <strong>Backend:</strong>{" "}
          {health?.ok ? `${health.app} is awake` : "not connected"}
        </div>

        <button onClick={handleSelectFolder}>Choose Library Folder</button>

        {selectedFolder && (
          <div className="folder-card">
            <strong>Selected folder:</strong>
            <span>{selectedFolder}</span>
          </div>
        )}
      </section>
    </main>
  );
}

export default App;

import { useEffect, useMemo, useRef, useState } from "react";
import { ChatWindow } from "./components/chat/ChatWindow";
import { LibraryManagementModal } from "./components/sidebar/Libraries/LibraryManagementModal/LibraryManagementModal";
import { Sidebar } from "./components/sidebar/Sidebar";
import { Topbar } from "./components/topbar/Topbar";
import {
  addLibrary,
  archiveLibrary,
  editLibrary,
  fetchAppState,
  fetchArchivedLibraries,
  fetchLibraries,
  restoreLibrary,
  updateSelectedLibrary,
} from "./domains/library/library.api";
import type { Library, LibraryListItem } from "./domains/library/library.types";
import "./App.css";

export function App() {
  const [sidebarCollapsed, setSidebarCollapsed] = useState(false);
  const [libraries, setLibraries] = useState<Library[]>([]);
  const [archivedLibraries, setArchivedLibraries] = useState<Library[]>([]);
  const [selectedLibraryId, setSelectedLibraryId] = useState<string | null>(
    null,
  );
  const [managedLibraryId, setManagedLibraryId] = useState<string | null>(null);
  const [loadingLibraries, setLoadingLibraries] = useState(true);
  const [addingLibrary, setAddingLibrary] = useState(false);
  const [savingLibrary, setSavingLibrary] = useState(false);
  const [archivingLibrary, setArchivingLibrary] = useState(false);
  const [restoringLibraryId, setRestoringLibraryId] = useState<string | null>(
    null,
  );

  const appMainRef = useRef<HTMLElement | null>(null);

  const libraryListItems = useMemo<LibraryListItem[]>(() => {
    return libraries.map((library) => ({
      ...library,
      subtitle: library.description ?? library.rootPath,
      status: library.id === selectedLibraryId ? "active" : "draft",
    }));
  }, [libraries, selectedLibraryId]);

  const archivedLibraryListItems = useMemo<LibraryListItem[]>(() => {
    return archivedLibraries.map((library) => ({
      ...library,
      subtitle: library.description ?? library.rootPath,
      status: "offline",
    }));
  }, [archivedLibraries]);

  const selectedLibrary = useMemo(() => {
    return (
      libraryListItems.find((library) => library.id === selectedLibraryId) ??
      null
    );
  }, [libraryListItems, selectedLibraryId]);

  const managedLibrary = useMemo(() => {
    return libraries.find((library) => library.id === managedLibraryId) ?? null;
  }, [libraries, managedLibraryId]);

  useEffect(() => {
    async function loadArchivistState() {
      try {
        const [loadedLibraries, loadedArchivedLibraries, appState] =
          await Promise.all([
            fetchLibraries(),
            fetchArchivedLibraries(),
            fetchAppState(),
          ]);

        setLibraries(loadedLibraries);
        setArchivedLibraries(loadedArchivedLibraries);
        setSelectedLibraryId(appState.selectedLibraryId);
      } catch (error) {
        window.alert(
          error instanceof Error
            ? error.message
            : "Archivist could not load its state.",
        );
      } finally {
        setLoadingLibraries(false);
      }
    }

    void loadArchivistState();
  }, []);

  async function handleSelectLibrary(libraryId: string) {
    if (libraryId === selectedLibraryId) {
      return;
    }

    try {
      const appState = await updateSelectedLibrary(libraryId);

      setSelectedLibraryId(appState.selectedLibraryId);
    } catch (error) {
      window.alert(
        error instanceof Error
          ? error.message
          : "Archivist could not select the Library.",
      );
    }
  }

  async function handleAddLibrary() {
    if (addingLibrary) {
      return;
    }

    if (!window.archivist) {
      window.alert(
        "The folder picker is only available inside the Electron app.",
      );

      return;
    }

    const rootPath = await window.archivist.selectFolder();

    if (!rootPath) {
      return;
    }

    setAddingLibrary(true);

    try {
      const result = await addLibrary(rootPath);

      setLibraries((current) => [...current, result.library]);
      setSelectedLibraryId(result.selectedLibraryId);
    } catch (error) {
      window.alert(
        error instanceof Error
          ? error.message
          : "Archivist could not create the Library.",
      );
    } finally {
      setAddingLibrary(false);
    }
  }

  async function handleSaveLibrary(
    libraryId: string,
    input: {
      name: string;
      description: string | null;
    },
  ) {
    setSavingLibrary(true);

    try {
      const updatedLibrary = await editLibrary(libraryId, input);

      setLibraries((current) =>
        current.map((library) =>
          library.id === libraryId ? updatedLibrary : library,
        ),
      );

      setManagedLibraryId(null);
    } catch (error) {
      window.alert(
        error instanceof Error
          ? error.message
          : "Archivist could not update the Library.",
      );
    } finally {
      setSavingLibrary(false);
    }
  }

  async function handleArchiveLibrary(libraryId: string) {
    setArchivingLibrary(true);

    try {
      const result = await archiveLibrary(libraryId);

      setLibraries((current) =>
        current.filter((library) => library.id !== libraryId),
      );

      setArchivedLibraries((current) => [result.library, ...current]);

      setSelectedLibraryId(result.selectedLibraryId);
      setManagedLibraryId(null);
    } catch (error) {
      window.alert(
        error instanceof Error
          ? error.message
          : "Archivist could not archive the Library.",
      );
    } finally {
      setArchivingLibrary(false);
    }
  }

  async function handleRestoreLibrary(libraryId: string) {
    if (restoringLibraryId) {
      return;
    }

    setRestoringLibraryId(libraryId);

    try {
      const restoredLibrary = await restoreLibrary(libraryId);

      setArchivedLibraries((current) =>
        current.filter((library) => library.id !== libraryId),
      );

      setLibraries((current) => [restoredLibrary, ...current]);
    } catch (error) {
      window.alert(
        error instanceof Error
          ? error.message
          : "Archivist could not restore the Library.",
      );
    } finally {
      setRestoringLibraryId(null);
    }
  }

  return (
    <>
      <Sidebar
        collapsed={sidebarCollapsed}
        libraries={libraryListItems}
        archivedLibraries={archivedLibraryListItems}
        selectedLibraryId={selectedLibraryId}
        loadingLibraries={loadingLibraries}
        addingLibrary={addingLibrary}
        restoringLibraryId={restoringLibraryId}
        onSelectLibrary={handleSelectLibrary}
        onAddLibrary={handleAddLibrary}
        onManageLibrary={setManagedLibraryId}
        onRestoreLibrary={handleRestoreLibrary}
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

      {managedLibrary ? (
        <LibraryManagementModal
          library={managedLibrary}
          saving={savingLibrary}
          archiving={archivingLibrary}
          onClose={() => setManagedLibraryId(null)}
          onSave={handleSaveLibrary}
          onArchive={handleArchiveLibrary}
        />
      ) : null}
    </>
  );
}

export default App;

import { useEffect, useMemo, useState } from "react";
import { Library as LibraryIcon, Search, Sparkles } from "lucide-react";
import { ChatWindow } from "./components/chat/ChatWindow";
import { AgentManagementModal } from "./components/sidebar/Agents/AgentManagementModal/AgentManagementModal";
import { Agents } from "./components/sidebar/Agents/Agents";
import { ChatManagementModal } from "./components/sidebar/Chats/ChatManagementModal/ChatManagementModal";
import { Chats } from "./components/sidebar/Chats/Chats";
import { Libraries } from "./components/sidebar/Libraries/Libraries";
import { LibraryManagementModal } from "./components/sidebar/Libraries/LibraryManagementModal/LibraryManagementModal";
import { Topbar } from "./components/topbar/Topbar";
import {
  ChatPanelToolbar,
  type ChatDockView,
} from "./components/workbench/ChatPanelToolbar";
import { DockPlaceholder } from "./components/workbench/DockPlaceholder";
import { WorkbenchShell } from "./components/workbench/WorkbenchShell";
import {
  addAgent,
  archiveAgent,
  duplicateAgent,
  editAgent,
  fetchAgents,
  fetchArchivedAgents,
  removeAgent,
  restoreAgent,
} from "./domains/agent/agent.api";
import type { Agent, UpdateAgentInput } from "./domains/agent/agent.types";
import { fetchAIModels } from "./domains/ai/ai.api";
import type { ModelDefinition } from "./domains/ai/ai.types";
import {
  addChat,
  archiveChat,
  editChat,
  fetchArchivedChats,
  fetchChats,
  removeChat,
  restoreChat,
  updateSelectedChat,
} from "./domains/chat/chat.api";
import type { Chat, UpdateChatInput } from "./domains/chat/chat.types";
import { fetchContextCompilers } from "./domains/cognition/contextCompiler.api";
import type { ContextCompilerDefinition } from "./domains/cognition/contextCompiler.types";
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
  const [agents, setAgents] = useState<Agent[]>([]);
  const [archivedAgents, setArchivedAgents] = useState<Agent[]>([]);

  const [managedAgentId, setManagedAgentId] = useState<string | null>(null);

  const [loadingAgents, setLoadingAgents] = useState(true);

  const [addingAgent, setAddingAgent] = useState(false);

  const [savingAgent, setSavingAgent] = useState(false);

  const [duplicatingAgent, setDuplicatingAgent] = useState(false);

  const [archivingAgent, setArchivingAgent] = useState(false);

  const [restoringAgent, setRestoringAgent] = useState(false);

  const [deletingAgent, setDeletingAgent] = useState(false);

  const [aiModels, setAIModels] = useState<ModelDefinition[]>([]);

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

  const [restoringManagedLibrary, setRestoringManagedLibrary] = useState(false);

  const [chats, setChats] = useState<Chat[]>([]);
  const [archivedChats, setArchivedChats] = useState<Chat[]>([]);

  const [contextCompilers, setContextCompilers] = useState<
    ContextCompilerDefinition[]
  >([]);

  const [selectedChatId, setSelectedChatId] = useState<string | null>(null);

  const [managedChatId, setManagedChatId] = useState<string | null>(null);

  const [loadingChats, setLoadingChats] = useState(true);

  const [addingChat, setAddingChat] = useState(false);

  const [savingChat, setSavingChat] = useState(false);

  const [archivingChat, setArchivingChat] = useState(false);

  const [deletingChat, setDeletingChat] = useState(false);

  const [restoringManagedChat, setRestoringManagedChat] = useState(false);

  const [chatDockView, setChatDockView] = useState<ChatDockView>(null);

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

  const selectedChat = useMemo(() => {
    return chats.find((chat) => chat.id === selectedChatId) ?? null;
  }, [chats, selectedChatId]);

  const activeAgentId = selectedChat?.agentId ?? null;

  const activeAgent = useMemo(() => {
    return agents.find((agent) => agent.id === activeAgentId) ?? null;
  }, [activeAgentId, agents]);

  const chatDockPanelWidth = useMemo(() => {
    const labels =
      chatDockView === "chats"
        ? [...chats, ...archivedChats].map((chat) => chat.title)
        : chatDockView === "agents"
          ? [...agents, ...archivedAgents].map((agent) => agent.name)
          : [];

    const longestLabelLength = labels.reduce(
      (longest, label) => Math.max(longest, label.trim().length),
      0,
    );

    return Math.round(
      Math.min(
        202,
        Math.max(148, 108 + Math.min(longestLabelLength, 23) * 4.1),
      ),
    );
  }, [agents, archivedAgents, archivedChats, chatDockView, chats]);

  const managedAgent = useMemo(() => {
    return (
      agents.find((agent) => agent.id === managedAgentId) ??
      archivedAgents.find((agent) => agent.id === managedAgentId) ??
      null
    );
  }, [agents, archivedAgents, managedAgentId]);

  const managedLibrary = useMemo(() => {
    return (
      libraries.find((library) => library.id === managedLibraryId) ??
      archivedLibraries.find((library) => library.id === managedLibraryId) ??
      null
    );
  }, [archivedLibraries, libraries, managedLibraryId]);

  const managedChat = useMemo(() => {
    return (
      chats.find((chat) => chat.id === managedChatId) ??
      archivedChats.find((chat) => chat.id === managedChatId) ??
      null
    );
  }, [archivedChats, chats, managedChatId]);

  useEffect(() => {
    async function loadArchivistState() {
      try {
        const [
          loadedLibraries,
          loadedArchivedLibraries,
          loadedChats,
          loadedArchivedChats,
          appState,
          loadedContextCompilers,
          loadedAgents,
          loadedArchivedAgents,
          loadedAIModels,
        ] = await Promise.all([
          fetchLibraries(),
          fetchArchivedLibraries(),
          fetchChats(),
          fetchArchivedChats(),
          fetchAppState(),
          fetchContextCompilers(),
          fetchAgents(),
          fetchArchivedAgents(),
          fetchAIModels(),
        ]);

        setLibraries(loadedLibraries);
        setArchivedLibraries(loadedArchivedLibraries);

        setChats(loadedChats);
        setArchivedChats(loadedArchivedChats);

        setContextCompilers(loadedContextCompilers);

        setAgents(loadedAgents);
        setArchivedAgents(loadedArchivedAgents);
        setAIModels(loadedAIModels);

        setSelectedLibraryId(appState.selectedLibraryId);

        const selectedChatStillExists = loadedChats.some(
          (chat) => chat.id === appState.selectedChatId,
        );

        setSelectedChatId(
          selectedChatStillExists
            ? appState.selectedChatId
            : (loadedChats[0]?.id ?? null),
        );
      } catch (error) {
        window.alert(
          error instanceof Error
            ? error.message
            : "Archivist could not load its state.",
        );
      } finally {
        setLoadingLibraries(false);
        setLoadingChats(false);
        setLoadingAgents(false);
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

      if (updatedLibrary.archivedAt) {
        setArchivedLibraries((current) =>
          current.map((library) =>
            library.id === libraryId ? updatedLibrary : library,
          ),
        );
      } else {
        setLibraries((current) =>
          current.map((library) =>
            library.id === libraryId ? updatedLibrary : library,
          ),
        );
      }

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

  async function handleRestoreManagedLibrary(libraryId: string) {
    if (restoringManagedLibrary) {
      return;
    }

    setRestoringManagedLibrary(true);

    try {
      const restoredLibrary = await restoreLibrary(libraryId);

      setArchivedLibraries((current) =>
        current.filter((library) => library.id !== libraryId),
      );

      setLibraries((current) => [restoredLibrary, ...current]);

      setManagedLibraryId(null);
    } catch (error) {
      window.alert(
        error instanceof Error
          ? error.message
          : "Archivist could not restore the Library.",
      );
    } finally {
      setRestoringManagedLibrary(false);
    }
  }

  async function handleAddAgent() {
    if (addingAgent) {
      return;
    }

    setAddingAgent(true);

    try {
      const agent = await addAgent({
        name: "New Agent",
      });

      setAgents((current) => [agent, ...current]);

      setManagedAgentId(agent.id);
    } catch (error) {
      window.alert(
        error instanceof Error
          ? error.message
          : "Archivist could not create the Agent.",
      );
    } finally {
      setAddingAgent(false);
    }
  }

  async function handleSaveAgent(agentId: string, input: UpdateAgentInput) {
    setSavingAgent(true);

    try {
      const updatedAgent = await editAgent(agentId, input);

      if (updatedAgent.archivedAt) {
        setArchivedAgents((current) =>
          current.map((agent) => (agent.id === agentId ? updatedAgent : agent)),
        );
      } else {
        setAgents((current) =>
          current.map((agent) => (agent.id === agentId ? updatedAgent : agent)),
        );
      }

      setManagedAgentId(null);
    } catch (error) {
      window.alert(
        error instanceof Error
          ? error.message
          : "Archivist could not update the Agent.",
      );
    } finally {
      setSavingAgent(false);
    }
  }

  async function handleDuplicateAgent(agentId: string) {
    if (duplicatingAgent) {
      return;
    }

    setDuplicatingAgent(true);

    try {
      const duplicatedAgent = await duplicateAgent(agentId);

      setAgents((current) => [duplicatedAgent, ...current]);

      setManagedAgentId(duplicatedAgent.id);
    } catch (error) {
      window.alert(
        error instanceof Error
          ? error.message
          : "Archivist could not duplicate the Agent.",
      );
    } finally {
      setDuplicatingAgent(false);
    }
  }

  async function handleArchiveAgent(agentId: string) {
    if (archivingAgent) {
      return;
    }

    setArchivingAgent(true);

    try {
      const archivedAgent = await archiveAgent(agentId);

      setAgents((current) => current.filter((agent) => agent.id !== agentId));

      setArchivedAgents((current) => [archivedAgent, ...current]);

      setManagedAgentId(null);
    } catch (error) {
      window.alert(
        error instanceof Error
          ? error.message
          : "Archivist could not archive the Agent.",
      );
    } finally {
      setArchivingAgent(false);
    }
  }

  async function handleRestoreAgent(agentId: string) {
    if (restoringAgent) {
      return;
    }

    setRestoringAgent(true);

    try {
      const restoredAgent = await restoreAgent(agentId);

      setArchivedAgents((current) =>
        current.filter((agent) => agent.id !== agentId),
      );

      setAgents((current) => [restoredAgent, ...current]);

      setManagedAgentId(null);
    } catch (error) {
      window.alert(
        error instanceof Error
          ? error.message
          : "Archivist could not restore the Agent.",
      );
    } finally {
      setRestoringAgent(false);
    }
  }

  async function handleDeleteAgent(agentId: string) {
    if (deletingAgent) {
      return;
    }

    setDeletingAgent(true);

    try {
      await removeAgent(agentId);

      const defaultAgentId =
        agents.find((agent) => agent.isBuiltIn)?.id ?? null;

      setAgents((current) => current.filter((agent) => agent.id !== agentId));

      setArchivedAgents((current) =>
        current.filter((agent) => agent.id !== agentId),
      );

      if (defaultAgentId) {
        setChats((current) =>
          current.map((chat) =>
            chat.agentId === agentId
              ? {
                  ...chat,
                  agentId: defaultAgentId,
                }
              : chat,
          ),
        );

        setArchivedChats((current) =>
          current.map((chat) =>
            chat.agentId === agentId
              ? {
                  ...chat,
                  agentId: defaultAgentId,
                }
              : chat,
          ),
        );
      }

      setManagedAgentId(null);
    } catch (error) {
      window.alert(
        error instanceof Error
          ? error.message
          : "Archivist could not delete the Agent.",
      );
    } finally {
      setDeletingAgent(false);
    }
  }

  async function handleAddChat() {
    if (addingChat) {
      return;
    }

    setAddingChat(true);

    try {
      const chat = await addChat();

      setChats((current) => [chat, ...current]);

      setSelectedChatId(chat.id);
    } catch (error) {
      window.alert(
        error instanceof Error
          ? error.message
          : "Archivist could not create the chat.",
      );
    } finally {
      setAddingChat(false);
    }
  }

  async function handleSelectChat(chatId: string) {
    if (chatId === selectedChatId) {
      return;
    }

    try {
      const nextSelectedChatId = await updateSelectedChat(chatId);

      setSelectedChatId(nextSelectedChatId);
    } catch (error) {
      window.alert(
        error instanceof Error
          ? error.message
          : "Archivist could not select the chat.",
      );
    }
  }

  async function handleSelectAgent(agentId: string) {
    const chat = selectedChat;

    if (!chat || chat.agentId === agentId) {
      return;
    }

    try {
      const updatedChat = await editChat(chat.id, {
        agentId,
      });

      setChats((current) =>
        current.map((candidate) =>
          candidate.id === updatedChat.id ? updatedChat : candidate,
        ),
      );
    } catch (error) {
      window.alert(
        error instanceof Error
          ? error.message
          : "Archivist could not assign the Agent.",
      );
    }
  }

  function handleToggleChatDockView(nextView: Exclude<ChatDockView, null>) {
    setChatDockView((current) => (current === nextView ? null : nextView));
  }

  async function handleSaveChat(chatId: string, input: UpdateChatInput) {
    setSavingChat(true);

    try {
      const updatedChat = await editChat(chatId, input);

      if (updatedChat.archivedAt) {
        setArchivedChats((current) =>
          current.map((chat) => (chat.id === chatId ? updatedChat : chat)),
        );
      } else {
        setChats((current) =>
          current.map((chat) => (chat.id === chatId ? updatedChat : chat)),
        );
      }

      setManagedChatId(null);
    } catch (error) {
      window.alert(
        error instanceof Error
          ? error.message
          : "Archivist could not update the chat.",
      );
    } finally {
      setSavingChat(false);
    }
  }

  async function handleArchiveChat(chatId: string) {
    setArchivingChat(true);

    try {
      const result = await archiveChat(chatId);

      setChats((current) => current.filter((chat) => chat.id !== chatId));

      setArchivedChats((current) => [result.chat, ...current]);

      setSelectedChatId(result.selectedChatId);
      setManagedChatId(null);
    } catch (error) {
      window.alert(
        error instanceof Error
          ? error.message
          : "Archivist could not archive the chat.",
      );
    } finally {
      setArchivingChat(false);
    }
  }

  async function handleRestoreManagedChat(chatId: string) {
    if (restoringManagedChat) {
      return;
    }

    setRestoringManagedChat(true);

    try {
      const restoredChat = await restoreChat(chatId);

      setArchivedChats((current) =>
        current.filter((chat) => chat.id !== chatId),
      );

      setChats((current) => [restoredChat, ...current]);

      setManagedChatId(null);
    } catch (error) {
      window.alert(
        error instanceof Error
          ? error.message
          : "Archivist could not restore the chat.",
      );
    } finally {
      setRestoringManagedChat(false);
    }
  }

  async function handleDeleteChat(chatId: string) {
    setDeletingChat(true);

    try {
      const result = await removeChat(chatId);

      setChats((current) => current.filter((chat) => chat.id !== chatId));

      setArchivedChats((current) =>
        current.filter((chat) => chat.id !== chatId),
      );

      setSelectedChatId(result.selectedChatId);
      setManagedChatId(null);
    } catch (error) {
      window.alert(
        error instanceof Error
          ? error.message
          : "Archivist could not permanently delete the chat.",
      );
    } finally {
      setDeletingChat(false);
    }
  }

  function handleChatActivity(chatId: string) {
    const now = new Date().toISOString();

    setChats((current) => {
      const activeChat = current.find((chat) => chat.id === chatId);

      if (!activeChat) {
        return current;
      }

      const updatedChat = {
        ...activeChat,
        updatedAt: now,
      };

      return [updatedChat, ...current.filter((chat) => chat.id !== chatId)];
    });
  }

  return (
    <>
      <Topbar />

      <WorkbenchShell
        leftViews={[
          {
            id: "libraries",
            title: "Library Explorer",
            icon: <LibraryIcon size={15} strokeWidth={1.9} />,
            content: (
              <div className="left-dock-stack">
                <Libraries
                  libraries={libraryListItems}
                  archivedLibraries={archivedLibraryListItems}
                  selectedLibraryId={selectedLibraryId}
                  loading={loadingLibraries}
                  adding={addingLibrary}
                  onSelectLibrary={handleSelectLibrary}
                  onAddLibrary={handleAddLibrary}
                  onManageLibrary={setManagedLibraryId}
                  onManageArchivedLibrary={setManagedLibraryId}
                />
              </div>
            ),
          },
          {
            id: "search",
            title: "Library Search",
            icon: <Search size={15} strokeWidth={1.9} />,
            content: (
              <DockPlaceholder
                icon={<Search size={20} strokeWidth={1.9} />}
                title="Library Search"
                description="Project-wide file and content search will live here."
              />
            ),
          },
          {
            id: "skills",
            title: "Skills",
            icon: <Sparkles size={15} strokeWidth={1.9} />,
            content: (
              <DockPlaceholder
                icon={<Sparkles size={20} strokeWidth={1.9} />}
                title="Skills"
                description="Reusable tools and guided workflows will live here."
              />
            ),
          },
        ]}
        artifactPanel={
          <DockPlaceholder
            icon={<Sparkles size={20} strokeWidth={1.9} />}
            title="No artifacts yet"
            description="Attachments, generated files, sources, and tool output will collect here."
          />
        }
        workspace={
          <ChatWindow
            selectedChat={selectedChat}
            onChatActivity={handleChatActivity}
            toolbar={
              <ChatPanelToolbar
                selectedChat={selectedChat}
                activeAgent={activeAgent}
                selectedLibraryName={selectedLibrary?.name ?? null}
                activeView={chatDockView}
                onToggleView={handleToggleChatDockView}
              />
            }
            controlPanelLabel={
              chatDockView === "chats"
                ? "Chats"
                : chatDockView === "agents"
                  ? "Agents"
                  : null
            }
            controlPanelWidth={chatDockPanelWidth}
            controlPanelActionLabel={
              chatDockView === "chats"
                ? addingChat
                  ? "Creating Chat..."
                  : "New Chat"
                : chatDockView === "agents"
                  ? addingAgent
                    ? "Creating Agent..."
                    : "New Agent"
                  : null
            }
            controlPanelActionBusy={
              chatDockView === "chats"
                ? addingChat
                : chatDockView === "agents"
                  ? addingAgent
                  : false
            }
            onControlPanelAction={
              chatDockView === "chats"
                ? handleAddChat
                : chatDockView === "agents"
                  ? handleAddAgent
                  : null
            }
            controlPanel={
              chatDockView === "chats" ? (
                <div className="dock-drawer-stack">
                  <Chats
                    chats={chats}
                    archivedChats={archivedChats}
                    selectedChatId={selectedChatId}
                    loading={loadingChats}
                    onSelectChat={handleSelectChat}
                    onManageChat={setManagedChatId}
                    onManageArchivedChat={setManagedChatId}
                  />
                </div>
              ) : chatDockView === "agents" ? (
                <div className="dock-drawer-stack">
                  <Agents
                    agents={agents}
                    archivedAgents={archivedAgents}
                    activeAgentId={activeAgentId}
                    loading={loadingAgents}
                    onSelectAgent={handleSelectAgent}
                    onManageAgent={setManagedAgentId}
                    onManageArchivedAgent={setManagedAgentId}
                  />
                </div>
              ) : null
            }
          />
        }
      />

      {managedAgent ? (
        <AgentManagementModal
          agent={managedAgent}
          models={aiModels}
          contextCompilers={contextCompilers}
          saving={savingAgent}
          duplicating={duplicatingAgent}
          archiving={archivingAgent}
          restoring={restoringAgent}
          deleting={deletingAgent}
          onClose={() => setManagedAgentId(null)}
          onSave={handleSaveAgent}
          onDuplicate={handleDuplicateAgent}
          onArchive={handleArchiveAgent}
          onRestore={handleRestoreAgent}
          onDelete={handleDeleteAgent}
        />
      ) : null}

      {managedLibrary ? (
        <LibraryManagementModal
          library={managedLibrary}
          saving={savingLibrary}
          archiving={archivingLibrary}
          restoring={restoringManagedLibrary}
          onClose={() => setManagedLibraryId(null)}
          onSave={handleSaveLibrary}
          onArchive={handleArchiveLibrary}
          onRestore={handleRestoreManagedLibrary}
        />
      ) : null}

      {managedChat ? (
        <ChatManagementModal
          chat={managedChat}
          agents={agents}
          saving={savingChat}
          archiving={archivingChat}
          restoring={restoringManagedChat}
          deleting={deletingChat}
          onClose={() => setManagedChatId(null)}
          onSave={handleSaveChat}
          onArchive={handleArchiveChat}
          onRestore={handleRestoreManagedChat}
          onDelete={handleDeleteChat}
        />
      ) : null}
    </>
  );
}

export default App;

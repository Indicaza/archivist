import { useState } from "react";
import {
  Archive,
  Bot,
  ChevronRight,
  Pencil,
  Plus,
  ShieldCheck,
} from "lucide-react";
import type { Agent } from "../../../domains/agent/agent.types";
import { SidebarButton } from "../SidebarButton/SidebarButton";
import { SidebarCard } from "../SidebarCard/SidebarCard";
import styles from "./Agents.module.css";

type AgentsProps = {
  agents: Agent[];
  archivedAgents: Agent[];
  activeAgentId: string | null;
  loading: boolean;
  adding: boolean;
  onAddAgent: () => void;
  onManageAgent: (agentId: string) => void;
  onManageArchivedAgent: (agentId: string) => void;
};

function getAgentSubtitle(agent: Agent): string {
  return (
    agent.profession.jobTitle ?? agent.description ?? "Configurable AI behavior"
  );
}

export function Agents({
  agents,
  archivedAgents,
  activeAgentId,
  loading,
  adding,
  onAddAgent,
  onManageAgent,
  onManageArchivedAgent,
}: AgentsProps) {
  const [agentsOpen, setAgentsOpen] = useState(true);
  const [allAgentsOpen, setAllAgentsOpen] = useState(true);
  const [archivedAgentsOpen, setArchivedAgentsOpen] = useState(false);

  const [pressedAgentId, setPressedAgentId] = useState<string | null>(null);

  function pressAgent(agentId: string, action: (agentId: string) => void) {
    setPressedAgentId(agentId);
    action(agentId);

    window.setTimeout(() => {
      setPressedAgentId((current) => (current === agentId ? null : current));
    }, 280);
  }

  return (
    <section>
      <div
        className={styles.groupHeader}
        onClick={() => setAgentsOpen((value) => !value)}
        role="button"
        tabIndex={0}
      >
        <ChevronRight
          size={16}
          strokeWidth={2.25}
          className={`${styles.caret} ${
            agentsOpen ? styles.caretOpen : styles.caretClosed
          }`}
        />

        <Bot size={16} strokeWidth={2.1} />
        <span>Agents</span>
      </div>

      <div
        className={`${styles.groupContent} ${
          agentsOpen ? styles.open : styles.closed
        }`}
      >
        <div className={styles.groupInner}>
          <div className={styles.actionsAreaTop}>
            <SidebarButton
              label={adding ? "Creating Agent..." : "Create Agent"}
              icon={<Plus size={17} />}
              onClick={adding ? undefined : onAddAgent}
            />
          </div>

          <div
            className={styles.subHeader}
            onClick={() => setAllAgentsOpen((value) => !value)}
            role="button"
            tabIndex={0}
          >
            <ChevronRight
              size={15}
              className={`${styles.caret} ${
                allAgentsOpen ? styles.caretOpen : styles.caretClosed
              }`}
            />

            <ShieldCheck size={15} />
            <span>Available Agents</span>
          </div>

          <div
            className={`${styles.subContent} ${
              allAgentsOpen ? styles.open : styles.closed
            }`}
          >
            {loading ? (
              <p className={styles.emptyText}>Loading Agents...</p>
            ) : agents.length === 0 ? (
              <p className={styles.emptyText}>No Agents available.</p>
            ) : (
              <ul className={styles.list}>
                {agents.map((agent) => (
                  <li key={agent.id}>
                    <SidebarCard
                      title={agent.name}
                      subtitle={getAgentSubtitle(agent)}
                      selected={agent.id === activeAgentId}
                      pressed={pressedAgentId === agent.id}
                      leading={
                        agent.isBuiltIn ? (
                          <ShieldCheck size={17} />
                        ) : (
                          <Bot size={17} />
                        )
                      }
                      action={
                        <button
                          type="button"
                          className={styles.manageButton}
                          aria-label={`Manage ${agent.name}`}
                          onClick={(event) => {
                            event.stopPropagation();

                            pressAgent(agent.id, onManageAgent);
                          }}
                        >
                          <Pencil size={15} />
                        </button>
                      }
                      onClick={() => pressAgent(agent.id, onManageAgent)}
                    />
                  </li>
                ))}
              </ul>
            )}
          </div>

          <div
            className={styles.subHeader}
            onClick={() => setArchivedAgentsOpen((value) => !value)}
            role="button"
            tabIndex={0}
          >
            <ChevronRight
              size={15}
              className={`${styles.caret} ${
                archivedAgentsOpen ? styles.caretOpen : styles.caretClosed
              }`}
            />

            <Archive size={15} />
            <span>Archived Agents</span>
          </div>

          <div
            className={`${styles.subContent} ${
              archivedAgentsOpen ? styles.open : styles.closed
            }`}
          >
            {archivedAgents.length === 0 ? (
              <p className={styles.emptyText}>No archived Agents.</p>
            ) : (
              <ul className={styles.list}>
                {archivedAgents.map((agent) => (
                  <li key={agent.id}>
                    <SidebarCard
                      title={agent.name}
                      subtitle={getAgentSubtitle(agent)}
                      archived
                      pressed={pressedAgentId === agent.id}
                      leading={<Archive size={17} />}
                      action={
                        <button
                          type="button"
                          className={styles.manageButton}
                          aria-label={`Manage archived ${agent.name}`}
                          onClick={(event) => {
                            event.stopPropagation();

                            pressAgent(agent.id, onManageArchivedAgent);
                          }}
                        >
                          <Pencil size={15} />
                        </button>
                      }
                      onClick={() =>
                        pressAgent(agent.id, onManageArchivedAgent)
                      }
                    />
                  </li>
                ))}
              </ul>
            )}
          </div>
        </div>
      </div>
    </section>
  );
}

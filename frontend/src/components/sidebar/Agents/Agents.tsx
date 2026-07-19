import { useState } from "react";
import { Archive, Bot, ChevronRight, Pencil, ShieldCheck } from "lucide-react";
import type { Agent } from "../../../domains/agent/agent.types";
import styles from "./Agents.module.css";

type AgentsProps = {
  agents: Agent[];
  archivedAgents: Agent[];
  activeAgentId: string | null;
  loading: boolean;
  onSelectAgent: (agentId: string) => void;
  onManageAgent: (agentId: string) => void;
  onManageArchivedAgent: (agentId: string) => void;
};

export function Agents({
  agents,
  archivedAgents,
  activeAgentId,
  loading,
  onSelectAgent,
  onManageAgent,
  onManageArchivedAgent,
}: AgentsProps) {
  const [archivedOpen, setArchivedOpen] = useState(false);

  return (
    <section className={styles.section}>
      <div className={styles.listViewport}>
        {loading ? (
          <div className={styles.empty}>Loading Agents...</div>
        ) : agents.length > 0 ? (
          <ul className={styles.list}>
            {agents.map((agent) => {
              const selected = agent.id === activeAgentId;

              return (
                <li key={agent.id} className={styles.rowWrap}>
                  <button
                    className={`${styles.row} ${selected ? styles.selected : ""}`}
                    type="button"
                    onClick={() => onSelectAgent(agent.id)}
                    title={`Use ${agent.name}`}
                  >
                    {agent.isBuiltIn ? (
                      <ShieldCheck size={12} strokeWidth={2} />
                    ) : (
                      <Bot size={12} strokeWidth={2} />
                    )}
                    <span>{agent.name}</span>
                  </button>

                  <button
                    className={styles.manageButton}
                    type="button"
                    onClick={() => onManageAgent(agent.id)}
                    aria-label={`Manage ${agent.name}`}
                    title="Manage Agent"
                  >
                    <Pencil size={11} strokeWidth={2.1} />
                  </button>
                </li>
              );
            })}
          </ul>
        ) : (
          <div className={styles.empty}>No Agents available.</div>
        )}
      </div>

      <button
        className={styles.archiveToggle}
        type="button"
        onClick={() => setArchivedOpen((current) => !current)}
      >
        <ChevronRight
          size={11}
          strokeWidth={2.2}
          className={archivedOpen ? styles.caretOpen : ""}
        />
        <Archive size={11} strokeWidth={2} />
        <span>Archived</span>
        <span className={styles.count}>{archivedAgents.length}</span>
      </button>

      {archivedOpen ? (
        <div className={styles.archivedList}>
          {archivedAgents.length > 0 ? (
            archivedAgents.map((agent) => (
              <button
                key={agent.id}
                className={styles.archivedRow}
                type="button"
                onClick={() => onManageArchivedAgent(agent.id)}
                title={agent.name}
              >
                <Archive size={11} strokeWidth={2} />
                <span>{agent.name}</span>
              </button>
            ))
          ) : (
            <div className={styles.archivedEmpty}>No archived Agents.</div>
          )}
        </div>
      ) : null}
    </section>
  );
}

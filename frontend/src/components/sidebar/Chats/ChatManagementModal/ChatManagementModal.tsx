import {
  type FormEvent,
  type MouseEvent,
  useEffect,
  useMemo,
  useState,
} from "react";
import {
  Archive,
  ArchiveRestore,
  BrainCircuit,
  CalendarDays,
  RotateCcw,
  Save,
  Trash2,
  X,
} from "lucide-react";
import { createPortal } from "react-dom";
import type {
  Chat,
  UpdateChatInput,
} from "../../../../domains/chat/chat.types";
import type {
  ContextCompilerConfig,
  ContextCompilerDefinition,
  ContextCompilerReference,
} from "../../../../domains/cognition/contextCompiler.types";
import styles from "./ChatManagementModal.module.css";

type ChatManagementModalProps = {
  chat: Chat;
  contextCompilers: ContextCompilerDefinition[];
  saving: boolean;
  archiving: boolean;
  restoring: boolean;
  deleting: boolean;
  onClose: () => void;
  onSave: (chatId: string, input: UpdateChatInput) => Promise<void>;
  onArchive: (chatId: string) => Promise<void>;
  onRestore: (chatId: string) => Promise<void>;
  onDelete: (chatId: string) => Promise<void>;
};

function formatDate(value: string): string {
  const date = new Date(value);

  if (Number.isNaN(date.getTime())) {
    return "Unknown";
  }

  return date.toLocaleString();
}

function createCompilerKey(reference: ContextCompilerReference): string {
  return `${reference.id}:v${reference.version}`;
}

function parseCompilerKey(value: string): ContextCompilerReference {
  const separatorIndex = value.lastIndexOf(":v");

  if (separatorIndex < 0) {
    throw new Error("Invalid Context Compiler selection.");
  }

  const id = value.slice(0, separatorIndex);
  const version = Number(value.slice(separatorIndex + 2));

  if (!id || !Number.isInteger(version) || version <= 0) {
    throw new Error("Invalid Context Compiler selection.");
  }

  return {
    id,
    version,
  };
}

function configsEqual(
  left: ContextCompilerConfig,
  right: ContextCompilerConfig,
): boolean {
  return JSON.stringify(left) === JSON.stringify(right);
}

export function ChatManagementModal({
  chat,
  contextCompilers,
  saving,
  archiving,
  restoring,
  deleting,
  onClose,
  onSave,
  onArchive,
  onRestore,
  onDelete,
}: ChatManagementModalProps) {
  const [title, setTitle] = useState(chat.title);

  const [compilerReference, setCompilerReference] = useState(
    chat.context.compiler,
  );

  const [compilerConfig, setCompilerConfig] = useState<ContextCompilerConfig>(
    chat.context.config,
  );

  const [confirmingArchive, setConfirmingArchive] = useState(false);
  const [confirmingDelete, setConfirmingDelete] = useState(false);

  const archived = chat.archivedAt !== null;
  const busy = saving || archiving || restoring || deleting;

  const selectedCompiler = useMemo(() => {
    const selectedKey = createCompilerKey(compilerReference);

    return (
      contextCompilers.find(
        (definition) =>
          createCompilerKey(definition.descriptor) === selectedKey,
      ) ?? null
    );
  }, [compilerReference, contextCompilers]);

  const contextChanged = useMemo(() => {
    return (
      createCompilerKey(compilerReference) !==
        createCompilerKey(chat.context.compiler) ||
      !configsEqual(compilerConfig, chat.context.config)
    );
  }, [
    chat.context.compiler,
    chat.context.config,
    compilerConfig,
    compilerReference,
  ]);

  const usingCompilerDefaults = useMemo(() => {
    if (!selectedCompiler) {
      return true;
    }

    return configsEqual(compilerConfig, selectedCompiler.defaultConfig);
  }, [compilerConfig, selectedCompiler]);

  const canSave = useMemo(() => {
    const normalizedTitle = title.trim();

    return (
      normalizedTitle.length > 0 &&
      normalizedTitle.length <= 120 &&
      (normalizedTitle !== chat.title || contextChanged) &&
      !busy
    );
  }, [busy, chat.title, contextChanged, title]);

  useEffect(() => {
    const previousOverflow = document.body.style.overflow;

    document.body.style.overflow = "hidden";

    function handleKeyDown(event: KeyboardEvent) {
      if (event.key === "Escape" && !busy) {
        onClose();
      }
    }

    window.addEventListener("keydown", handleKeyDown);

    return () => {
      document.body.style.overflow = previousOverflow;
      window.removeEventListener("keydown", handleKeyDown);
    };
  }, [busy, onClose]);

  async function handleSubmit(event: FormEvent<HTMLFormElement>) {
    event.preventDefault();

    if (!canSave) {
      return;
    }

    await onSave(chat.id, {
      title: title.trim(),
      context: {
        compiler: compilerReference,
        config: compilerConfig,
      },
    });
  }

  function handleBackdropClick(event: MouseEvent<HTMLDivElement>) {
    if (event.target === event.currentTarget && !busy) {
      onClose();
    }
  }

  function clearConfirmations() {
    setConfirmingArchive(false);
    setConfirmingDelete(false);
  }

  function handleCompilerChange(value: string) {
    const nextReference = parseCompilerKey(value);

    const nextDefinition = contextCompilers.find(
      (definition) =>
        createCompilerKey(definition.descriptor) ===
        createCompilerKey(nextReference),
    );

    if (!nextDefinition) {
      return;
    }

    setCompilerReference(nextReference);
    setCompilerConfig({
      ...nextDefinition.defaultConfig,
    });

    clearConfirmations();
  }

  function handleResetCompiler() {
    if (!selectedCompiler) {
      return;
    }

    setCompilerConfig({
      ...selectedCompiler.defaultConfig,
    });

    clearConfirmations();
  }

  function updateConfigValue(key: string, value: unknown) {
    setCompilerConfig((current) => ({
      ...current,
      [key]: value,
    }));

    clearConfirmations();
  }

  async function handleArchive() {
    if (!confirmingArchive) {
      setConfirmingArchive(true);
      return;
    }

    await onArchive(chat.id);
  }

  async function handleDelete() {
    if (!archived) {
      return;
    }

    if (!confirmingDelete) {
      setConfirmingDelete(true);
      return;
    }

    await onDelete(chat.id);
  }

  return createPortal(
    <div
      className={`appModalBackdrop ${styles.backdrop}`}
      onMouseDown={handleBackdropClick}
      role="presentation"
    >
      <section
        className={`appModalSurface ${styles.modal}`}
        role="dialog"
        aria-modal="true"
        aria-labelledby="chat-management-title"
      >
        <header className={styles.header}>
          <div className={styles.heading}>
            <div>
              <div className={styles.eyebrow}>
                {archived ? "Archived chat" : "Chat management"}
              </div>

              <h2 id="chat-management-title" className={styles.title}>
                {chat.title}
              </h2>

              <p className={styles.subtitle}>
                {archived
                  ? "Restore this conversation or permanently remove it."
                  : "Rename this conversation or tune its active context compiler."}
              </p>
            </div>
          </div>

          <button
            className={styles.closeButton}
            type="button"
            onClick={onClose}
            disabled={busy}
            aria-label="Close Chat management"
            title="Close"
          >
            <X size={18} strokeWidth={2.2} />
          </button>
        </header>

        <form className={styles.form} onSubmit={handleSubmit}>
          <label className={styles.field}>
            <span className={styles.label}>Chat name</span>

            <input
              className={styles.input}
              value={title}
              onChange={(event) => {
                setTitle(event.target.value);
                clearConfirmations();
              }}
              maxLength={120}
              disabled={busy}
              autoComplete="off"
            />

            <span className={styles.characterCount}>{title.length}/120</span>
          </label>

          {!archived ? (
            <section className={styles.compilerSection}>
              <div className={styles.compilerHeading}>
                <BrainCircuit size={18} strokeWidth={2.1} />

                <div>
                  <h3>Context Compiler</h3>
                  <p>Choose how this Chat selects and budgets model context.</p>
                </div>
              </div>

              <div className={styles.compilerSelectRow}>
                <label className={styles.field}>
                  <span className={styles.label}>Compiler</span>

                  <select
                    className={styles.select}
                    value={createCompilerKey(compilerReference)}
                    onChange={(event) =>
                      handleCompilerChange(event.target.value)
                    }
                    disabled={busy || contextCompilers.length === 0}
                  >
                    {contextCompilers.map((definition) => {
                      const key = createCompilerKey(definition.descriptor);

                      return (
                        <option key={key} value={key}>
                          {definition.descriptor.name} v
                          {definition.descriptor.version}
                        </option>
                      );
                    })}
                  </select>
                </label>

                <button
                  className={styles.resetCompilerButton}
                  type="button"
                  onClick={handleResetCompiler}
                  disabled={busy || !selectedCompiler || usingCompilerDefaults}
                >
                  <RotateCcw size={14} strokeWidth={2.2} />
                  <span>Reset to defaults</span>
                </button>
              </div>

              {selectedCompiler ? (
                <div className={styles.compilerFields}>
                  {selectedCompiler.configFields.map((field) => {
                    const value = compilerConfig[field.key];

                    if (field.type === "integer") {
                      return (
                        <label className={styles.field} key={field.key}>
                          <span className={styles.label}>{field.label}</span>

                          <input
                            className={styles.numberInput}
                            type="number"
                            min={field.minimum}
                            max={field.maximum}
                            step={1}
                            value={
                              typeof value === "number" ? value : field.minimum
                            }
                            onChange={(event) =>
                              updateConfigValue(
                                field.key,
                                Number(event.target.value),
                              )
                            }
                            disabled={busy}
                          />

                          <span className={styles.fieldDescription}>
                            {field.description}
                          </span>
                        </label>
                      );
                    }

                    if (field.type === "boolean") {
                      return (
                        <label className={styles.toggleField} key={field.key}>
                          <input
                            type="checkbox"
                            checked={value === true}
                            onChange={(event) =>
                              updateConfigValue(field.key, event.target.checked)
                            }
                            disabled={busy}
                          />

                          <span>
                            <strong>{field.label}</strong>
                            <small>{field.description}</small>
                          </span>
                        </label>
                      );
                    }

                    return (
                      <label className={styles.field} key={field.key}>
                        <span className={styles.label}>{field.label}</span>

                        <select
                          className={styles.select}
                          value={
                            typeof value === "string"
                              ? value
                              : field.options[0]?.value
                          }
                          onChange={(event) =>
                            updateConfigValue(field.key, event.target.value)
                          }
                          disabled={busy}
                        >
                          {field.options.map((option) => (
                            <option key={option.value} value={option.value}>
                              {option.label}
                            </option>
                          ))}
                        </select>

                        <span className={styles.fieldDescription}>
                          {field.description}
                        </span>
                      </label>
                    );
                  })}
                </div>
              ) : (
                <p className={styles.compilerError}>
                  This Chat references a compiler that is not currently
                  registered.
                </p>
              )}
            </section>
          ) : null}

          <div className={styles.detailsGrid}>
            <div className={styles.detailCard}>
              <CalendarDays
                className={styles.detailIcon}
                size={17}
                strokeWidth={2}
              />

              <div className={styles.detailContent}>
                <span className={styles.detailLabel}>Created</span>
                <span className={styles.detailValue}>
                  {formatDate(chat.createdAt)}
                </span>
              </div>
            </div>

            <div className={styles.detailCard}>
              <CalendarDays
                className={styles.detailIcon}
                size={17}
                strokeWidth={2}
              />

              <div className={styles.detailContent}>
                <span className={styles.detailLabel}>Last activity</span>
                <span className={styles.detailValue}>
                  {formatDate(chat.updatedAt)}
                </span>
              </div>
            </div>
          </div>

          {confirmingArchive ? (
            <div className={styles.warningNotice}>
              <strong>Archive this chat?</strong>
              <span>
                It will leave the active list but can be restored later.
              </span>
            </div>
          ) : null}

          {confirmingDelete ? (
            <div className={`${styles.warningNotice} ${styles.deleteNotice}`}>
              <strong>Delete permanently?</strong>
              <span>
                The conversation and every message inside it will be removed.
                This cannot be undone.
              </span>
            </div>
          ) : null}

          <footer className={styles.footer}>
            <div className={styles.destructiveActions}>
              {archived ? (
                <>
                  <button
                    className={styles.restoreButton}
                    type="button"
                    onClick={() => void onRestore(chat.id)}
                    disabled={busy}
                  >
                    <ArchiveRestore size={16} strokeWidth={2.2} />
                    <span>{restoring ? "Restoring..." : "Restore"}</span>
                  </button>

                  <button
                    className={`${styles.deleteButton} ${
                      confirmingDelete ? styles.deleteButtonConfirming : ""
                    }`}
                    type="button"
                    onClick={() => void handleDelete()}
                    disabled={busy}
                  >
                    <Trash2 size={16} strokeWidth={2.2} />

                    <span>
                      {deleting
                        ? "Deleting..."
                        : confirmingDelete
                          ? "Confirm Delete"
                          : "Delete Permanently"}
                    </span>
                  </button>
                </>
              ) : (
                <button
                  className={`${styles.archiveButton} ${
                    confirmingArchive ? styles.archiveButtonConfirming : ""
                  }`}
                  type="button"
                  onClick={() => void handleArchive()}
                  disabled={busy}
                >
                  <Archive size={16} strokeWidth={2.2} />

                  <span>
                    {archiving
                      ? "Archiving..."
                      : confirmingArchive
                        ? "Confirm Archive"
                        : "Archive"}
                  </span>
                </button>
              )}
            </div>

            <div className={styles.primaryActions}>
              <button
                className={styles.cancelButton}
                type="button"
                onClick={onClose}
                disabled={busy}
              >
                Cancel
              </button>

              <button
                className={styles.saveButton}
                type="submit"
                disabled={!canSave}
              >
                <Save size={16} strokeWidth={2.2} />
                <span>{saving ? "Saving..." : "Save"}</span>
              </button>
            </div>
          </footer>
        </form>
      </section>
    </div>,
    document.body,
  );
}

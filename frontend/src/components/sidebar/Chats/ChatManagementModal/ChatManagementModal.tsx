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
  CalendarDays,
  MessageSquareText,
  Save,
  Trash2,
  X,
} from "lucide-react";
import { createPortal } from "react-dom";
import type { Chat } from "../../../../domains/chat/chat.types";
import styles from "./ChatManagementModal.module.css";

type ChatManagementModalProps = {
  chat: Chat;
  saving: boolean;
  archiving: boolean;
  restoring: boolean;
  deleting: boolean;
  onClose: () => void;
  onSave: (chatId: string, title: string) => Promise<void>;
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

export function ChatManagementModal({
  chat,
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
  const [confirmingArchive, setConfirmingArchive] = useState(false);
  const [confirmingDelete, setConfirmingDelete] = useState(false);

  const archived = chat.archivedAt !== null;
  const busy = saving || archiving || restoring || deleting;

  const canSave = useMemo(() => {
    const normalizedTitle = title.trim();

    return (
      normalizedTitle.length > 0 &&
      normalizedTitle.length <= 120 &&
      normalizedTitle !== chat.title &&
      !busy
    );
  }, [busy, chat.title, title]);

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

    await onSave(chat.id, title.trim());
  }

  function handleBackdropClick(event: MouseEvent<HTMLDivElement>) {
    if (event.target === event.currentTarget && !busy) {
      onClose();
    }
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
            <span className={styles.headingIcon}>
              <MessageSquareText size={20} strokeWidth={2.1} />
            </span>

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
                  : "Rename or archive this conversation."}
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
                setConfirmingArchive(false);
                setConfirmingDelete(false);
              }}
              maxLength={120}
              disabled={busy}
              autoComplete="off"
            />

            <span className={styles.characterCount}>{title.length}/120</span>
          </label>

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

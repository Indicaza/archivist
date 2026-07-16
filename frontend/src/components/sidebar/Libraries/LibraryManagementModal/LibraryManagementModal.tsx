import {
  Archive,
  ArchiveRestore,
  CalendarClock,
  FolderOpen,
  Save,
  X,
} from "lucide-react";
import { type FormEvent, type MouseEvent, useEffect, useState } from "react";
import { createPortal } from "react-dom";
import type { Library } from "../../../../domains/library/library.types";
import styles from "./LibraryManagementModal.module.css";

type LibraryManagementModalProps = {
  library: Library;
  saving: boolean;
  archiving: boolean;
  restoring: boolean;
  onClose: () => void;
  onSave: (
    libraryId: string,
    input: {
      name: string;
      description: string | null;
    },
  ) => Promise<void>;
  onArchive: (libraryId: string) => Promise<void>;
  onRestore: (libraryId: string) => Promise<void>;
};

function formatTimestamp(timestamp: string): string {
  return new Intl.DateTimeFormat(undefined, {
    dateStyle: "medium",
    timeStyle: "short",
  }).format(new Date(timestamp));
}

export function LibraryManagementModal({
  library,
  saving,
  archiving,
  restoring,
  onClose,
  onSave,
  onArchive,
  onRestore,
}: LibraryManagementModalProps) {
  const [name, setName] = useState(library.name);
  const [description, setDescription] = useState(library.description ?? "");
  const [confirmingArchive, setConfirmingArchive] = useState(false);

  const archived = library.archivedAt !== null;
  const busy = saving || archiving || restoring;

  const trimmedName = name.trim();
  const trimmedDescription = description.trim();

  const changed =
    trimmedName !== library.name ||
    trimmedDescription !== (library.description ?? "");

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

    if (!trimmedName || !changed || busy) {
      return;
    }

    await onSave(library.id, {
      name: trimmedName,
      description: trimmedDescription || null,
    });
  }

  async function handleArchive() {
    if (!confirmingArchive) {
      setConfirmingArchive(true);
      return;
    }

    await onArchive(library.id);
  }

  function handleBackdropClick(event: MouseEvent<HTMLDivElement>) {
    if (event.target === event.currentTarget && !busy) {
      onClose();
    }
  }

  return createPortal(
    <div
      className={`appModalBackdrop ${styles.backdrop}`}
      role="presentation"
      onMouseDown={handleBackdropClick}
    >
      <section
        className={`appModalSurface ${styles.modal}`}
        role="dialog"
        aria-modal="true"
        aria-labelledby="library-management-title"
      >
        <header className={styles.header}>
          <div className={styles.heading}>
            <div className={styles.eyebrow}>
              {archived ? "Archived Library" : "Library management"}
            </div>

            <h2 id="library-management-title" className={styles.title}>
              {library.name}
            </h2>

            <p className={styles.subtitle}>
              {archived
                ? "Restore this Library or update its details."
                : "Update or archive this Library."}
            </p>
          </div>

          <button
            className={styles.closeButton}
            type="button"
            onClick={onClose}
            disabled={busy}
            aria-label="Close Library management"
            title="Close"
          >
            <X size={19} strokeWidth={2.2} />
          </button>
        </header>

        <form className={styles.form} onSubmit={handleSubmit}>
          <div className={styles.field}>
            <label className={styles.label} htmlFor="library-name">
              Library name
            </label>

            <input
              id="library-name"
              className={styles.input}
              value={name}
              onChange={(event) => {
                setName(event.target.value);
                setConfirmingArchive(false);
              }}
              maxLength={120}
              disabled={busy}
            />
          </div>

          <div className={styles.field}>
            <label className={styles.label} htmlFor="library-description">
              Description
            </label>

            <textarea
              id="library-description"
              className={styles.textarea}
              value={description}
              onChange={(event) => {
                setDescription(event.target.value);
                setConfirmingArchive(false);
              }}
              maxLength={500}
              rows={4}
              placeholder="What does this Library contain?"
              disabled={busy}
            />

            <div className={styles.characterCount}>
              {description.length} / 500
            </div>
          </div>

          <div className={styles.detailsGrid}>
            <div className={styles.detailCard}>
              <FolderOpen
                className={styles.detailIcon}
                size={17}
                strokeWidth={2.1}
              />

              <div className={styles.detailContent}>
                <span className={styles.detailLabel}>Root folder</span>

                <span className={styles.pathValue} title={library.rootPath}>
                  {library.rootPath}
                </span>
              </div>
            </div>

            <div className={styles.detailCard}>
              <CalendarClock
                className={styles.detailIcon}
                size={17}
                strokeWidth={2.1}
              />

              <div className={styles.detailContent}>
                <span className={styles.detailLabel}>Created</span>

                <span className={styles.detailValue}>
                  {formatTimestamp(library.createdAt)}
                </span>
              </div>
            </div>

            <div className={styles.detailCard}>
              <CalendarClock
                className={styles.detailIcon}
                size={17}
                strokeWidth={2.1}
              />

              <div className={styles.detailContent}>
                <span className={styles.detailLabel}>Last updated</span>

                <span className={styles.detailValue}>
                  {formatTimestamp(library.updatedAt)}
                </span>
              </div>
            </div>
          </div>

          {confirmingArchive ? (
            <div className={styles.archiveNotice}>
              <div className={styles.archiveNoticeTitle}>
                Archive this Library?
              </div>

              <div>
                This removes it from the active sidebar, but keeps its folder
                and Archivist history intact.
              </div>
            </div>
          ) : null}

          <footer className={styles.footer}>
            {archived ? (
              <button
                className={styles.restoreButton}
                type="button"
                onClick={() => void onRestore(library.id)}
                disabled={busy}
              >
                <ArchiveRestore size={17} strokeWidth={2.15} />

                {restoring ? "Restoring..." : "Restore Library"}
              </button>
            ) : (
              <button
                className={`${styles.archiveButton} ${
                  confirmingArchive ? styles.archiveButtonConfirming : ""
                }`}
                type="button"
                onClick={() => void handleArchive()}
                disabled={busy}
              >
                <Archive size={17} strokeWidth={2.15} />

                {archiving
                  ? "Archiving..."
                  : confirmingArchive
                    ? "Confirm archive"
                    : "Archive Library"}
              </button>
            )}

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
                disabled={!trimmedName || !changed || busy}
              >
                <Save size={17} strokeWidth={2.15} />

                {saving ? "Saving..." : "Save changes"}
              </button>
            </div>
          </footer>
        </form>
      </section>
    </div>,
    document.body,
  );
}

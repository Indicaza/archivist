// frontend/src/components/topbar/ActiveLibraryBadge/ActiveLibraryBadge.tsx
import { BookOpen } from "lucide-react";
import type { LibraryListItem } from "../../../domains/library/library.types";
import styles from "./ActiveLibraryBadge.module.css";

type ActiveLibraryBadgeProps = {
  selectedLibrary: LibraryListItem | null;
};

export function ActiveLibraryBadge({
  selectedLibrary,
}: ActiveLibraryBadgeProps) {
  return (
    <aside className={styles.badge} aria-label="Active Library">
      <span className={styles.iconWrap} aria-hidden>
        <BookOpen size={16} strokeWidth={2.2} />
      </span>

      <span className={styles.text}>
        <span className={styles.eyebrow}>Active Library</span>
        <span className={styles.name}>
          {selectedLibrary ? selectedLibrary.name : "No Library selected"}
        </span>
      </span>

      <span
        className={`${styles.statusDot} ${
          selectedLibrary ? styles[selectedLibrary.status] : styles.offline
        }`}
      />
    </aside>
  );
}

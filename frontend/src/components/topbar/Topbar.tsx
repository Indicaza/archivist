import type { LibraryListItem } from "../../domains/library/library.types";
import { ActiveLibraryBadge } from "./ActiveLibraryBadge/ActiveLibraryBadge";
import styles from "./Topbar.module.css";

type TopbarProps = {
  selectedLibrary: LibraryListItem | null;
};

export function Topbar({ selectedLibrary }: TopbarProps) {
  return (
    <>
      <header className={styles.topbar}>
        <div className={styles.topbarContainer}>
          <div className={styles.topbarLeft}>
            <h1 className={styles.topbarTitle}>Archivist</h1>
          </div>

          <div className={styles.topbarRight}>
            <button className={styles.userAvatar} aria-label="User profile" />
          </div>
        </div>
      </header>

      <ActiveLibraryBadge selectedLibrary={selectedLibrary} />
    </>
  );
}

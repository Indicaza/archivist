import { Logo } from "../logo/Logo";
import styles from "./Topbar.module.css";

export function Topbar() {
  return (
    <header className={styles.topbar}>
      <div className={styles.topbarContainer}>
        <div className={styles.topbarLeft}>
          <Logo size="md" />
          <h1 className={styles.topbarTitle}>Archivist</h1>
        </div>

        <div className={styles.topbarRight}>
          <button className={styles.userAvatar} aria-label="User profile" />
        </div>
      </div>
    </header>
  );
}

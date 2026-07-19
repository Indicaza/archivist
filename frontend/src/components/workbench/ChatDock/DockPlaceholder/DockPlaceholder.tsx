import type { ReactNode } from "react";
import styles from "./DockPlaceholder.module.css";

type DockPlaceholderProps = {
  icon: ReactNode;
  title: string;
  description: string;
};

export function DockPlaceholder({
  icon,
  title,
  description,
}: DockPlaceholderProps) {
  return (
    <div className={styles.placeholder}>
      <div className={styles.icon}>{icon}</div>
      <strong>{title}</strong>
      <span>{description}</span>
    </div>
  );
}

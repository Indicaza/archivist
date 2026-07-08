import type { ReactNode } from "react";
import styles from "./SidebarButton.module.css";

type SidebarButtonProps = {
  label: string;
  icon?: ReactNode;
  onClick?: () => void;
};

export function SidebarButton({ label, icon, onClick }: SidebarButtonProps) {
  return (
    <button className={styles.button} onClick={onClick} type="button">
      {icon ? <span className={styles.icon}>{icon}</span> : null}
      <span>{label}</span>
    </button>
  );
}

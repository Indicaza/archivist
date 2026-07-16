import type { ButtonHTMLAttributes, ReactNode } from "react";
import styles from "./SidebarCard.module.css";

type SidebarCardProps = {
  title: string;
  subtitle?: string;
  leading?: ReactNode;
  trailing?: ReactNode;
  action?: ReactNode;

  selected?: boolean;
  pressed?: boolean;
  archived?: boolean;
  interactive?: boolean;
  shimmerOnPress?: boolean;

  className?: string;
  contentClassName?: string;
} & Omit<ButtonHTMLAttributes<HTMLButtonElement>, "children" | "title">;

export function SidebarCard({
  title,
  subtitle,
  leading,
  trailing,
  action,

  selected = false,
  pressed = false,
  archived = false,
  interactive = true,
  shimmerOnPress = true,

  className = "",
  contentClassName = "",

  type = "button",
  ...buttonProps
}: SidebarCardProps) {
  const cardClassName = [
    styles.card,
    selected ? styles.selected : "",
    pressed ? styles.pressed : "",
    archived ? styles.archived : "",
    !interactive ? styles.static : "",
    shimmerOnPress && pressed ? styles.shimmer : "",
    className,
  ]
    .filter(Boolean)
    .join(" ");

  const content = (
    <>
      {leading ? <span className={styles.leading}>{leading}</span> : null}

      <span className={`${styles.content} ${contentClassName}`}>
        <span className={styles.title}>{title}</span>

        {subtitle ? <span className={styles.subtitle}>{subtitle}</span> : null}
      </span>

      {trailing ? <span className={styles.trailing}>{trailing}</span> : null}
    </>
  );

  return (
    <li className={styles.item}>
      {interactive ? (
        <button {...buttonProps} className={cardClassName} type={type}>
          {content}
        </button>
      ) : (
        <div className={cardClassName}>{content}</div>
      )}

      {action ? <div className={styles.action}>{action}</div> : null}
    </li>
  );
}

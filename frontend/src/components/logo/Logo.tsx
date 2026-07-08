import type { CSSProperties } from "react";
import logoUrl from "../../assets/archivist-logo.png";
import styles from "./Logo.module.css";

type LogoProps = {
  size?: "sm" | "md";
};

export function Logo({ size = "md" }: LogoProps) {
  const maskStyle = {
    maskImage: `url(${logoUrl})`,
    WebkitMaskImage: `url(${logoUrl})`,
  } as CSSProperties;

  return (
    <span
      className={`${styles.logo} ${styles[size]}`}
      style={maskStyle}
      aria-hidden="true"
    />
  );
}

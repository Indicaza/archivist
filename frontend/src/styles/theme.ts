export const theme = {
  layout: {
    topbarHeight: "80px",
    sidebarWidth: "260px",
    inspectorWidth: "320px",
    contentMaxWidth: "920px",
    viewportMobile: "900px",
    zIndex: {
      sidebar: 25,
      topbar: 15,
      revealBtn: 30,
      inspector: 20,
      modal: 50,
      toast: 60,
    },
    radii: {
      sm: "8px",
      md: "12px",
      lg: "14px",
      xl: "18px",
      pill: "100px",
      round: "999px",
    },
    shadows: {
      soft: "2px 2px 10px 1px rgba(0,0,0,0.75)",
      panel: "0 12px 32px rgba(0,0,0,0.32)",
      insetThin: "inset 0 0 0 1px #141414",
      avatar: "3px 3px 3px rgba(1,1,1,0.95)",
      modal: "0 12px 30px rgba(0,0,0,0.4)",
      glow: "0 0 24px rgba(125, 92, 255, 0.16)",
    },
  },

  motion: {
    durations: {
      fast: "0.2s",
      base: "0.25s",
      slow: "0.35s",
    },
    easings: {
      standard: "ease",
      standardInOut: "cubic-bezier(0.4, 0, 0.2, 1)",
      bouncy: "cubic-bezier(0.68, -0.55, 0.27, 1.5)",
      springy: "cubic-bezier(0.25, 1.25, 0.5, 1)",
    },
    anim: {
      popStartY: "50px",
      popOvershootY: "-10px",
      popStartScale: "0.95",
      popOvershootScale: "1.025",
    },
  },

  colors: {
    app: {
      bg: "#171717",
      bgDeep: "#0f1115",
      text: "#e5e7eb",
      mutedText: "#94a3b8",
      border: "rgba(83,83,83,0.75)",
      accent: "#8b5cf6",
      accentSoft: "rgba(139, 92, 246, 0.16)",
      success: "#22c55e",
      warning: "#f59e0b",
      danger: "#ef4444",
    },

    topbar: {
      bg: "#181818",
      border: "rgba(83,83,83,0.75)",
      title: "#ffffff",
      subtitle: "#94a3b8",
    },

    sidebar: {
      bg: "#181818",
      border: "rgba(83,83,83,0.75)",
      hover: "#111827",
      active: "#0b1222",
      title: "#94a3b8",
      dotOnline: "#22c55e",
      revealHover: "#273244",
      btnBorder: "#334155",
    },

    workspace: {
      bg: "#212121",
      panelBg: "#181818",
      panelBorder: "rgba(83,83,83,0.75)",
      emptyText: "#94a3b8",
    },

    artifactStream: {
      systemBg: "#1f2937",
      systemText: "#cbd5e1",
      cardBg: "#20242c",
      cardBorder: "#101010",
      cardShadow: "2px 2px 10px 1px rgba(0,0,0,0.65)",
      sourceBg: "#111827",
      sourceBorder: "#334155",
    },

    messages: {
      userBg: "#1d4ed8",
      userText: "#e6eeff",
      assistantBg: "#0f766e",
      assistantText: "#eafffa",
      systemBg: "#1f2937",
      systemText: "#cbd5e1",
    },

    composer: {
      bg: "#303030",
      ring: "#1f2937",
      ringFocus: "#334155",
      inputText: "#e5e7eb",
      placeholder: "#94a3b8",
      sendBg: "#2563eb",
      sendBgHover: "#1d4ed8",
    },

    inspector: {
      bg: "#181818",
      border: "rgba(83,83,83,0.75)",
      title: "#ffffff",
      text: "#cbd5e1",
      mutedText: "#94a3b8",
      chipBg: "#111827",
      chipBorder: "#334155",
    },

    modal: {
      bg: "#ffffff",
      text: "#0f172a",
    },
  },

  topbar: {
    paddingX: "42px",
    titleSize: "28px",
    subtitleSize: "13px",
  },

  sidebar: {
    itemHeight: "40px",
    itemGap: "10px",
    itemRadius: "8px",
    collapseBtnSize: "42px",
    revealBtnSize: "42px",
  },

  workspace: {
    gap: "10px",
    padding: "16px",
  },

  artifactStream: {
    gap: "12px",
    padding: "16px",
    cardPadding: "14px 18px",
    cardRadius: "14px",
  },

  messages: {
    maxWidth: "760px",
    relativeMax: "85%",
    radius: "14px",
    padding: "12px 22px",
    tailSize: "15px",
    tailWidth: "10px",
  },

  composer: {
    maxHeight: "240px",
    paddingX: "16px",
    paddingRightReserve: "56px",
    pillRadius: "100px",
    grownRadius: "14px",
    sendSize: "42px",
    sendFontSize: "1.1rem",
  },

  inspector: {
    padding: "16px",
    sectionGap: "14px",
    chipRadius: "999px",
  },
} as const;

export function applyTheme(t: typeof theme = theme): void {
  const root = document.documentElement;

  set("--topbar-height", t.layout.topbarHeight);
  set("--sidebar-width", t.layout.sidebarWidth);
  set("--inspector-width", t.layout.inspectorWidth);
  set("--content-max-width", t.layout.contentMaxWidth);
  set("--viewport-mobile", t.layout.viewportMobile);

  set("--z-sidebar", t.layout.zIndex.sidebar);
  set("--z-topbar", t.layout.zIndex.topbar);
  set("--z-reveal", t.layout.zIndex.revealBtn);
  set("--z-inspector", t.layout.zIndex.inspector);
  set("--z-modal", t.layout.zIndex.modal);
  set("--z-toast", t.layout.zIndex.toast);

  set("--r-sm", t.layout.radii.sm);
  set("--r-md", t.layout.radii.md);
  set("--r-lg", t.layout.radii.lg);
  set("--r-xl", t.layout.radii.xl);
  set("--r-pill", t.layout.radii.pill);
  set("--r-round", t.layout.radii.round);

  set("--shadow-soft", t.layout.shadows.soft);
  set("--shadow-panel", t.layout.shadows.panel);
  set("--shadow-inset-thin", t.layout.shadows.insetThin);
  set("--shadow-avatar", t.layout.shadows.avatar);
  set("--shadow-modal", t.layout.shadows.modal);
  set("--shadow-glow", t.layout.shadows.glow);

  set("--dur-fast", t.motion.durations.fast);
  set("--dur-base", t.motion.durations.base);
  set("--dur-slow", t.motion.durations.slow);

  set("--ease-standard", t.motion.easings.standard);
  set("--ease-standard-inout", t.motion.easings.standardInOut);
  set("--ease-bouncy", t.motion.easings.bouncy);
  set("--ease-springy", t.motion.easings.springy);

  set("--anim-pop-start-y", t.motion.anim.popStartY);
  set("--anim-pop-overshoot-y", t.motion.anim.popOvershootY);
  set("--anim-pop-start-scale", t.motion.anim.popStartScale);
  set("--anim-pop-overshoot-scale", t.motion.anim.popOvershootScale);

  setGroup("--app", t.colors.app);
  setGroup("--topbar", t.colors.topbar);
  setGroup("--sidebar", t.colors.sidebar);
  setGroup("--workspace", t.colors.workspace);
  setGroup("--artifact", t.colors.artifactStream);
  setGroup("--message", t.colors.messages);
  setGroup("--composer", t.colors.composer);
  setGroup("--inspector", t.colors.inspector);
  setGroup("--modal", t.colors.modal);

  setGroup("--topbar", {
    paddingX: t.topbar.paddingX,
    titleSize: t.topbar.titleSize,
    subtitleSize: t.topbar.subtitleSize,
  });

  setGroup("--sidebar", {
    itemHeight: t.sidebar.itemHeight,
    itemGap: t.sidebar.itemGap,
    itemRadius: t.sidebar.itemRadius,
    collapseBtnSize: t.sidebar.collapseBtnSize,
    revealBtnSize: t.sidebar.revealBtnSize,
  });

  setGroup("--workspace", {
    gap: t.workspace.gap,
    padding: t.workspace.padding,
  });

  setGroup("--artifact", {
    gap: t.artifactStream.gap,
    padding: t.artifactStream.padding,
    cardPadding: t.artifactStream.cardPadding,
    cardRadius: t.artifactStream.cardRadius,
  });

  setGroup("--message", {
    maxWidth: t.messages.maxWidth,
    relativeMax: t.messages.relativeMax,
    radius: t.messages.radius,
    padding: t.messages.padding,
    tailSize: t.messages.tailSize,
    tailWidth: t.messages.tailWidth,
  });

  setGroup("--composer", {
    maxHeight: t.composer.maxHeight,
    paddingX: t.composer.paddingX,
    paddingRightReserve: t.composer.paddingRightReserve,
    pillRadius: t.composer.pillRadius,
    grownRadius: t.composer.grownRadius,
    sendSize: t.composer.sendSize,
    sendFontSize: t.composer.sendFontSize,
  });

  setGroup("--inspector", {
    padding: t.inspector.padding,
    sectionGap: t.inspector.sectionGap,
    chipRadius: t.inspector.chipRadius,
  });

  function set(name: string, value: string | number): void {
    root.style.setProperty(name, String(value));
  }

  function setGroup(
    prefix: string,
    obj: Record<string, string | number>,
  ): void {
    Object.entries(obj).forEach(([k, v]) => {
      const kebab = k.replace(/[A-Z]/g, (m) => "-" + m.toLowerCase());
      root.style.setProperty(`${prefix}-${kebab}`, String(v));
    });
  }
}

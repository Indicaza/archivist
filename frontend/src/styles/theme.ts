// frontend/src/styles/theme.ts

export const theme = {
  layout: {
    topbarHeight: "75px",
    sidebarWidth: "300px",
    sidebarPanelWidth: "300px",
    inspectorWidth: "320px",
    contentMaxWidth: "920px",
    viewportMobile: "900px",

    zIndex: {
      sidebar: 10,
      topbar: 30,
      revealBtn: 20,
      inspector: 20,
      modal: 50,
      toast: 60,
      composer: 12,
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
      soft: "0 10px 26px rgba(0, 0, 0, 0.46)",
      panel: "0 18px 46px rgba(0, 0, 0, 0.44)",
      card: "0 16px 34px rgba(0, 0, 0, 0.48)",
      cardLift: "0 22px 52px rgba(0, 0, 0, 0.58)",
      insetThin: "inset 0 0 0 1px rgba(255, 255, 255, 0.035)",
      avatar: "0 8px 20px rgba(0, 0, 0, 0.48)",
      modal: "0 18px 44px rgba(0, 0, 0, 0.52)",
      glow: "0 0 24px rgba(143, 122, 223, 0.14)",
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
      bouncy: "cubic-bezier(0.22, 1.18, 0.36, 1)",
      springy: "cubic-bezier(0.25, 1.08, 0.5, 1)",
    },

    anim: {
      popStartY: "10px",
      popOvershootY: "-2px",
      popStartScale: "0.99",
      popOvershootScale: "1.006",
    },

    modal: {
      backdropDuration: "160ms",
      enterDuration: "320ms",
      startY: "20px",
      overshootY: "-3px",
      startScale: "0.88",
      overshootScale: "1.025",
    },
  },

  colors: {
    app: {
      bg: "#11100e",
      bgDeep: "#090908",
      text: "#eee8dc",
      mutedText: "#a79d8e",
      border: "rgba(214, 198, 170, 0.16)",
      accent: "#8f7adf",
      accentSoft: "rgba(143, 122, 223, 0.13)",
      success: "#7fa878",
      warning: "#c49a5a",
      danger: "#c85f5f",
    },

    topbar: {
      bg: "#12110f",
      border: "rgba(214, 198, 170, 0.14)",
      title: "#f6efe4",
      subtitle: "#a79d8e",
    },

    sidebar: {
      bg: "#100f0d",
      border: "rgba(214, 198, 170, 0.12)",
      hover: "rgba(246, 239, 228, 0.045)",
      active: "rgba(143, 122, 223, 0.12)",
      title: "#a79d8e",
      dotOnline: "#7fa878",
      revealHover: "rgba(143, 122, 223, 0.14)",
      btnBorder: "rgba(214, 198, 170, 0.22)",
    },

    workspace: {
      bg: "#151410",
      bgDeep: "#0d0c0a",
      panelBg: "rgba(34, 31, 26, 0.82)",
      panelBorder: "rgba(214, 198, 170, 0.16)",
      emptyText: "#a79d8e",
    },

    artifactStream: {
      systemBg: "rgba(35, 32, 27, 0.78)",
      systemText: "#d0c5b4",
      cardBg: "rgba(34, 31, 26, 0.86)",
      cardBgLifted: "rgba(42, 38, 31, 0.9)",
      cardBorder: "rgba(214, 198, 170, 0.14)",
      cardShadow: "0 16px 34px rgba(0, 0, 0, 0.48)",
      sourceBg: "rgba(18, 17, 15, 0.74)",
      sourceBorder: "rgba(196, 154, 90, 0.24)",
    },

    messages: {
      userBg: "rgba(24, 23, 20, 0.82)",
      userText: "#eee8dc",
      userBorder: "rgba(214, 198, 170, 0.16)",
      assistantBg: "rgba(38, 35, 29, 0.9)",
      assistantText: "#f3eadb",
      assistantBorder: "rgba(214, 198, 170, 0.18)",
      systemBg: "rgba(35, 32, 27, 0.7)",
      systemText: "#d0c5b4",
    },

    composer: {
      bg: "rgba(29, 27, 23, 0.96)",
      ring: "rgba(214, 198, 170, 0.18)",
      ringFocus: "rgba(143, 122, 223, 0.45)",
      inputText: "#eee8dc",
      placeholder: "#918779",
      sendBg: "#6f5bb8",
      sendBgHover: "#8f7adf",
    },

    inspector: {
      bg: "#100f0d",
      border: "rgba(214, 198, 170, 0.12)",
      title: "#f6efe4",
      text: "#d0c5b4",
      mutedText: "#a79d8e",
      chipBg: "rgba(246, 239, 228, 0.045)",
      chipBorder: "rgba(214, 198, 170, 0.16)",
    },

    modal: {
      overlay: "rgba(5, 5, 4, 0.72)",
      bg: "rgba(24, 22, 18, 0.98)",
      surface: "rgba(34, 31, 26, 0.9)",
      surfaceHover: "rgba(42, 38, 31, 0.94)",
      text: "#f3eadb",
      mutedText: "#a79d8e",
      border: "rgba(214, 198, 170, 0.16)",
      inputBg: "rgba(13, 12, 10, 0.72)",
      dangerSoft: "rgba(200, 95, 95, 0.12)",
      dangerBorder: "rgba(200, 95, 95, 0.3)",
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

  sidebarCard: {
    minHeight: "58px",
    gap: "10px",

    paddingX: "8px",
    paddingY: "8px",
    actionReserve: "38px",
    radius: "10px",

    actionTop: "7px",
    actionRight: "7px",

    enterDuration: "180ms",
    motionDuration: "260ms",
    colorDuration: "180ms",
    shadowDuration: "220ms",
    pressDuration: "80ms",

    motionEasing: "cubic-bezier(0.18, 1.12, 0.32, 1)",

    hoverX: "4px",
    hoverScale: "1.018",

    neighborX: "2px",
    neighborScale: "1.01",

    pressScale: "0.99",

    leadingHoverScale: "1.09",
    leadingHoverRotate: "-2deg",
    contentHoverX: "1px",

    hoverBg: "rgba(255, 255, 255, 0.045)",
    hoverBorder: "rgba(214, 198, 170, 0.14)",
    hoverShadow: "0 8px 18px rgba(0, 0, 0, 0.22)",

    neighborBg: "rgba(255, 255, 255, 0.024)",
    neighborBorder: "rgba(214, 198, 170, 0.075)",

    selectedBg:
      "linear-gradient(90deg, rgba(143, 122, 223, 0.11), transparent 64%), rgba(255, 255, 255, 0.035)",

    selectedBorder: "rgba(143, 122, 223, 0.23)",

    selectedShadow:
      "0 8px 20px rgba(0, 0, 0, 0.24), inset 0 0 0 1px rgba(143, 122, 223, 0.07)",

    archivedOpacity: "0.72",

    shimmerWidth: "22%",
    shimmerOpacity: "0.82",
    shimmerDuration: "270ms",
    shimmerSkew: "-24deg",

    shimmerAccent: "rgba(143, 122, 223, 0.38)",
    shimmerHighlight: "rgba(235, 245, 255, 0.68)",
    shimmerWarm: "rgba(196, 154, 90, 0.3)",
  },

  workspace: {
    gap: "10px",
    padding: "16px",
  },

  artifactStream: {
    gap: "12px",
    padding: "16px",
    cardPadding: "14px 18px",
    cardRadius: "16px",
  },

  messages: {
    maxWidth: "760px",
    relativeMax: "92%",
    radius: "16px",
    padding: "14px 18px",
    tailSize: "0px",
    tailWidth: "0px",
  },

  composer: {
    maxHeight: "240px",
    paddingX: "16px",
    paddingRightReserve: "56px",
    pillRadius: "100px",
    grownRadius: "16px",
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
  set("--sidebar-panel-width", t.layout.sidebarPanelWidth);
  set("--inspector-width", t.layout.inspectorWidth);
  set("--content-max-width", t.layout.contentMaxWidth);
  set("--viewport-mobile", t.layout.viewportMobile);

  set("--z-sidebar", t.layout.zIndex.sidebar);
  set("--z-topbar", t.layout.zIndex.topbar);
  set("--z-reveal", t.layout.zIndex.revealBtn);
  set("--z-inspector", t.layout.zIndex.inspector);
  set("--z-modal", t.layout.zIndex.modal);
  set("--z-toast", t.layout.zIndex.toast);
  set("--z-composer", t.layout.zIndex.composer);

  set("--r-sm", t.layout.radii.sm);
  set("--r-md", t.layout.radii.md);
  set("--r-lg", t.layout.radii.lg);
  set("--r-xl", t.layout.radii.xl);
  set("--r-pill", t.layout.radii.pill);
  set("--r-round", t.layout.radii.round);

  set("--shadow-soft", t.layout.shadows.soft);
  set("--shadow-panel", t.layout.shadows.panel);
  set("--shadow-card", t.layout.shadows.card);
  set("--shadow-card-lift", t.layout.shadows.cardLift);
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

  set("--modal-backdrop-duration", t.motion.modal.backdropDuration);
  set("--modal-enter-duration", t.motion.modal.enterDuration);
  set("--modal-start-y", t.motion.modal.startY);
  set("--modal-overshoot-y", t.motion.modal.overshootY);
  set("--modal-start-scale", t.motion.modal.startScale);
  set("--modal-overshoot-scale", t.motion.modal.overshootScale);

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

  setGroup("--sidebar-card", {
    minHeight: t.sidebarCard.minHeight,
    gap: t.sidebarCard.gap,

    paddingX: t.sidebarCard.paddingX,
    paddingY: t.sidebarCard.paddingY,
    actionReserve: t.sidebarCard.actionReserve,
    radius: t.sidebarCard.radius,

    actionTop: t.sidebarCard.actionTop,
    actionRight: t.sidebarCard.actionRight,

    enterDuration: t.sidebarCard.enterDuration,
    motionDuration: t.sidebarCard.motionDuration,
    colorDuration: t.sidebarCard.colorDuration,
    shadowDuration: t.sidebarCard.shadowDuration,
    pressDuration: t.sidebarCard.pressDuration,

    motionEasing: t.sidebarCard.motionEasing,

    hoverX: t.sidebarCard.hoverX,
    hoverScale: t.sidebarCard.hoverScale,

    neighborX: t.sidebarCard.neighborX,
    neighborScale: t.sidebarCard.neighborScale,

    pressScale: t.sidebarCard.pressScale,

    leadingHoverScale: t.sidebarCard.leadingHoverScale,
    leadingHoverRotate: t.sidebarCard.leadingHoverRotate,
    contentHoverX: t.sidebarCard.contentHoverX,

    hoverBg: t.sidebarCard.hoverBg,
    hoverBorder: t.sidebarCard.hoverBorder,
    hoverShadow: t.sidebarCard.hoverShadow,

    neighborBg: t.sidebarCard.neighborBg,
    neighborBorder: t.sidebarCard.neighborBorder,

    selectedBg: t.sidebarCard.selectedBg,
    selectedBorder: t.sidebarCard.selectedBorder,
    selectedShadow: t.sidebarCard.selectedShadow,

    archivedOpacity: t.sidebarCard.archivedOpacity,

    shimmerWidth: t.sidebarCard.shimmerWidth,
    shimmerOpacity: t.sidebarCard.shimmerOpacity,
    shimmerDuration: t.sidebarCard.shimmerDuration,
    shimmerSkew: t.sidebarCard.shimmerSkew,

    shimmerAccent: t.sidebarCard.shimmerAccent,
    shimmerHighlight: t.sidebarCard.shimmerHighlight,
    shimmerWarm: t.sidebarCard.shimmerWarm,
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
    Object.entries(obj).forEach(([key, value]) => {
      const kebabCaseKey = key.replace(
        /[A-Z]/g,
        (character) => `-${character.toLowerCase()}`,
      );

      root.style.setProperty(`${prefix}-${kebabCaseKey}`, String(value));
    });
  }
}

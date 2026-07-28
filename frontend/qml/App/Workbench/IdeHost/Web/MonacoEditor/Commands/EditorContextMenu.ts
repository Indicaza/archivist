import type {
  ArchivistTheme,
} from "../../IdeHost.types.js";
import {
  type EditorCommandContext,
  EditorCommandRegistry,
  type ResolvedEditorCommand,
} from "./EditorCommandRegistry.js";
import "./EditorContextMenu.css";

interface EditorContextMenuCallbacks {
  reportStatus(message: string): void;
  restoreEditorFocus(): void;
}

export class EditorContextMenu {
  private readonly element = document.createElement("div");
  private currentContext: EditorCommandContext | null = null;

  constructor(
    private readonly registry: EditorCommandRegistry,
    private readonly callbacks: EditorContextMenuCallbacks,
  ) {
    this.element.className = "archivist-editor-context-menu";
    this.element.setAttribute("role", "menu");
    this.element.setAttribute("aria-label", "Editor actions");
    this.element.hidden = true;
    document.body.append(this.element);

    this.element.addEventListener("click", (event) => {
      const target = event.target;

      if (!(target instanceof Element)) {
        return;
      }

      const button = target.closest<HTMLButtonElement>(
        "button[data-command-id]",
      );

      if (!button || button.disabled) {
        return;
      }

      void this.invoke(String(button.dataset.commandId || ""));
    });

    this.element.addEventListener("keydown", (event) => {
      this.handleKeyDown(event);
    });

    this.element.addEventListener("contextmenu", (event) => {
      event.preventDefault();
    });

    window.addEventListener(
      "pointerdown",
      this.handleWindowPointerDown,
      true,
    );
    window.addEventListener("blur", this.handleWindowBlur);
    window.addEventListener("resize", this.handleWindowResize);
  }

  applyTheme(theme: ArchivistTheme): void {
    this.element.style.setProperty(
      "--editor-menu-bg",
      theme.controlSurfaceBg,
    );
    this.element.style.setProperty(
      "--editor-menu-border",
      theme.panelBorder,
    );
    this.element.style.setProperty(
      "--editor-menu-divider",
      theme.quietBorder,
    );
    this.element.style.setProperty(
      "--editor-menu-text",
      theme.appText,
    );
    this.element.style.setProperty(
      "--editor-menu-muted",
      theme.mutedText,
    );
    this.element.style.setProperty(
      "--editor-menu-hover",
      theme.hoverBg,
    );
    this.element.style.setProperty(
      "--editor-menu-active",
      theme.activeBg,
    );
    this.element.style.setProperty(
      "--editor-menu-accent",
      theme.accentBright,
    );
    this.element.style.setProperty(
      "--editor-menu-font-size",
      `${Math.max(11, theme.textControlSize - 1)}px`,
    );
  }

  show(
    clientX: number,
    clientY: number,
    context: EditorCommandContext,
  ): void {
    const commands = this.registry.resolve(context);

    if (commands.length === 0) {
      this.hide(false);
      return;
    }

    this.currentContext = context;
    this.render(commands);
    this.element.hidden = false;
    this.element.style.left = `${Math.max(0, clientX)}px`;
    this.element.style.top = `${Math.max(0, clientY)}px`;

    requestAnimationFrame(() => {
      this.clampToViewport();
      this.enabledButtons()[0]?.focus();
    });
  }

  hide(restoreEditorFocus = true): void {
    if (this.element.hidden) {
      return;
    }

    this.element.hidden = true;
    this.currentContext = null;

    if (restoreEditorFocus) {
      this.callbacks.restoreEditorFocus();
    }
  }

  dispose(): void {
    window.removeEventListener(
      "pointerdown",
      this.handleWindowPointerDown,
      true,
    );
    window.removeEventListener("blur", this.handleWindowBlur);
    window.removeEventListener("resize", this.handleWindowResize);
    this.element.remove();
    this.currentContext = null;
  }

  private render(commands: readonly ResolvedEditorCommand[]): void {
    this.element.replaceChildren();
    let previousGroup = "";

    for (const command of commands) {
      if (
        previousGroup
        && previousGroup !== command.definition.group
      ) {
        const separator = document.createElement("div");
        separator.className = "archivist-editor-context-menu__separator";
        separator.setAttribute("role", "separator");
        this.element.append(separator);
      }

      previousGroup = command.definition.group;

      const button = document.createElement("button");
      button.className = "archivist-editor-context-menu__item";
      button.type = "button";
      button.setAttribute("role", "menuitem");
      button.tabIndex = -1;
      button.disabled = !command.enabled;
      button.dataset.commandId = command.definition.id;

      const label = document.createElement("span");
      label.className = "archivist-editor-context-menu__label";
      label.textContent = command.definition.label;
      button.append(label);

      if (command.definition.shortcut) {
        const shortcut = document.createElement("span");
        shortcut.className = "archivist-editor-context-menu__shortcut";
        shortcut.textContent = command.definition.shortcut;
        button.append(shortcut);
      }

      this.element.append(button);
    }
  }

  private async invoke(commandId: string): Promise<void> {
    const context = this.currentContext;

    if (!context || !commandId) {
      return;
    }

    this.hide(false);
    this.callbacks.restoreEditorFocus();

    try {
      await this.registry.execute(commandId, context);
    } catch (error) {
      const message = error instanceof Error
        ? error.message
        : "The editor command failed.";
      console.error(`[Editor Command] ${message}`);
      this.callbacks.reportStatus(message);
    } finally {
      this.callbacks.restoreEditorFocus();
    }
  }

  private clampToViewport(): void {
    if (this.element.hidden) {
      return;
    }

    const margin = 6;
    const bounds = this.element.getBoundingClientRect();
    const requestedLeft = Number.parseFloat(this.element.style.left) || 0;
    const requestedTop = Number.parseFloat(this.element.style.top) || 0;
    const left = Math.min(
      Math.max(margin, requestedLeft),
      Math.max(margin, window.innerWidth - bounds.width - margin),
    );
    const top = Math.min(
      Math.max(margin, requestedTop),
      Math.max(margin, window.innerHeight - bounds.height - margin),
    );

    this.element.style.left = `${left}px`;
    this.element.style.top = `${top}px`;
  }

  private enabledButtons(): HTMLButtonElement[] {
    return [
      ...this.element.querySelectorAll<HTMLButtonElement>(
        "button[data-command-id]:not(:disabled)",
      ),
    ];
  }

  private handleKeyDown(event: KeyboardEvent): void {
    if (event.key === "Escape") {
      event.preventDefault();
      this.hide();
      return;
    }

    const buttons = this.enabledButtons();

    if (buttons.length === 0) {
      return;
    }

    const currentIndex = buttons.indexOf(
      document.activeElement as HTMLButtonElement,
    );
    let nextIndex = currentIndex;

    switch (event.key) {
      case "ArrowDown":
        nextIndex = currentIndex < 0
          ? 0
          : (currentIndex + 1) % buttons.length;
        break;
      case "ArrowUp":
        nextIndex = currentIndex < 0
          ? buttons.length - 1
          : (currentIndex - 1 + buttons.length)
            % buttons.length;
        break;
      case "Home":
        nextIndex = 0;
        break;
      case "End":
        nextIndex = buttons.length - 1;
        break;
      default:
        return;
    }

    event.preventDefault();
    buttons[nextIndex]?.focus();
  }

  private readonly handleWindowPointerDown = (
    event: PointerEvent,
  ): void => {
    const target = event.target;

    if (
      !this.element.hidden
      && target instanceof Node
      && !this.element.contains(target)
    ) {
      this.hide();
    }
  };

  private readonly handleWindowBlur = (): void => {
    this.hide(false);
  };

  private readonly handleWindowResize = (): void => {
    this.hide(false);
  };
}

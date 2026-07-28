import type * as Monaco from "monaco-editor/editor/editor.api";

export interface EditorCommandContext {
  editor: Monaco.editor.IStandaloneCodeEditor;
  model: Monaco.editor.ITextModel;
  position: Monaco.Position;
  selection: Monaco.Selection;
  readOnly: boolean;
}

export interface EditorCommandDefinition {
  id: string;
  label: string;
  group: string;
  groupOrder: number;
  order: number;
  shortcut?: string;
  isVisible?(context: EditorCommandContext): boolean;
  isEnabled?(context: EditorCommandContext): boolean;
  run(context: EditorCommandContext): void | Promise<void>;
}

export interface ResolvedEditorCommand {
  definition: EditorCommandDefinition;
  enabled: boolean;
}

export class EditorCommandRegistry {
  private readonly definitions = new Map<
    string,
    EditorCommandDefinition
  >();

  register(definition: EditorCommandDefinition): {
    dispose(): void;
  } {
    if (this.definitions.has(definition.id)) {
      throw new Error(
        `Editor command is already registered: ${definition.id}`,
      );
    }

    this.definitions.set(definition.id, definition);

    return {
      dispose: () => {
        if (this.definitions.get(definition.id) === definition) {
          this.definitions.delete(definition.id);
        }
      },
    };
  }

  resolve(
    context: EditorCommandContext,
  ): ResolvedEditorCommand[] {
    return [...this.definitions.values()]
      .filter((definition) =>
        definition.isVisible?.(context) !== false
      )
      .sort((left, right) =>
        left.groupOrder - right.groupOrder
        || left.group.localeCompare(right.group)
        || left.order - right.order
        || left.label.localeCompare(right.label)
      )
      .map((definition) => ({
        definition,
        enabled: definition.isEnabled?.(context) !== false,
      }));
  }

  async execute(
    commandId: string,
    context: EditorCommandContext,
  ): Promise<boolean> {
    const definition = this.definitions.get(commandId);

    if (
      !definition
      || definition.isVisible?.(context) === false
      || definition.isEnabled?.(context) === false
    ) {
      return false;
    }

    await definition.run(context);
    return true;
  }
}

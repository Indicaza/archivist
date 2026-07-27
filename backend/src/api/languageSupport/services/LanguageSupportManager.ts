import {
  getLanguageServerDefinition,
  getLanguageServerDefinitions,
} from "../registry/LanguageServerRegistry.js";
import type {
  LanguageServerAvailability,
  LanguageServerDefinition,
  ResolvedLanguageWorkspace,
} from "../types/LanguageSupportTypes.js";
import { resolveLanguageServerExecutable } from "./LanguageServerExecutableResolver.js";
import { resolveLanguageWorkspace } from "./LanguageWorkspaceResolver.js";

export class LanguageSupportManager {
  describeServers(input?: {
    libraryId?: string;
    workspaceRoot?: string;
    filePath?: string;
  }): LanguageServerAvailability[] {
    return getLanguageServerDefinitions().map(
      (definition) =>
        this.describeServer(definition, input),
    );
  }

  describeServerById(
    serverId: string,
    input?: {
      libraryId?: string;
      workspaceRoot?: string;
      filePath?: string;
    },
  ): LanguageServerAvailability | null {
    const definition =
      getLanguageServerDefinition(serverId);

    return definition
      ? this.describeServer(definition, input)
      : null;
  }

  resolveWorkspace(
    serverId: string,
    input: {
      libraryId: string;
      workspaceRoot?: string;
      filePath?: string;
    },
  ): ResolvedLanguageWorkspace | null {
    const definition =
      getLanguageServerDefinition(serverId);

    return definition
      ? resolveLanguageWorkspace(definition, input)
      : null;
  }

  private describeServer(
    definition: LanguageServerDefinition,
    input?: {
      libraryId?: string;
      workspaceRoot?: string;
      filePath?: string;
    },
  ): LanguageServerAvailability {
    if (!definition.enabledByDefault) {
      return {
        id: definition.id,
        displayName: definition.displayName,
        languageIds: definition.languageIds,
        state: "disabled",
        executablePath: null,
        workspaceRoot: null,
      };
    }

    let workspaceRoot =
      input?.workspaceRoot?.trim() || undefined;

    if (input?.libraryId) {
      workspaceRoot = resolveLanguageWorkspace(
        definition,
        {
          libraryId: input.libraryId,
          workspaceRoot,
          filePath: input.filePath,
        },
      ).workspaceRoot;
    }

    const executablePath =
      resolveLanguageServerExecutable(
        definition,
        workspaceRoot,
      );

    return {
      id: definition.id,
      displayName: definition.displayName,
      languageIds: definition.languageIds,
      state: executablePath
        ? "available"
        : "missing",
      executablePath,
      workspaceRoot: workspaceRoot ?? null,
    };
  }
}

export const languageSupportManager =
  new LanguageSupportManager();

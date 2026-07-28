type JsonObject = Record<string, unknown>;

const settingsByServerId: Readonly<Record<string, JsonObject>> = {
  html: {
    html: {
      format: {
        enable: true,
      },
      hover: {
        documentation: true,
        references: true,
      },
      validate: {
        scripts: true,
        styles: true,
      },
    },
  },
  css: {
    css: {
      validate: true,
    },
    scss: {
      validate: true,
    },
    less: {
      validate: true,
    },
  },
  json: {
    json: {
      format: {
        enable: true,
      },
      schemas: [],
      validate: {
        enable: true,
      },
    },
  },
  yaml: {
    yaml: {
      completion: true,
      format: {
        enable: true,
      },
      hover: true,
      schemas: {},
      validate: true,
    },
  },
  python: {
    python: {
      analysis: {
        autoSearchPaths: true,
        diagnosticMode: "workspace",
        useLibraryCodeForTypes: true,
      },
    },
  },
};

const initializationOptionsByServerId:
  Readonly<Record<string, JsonObject>> = {
    html: {
      provideFormatter: true,
    },
    css: {
      provideFormatter: true,
    },
    json: {
      provideFormatter: true,
    },
  };

function nestedValue(
  source: JsonObject,
  pathValue: string,
): unknown {
  if (!pathValue) {
    return source;
  }

  let current: unknown = source;

  for (const segment of pathValue.split(".")) {
    if (
      !current
      || typeof current !== "object"
      || Array.isArray(current)
    ) {
      return null;
    }

    current = (current as JsonObject)[segment];
  }

  return current ?? null;
}

export function languageServerSettings(
  serverId: string,
): JsonObject {
  return settingsByServerId[serverId] ?? {};
}

export function languageServerConfiguration(
  serverId: string,
  section: string,
): unknown {
  return nestedValue(
    languageServerSettings(serverId),
    section,
  );
}

export function languageServerInitializationOptions(
  serverId: string,
): JsonObject {
  return initializationOptionsByServerId[serverId] ?? {};
}

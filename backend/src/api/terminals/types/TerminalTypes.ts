export type TerminalSocketContext = {
  sessionId: string;
  collectionId: string;
  libraryId: string;
  cols: number;
  rows: number;
};

export type TerminalClientMessage =
  | {
      type: "input";
      data: string;
    }
  | {
      type: "resize";
      cols: number;
      rows: number;
    }
  | {
      type: "kill";
    }
  | {
      type: "ping";
      sentAt: number;
    };

export type TerminalServerMessage =
  | {
      type: "ready";
      sessionId: string;
      cwd: string;
      shell: string;
    }
  | {
      type: "output";
      data: string;
    }
  | {
      type: "exit";
      exitCode: number;
      signal: number;
    }
  | {
      type: "error";
      message: string;
    }
  | {
      type: "pong";
      sentAt: number;
    };

interface Window {
  qt?: {
    webChannelTransport: unknown;
  };
}

declare class QWebChannel {
  constructor(
    transport: unknown,
    callback: (
      channel: import("./IdeHost.types.js").WebChannelInstance
    ) => void,
  );
}

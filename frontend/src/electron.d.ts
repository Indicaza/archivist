export {};

declare global {
  interface Window {
    archivist?: {
      selectFolder: () => Promise<string | null>;
    };
  }
}

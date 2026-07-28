export {};

declare global {
  namespace JSX {
    interface Element {}

    interface IntrinsicElements {
      main: {
        className?: string;
        children?: unknown;
      };
      h1: {
        children?: unknown;
      };
      p: {
        children?: unknown;
      };
    }
  }
}

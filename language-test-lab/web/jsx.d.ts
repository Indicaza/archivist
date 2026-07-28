export {};

type TestIntrinsicElementProps = {
  [property: string]: unknown;
  children?: unknown;
  className?: string;
};

declare global {
  namespace JSX {
    interface Element {}

    interface IntrinsicElements {
      [elementName: string]: TestIntrinsicElementProps;
    }
  }
}

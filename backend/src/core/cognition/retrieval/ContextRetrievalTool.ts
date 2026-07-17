import type {
  ContextRetrievalInput,
  ContextRetrievalResult,
} from "./ContextRetrievalTypes.js";

export interface ContextRetrievalTool {
  readonly id: string;
  readonly name: string;

  search(input: ContextRetrievalInput): ContextRetrievalResult;
}

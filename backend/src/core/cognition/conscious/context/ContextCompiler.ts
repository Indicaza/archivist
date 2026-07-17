import type {
  CompileContextInput,
  CompiledContext,
  ContextCompilerConfig,
  ContextCompilerDescriptor,
} from "./ContextCompilerTypes.js";

export interface ContextCompiler<
  TConfig extends ContextCompilerConfig = ContextCompilerConfig,
> {
  readonly descriptor: ContextCompilerDescriptor;

  compile(input: CompileContextInput<TConfig>): CompiledContext;
}

import type { z } from "zod";
import type { ContextCompiler } from "./ContextCompiler.js";
import type {
  ContextCompilerConfig,
  ContextCompilerDescriptor,
} from "./ContextCompilerTypes.js";

export type ContextCompilerIntegerField = {
  type: "integer";
  key: string;
  label: string;
  description: string;
  minimum: number;
  maximum: number;
  step: number;
};

export type ContextCompilerBooleanField = {
  type: "boolean";
  key: string;
  label: string;
  description: string;
};

export type ContextCompilerSelectOption = {
  value: string;
  label: string;
};

export type ContextCompilerSelectField = {
  type: "select";
  key: string;
  label: string;
  description: string;
  options: ContextCompilerSelectOption[];
};

export type ContextCompilerConfigField =
  | ContextCompilerIntegerField
  | ContextCompilerBooleanField
  | ContextCompilerSelectField;

export type ContextCompilerDefinition<
  TConfig extends ContextCompilerConfig = ContextCompilerConfig,
> = {
  descriptor: ContextCompilerDescriptor;
  description: string;
  configSchema: z.ZodType<TConfig>;
  defaultConfig: TConfig;
  configFields: ContextCompilerConfigField[];
  compiler: ContextCompiler<TConfig>;
};

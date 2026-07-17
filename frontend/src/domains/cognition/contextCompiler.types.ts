export type ContextCompilerReference = {
  id: string;
  version: number;
};

export type ContextCompilerConfig = Record<string, unknown>;

export type ContextCompilerDescriptor = {
  id: string;
  version: number;
  name: string;
};

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

export type ContextCompilerDefinition = {
  descriptor: ContextCompilerDescriptor;
  description: string;
  defaultConfig: ContextCompilerConfig;
  configFields: ContextCompilerConfigField[];
};

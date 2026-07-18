import {
  Archive,
  ArchiveRestore,
  Bot,
  BrainCircuit,
  Copy,
  RotateCcw,
  Save,
  Settings2,
  Trash2,
  X,
} from "lucide-react";
import {
  type FormEvent,
  type MouseEvent,
  type ReactNode,
  useEffect,
  useMemo,
  useState,
} from "react";
import { createPortal } from "react-dom";
import type {
  Agent,
  AgentVerbosity,
  UpdateAgentInput,
} from "../../../../domains/agent/agent.types";
import type { ModelDefinition } from "../../../../domains/ai/ai.types";
import type {
  ContextCompilerConfig,
  ContextCompilerDefinition,
  ContextCompilerReference,
} from "../../../../domains/cognition/contextCompiler.types";
import styles from "./AgentManagementModal.module.css";

type AgentManagementModalProps = {
  agent: Agent;
  models: ModelDefinition[];
  contextCompilers: ContextCompilerDefinition[];
  saving: boolean;
  duplicating: boolean;
  archiving: boolean;
  restoring: boolean;
  deleting: boolean;
  onClose: () => void;
  onSave: (agentId: string, input: UpdateAgentInput) => Promise<void>;
  onDuplicate: (agentId: string) => Promise<void>;
  onArchive: (agentId: string) => Promise<void>;
  onRestore: (agentId: string) => Promise<void>;
  onDelete: (agentId: string) => Promise<void>;
};

type ListFieldProps = {
  label: string;
  value: string;
  disabled: boolean;
  onChange: (value: string) => void;
};

type NumberFieldProps = {
  label: string;
  value: string;
  min: number;
  max: number;
  step?: number | "any";
  disabled: boolean;
  help?: string;
  onChange: (value: string) => void;
  onBlur?: () => void;
};

function listToText(items: string[]): string {
  return items.join("\n");
}

function textToList(value: string): string[] {
  return value
    .split("\n")
    .map((item) => item.trim())
    .filter(Boolean);
}

function nullableNumber(value: string): number | null {
  if (!value.trim()) {
    return null;
  }

  const parsed = Number(value);

  return Number.isFinite(parsed) ? parsed : null;
}

function clampNumber(value: number, minimum: number, maximum: number): number {
  return Math.min(maximum, Math.max(minimum, value));
}

function snapNumberToStep(
  value: number,
  minimum: number,
  maximum: number,
  step: number,
): number {
  const clamped = clampNumber(value, minimum, maximum);

  if (!Number.isFinite(step) || step <= 0) {
    return clamped;
  }

  const snapped = minimum + Math.round((clamped - minimum) / step) * step;

  return clampNumber(snapped, minimum, maximum);
}

function createCompilerKey(reference: ContextCompilerReference): string {
  return `${reference.id}:v${reference.version}`;
}

function parseCompilerKey(value: string): ContextCompilerReference | null {
  const separatorIndex = value.lastIndexOf(":v");

  if (separatorIndex < 1) {
    return null;
  }

  const id = value.slice(0, separatorIndex);
  const version = Number(value.slice(separatorIndex + 2));

  if (!id || !Number.isInteger(version) || version <= 0) {
    return null;
  }

  return {
    id,
    version,
  };
}

function ListField({ label, value, disabled, onChange }: ListFieldProps) {
  return (
    <label className={styles.field}>
      <span className={styles.label}>{label}</span>

      <textarea
        className={styles.listEditor}
        value={value}
        onChange={(event) => onChange(event.target.value)}
        disabled={disabled}
        placeholder="One item per line"
      />

      <span className={styles.helpText}>One item per line.</span>
    </label>
  );
}

function NumberField({
  label,
  value,
  min,
  max,
  step = "any",
  disabled,
  help,
  onChange,
  onBlur,
}: NumberFieldProps) {
  return (
    <label className={styles.field}>
      <span className={styles.label}>{label}</span>

      <input
        className={styles.input}
        type="number"
        value={value}
        min={min}
        max={max}
        step={step}
        onChange={(event) => onChange(event.target.value)}
        onBlur={onBlur}
        disabled={disabled}
      />

      {help ? <span className={styles.helpText}>{help}</span> : null}
    </label>
  );
}

export function AgentManagementModal({
  agent,
  models,
  contextCompilers,
  saving,
  duplicating,
  archiving,
  restoring,
  deleting,
  onClose,
  onSave,
  onDuplicate,
  onArchive,
  onRestore,
  onDelete,
}: AgentManagementModalProps) {
  const [name, setName] = useState(agent.name);

  const [description, setDescription] = useState(agent.description ?? "");

  const [personality, setPersonality] = useState(
    agent.identity.personality ?? "",
  );

  const [temperament, setTemperament] = useState(
    agent.identity.temperament ?? "",
  );

  const [voice, setVoice] = useState(agent.identity.voice ?? "");

  const [backstory, setBackstory] = useState(agent.identity.backstory ?? "");

  const [jobTitle, setJobTitle] = useState(agent.profession.jobTitle ?? "");

  const [mission, setMission] = useState(agent.profession.mission ?? "");

  const [expertise, setExpertise] = useState(
    listToText(agent.profession.expertise),
  );

  const [responsibilities, setResponsibilities] = useState(
    listToText(agent.profession.responsibilities),
  );

  const [successCriteria, setSuccessCriteria] = useState(
    listToText(agent.profession.successCriteria),
  );

  const [limitations, setLimitations] = useState(
    listToText(agent.profession.limitations),
  );

  const [doctrine, setDoctrine] = useState(agent.doctrine ?? "");

  const [responseStyle, setResponseStyle] = useState(
    agent.outputContract.responseStyle ?? "",
  );

  const [verbosity, setVerbosity] = useState<AgentVerbosity>(
    agent.outputContract.verbosity,
  );

  const [formattingRules, setFormattingRules] = useState(
    listToText(agent.outputContract.formattingRules),
  );

  const [codeOutputPreferences, setCodeOutputPreferences] = useState(
    listToText(agent.outputContract.codeOutputPreferences),
  );

  const [citationRequirements, setCitationRequirements] = useState(
    agent.outputContract.citationRequirements ?? "",
  );

  const [followUpBehavior, setFollowUpBehavior] = useState(
    agent.outputContract.followUpBehavior ?? "",
  );

  const [systemInstructions, setSystemInstructions] = useState(
    agent.systemInstructions ?? "",
  );

  const [modelId, setModelId] = useState(agent.generation.model);

  const [temperature, setTemperature] = useState(
    agent.generation.temperature?.toString() ?? "",
  );

  const [topP, setTopP] = useState(agent.generation.topP?.toString() ?? "");

  const [maxOutputTokens, setMaxOutputTokens] = useState(
    agent.generation.maxOutputTokens?.toString() ?? "",
  );

  const [compilerReference, setCompilerReference] = useState(
    agent.context.compiler,
  );

  const [compilerConfig, setCompilerConfig] = useState<ContextCompilerConfig>({
    ...agent.context.config,
  });

  const [confirmingArchive, setConfirmingArchive] = useState(false);

  const [confirmingDelete, setConfirmingDelete] = useState(false);

  const archived = agent.archivedAt !== null;

  const busy = saving || duplicating || archiving || restoring || deleting;

  const selectedModel = useMemo(() => {
    return (
      models.find(
        (model) => model.provider === "openai" && model.modelId === modelId,
      ) ?? null
    );
  }, [modelId, models]);

  const selectedCompiler = useMemo(() => {
    const selectedKey = createCompilerKey(compilerReference);

    return (
      contextCompilers.find(
        (definition) =>
          createCompilerKey(definition.descriptor) === selectedKey,
      ) ?? null
    );
  }, [compilerReference, contextCompilers]);

  const generationConflict =
    Boolean(temperature.trim()) && Boolean(topP.trim());

  const canSave =
    !archived &&
    !busy &&
    name.trim().length > 0 &&
    name.trim().length <= 120 &&
    Boolean(selectedModel) &&
    Boolean(selectedCompiler) &&
    !generationConflict;

  useEffect(() => {
    const previousOverflow = document.body.style.overflow;

    document.body.style.overflow = "hidden";

    function handleKeyDown(event: KeyboardEvent) {
      if (event.key === "Escape" && !busy) {
        onClose();
      }
    }

    window.addEventListener("keydown", handleKeyDown);

    return () => {
      document.body.style.overflow = previousOverflow;

      window.removeEventListener("keydown", handleKeyDown);
    };
  }, [busy, onClose]);

  function clearConfirmations() {
    setConfirmingArchive(false);
    setConfirmingDelete(false);
  }

  function handleModelChange(nextModelId: string) {
    const definition = models.find(
      (model) => model.provider === "openai" && model.modelId === nextModelId,
    );

    setModelId(nextModelId);

    if (definition) {
      setTemperature(definition.defaults.temperature?.toString() ?? "");

      setTopP(definition.defaults.topP?.toString() ?? "");

      setMaxOutputTokens(definition.defaults.maxOutputTokens?.toString() ?? "");
    }

    clearConfirmations();
  }

  function handleCompilerChange(value: string) {
    const reference = parseCompilerKey(value);

    if (!reference) {
      return;
    }

    const definition = contextCompilers.find(
      (candidate) =>
        createCompilerKey(candidate.descriptor) ===
        createCompilerKey(reference),
    );

    if (!definition) {
      return;
    }

    setCompilerReference(reference);

    setCompilerConfig({
      ...definition.defaultConfig,
    });

    clearConfirmations();
  }

  function updateCompilerConfig(key: string, value: unknown) {
    setCompilerConfig((current) => ({
      ...current,
      [key]: value,
    }));

    clearConfirmations();
  }

  function normalizeCompilerNumber(
    key: string,
    minimum: number,
    maximum: number,
    step: number,
  ): void {
    const currentValue = compilerConfig[key];

    if (typeof currentValue !== "number" || !Number.isFinite(currentValue)) {
      return;
    }

    updateCompilerConfig(
      key,
      snapNumberToStep(currentValue, minimum, maximum, step),
    );
  }

  function resetGenerationDefaults(): void {
    if (!selectedModel) {
      return;
    }

    setTemperature(selectedModel.defaults.temperature?.toString() ?? "");
    setTopP(selectedModel.defaults.topP?.toString() ?? "");
    setMaxOutputTokens(
      selectedModel.defaults.maxOutputTokens?.toString() ?? "",
    );

    clearConfirmations();
  }

  function resetCompilerDefaults(): void {
    if (!selectedCompiler) {
      return;
    }

    setCompilerConfig({
      ...selectedCompiler.defaultConfig,
    });

    clearConfirmations();
  }

  async function handleSubmit(event: FormEvent<HTMLFormElement>) {
    event.preventDefault();

    if (!canSave || !selectedModel) {
      return;
    }

    const input: UpdateAgentInput = {
      name: name.trim(),

      description: description.trim() || null,

      identity: {
        personality: personality.trim() || null,

        temperament: temperament.trim() || null,

        voice: voice.trim() || null,

        backstory: backstory.trim() || null,
      },

      profession: {
        jobTitle: jobTitle.trim() || null,

        mission: mission.trim() || null,

        expertise: textToList(expertise),

        responsibilities: textToList(responsibilities),

        successCriteria: textToList(successCriteria),

        limitations: textToList(limitations),
      },

      doctrine: doctrine.trim() || null,

      outputContract: {
        responseStyle: responseStyle.trim() || null,

        verbosity,

        formattingRules: textToList(formattingRules),

        codeOutputPreferences: textToList(codeOutputPreferences),

        citationRequirements: citationRequirements.trim() || null,

        followUpBehavior: followUpBehavior.trim() || null,
      },

      systemInstructions: systemInstructions.trim() || null,

      generation: {
        provider: "openai",
        model: selectedModel.modelId,

        temperature: selectedModel.supportedControls.includes("temperature")
          ? nullableNumber(temperature)
          : null,

        topP: selectedModel.supportedControls.includes("topP")
          ? nullableNumber(topP)
          : null,

        maxOutputTokens: selectedModel.supportedControls.includes(
          "maxOutputTokens",
        )
          ? nullableNumber(maxOutputTokens)
          : null,

        frequencyPenalty: null,
        presencePenalty: null,
      },

      context: {
        compiler: compilerReference,
        config: compilerConfig,
      },
    };

    await onSave(agent.id, input);
  }

  async function handleArchive() {
    if (!confirmingArchive) {
      setConfirmingArchive(true);
      return;
    }

    await onArchive(agent.id);
  }

  async function handleDelete() {
    if (!confirmingDelete) {
      setConfirmingDelete(true);
      return;
    }

    await onDelete(agent.id);
  }

  function handleBackdropClick(event: MouseEvent<HTMLDivElement>) {
    if (event.target === event.currentTarget && !busy) {
      onClose();
    }
  }

  return createPortal(
    <div
      className={`appModalBackdrop ${styles.backdrop}`}
      onMouseDown={handleBackdropClick}
      role="presentation"
    >
      <section
        className={`appModalSurface ${styles.modal}`}
        role="dialog"
        aria-modal="true"
        aria-labelledby="agent-management-title"
      >
        <header className={styles.header}>
          <div className={styles.heading}>
            <div className={styles.headingIcon}>
              <Bot size={21} strokeWidth={2.1} />
            </div>

            <div>
              <div className={styles.eyebrow}>
                {archived
                  ? "Archived Agent"
                  : agent.isBuiltIn
                    ? "Built-in Agent"
                    : "Agent Builder"}
              </div>

              <h2 id="agent-management-title" className={styles.title}>
                {agent.name}
              </h2>

              <p className={styles.subtitle}>
                Configure identity, profession, output, generation, and context.
              </p>
            </div>
          </div>

          <button
            className={styles.closeButton}
            type="button"
            onClick={onClose}
            disabled={busy}
            aria-label="Close Agent management"
          >
            <X size={18} />
          </button>
        </header>

        <form className={styles.form} onSubmit={handleSubmit}>
          <section className={styles.section}>
            <SectionHeading
              icon={<Bot size={18} />}
              title="Identity"
              description="Who this Agent is and how it sounds."
            />

            <div className={styles.twoColumns}>
              <TextInput
                label="Name"
                value={name}
                onChange={setName}
                disabled={busy || archived}
                maxLength={120}
              />

              <TextInput
                label="Professional title"
                value={jobTitle}
                onChange={setJobTitle}
                disabled={busy || archived}
              />
            </div>

            <TextArea
              label="Description"
              value={description}
              onChange={setDescription}
              disabled={busy || archived}
            />

            <div className={styles.twoColumns}>
              <TextArea
                label="Personality"
                value={personality}
                onChange={setPersonality}
                disabled={busy || archived}
              />

              <TextArea
                label="Temperament"
                value={temperament}
                onChange={setTemperament}
                disabled={busy || archived}
              />

              <TextArea
                label="Voice"
                value={voice}
                onChange={setVoice}
                disabled={busy || archived}
              />

              <TextArea
                label="Backstory"
                value={backstory}
                onChange={setBackstory}
                disabled={busy || archived}
              />
            </div>
          </section>

          <section className={styles.section}>
            <SectionHeading
              icon={<BrainCircuit size={18} />}
              title="Profession and doctrine"
              description="What this Agent does and how it approaches the work."
            />

            <TextArea
              label="Mission"
              value={mission}
              onChange={setMission}
              disabled={busy || archived}
            />

            <label className={styles.field}>
              <span className={styles.label}>Working doctrine</span>

              <textarea
                className={styles.textareaLarge}
                value={doctrine}
                onChange={(event) => setDoctrine(event.target.value)}
                disabled={busy || archived}
              />
            </label>

            <div className={styles.twoColumns}>
              <ListField
                label="Expertise"
                value={expertise}
                onChange={setExpertise}
                disabled={busy || archived}
              />

              <ListField
                label="Responsibilities"
                value={responsibilities}
                onChange={setResponsibilities}
                disabled={busy || archived}
              />

              <ListField
                label="Success criteria"
                value={successCriteria}
                onChange={setSuccessCriteria}
                disabled={busy || archived}
              />

              <ListField
                label="Limitations and deferral"
                value={limitations}
                onChange={setLimitations}
                disabled={busy || archived}
              />
            </div>
          </section>

          <section className={styles.section}>
            <SectionHeading
              icon={<Settings2 size={18} />}
              title="Output contract"
              description="Define what a successful response looks like."
            />

            <div className={styles.twoColumns}>
              <TextArea
                label="Response style"
                value={responseStyle}
                onChange={setResponseStyle}
                disabled={busy || archived}
              />

              <label className={styles.field}>
                <span className={styles.label}>Verbosity</span>

                <select
                  className={styles.select}
                  value={verbosity}
                  onChange={(event) =>
                    setVerbosity(event.target.value as AgentVerbosity)
                  }
                  disabled={busy || archived}
                >
                  <option value="concise">Concise</option>

                  <option value="balanced">Balanced</option>

                  <option value="detailed">Detailed</option>
                </select>
              </label>

              <ListField
                label="Formatting rules"
                value={formattingRules}
                onChange={setFormattingRules}
                disabled={busy || archived}
              />

              <ListField
                label="Code output preferences"
                value={codeOutputPreferences}
                onChange={setCodeOutputPreferences}
                disabled={busy || archived}
              />

              <TextArea
                label="Citation requirements"
                value={citationRequirements}
                onChange={setCitationRequirements}
                disabled={busy || archived}
              />

              <TextArea
                label="Follow-up behavior"
                value={followUpBehavior}
                onChange={setFollowUpBehavior}
                disabled={busy || archived}
              />
            </div>
          </section>

          <section className={styles.section}>
            <SectionHeading
              icon={<BrainCircuit size={18} />}
              title="System instructions"
              description="Explicit instructions applied after the structured profile."
            />

            <textarea
              className={styles.instructionEditor}
              value={systemInstructions}
              onChange={(event) => setSystemInstructions(event.target.value)}
              disabled={busy || archived}
            />
          </section>

          <section className={styles.section}>
            <div className={styles.sectionHeaderRow}>
              <SectionHeading
                icon={<Settings2 size={18} />}
                title="Generation"
                description="Select the model and supported controls."
              />

              <button
                className={styles.resetButton}
                type="button"
                onClick={resetGenerationDefaults}
                disabled={busy || archived || !selectedModel}
              >
                <RotateCcw size={14} />
                Reset generation
              </button>
            </div>

            <label className={styles.field}>
              <span className={styles.label}>Model</span>

              <select
                className={styles.select}
                value={modelId}
                onChange={(event) => handleModelChange(event.target.value)}
                disabled={busy || archived}
              >
                {models.map((model) => (
                  <option
                    key={`${model.provider}:${model.modelId}`}
                    value={model.modelId}
                  >
                    {model.displayName}
                  </option>
                ))}
              </select>

              {selectedModel ? (
                <span className={styles.helpText}>
                  {selectedModel.description}
                </span>
              ) : null}
            </label>

            <div className={styles.threeColumns}>
              {selectedModel?.supportedControls.includes("temperature") ? (
                <NumberField
                  label="Temperature"
                  value={temperature}
                  min={0}
                  max={2}
                  step={0.1}
                  onChange={setTemperature}
                  disabled={busy || archived}
                />
              ) : null}

              {selectedModel?.supportedControls.includes("topP") ? (
                <NumberField
                  label="Top-p"
                  value={topP}
                  min={0}
                  max={1}
                  step={0.05}
                  onChange={setTopP}
                  disabled={busy || archived}
                />
              ) : null}

              {selectedModel?.supportedControls.includes("maxOutputTokens") ? (
                <NumberField
                  label="Max output tokens"
                  value={maxOutputTokens}
                  min={1}
                  max={selectedModel.maximumOutputTokens ?? 128_000}
                  step={1}
                  onChange={setMaxOutputTokens}
                  disabled={busy || archived}
                />
              ) : null}
            </div>

            {generationConflict ? (
              <div className={styles.warning}>
                Set temperature or top-p, not both.
              </div>
            ) : null}
          </section>

          <section className={styles.section}>
            <div className={styles.sectionHeaderRow}>
              <SectionHeading
                icon={<BrainCircuit size={18} />}
                title="Context Compiler"
                description="Configure how this Agent selects and budgets context."
              />

              <button
                className={styles.resetButton}
                type="button"
                onClick={resetCompilerDefaults}
                disabled={busy || archived || !selectedCompiler}
              >
                <RotateCcw size={14} />
                Reset compiler
              </button>
            </div>

            <label className={styles.field}>
              <span className={styles.label}>Compiler</span>

              <select
                className={styles.select}
                value={createCompilerKey(compilerReference)}
                onChange={(event) => handleCompilerChange(event.target.value)}
                disabled={busy || archived}
              >
                {contextCompilers.map((definition) => {
                  const key = createCompilerKey(definition.descriptor);

                  return (
                    <option key={key} value={key}>
                      {definition.descriptor.name} v
                      {definition.descriptor.version}
                    </option>
                  );
                })}
              </select>
            </label>

            {selectedCompiler ? (
              <div className={styles.twoColumns}>
                {selectedCompiler.configFields.map((field) => {
                  const value = compilerConfig[field.key];

                  if (field.type === "boolean") {
                    return (
                      <label className={styles.toggleField} key={field.key}>
                        <input
                          type="checkbox"
                          checked={value === true}
                          onChange={(event) =>
                            updateCompilerConfig(
                              field.key,
                              event.target.checked,
                            )
                          }
                          disabled={busy || archived}
                        />

                        <span>
                          <strong>{field.label}</strong>

                          <small>{field.description}</small>
                        </span>
                      </label>
                    );
                  }

                  if (field.type === "select") {
                    return (
                      <label className={styles.field} key={field.key}>
                        <span className={styles.label}>{field.label}</span>

                        <select
                          className={styles.select}
                          value={
                            typeof value === "string"
                              ? value
                              : (field.options[0]?.value ?? "")
                          }
                          onChange={(event) =>
                            updateCompilerConfig(field.key, event.target.value)
                          }
                          disabled={busy || archived}
                        >
                          {field.options.map((option) => (
                            <option key={option.value} value={option.value}>
                              {option.label}
                            </option>
                          ))}
                        </select>

                        <span className={styles.helpText}>
                          {field.description}
                        </span>
                      </label>
                    );
                  }

                  return (
                    <NumberField
                      key={field.key}
                      label={field.label}
                      value={typeof value === "number" ? String(value) : ""}
                      min={field.minimum}
                      max={field.maximum}
                      step="any"
                      onChange={(nextValue) => {
                        if (!nextValue.trim()) {
                          return;
                        }

                        const parsedValue = Number(nextValue);

                        if (Number.isFinite(parsedValue)) {
                          updateCompilerConfig(field.key, parsedValue);
                        }
                      }}
                      onBlur={() =>
                        normalizeCompilerNumber(
                          field.key,
                          field.minimum,
                          field.maximum,
                          field.step,
                        )
                      }
                      disabled={busy || archived}
                      help={`${field.description} Values are adjusted to the nearest supported increment when you leave the field.`}
                    />
                  );
                })}
              </div>
            ) : (
              <div className={styles.warning}>
                This Context Compiler is not registered.
              </div>
            )}
          </section>

          {confirmingArchive ? (
            <div className={styles.warning}>
              This Agent can only be archived when no Chats are assigned to it.
            </div>
          ) : null}

          {confirmingDelete ? (
            <div className={styles.dangerNotice}>
              Deleting this Agent permanently removes its configuration.
              Assigned Chats will be moved to Archivist Default.
            </div>
          ) : null}

          <footer className={styles.footer}>
            <div className={styles.destructiveActions}>
              {archived ? (
                <>
                  <button
                    className={styles.restoreButton}
                    type="button"
                    onClick={() => void onRestore(agent.id)}
                    disabled={busy}
                  >
                    <ArchiveRestore size={16} />

                    {restoring ? "Restoring..." : "Restore"}
                  </button>

                  {!agent.isBuiltIn ? (
                    <button
                      className={styles.deleteButton}
                      type="button"
                      onClick={() => void handleDelete()}
                      disabled={busy}
                    >
                      <Trash2 size={16} />

                      {deleting
                        ? "Deleting..."
                        : confirmingDelete
                          ? "Confirm Delete"
                          : "Delete"}
                    </button>
                  ) : null}
                </>
              ) : (
                <>
                  <button
                    className={styles.secondaryButton}
                    type="button"
                    onClick={() => void onDuplicate(agent.id)}
                    disabled={busy}
                  >
                    <Copy size={16} />

                    {duplicating ? "Duplicating..." : "Duplicate"}
                  </button>

                  {!agent.isBuiltIn ? (
                    <button
                      className={styles.archiveButton}
                      type="button"
                      onClick={() => void handleArchive()}
                      disabled={busy}
                    >
                      <Archive size={16} />

                      {archiving
                        ? "Archiving..."
                        : confirmingArchive
                          ? "Confirm Archive"
                          : "Archive"}
                    </button>
                  ) : null}
                </>
              )}
            </div>

            <div className={styles.primaryActions}>
              <button
                className={styles.secondaryButton}
                type="button"
                onClick={onClose}
                disabled={busy}
              >
                Cancel
              </button>

              {!archived ? (
                <button
                  className={styles.saveButton}
                  type="submit"
                  disabled={!canSave}
                >
                  <Save size={16} />

                  {saving ? "Saving..." : "Save"}
                </button>
              ) : null}
            </div>
          </footer>
        </form>
      </section>
    </div>,
    document.body,
  );
}

type SectionHeadingProps = {
  icon: ReactNode;
  title: string;
  description: string;
};

function SectionHeading({ icon, title, description }: SectionHeadingProps) {
  return (
    <div className={styles.sectionHeading}>
      {icon}

      <div>
        <h3>{title}</h3>
        <p>{description}</p>
      </div>
    </div>
  );
}

type TextInputProps = {
  label: string;
  value: string;
  disabled: boolean;
  maxLength?: number;
  onChange: (value: string) => void;
};

function TextInput({
  label,
  value,
  disabled,
  maxLength,
  onChange,
}: TextInputProps) {
  return (
    <label className={styles.field}>
      <span className={styles.label}>{label}</span>

      <input
        className={styles.input}
        value={value}
        maxLength={maxLength}
        onChange={(event) => onChange(event.target.value)}
        disabled={disabled}
      />
    </label>
  );
}

type TextAreaProps = {
  label: string;
  value: string;
  disabled: boolean;
  onChange: (value: string) => void;
};

function TextArea({ label, value, disabled, onChange }: TextAreaProps) {
  return (
    <label className={styles.field}>
      <span className={styles.label}>{label}</span>

      <textarea
        className={styles.textareaSmall}
        value={value}
        onChange={(event) => onChange(event.target.value)}
        disabled={disabled}
      />
    </label>
  );
}

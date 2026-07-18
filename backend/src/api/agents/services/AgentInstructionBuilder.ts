import { DEFAULT_AGENT_SYSTEM_INSTRUCTIONS } from "../models/AgentDefaults.js";
import type { Agent } from "../types/AgentTypes.js";

function normalizeInstruction(value: string | null | undefined): string {
  return value?.trim().replace(/\s+/g, " ") ?? "";
}

function addSection(
  sections: string[],
  heading: string,
  lines: Array<string | null | undefined>,
): void {
  const content = lines
    .map((line) => line?.trim())
    .filter((line): line is string => Boolean(line));

  if (content.length === 0) {
    return;
  }

  sections.push([`## ${heading}`, ...content].join("\n"));
}

function addListSection(
  sections: string[],
  heading: string,
  items: string[],
): void {
  const content = items.map((item) => item.trim()).filter(Boolean);

  if (content.length === 0) {
    return;
  }

  sections.push(
    [`## ${heading}`, ...content.map((item) => `- ${item}`)].join("\n"),
  );
}

function getEffectiveSystemInstructions(agent: Agent): string | null {
  const instructions = agent.systemInstructions?.trim() ?? "";

  if (!instructions) {
    return null;
  }

  const inheritedDefault =
    normalizeInstruction(instructions) ===
    normalizeInstruction(DEFAULT_AGENT_SYSTEM_INSTRUCTIONS);

  /*
   * Older and newly created custom Agents may contain
   * Archivist Default's system instructions because the
   * creation model historically used them as a fallback.
   *
   * Those instructions describe the built-in Archivist,
   * not the custom Agent, and must not override the custom
   * Agent's identity.
   */
  if (!agent.isBuiltIn && inheritedDefault) {
    return null;
  }

  return instructions;
}

export function buildAgentInstructions(agent: Agent): string {
  const sections: string[] = [];

  addSection(sections, "Operating Authority", [
    [
      `You are ${agent.name}.`,
      "The Agent profile below defines your active identity, temperament, voice, behavior, profession, and response contract.",
      "Enact that profile consistently in every response, including casual conversation.",
      "Do not revert to a generic friendly, neutral, or conventionally helpful assistant persona when that would conflict with this Agent.",
      "Behavioral fields such as personality, temperament, voice, doctrine, and response style are operational instructions, not decorative metadata.",
    ].join(" "),
  ]);

  addSection(sections, "Agent Identity", [
    `Name: ${agent.name}`,

    agent.description ? `Description: ${agent.description}` : null,

    agent.identity.personality
      ? `Personality: ${agent.identity.personality}`
      : null,

    agent.identity.temperament
      ? `Temperament: ${agent.identity.temperament}`
      : null,

    agent.identity.voice ? `Voice: ${agent.identity.voice}` : null,

    agent.identity.backstory ? `Background: ${agent.identity.backstory}` : null,
  ]);

  addSection(sections, "Behavioral Enforcement", [
    agent.identity.personality
      ? "Express the configured personality directly rather than merely understanding or describing it."
      : null,

    agent.identity.temperament
      ? "Let the configured temperament affect word choice, patience, warmth, bluntness, humor, and willingness to engage."
      : null,

    agent.identity.voice
      ? "Write in the configured voice throughout the response."
      : null,

    [
      "Do not soften, sanitize, or replace the configured personality merely because another style would be more conventionally assistant-like.",
      "Still follow platform safety requirements and do not fabricate facts or capabilities.",
    ].join(" "),
  ]);

  addSection(sections, "Profession", [
    agent.profession.jobTitle
      ? `Job title: ${agent.profession.jobTitle}`
      : null,

    agent.profession.mission ? `Mission: ${agent.profession.mission}` : null,
  ]);

  addListSection(sections, "Expertise", agent.profession.expertise);

  addListSection(
    sections,
    "Responsibilities",
    agent.profession.responsibilities,
  );

  addListSection(
    sections,
    "Success Criteria",
    agent.profession.successCriteria,
  );

  addListSection(
    sections,
    "Limitations and Deferral",
    agent.profession.limitations,
  );

  addSection(sections, "Working Doctrine", [agent.doctrine]);

  addSection(sections, "Output Contract", [
    agent.outputContract.responseStyle
      ? `Response style: ${agent.outputContract.responseStyle}`
      : null,

    `Verbosity preference: ${agent.outputContract.verbosity}`,

    agent.outputContract.citationRequirements
      ? `Citation requirements: ${agent.outputContract.citationRequirements}`
      : null,

    agent.outputContract.followUpBehavior
      ? `Follow-up behavior: ${agent.outputContract.followUpBehavior}`
      : null,
  ]);

  addListSection(
    sections,
    "Formatting Rules",
    agent.outputContract.formattingRules,
  );

  addListSection(
    sections,
    "Code Output Preferences",
    agent.outputContract.codeOutputPreferences,
  );

  const effectiveSystemInstructions = getEffectiveSystemInstructions(agent);

  addSection(sections, "Additional Explicit Instructions", [
    effectiveSystemInstructions,
  ]);

  addSection(sections, "Final Behavioral Check", [
    [
      `Before answering, verify that the response sounds and behaves like ${agent.name}.`,
      "The response should visibly reflect the configured personality, temperament, voice, doctrine, and output contract.",
      "A response that could have been produced unchanged by a generic assistant does not satisfy this Agent profile.",
    ].join(" "),
  ]);

  return sections.join("\n\n").trim();
}

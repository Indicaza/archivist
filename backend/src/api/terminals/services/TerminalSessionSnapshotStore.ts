import { createHash } from "node:crypto";
import {
  mkdirSync,
  readFileSync,
  renameSync,
  rmSync,
  writeFileSync,
} from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import type { TerminalSocketContext } from "../types/TerminalTypes.js";

export type TerminalSessionSnapshot = {
  version: 1;
  sessionId: string;
  collectionId: string;
  libraryId: string;
  cwd: string;
  shell: string;
  scrollback: string;
  savedAt: string;
};

const serviceDirectory = path.dirname(
  fileURLToPath(import.meta.url),
);
const snapshotDirectory = path.resolve(
  serviceDirectory,
  "../../../..",
  "data",
  "terminals",
);

function snapshotIdentity(
  context: Pick<
    TerminalSocketContext,
    "sessionId" | "collectionId" | "libraryId"
  >,
): string {
  return [
    context.collectionId,
    context.libraryId,
    context.sessionId,
  ].join(":");
}

function snapshotPath(
  context: Pick<
    TerminalSocketContext,
    "sessionId" | "collectionId" | "libraryId"
  >,
): string {
  const digest = createHash("sha256")
    .update(snapshotIdentity(context))
    .digest("hex");

  return path.join(snapshotDirectory, `${digest}.json`);
}

function snapshotMatchesContext(
  snapshot: TerminalSessionSnapshot,
  context: Pick<
    TerminalSocketContext,
    "sessionId" | "collectionId" | "libraryId"
  >,
): boolean {
  return (
    snapshot.version === 1
    && snapshot.sessionId === context.sessionId
    && snapshot.collectionId === context.collectionId
    && snapshot.libraryId === context.libraryId
  );
}

export function loadTerminalSessionSnapshot(
  context: Pick<
    TerminalSocketContext,
    "sessionId" | "collectionId" | "libraryId"
  >,
): TerminalSessionSnapshot | null {
  try {
    const parsed = JSON.parse(
      readFileSync(snapshotPath(context), "utf8"),
    ) as TerminalSessionSnapshot;

    return snapshotMatchesContext(parsed, context)
      ? parsed
      : null;
  } catch {
    return null;
  }
}

export function saveTerminalSessionSnapshot(
  snapshot: TerminalSessionSnapshot,
): void {
  mkdirSync(snapshotDirectory, {
    recursive: true,
  });

  const targetPath = snapshotPath(snapshot);
  const temporaryPath =
    `${targetPath}.${process.pid}.tmp`;

  writeFileSync(
    temporaryPath,
    `${JSON.stringify(snapshot)}\n`,
    "utf8",
  );
  renameSync(temporaryPath, targetPath);
}

export function deleteTerminalSessionSnapshot(
  context: Pick<
    TerminalSocketContext,
    "sessionId" | "collectionId" | "libraryId"
  >,
): void {
  rmSync(snapshotPath(context), {
    force: true,
  });
}

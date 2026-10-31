// Fixed locations in the repository. Nothing here comes from a request.
import path from "node:path";
import { fileURLToPath } from "node:url";

const HERE = path.dirname(fileURLToPath(import.meta.url));

export const REPO_ROOT = path.resolve(HERE, "..", "..");
export const BUILD_DIR = path.join(REPO_ROOT, "build", "frontend-debug");
export const SCENARIO_DIR = path.join(HERE, "ui-scenarios");
export const CLI_PATH = path.join(HERE, "cli.mjs");

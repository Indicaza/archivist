#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";
import process from "node:process";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(scriptDirectory, "..");
const backendLog = path.join(
  repositoryRoot,
  "backend/data/runtime/logs/backend.log",
);
const frontendLog = path.join(
  repositoryRoot,
  "backend/data/runtime/logs/frontend.log",
);

function parsedArguments() {
  const args = process.argv.slice(2);
  let lines = 500;
  let full = false;

  for (let index = 0; index < args.length; index += 1) {
    const value = args[index];

    if (value === "--full") {
      full = true;
      continue;
    }

    if (value === "--lines") {
      lines = Math.max(
        20,
        Math.min(5000, Number(args[index + 1] || 500)),
      );
      index += 1;
    }
  }

  return { lines, full };
}

function section(title) {
  process.stdout.write(
    `\n===== ${title} =====\n`,
  );
}

function run(command, args) {
  const result = spawnSync(command, args, {
    cwd: repositoryRoot,
    encoding: "utf8",
    env: process.env,
  });

  return {
    status: result.status,
    output: [
      String(result.stdout || "").trim(),
      String(result.stderr || "").trim(),
    ].filter(Boolean).join("\n"),
  };
}

function printCommand(command, args) {
  const result = run(command, args);

  process.stdout.write(
    `${result.output || `(exit ${result.status ?? "unknown"})`}\n`,
  );
}

function readTail(filePath, maximumLines) {
  if (!fs.existsSync(filePath)) {
    return [`Missing log: ${filePath}`];
  }

  return fs.readFileSync(filePath, "utf8")
    .split(/\r?\n/)
    .slice(-maximumLines);
}

function relevantLanguageLines(lines) {
  const pattern =
    /language support|language-support|publishDiagnostics|diagnostic|didOpen|module_not_found|err_package|too many language servers|typescriptreact|javascriptreact|tsx|jsx/i;

  return lines.filter((line) => pattern.test(line));
}

function listFiles(rootPath, maximumDepth = 4) {
  const results = [];
  const ignoredDirectories = new Set([
    ".cache",
    "__pycache__",
    "build",
    "dist",
    "node_modules",
    "target",
  ]);

  function visit(directory, depth) {
    if (depth > maximumDepth || !fs.existsSync(directory)) {
      return;
    }

    for (const entry of fs.readdirSync(directory, {
      withFileTypes: true,
    })) {
      const absolute = path.join(directory, entry.name);

      if (entry.isDirectory()) {
        if (!ignoredDirectories.has(entry.name)) {
          visit(absolute, depth + 1);
        }
      } else if (entry.isFile()) {
        results.push(
          path.relative(repositoryRoot, absolute),
        );
      }
    }
  }

  visit(rootPath, 0);
  return results.sort();
}

async function fetchJson(pathValue) {
  const port = Number(process.env.PORT || 3333);
  const url = `http://127.0.0.1:${port}${pathValue}`;

  try {
    const response = await fetch(url, {
      signal: AbortSignal.timeout(3000),
    });
    const text = await response.text();

    if (!response.ok) {
      return {
        ok: false,
        url,
        status: response.status,
        body: text,
      };
    }

    return {
      ok: true,
      url,
      body: JSON.parse(text),
    };
  } catch (error) {
    return {
      ok: false,
      url,
      error: error instanceof Error
        ? error.message
        : String(error),
    };
  }
}

function printJson(value) {
  process.stdout.write(
    `${JSON.stringify(value, null, 2)}\n`,
  );
}

const options = parsedArguments();

section("Generated");
process.stdout.write(`${new Date().toISOString()}\n`);

section("Repository");
printCommand("git", ["branch", "--show-current"]);
printCommand("git", ["status", "--short"]);
printCommand("git", ["log", "--oneline", "-5"]);

section("Runtime");
printCommand(process.execPath, ["--version"]);
printCommand(
  process.platform === "win32" ? "npm.cmd" : "npm",
  ["--version"],
);

section("Language tool doctor");
printCommand(process.execPath, [
  path.join(scriptDirectory, "language-tools.mjs"),
  "doctor",
]);

section("Registered server availability");
printJson(await fetchJson(
  "/api/language-support/config",
));

section("Current sessions");
printJson(await fetchJson(
  "/api/language-support/sessions",
));

section("Client language events");
printJson(await fetchJson(
  "/api/language-support/events?limit=300",
));

section("Language test lab");
const labFiles = listFiles(
  path.join(repositoryRoot, "language-test-lab"),
);
process.stdout.write(
  `${labFiles.join("\n") || "language-test-lab is missing"}\n`,
);

for (const [title, filePath] of [
  ["Backend language log", backendLog],
  ["Frontend language log", frontendLog],
]) {
  section(title);
  const tail = readTail(filePath, options.lines);
  const selected = options.full
    ? tail
    : relevantLanguageLines(tail);

  process.stdout.write(
    `${selected.join("\n") || "No matching lines."}\n`,
  );
}

section("Reproduction note");
process.stdout.write(
  "Open the failing file, wait two seconds, then rerun this command.\n"
  + "Client events are fetched directly from the backend and do not depend on QWebEngine console logs.\n"
  + "Use --full for unfiltered log tails or --lines 1500 for a larger window.\n",
);

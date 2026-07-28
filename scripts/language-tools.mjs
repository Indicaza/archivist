#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";
import process from "node:process";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const repositoryRoot = path.resolve(scriptDirectory, "..");
const manifestPath = path.join(scriptDirectory, "language-tools.json");
const packagePath = path.join(repositoryRoot, "package.json");
const nodeModulesPath = path.join(repositoryRoot, "node_modules");
const binDirectory = path.join(nodeModulesPath, ".bin");

function readJson(filePath) {
  return JSON.parse(fs.readFileSync(filePath, "utf8"));
}

function writeJson(filePath, value) {
  fs.writeFileSync(
    filePath,
    `${JSON.stringify(value, null, 2)}\n`,
    "utf8",
  );
}

function executableName(name) {
  return process.platform === "win32" ? `${name}.cmd` : name;
}

function npmCommand() {
  return process.platform === "win32" ? "npm.cmd" : "npm";
}

function commandPath(name) {
  const pathEntries = String(process.env.PATH || "")
    .split(path.delimiter)
    .filter(Boolean);
  const extensions = process.platform === "win32"
    ? String(process.env.PATHEXT || ".EXE;.CMD;.BAT;.COM")
      .split(";")
      .filter(Boolean)
    : [""];

  for (const directory of pathEntries) {
    for (const extension of extensions) {
      const candidate = path.join(directory, `${name}${extension}`);

      try {
        fs.accessSync(candidate, fs.constants.X_OK);
        return candidate;
      } catch {
        continue;
      }
    }
  }

  return null;
}

function run(command, args, options = {}) {
  const result = spawnSync(command, args, {
    cwd: repositoryRoot,
    encoding: "utf8",
    stdio: options.capture ? "pipe" : "inherit",
    env: process.env,
  });

  if (result.error) {
    throw result.error;
  }

  if (result.status !== 0 && !options.allowFailure) {
    const detail = options.capture
      ? String(result.stderr || result.stdout || "").trim()
      : "";
    throw new Error(
      `${command} ${args.join(" ")} failed with exit code ${result.status}`
      + (detail ? `: ${detail}` : ""),
    );
  }

  return {
    status: result.status ?? 1,
    stdout: String(result.stdout || "").trim(),
    stderr: String(result.stderr || "").trim(),
  };
}

function manifest() {
  const value = readJson(manifestPath);

  if (
    value.schemaVersion !== 1
    || !Array.isArray(value.managedPackages)
    || !Array.isArray(value.npmTools)
    || !Array.isArray(value.nativeTools)
  ) {
    throw new Error(
      "scripts/language-tools.json has an unsupported shape.",
    );
  }

  return value;
}

function packageDirectory(packageName) {
  return path.join(nodeModulesPath, ...packageName.split("/"));
}

function installedPackageVersion(packageName) {
  const installedPackagePath = path.join(
    packageDirectory(packageName),
    "package.json",
  );

  if (!fs.existsSync(installedPackagePath)) {
    return null;
  }

  return String(readJson(installedPackagePath).version || "") || null;
}

function localBinaryPath(binary) {
  const candidate = path.join(binDirectory, executableName(binary));
  return fs.existsSync(candidate) ? candidate : null;
}

function syncRootPackage(toolManifest) {
  const rootPackage = readJson(packagePath);
  const devDependencies = {
    ...(rootPackage.devDependencies || {}),
  };

  for (const packageName of toolManifest.managedPackages) {
    delete devDependencies[packageName];
  }

  for (const tool of toolManifest.npmTools) {
    devDependencies[tool.package] = tool.version;
  }

  rootPackage.devDependencies = Object.fromEntries(
    Object.entries(devDependencies).sort(([left], [right]) =>
      left.localeCompare(right)
    ),
  );
  writeJson(packagePath, rootPackage);
}

function latestNpmVersion(packageName) {
  const result = run(
    npmCommand(),
    ["view", packageName, "version", "--json"],
    { capture: true },
  );
  const parsed = JSON.parse(result.stdout);

  if (typeof parsed !== "string" || !parsed) {
    throw new Error(`npm did not return a version for ${packageName}.`);
  }

  return parsed;
}

function firstOutputLine(value) {
  return String(value || "")
    .split(/\r?\n/)
    .map((line) => line.trim())
    .find(Boolean) || "version unavailable";
}

function printHeader(title) {
  process.stdout.write(`\n${title}\n${"-".repeat(title.length)}\n`);
}

function doctor() {
  const toolManifest = manifest();
  let managedMissing = 0;
  let nativeMissing = 0;

  printHeader("Project-local language tools");

  for (const tool of toolManifest.npmTools) {
    const installedVersion = installedPackageVersion(tool.package);
    const missingBinaries = tool.binaries.filter(
      (binary) => !localBinaryPath(binary),
    );
    const versionMatches = installedVersion === tool.version;
    const ready = versionMatches && missingBinaries.length === 0;

    if (!ready) {
      managedMissing += 1;
    }

    const marker = ready ? "READY" : "NEEDS SYNC";
    process.stdout.write(
      `${marker.padEnd(10)} ${tool.displayName}\n`
      + `           package ${tool.package}@${tool.version}\n`
      + `           installed ${installedVersion || "missing"}\n`,
    );

    if (missingBinaries.length > 0) {
      process.stdout.write(
        `           missing binaries: ${missingBinaries.join(", ")}\n`,
      );
    }
  }

  printHeader("Built-in Monaco language services");
  process.stdout.write(
    "READY      HTML / CSS / SCSS / Less / JSON\n"
    + "           Monaco workers bundled with Archivist\n"
    + "READY      SQL\n"
    + "           Monaco syntax and isolated completion provider\n",
  );

  printHeader("Native language tools");

  for (const tool of toolManifest.nativeTools) {
    const executable = tool.binaries
      .map((binary) => commandPath(binary))
      .find(Boolean);

    if (!executable) {
      nativeMissing += 1;
      process.stdout.write(
        `MISSING    ${tool.displayName}\n`
        + `           ${tool.updateHint}\n`,
      );
      continue;
    }

    const version = run(
      executable,
      tool.versionArgs || ["--version"],
      { capture: true, allowFailure: true },
    );
    const output = firstOutputLine(
      version.stdout || version.stderr,
    );

    if (version.status !== 0) {
      nativeMissing += 1;
      process.stdout.write(
        `BROKEN     ${tool.displayName}\n`
        + `           ${executable}\n`
        + `           ${output || "Version check failed."}\n`
        + `           ${tool.updateHint}\n`,
      );
      continue;
    }

    process.stdout.write(
      `READY      ${tool.displayName}\n`
      + `           ${executable}\n`
      + `           ${output}\n`,
    );
  }

  process.stdout.write(
    `\nManaged npm tools needing sync: ${managedMissing}. `
    + `Optional native tools missing or broken: ${nativeMissing}.\n`
    + "Managed npm tools are pinned by scripts/language-tools.json.\n"
    + "Run npm run lsp:install after cloning or changing a version.\n",
  );

  process.exitCode = managedMissing > 0 ? 1 : 0;
}

function install() {
  const toolManifest = manifest();
  syncRootPackage(toolManifest);
  run(npmCommand(), ["install"]);
  doctor();
}

function check() {
  const toolManifest = manifest();
  let outdated = 0;

  printHeader("Language tool update check");

  for (const tool of toolManifest.npmTools) {
    const latest = latestNpmVersion(tool.package);
    const current = tool.version;
    const manual = tool.updatePolicy === "manual";
    const marker = current === latest
      ? "CURRENT"
      : manual
        ? "MANUAL"
        : "UPDATE";

    if (current !== latest && !manual) {
      outdated += 1;
    }

    process.stdout.write(
      `${marker.padEnd(8)} ${tool.package} ${current}`
      + (current === latest ? "" : ` -> ${latest}`)
      + (manual && current !== latest ? " (pinned manually)" : "")
      + "\n",
    );
  }

  process.exitCode = outdated > 0 ? 2 : 0;
}

function update() {
  const toolManifest = manifest();

  for (const tool of toolManifest.npmTools) {
    if (tool.updatePolicy === "manual") {
      process.stdout.write(
        `Keeping manual pin ${tool.package}@${tool.version}\n`,
      );
      continue;
    }

    const latest = latestNpmVersion(tool.package);

    if (tool.version !== latest) {
      process.stdout.write(
        `Updating ${tool.package}: ${tool.version} -> ${latest}\n`,
      );
      tool.version = latest;
    }
  }

  writeJson(manifestPath, toolManifest);
  syncRootPackage(toolManifest);
  run(npmCommand(), ["install"]);
  doctor();
}

function setVersion(identifier, version) {
  if (!identifier || !version) {
    throw new Error(
      "Usage: npm run lsp:set -- <tool-id-or-package> <exact-version>",
    );
  }

  const toolManifest = manifest();
  const tool = toolManifest.npmTools.find(
    (candidate) =>
      candidate.id === identifier
      || candidate.package === identifier,
  );

  if (!tool) {
    throw new Error(`Unknown managed npm language tool: ${identifier}`);
  }

  const resolved = run(
    npmCommand(),
    ["view", `${tool.package}@${version}`, "version", "--json"],
    { capture: true },
  );
  const publishedVersion = JSON.parse(resolved.stdout);

  if (publishedVersion !== version) {
    throw new Error(
      `${tool.package}@${version} is not an exact published version.`,
    );
  }

  tool.version = version;
  writeJson(manifestPath, toolManifest);
  syncRootPackage(toolManifest);
  run(npmCommand(), ["install"]);
  doctor();
}

function help() {
  process.stdout.write(
    "Archivist language tool manager\n\n"
    + "npm run lsp:doctor\n"
    + "  Show project-local and native server availability.\n\n"
    + "npm run lsp:install\n"
    + "  Sync exact manifest versions into package.json and package-lock.json.\n\n"
    + "npm run lsp:check\n"
    + "  Compare pinned npm server versions with the npm registry.\n\n"
    + "npm run lsp:update\n"
    + "  Pin every managed npm server to its latest stable release and install.\n\n"
    + "npm run lsp:set -- <tool-id-or-package> <exact-version>\n"
    + "  Pin one managed npm server to a chosen published version.\n",
  );
}

try {
  const [command = "help", ...args] = process.argv.slice(2);

  switch (command) {
    case "doctor":
      doctor();
      break;
    case "install":
      install();
      break;
    case "check":
      check();
      break;
    case "update":
      update();
      break;
    case "set":
      setVersion(args[0], args[1]);
      break;
    case "help":
    case "--help":
    case "-h":
      help();
      break;
    default:
      throw new Error(`Unknown command: ${command}`);
  }
} catch (error) {
  const message = error instanceof Error ? error.message : String(error);
  console.error(`Language tool manager failed: ${message}`);
  process.exitCode = 1;
}

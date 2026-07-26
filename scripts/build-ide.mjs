import { createHash } from "node:crypto";
import {
  readdir,
  readFile,
  stat,
  writeFile,
} from "node:fs/promises";
import path from "node:path";
import { spawn } from "node:child_process";
import { fileURLToPath } from "node:url";

const root = path.resolve(
  path.dirname(fileURLToPath(import.meta.url)),
  "..",
);
const webRoot = path.join(
  root,
  "frontend",
  "qml",
  "App",
  "Workbench",
  "IdeHost",
  "Web",
);
const distributionRoot = path.join(
  root,
  "frontend",
  "dist",
  "ide",
);
const stampPath = path.join(
  distributionRoot,
  ".archivist-build-hash",
);
const force = process.argv.includes("--force");

const inputPaths = [
  webRoot,
  path.join(root, "frontend", "package.json"),
];

async function collectFiles(
  candidatePath,
  files,
) {
  const details = await stat(candidatePath);

  if (details.isFile()) {
    files.push(candidatePath);
    return;
  }

  const entries = await readdir(
    candidatePath,
    {
      withFileTypes: true,
    },
  );

  entries.sort((left, right) =>
    left.name.localeCompare(right.name),
  );

  for (const entry of entries) {
    if (
      entry.name === "node_modules"
      || entry.name === "dist"
    ) {
      continue;
    }

    await collectFiles(
      path.join(candidatePath, entry.name),
      files,
    );
  }
}

async function calculateBuildHash() {
  const files = [];

  for (const inputPath of inputPaths) {
    await collectFiles(inputPath, files);
  }

  files.sort();
  const hash = createHash("sha256");

  for (const file of files) {
    hash.update(
      path.relative(root, file),
    );
    hash.update("\0");
    hash.update(await readFile(file));
    hash.update("\0");
  }

  return hash.digest("hex");
}

async function readCurrentStamp() {
  try {
    return (
      await readFile(stampPath, "utf8")
    ).trim();
  } catch {
    return "";
  }
}

function runIdeBuild() {
  return new Promise((resolve, reject) => {
    const child = spawn(
      "npm",
      [
        "run",
        "build:ide",
        "-w",
        "frontend",
      ],
      {
        cwd: root,
        stdio: "inherit",
      },
    );

    child.on("error", reject);
    child.on("exit", (code, signal) => {
      if (code === 0) {
        resolve();
        return;
      }

      reject(
        new Error(
          signal
            ? `IDE build stopped by ${signal}.`
            : `IDE build exited with code ${code}.`,
        ),
      );
    });
  });
}

const expectedHash = await calculateBuildHash();
const currentHash = await readCurrentStamp();
const indexPath = path.join(
  distributionRoot,
  "index.html",
);

let outputExists = false;

try {
  outputExists = (await stat(indexPath)).isFile();
} catch {
  outputExists = false;
}

if (
  !force
  && outputExists
  && currentHash === expectedHash
) {
  console.log(
    "Archivist IDE shell is unchanged; using cached build.",
  );
  process.exit(0);
}

await runIdeBuild();
await writeFile(
  stampPath,
  `${expectedHash}\n`,
  "utf8",
);

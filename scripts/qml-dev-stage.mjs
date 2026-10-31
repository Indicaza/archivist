import { cp, readFile, readdir, rm, writeFile } from "node:fs/promises";
import path from "node:path";

export function withDevModuleImport(source, relativePath) {
  if (!relativePath.endsWith(".qml") || relativePath === "App.qml" || /^import Archivist(?:\s|$)/m.test(source))
    return source;
  const moduleImport = "import Archivist 1.0\n";
  const header = source.match(/^(?:\s*pragma\s+[^\n]+\n)+/);
  if (header)
    return header[0] + moduleImport + source.slice(header[0].length);
  return moduleImport + source;
}

async function rewriteQml(root, relative = "") {
  const directory = path.join(root, relative);
  for (const item of await readdir(directory, { withFileTypes: true })) {
    const fileRelative = path.join(relative, item.name);
    const full = path.join(root, fileRelative);
    if (item.isDirectory()) await rewriteQml(root, fileRelative);
    else if (item.isFile() && item.name.endsWith(".qml")) {
      const original = await readFile(full, "utf8");
      const transformed = withDevModuleImport(original, fileRelative);
      if (transformed !== original) await writeFile(full, transformed, "utf8");
    }
  }
}

export async function refreshDevQmlTree(sourceRoot, stagedRoot) {
  await rm(stagedRoot, { recursive: true, force: true });
  await cp(sourceRoot, stagedRoot, { recursive: true });
  await rewriteQml(stagedRoot);
}

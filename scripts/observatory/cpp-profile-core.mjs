import path from "node:path";

export const DEFAULT_CPP_SOURCES = [
  "main.cpp",
  "ai_tools.cpp",
  "chat_store.cpp",
  "ai_runtime.cpp",
  "library_store.cpp",
];

export function splitShellCommand(command) {
  if (typeof command !== "string") throw new Error("Ninja compile command is missing.");
  const words = [];
  let current = "";
  let state = "plain";
  let started = false;
  for (let i = 0; i < command.length; i += 1) {
    const char = command[i];
    if (state === "single") {
      if (char === "'") state = "plain";
      else current += char;
      continue;
    }
    if (state === "double") {
      if (char === '"') {
        state = "plain";
      } else if (char === "\\") {
        const next = command[i + 1];
        if (next === '"' || next === "\\" || next === "$" || next === "`" || next === "\n") {
          if (next !== "\n") current += next;
          i += 1;
        } else {
          current += char;
        }
      } else {
        current += char;
      }
      continue;
    }
    if (/\s/.test(char)) {
      if (started) {
        words.push(current);
        current = "";
        started = false;
      }
    } else if (char === "'") {
      state = "single";
      started = true;
    } else if (char === '"') {
      state = "double";
      started = true;
    } else if (char === "\\") {
      if (i + 1 === command.length) throw new Error("Trailing shell escape in compile command.");
      if (command[i + 1] !== "\n") current += command[i + 1];
      i += 1;
      started = true;
    } else if (char === ";" || char === "|" || char === "&" || char === "<" || char === ">") {
      throw new Error("Ninja compile command has unsupported shell operators.");
    } else if (char === "`" || char === "$") {
      throw new Error("Ninja compile command has unsupported shell expansion.");
    } else {
      current += char;
      started = true;
    }
  }
  if (state !== "plain") throw new Error("Unterminated quote in Ninja compile command.");
  if (started) words.push(current);
  return words;
}

export function selectCompileJobs(compdb, sourceRoot, names) {
  if (!Array.isArray(compdb)) throw new Error("Ninja compilation database must be a JSON array.");
  const requested = [...new Set(names)];
  const jobs = [];
  for (const name of requested) {
    if (!name || name.startsWith("-")) throw new Error("Invalid source selector.");
    const matches = compdb.filter((job) => {
      if (typeof job.file !== "string" || typeof job.command !== "string") return false;
      const file = path.resolve(job.directory ?? sourceRoot, job.file);
      const relative = path.relative(sourceRoot, file);
      if (relative.startsWith("..") || path.isAbsolute(relative) || !relative.endsWith(".cpp")) return false;
      return name.includes("/") ? relative === name.replace(/^frontend\//, "") : path.basename(file) === name;
    });
    if (matches.length !== 1) {
      throw new Error(`Expected one compilation entry for ${name}; found ${matches.length}. Use a path relative to frontend/src for ambiguous names.`);
    }
    jobs.push(matches[0]);
  }
  return jobs;
}

export function isolatedCompile(job, objectFile, depFile) {
  const args = Array.isArray(job.arguments) ? [...job.arguments] : splitShellCommand(job.command);
  if (args.length < 3 || !args.includes("-c")) throw new Error("Expected a single C++ compile command.");
  const executable = args.shift();
  if (!/clang(\\+\\+)?$/.test(path.basename(executable)) && !/^(c\\+\\+|g\\+\\+)$/.test(path.basename(executable))) {
    throw new Error(`Unsupported compiler executable: ${path.basename(executable)}`);
  }
  let foundOutput = false;
  let foundDep = false;
  for (let i = 0; i < args.length; i += 1) {
    if (args[i] === "-o") {
      if (i + 1 >= args.length) throw new Error("Missing compile output.");
      args[++i] = objectFile;
      foundOutput = true;
    } else if (args[i].startsWith("-o") && args[i].length > 2) {
      args[i] = "-o" + objectFile;
      foundOutput = true;
    } else if (args[i] === "-MF") {
      if (i + 1 >= args.length) throw new Error("Missing dependency output.");
      args[++i] = depFile;
      foundDep = true;
    } else if (args[i].startsWith("-MF") && args[i].length > 3) {
      args[i] = "-MF" + depFile;
      foundDep = true;
    }
  }
  if (!foundOutput) throw new Error("Compile command has no -o output; refusing to run.");
  if (!foundDep && (args.includes("-MD") || args.includes("-MMD"))) {
    args.push("-MF", depFile);
  }
  args.push("-ftime-trace");
  return { executable, args, cwd: job.directory };
}

export function readableHeader(file, root) {
  const normalized = file.replaceAll("\\", "/");
  const relative = path.relative(root, normalized);
  if (!relative.startsWith("..") && !path.isAbsolute(relative)) return relative;
  const marker = "/include/";
  const at = normalized.lastIndexOf(marker);
  return at >= 0 ? normalized.slice(at + marker.length) : path.basename(normalized);
}

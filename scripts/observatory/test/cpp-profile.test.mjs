import assert from "node:assert/strict";
import { execFileSync, spawnSync } from "node:child_process";
import { mkdtemp, mkdir, readFile, rm, stat, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import test from "node:test";
import { isolatedCompile, readableHeader, selectCompileJobs, splitShellCommand } from "../cpp-profile-core.mjs";
import { summarizeTrace, traceFileFor } from "../time-trace.mjs";

const has = (command) => spawnSync(command, ["--version"], { stdio: "ignore" }).status === 0;

test("Ninja command tokenizer handles quoted paths and refuses shell operators", () => {
  assert.deepEqual(splitShellCommand('"/some/clang++" -DROOT="my source" -c \'src/test file.cpp\' -o obj.o'), [
    "/some/clang++", "-DROOT=my source", "-c", "src/test file.cpp", "-o", "obj.o",
  ]);
  assert.deepEqual(splitShellCommand('clang++ -DNAME="one \\"two\\"" -c x.cpp -o x.o'), [
    "clang++", '-DNAME=one "two"', "-c", "x.cpp", "-o", "x.o",
  ]);
  assert.throws(() => splitShellCommand("clang++ -c x.cpp && rm -rf build"), /unsupported shell operators/);
  assert.throws(() => splitShellCommand("clang++ -c \'unclosed"), /Unterminated quote/);
});

test("only the requested repository source is selected from Ninja's compdb", () => {
  const root = "/repo/frontend/src";
  const entries = [
    { directory: "/repo/build/frontend-debug", file: "/repo/frontend/src/Core/Domains/AI/ai_tools.cpp", command: "clang++ -c a.cpp -o a.o" },
    { directory: "/repo/build/frontend-debug", file: "/repo/frontend/src/App/Domains/Chat/chat_store.cpp", command: "clang++ -c b.cpp -o b.o" },
    { directory: "/repo/build/frontend-debug", file: "/tmp/generated/ai_tools.cpp", command: "clang++ -c c.cpp -o c.o" },
  ];
  assert.equal(selectCompileJobs(entries, root, ["ai_tools.cpp"])[0], entries[0]);
  assert.equal(selectCompileJobs(entries, root, ["Core/Domains/AI/ai_tools.cpp"])[0], entries[0]);
  assert.equal(selectCompileJobs(entries, root, ["frontend/src/App/Domains/Chat/chat_store.cpp"])[0], entries[1]);
  assert.throws(() => selectCompileJobs(entries, root, ["missing.cpp"]), /found 0/);
});

test("profile compiler output and depfile never target the actual Ninja outputs", () => {
  const job = {
    directory: "/repo/build/frontend-debug",
    command: '/usr/bin/clang++ -MD -MT a.o -MF "/repo/build/a.d" -c "/repo/frontend/src/main.cpp" -o "/repo/build/a.o"',
  };
  const command = isolatedCompile(job, "/tmp/profile/a.cpp.o", "/tmp/profile/a.d");
  assert.equal(command.executable, "/usr/bin/clang++");
  assert.deepEqual(command.args.slice(command.args.indexOf("-MF"), command.args.indexOf("-MF") + 2), ["-MF", "/tmp/profile/a.d"]);
  assert.deepEqual(command.args.slice(command.args.indexOf("-o"), command.args.indexOf("-o") + 2), ["-o", "/tmp/profile/a.cpp.o"]);
  assert.ok(command.args.includes("-ftime-trace"));
  assert.ok(!command.args.includes("/repo/build/a.o"));
  assert.ok(!command.args.includes("/repo/build/a.d"));
  assert.throws(() => isolatedCompile({ command: "clang++ -c x.cpp", directory: "/tmp" }, "/tmp/x.o", "/tmp/x.d"), /no -o/);
  assert.equal(readableHeader("/repo/frontend/src/Core/abc.h", "/repo"), "frontend/src/Core/abc.h");
});

test("Ninja compdb can compile one Clang trace to isolation without rewriting its native object", {
  skip: !(has("cmake") && has("ninja") && has("clang++")) && "requires CMake, Ninja and Clang",
}, async () => {
  const temp = await mkdtemp(path.join(os.tmpdir(), "archivist-cpp-profile-test-"));
  try {
    const src = path.join(temp, "src");
    const build = path.join(temp, "build");
    const scratch = path.join(temp, "scratch");
    await mkdir(src);
    await mkdir(scratch);
    await writeFile(path.join(src, "main.cpp"), "#include <string>\nint main() { std::string s = \"hello\"; return (int)s.size(); }\n");
    await writeFile(path.join(src, "CMakeLists.txt"), [
      "cmake_minimum_required(VERSION 3.24)",
      "project(ProfileTest LANGUAGES CXX)",
      "set(CMAKE_CXX_STANDARD 20)",
      "add_executable(profile-target main.cpp)",
    ].join("\n"));
    execFileSync("cmake", ["-S", src, "-B", build, "-G", "Ninja", "-DCMAKE_CXX_COMPILER=clang++"], { stdio: "ignore" });
    execFileSync("cmake", ["--build", build], { stdio: "ignore" });
    const compdb = JSON.parse(execFileSync("ninja", ["-C", build, "-t", "compdb"], { encoding: "utf8" }));
    const job = selectCompileJobs(compdb, src, ["main.cpp"])[0];
    const originalObject = path.join(build, job.output);
    const original = await stat(originalObject);
    const object = path.join(scratch, "main.cpp.o");
    const command = isolatedCompile(job, object, path.join(scratch, "main.d"));
    const run = spawnSync(command.executable, command.args, { cwd: command.cwd, encoding: "utf8" });
    assert.equal(run.status, 0, run.stderr);
    const trace = JSON.parse(await readFile(traceFileFor(scratch, "main.cpp.o"), "utf8"));
    assert.ok(summarizeTrace(trace).compileUs > 0);
    assert.equal((await stat(originalObject)).mtimeMs, original.mtimeMs);
  } finally {
    await rm(temp, { recursive: true, force: true });
  }
});

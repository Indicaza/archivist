import assert from "node:assert/strict";
import { execFileSync, spawnSync } from "node:child_process";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import test from "node:test";
import { tempDir } from "./helpers.mjs";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../../..");
const has = (command) => spawnSync(command, ["--version"], { stdio: "ignore" }).status === 0;

test("Archivist Debug PCH is private, C++ only and uses stable external headers", async () => {
  const cmake = await readFile(path.join(root, "frontend", "CMakeLists.txt"), "utf8");
  assert.match(cmake, /option\(ARCHIVIST_DEBUG_PCH[^\n]* ON\)/);
  assert.match(cmake, /if\(ARCHIVIST_DEBUG_PCH AND CMAKE_BUILD_TYPE STREQUAL "Debug"\)/);
  assert.match(cmake, /target_precompile_headers\(archivist-core PRIVATE\s+"\$<\$<COMPILE_LANGUAGE:CXX>:/);
  assert.match(cmake, /target_precompile_headers\(archivist-qt PRIVATE\s+"\$<\$<COMPILE_LANGUAGE:CXX>:/);
  for (const file of ["src/Core/Support/stable_pch.h", "src/App/Services/stable_pch.h"]) {
    const contents = await readFile(path.join(root, "frontend", file), "utf8");
    const includes = [...contents.matchAll(/^#include\s+(.+)$/gm)].map((match) => match[1]);
    assert.ok(includes.length >= 10);
    assert.ok(includes.every((include) => /^<[^>]+>$/.test(include)), "PCH must not depend on volatile Archivist headers");
  }
});

test("CMake C++-only PCH builds Debug and stays disabled in Release", {
  skip: !(has("cmake") && has("ninja")) && "requires CMake and Ninja",
}, async () => {
  const temp = await tempDir();
  const src = path.join(temp, "src");
  await mkdir(src);
  const pch = path.join(src, "stable.h");
  await writeFile(pch, "#pragma once\n#include <string>\n");
  await writeFile(path.join(src, "main.cpp"), "int main() { return 0; }\n");
  await writeFile(path.join(src, "CMakeLists.txt"), [
    "cmake_minimum_required(VERSION 3.24)",
    "project(PchProbe LANGUAGES CXX)",
    "set(CMAKE_CXX_STANDARD 20)",
    "option(ARCHIVIST_DEBUG_PCH \"Compile PCH\" ON)",
    "add_executable(pch-probe main.cpp)",
    "if(ARCHIVIST_DEBUG_PCH AND CMAKE_BUILD_TYPE STREQUAL \"Debug\")",
    '  target_precompile_headers(pch-probe PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/stable.h>")',
    "endif()",
    "",
  ].join("\n"));
  for (const mode of ["Debug", "Release"]) {
    const build = path.join(temp, mode);
    execFileSync("cmake", ["-S", src, "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=" + mode, "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"], { stdio: "ignore" });
    execFileSync("cmake", ["--build", build], { stdio: "ignore" });
    const commands = await readFile(path.join(build, "compile_commands.json"), "utf8");
    assert.equal(commands.includes("cmake_pch"), mode === "Debug", mode + " PCH mode");
  }
});

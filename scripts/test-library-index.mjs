#!/usr/bin/env node

import path from "node:path";
import { fileURLToPath } from "node:url";
import Database from "better-sqlite3";

const rootPath = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const databasePath = path.join(rootPath, "backend", "data", "archivist.db");
const apiBaseUrl = process.env.ARCHIVIST_API_BASE_URL ?? "http://127.0.0.1:3333/api";
const query = process.argv.slice(2).join(" ").trim();

function printBoundary(label) {
  console.log(`\n===== ${label} =====`);
}

function compactText(value, maximumLength = 180) {
  const compact = String(value ?? "")
    .replaceAll(/\s+/g, " ")
    .trim();

  if (compact.length <= maximumLength) {
    return compact;
  }

  return `${compact.slice(0, maximumLength - 1)}…`;
}

async function requestJson(url, options = {}) {
  const response = await fetch(url, options);
  const text = await response.text();
  let payload;

  try {
    payload = text ? JSON.parse(text) : null;
  } catch {
    throw new Error(
      `${response.status} ${response.statusText}: ${compactText(text, 400) || "Invalid JSON response."}`,
    );
  }

  if (!response.ok) {
    const message = payload?.error?.message ?? payload?.message ?? response.statusText;
    throw new Error(`${response.status} ${message}`);
  }

  return payload;
}

function loadActiveLibrary() {
  let database;

  try {
    database = new Database(databasePath, {
      readonly: true,
      fileMustExist: true,
    });

    return database
      .prepare(
        `
          SELECT
            libraries.id,
            libraries.name
          FROM libraries
          LEFT JOIN app_settings
            ON app_settings.id = 1
          WHERE libraries.archived_at IS NULL
          ORDER BY
            CASE
              WHEN libraries.id = app_settings.selected_library_id THEN 0
              ELSE 1
            END,
            libraries.updated_at DESC,
            libraries.created_at DESC
          LIMIT 1
        `,
      )
      .get();
  } finally {
    database?.close();
  }
}

function printIndexSummary(label, index) {
  console.log(`\n${label}`);
  console.log(`  processed:   ${index.processedFileCount}`);
  console.log(`  unchanged:   ${index.unchangedFileCount}`);
  console.log(`  indexed:     ${index.indexedFileCount}`);
  console.log(`  empty:       ${index.emptyFileCount}`);
  console.log(`  unavailable: ${index.unavailableFileCount}`);
  console.log(`  failed:      ${index.failedFileCount}`);
  console.log(`  chunks:      ${index.chunkCount}`);
  console.log(`  duration:    ${index.durationMs} ms`);

  if (index.issues.length > 0) {
    console.log("  issues:");

    for (const issue of index.issues.slice(0, 10)) {
      console.log(`    - ${issue.relativePath}: ${compactText(issue.message)}`);
    }

    if (index.issues.length > 10) {
      console.log(`    - …and ${index.issues.length - 10} more`);
    }
  }
}

function printSearchResults(result) {
  console.log(`\nSearch results: ${result.candidates.length}`);
  console.log(`Search duration: ${result.durationMs} ms`);

  for (const warning of result.warnings) {
    console.log(`Warning: ${warning}`);
  }

  for (const [index, candidate] of result.candidates.slice(0, 5).entries()) {
    const metadata = candidate.metadata ?? {};
    const relativePath = metadata.relativePath ?? "unknown path";
    const startLine = metadata.startLine ?? "?";
    const endLine = metadata.endLine ?? "?";
    const excerpt = metadata.excerpt ?? candidate.content;

    console.log(`\n${index + 1}. ${relativePath}:${startLine}-${endLine}`);
    console.log(
      `   score=${candidate.score} · tokens=${candidate.estimatedTokens} · chunk=${candidate.id}`,
    );
    console.log(`   ${compactText(excerpt)}`);
  }
}

async function main() {
  printBoundary("ARCHIVIST LIBRARY INDEX SMOKE TEST — COPY FROM HERE");

  if (!query) {
    throw new Error(
      'Missing search text. Run: npm run test:library-index -- "a term you know exists"',
    );
  }

  const library = loadActiveLibrary();

  if (!library) {
    throw new Error("No active Library exists. Create or restore a Library first.");
  }

  console.log(`Library: ${library.name}`);
  console.log(`Library ID: ${library.id}`);
  console.log(`Query: ${query}`);
  console.log(`API: ${apiBaseUrl}`);

  await requestJson(`${apiBaseUrl}/health`);
  console.log("Backend health: PASS");

  console.log("\n[1/3] Running the first scan and index pass…");
  const firstScan = await requestJson(
    `${apiBaseUrl}/libraries/${encodeURIComponent(library.id)}/scan`,
    { method: "POST" },
  );
  printIndexSummary("First pass", firstScan.index);

  console.log("\n[2/3] Running the same scan again to verify incremental reuse…");
  const secondScan = await requestJson(
    `${apiBaseUrl}/libraries/${encodeURIComponent(library.id)}/scan`,
    { method: "POST" },
  );
  printIndexSummary("Second pass", secondScan.index);

  console.log(`\n[3/3] Searching the index for “${query}”…`);
  const searchUrl = new URL(`${apiBaseUrl}/cognition/search/library-files`);
  searchUrl.searchParams.set("q", query);
  searchUrl.searchParams.set("libraryId", library.id);
  searchUrl.searchParams.set("limit", "10");

  const search = await requestJson(searchUrl);
  printSearchResults(search.result);

  const checks = [
    {
      name: "Index contains chunks",
      passed: firstScan.index.chunkCount > 0,
      detail: `${firstScan.index.chunkCount} chunks`,
    },
    {
      name: "Second pass reused unchanged documents",
      passed:
        secondScan.index.unchangedFileCount > 0 ||
        secondScan.index.processedFileCount === 0,
      detail: `${secondScan.index.unchangedFileCount}/${secondScan.index.processedFileCount} unchanged`,
    },
    {
      name: "Known term returned search results",
      passed: search.result.candidates.length > 0,
      detail: `${search.result.candidates.length} results`,
    },
  ];

  console.log("\nChecks");

  for (const check of checks) {
    console.log(`  ${check.passed ? "PASS" : "FAIL"} — ${check.name} (${check.detail})`);
  }

  if (firstScan.index.failedFileCount > 0 || secondScan.index.failedFileCount > 0) {
    console.log(
      "  REVIEW — Some files failed extraction. Their paths and errors are printed above.",
    );
  }

  const passed = checks.every((check) => check.passed);
  console.log(`\nOverall: ${passed ? "PASS" : "REVIEW NEEDED"}`);
  printBoundary("ARCHIVIST LIBRARY INDEX SMOKE TEST — COPY THROUGH HERE");

  if (!passed) {
    process.exitCode = 1;
  }
}

main().catch((error) => {
  console.error(`\nFAIL — ${error instanceof Error ? error.message : String(error)}`);
  console.error("Keep the complete output and paste it into the coding chat.");
  printBoundary("ARCHIVIST LIBRARY INDEX SMOKE TEST — COPY THROUGH HERE");
  process.exitCode = 1;
});

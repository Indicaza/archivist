import { mkdtemp } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { Writable } from "node:stream";

export function tempDir() {
  return mkdtemp(path.join(os.tmpdir(), "observatory-test-"));
}

// A log stream that keeps everything in memory.
export function memoryLog() {
  const chunks = [];
  const stream = new Writable({
    write(chunk, _encoding, done) {
      chunks.push(String(chunk));
      done();
    },
  });
  stream.text = () => chunks.join("");
  return stream;
}

// A phase that runs a short inline Node script.
export function nodePhase(name, source, extra = {}) {
  return { name, command: process.execPath, args: ["-e", source], ...extra };
}

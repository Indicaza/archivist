// TEST FIXTURE ONLY: one process competing for the UI-test lock.
// node lock-contender.mjs <root> <startAtMs> <holdMs>
// Waits until startAtMs (so contenders start together), tries once, prints
// "won" or "busy", holds a win for holdMs, then releases.
import { acquireLock, releaseLock } from "../../control/jobs.mjs";

const [root, startAt, hold] = process.argv.slice(2);
while (Date.now() < Number(startAt)) await new Promise((resolve) => setTimeout(resolve, 1));
const result = await acquireLock(root, { owner: "cli", pid: process.pid });
process.stdout.write(result.ok ? "won\n" : "busy\n");
if (result.ok) {
  await new Promise((resolve) => setTimeout(resolve, Number(hold)));
  await releaseLock(root, { owner: "cli", pid: process.pid });
}

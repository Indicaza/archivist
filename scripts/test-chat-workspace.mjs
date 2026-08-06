import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import vm from "node:vm";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const policyPath = path.join(
  root,
  "frontend/qml/App/Workbench/Workspace/ChatViewportPolicy.js",
);
const workspacePath = path.join(
  root,
  "frontend/qml/App/Workbench/Workspace/Workspace.qml",
);
const messagePath = path.join(
  root,
  "frontend/qml/App/Workbench/Workspace/ChatMessage/ChatMessage.qml",
);
const activityRowPath = path.join(
  root,
  "frontend/qml/App/Workbench/Workspace/ChatMessage/RunActivityRow.qml",
);
const cmakePath = path.join(root, "frontend/CMakeLists.txt");

function read(filePath) {
  return fs.readFileSync(filePath, "utf8");
}

function loadPolicy() {
  const context = vm.createContext({
    Boolean,
    Math,
    Number,
  });
  const source = read(policyPath).replace(
    /^\s*\.pragma library\s*$/m,
    "",
  );
  vm.runInContext(source, context, {
    filename: policyPath,
  });
  return context;
}

function approximately(actual, expected, tolerance = 0.001) {
  assert.ok(
    Math.abs(actual - expected) <= tolerance,
    `Expected ${actual} to be within ${tolerance} of ${expected}.`,
  );
}

const policy = loadPolicy();

{
  const zone = policy.contentZone(1200, 0, 24, 760);
  approximately(zone.x, 220);
  approximately(zone.width, 760);
}

{
  const zone = policy.contentZone(1200, 48, 24, 760);
  approximately(zone.x, 220);
  approximately(zone.width, 760);
  approximately(zone.x + zone.width / 2, 600);
}

{
  const zone = policy.contentZone(1200, 280, 24, 760);
  approximately(zone.x, 304);
  approximately(zone.width, 760);
  approximately(zone.x + zone.width / 2, 684);
}

{
  const zone = policy.contentZone(1200, 700, 24, 760);
  approximately(zone.x, 724);
  approximately(zone.width, 452);
  approximately(zone.x + zone.width / 2, 950);
}

{
  const zone = policy.contentZone(320, 400, 24, 760);
  approximately(zone.x, 320);
  approximately(zone.width, 0);
}

approximately(policy.clampContentY(0, 2000, 800, 42, 44, -500), -42);
approximately(policy.clampContentY(0, 2000, 800, 42, 44, 5000), 1244);
approximately(policy.clampContentY(0, 2000, 800, 42, 44, 600), 600);

assert.equal(
  policy.isNearEnd(0, 2000, 800, 42, 44, 1244, 96),
  true,
  "A transcript inside the follow threshold must count as near the end.",
);
assert.equal(
  policy.isNearEnd(0, 2000, 800, 42, 44, 1000, 96),
  false,
  "A manually scrolled transcript must not count as near the end.",
);

assert.equal(policy.scrollDuration(0, true), 0);
assert.equal(policy.scrollDuration(1, false), 0);
assert.ok(
  policy.scrollDuration(120, false) >= 90
    && policy.scrollDuration(120, false) <= 180,
  "Streaming follow should use a short bounded glide.",
);
assert.ok(
  policy.scrollDuration(5000, true) >= 220
    && policy.scrollDuration(5000, true) <= 520,
  "Explicit bottom jumps should stay smooth without becoming sluggish.",
);
assert.ok(
  policy.scrollDuration(5000, true) > policy.scrollDuration(120, false),
  "Long explicit jumps should ease longer than small streaming adjustments.",
);

assert.equal(
  policy.shouldFollow({
    autoFollow: true,
    dragging: false,
    flicking: false,
    restoringViewport: false,
    restoringHistory: false,
  }),
  true,
  "Streaming growth should follow while automatic follow is active.",
);
assert.equal(
  policy.shouldFollow({
    autoFollow: true,
    dragging: true,
    flicking: false,
    restoringViewport: false,
    restoringHistory: false,
  }),
  false,
  "Dragging must immediately take ownership away from automatic follow.",
);
assert.equal(
  policy.shouldFollow({
    autoFollow: true,
    dragging: false,
    flicking: false,
    restoringViewport: true,
    restoringHistory: false,
  }),
  false,
  "Viewport restoration and live follow must never write contentY together.",
);
assert.equal(
  policy.shouldFollow({
    autoFollow: true,
    dragging: false,
    flicking: false,
    restoringViewport: false,
    restoringHistory: true,
  }),
  false,
  "History prepend restoration and live follow must never race.",
);

assert.equal(
  policy.shouldPrefetchHistory({
    visible: true,
    hasOlderMessages: true,
    nearBeginning: true,
    loadingMessages: false,
    loadingOlderMessages: false,
    historyLoadPending: false,
    restoringViewport: false,
    responding: true,
    autoFollow: true,
    scrollToEndPending: false,
  }),
  false,
  "Streaming at the bottom must never trigger an older-history prepend.",
);
assert.equal(
  policy.shouldPrefetchHistory({
    visible: true,
    hasOlderMessages: true,
    nearBeginning: true,
    loadingMessages: false,
    loadingOlderMessages: false,
    historyLoadPending: false,
    restoringViewport: false,
    responding: false,
    autoFollow: false,
    scrollToEndPending: false,
  }),
  true,
  "A manually scrolled transcript near the beginning should prefetch older history.",
);
assert.equal(
  policy.shouldPrefetchHistory({
    visible: true,
    hasOlderMessages: true,
    nearBeginning: true,
    loadingMessages: false,
    loadingOlderMessages: false,
    historyLoadPending: false,
    restoringViewport: true,
    responding: false,
    autoFollow: false,
    scrollToEndPending: false,
  }),
  false,
  "Chat viewport restoration must finish before history prefetch can begin.",
);
assert.equal(
  policy.shouldPrefetchHistory({
    visible: true,
    hasOlderMessages: true,
    nearBeginning: true,
    loadingMessages: false,
    loadingOlderMessages: false,
    historyLoadPending: false,
    restoringViewport: false,
    responding: false,
    autoFollow: false,
    scrollToEndPending: true,
  }),
  false,
  "A pending jump to the end must not race an older-history prepend.",
);

assert.equal(
  policy.shouldPrefetchHistory({
    visible: true,
    hasOlderMessages: true,
    nearBeginning: true,
    loadingMessages: false,
    loadingOlderMessages: false,
    historyLoadPending: false,
    restoringViewport: false,
    responding: false,
    autoFollow: false,
    scrollToEndPending: false,
    interacting: true,
  }),
  false,
  "Dragging, wheel movement, or flicking must not start a history prepend.",
);

assert.equal(
  policy.shouldEnableFollow({
    autoFollow: false,
    nearEnd: false,
    restoringViewport: false,
    restoringHistory: false,
  }),
  false,
  "A completed message must not drag a manually scrolled transcript to the bottom.",
);
assert.equal(
  policy.shouldEnableFollow({
    autoFollow: false,
    nearEnd: true,
    restoringViewport: false,
    restoringHistory: false,
  }),
  true,
  "A user already near the end should continue following new output.",
);
assert.equal(
  policy.shouldEnableFollow({
    autoFollow: true,
    nearEnd: false,
    restoringViewport: true,
    restoringHistory: false,
  }),
  false,
  "Chat switching must finish viewport restoration before follow can resume.",
);

const workspace = read(workspacePath);
const message = read(messagePath);
const activityRow = read(activityRowPath);
const cmake = read(cmakePath);

assert.match(workspace, /import "ChatViewportPolicy\.js" as ChatViewportPolicy/);
assert.match(workspace, /reuseItems:\s*false/);
assert.match(workspace, /ChatViewportPolicy\.shouldEnableFollow/);
assert.match(workspace, /ChatViewportPolicy\.shouldPrefetchHistory/);
assert.match(
  workspace,
  /shouldFollowEnd[\s\S]*scheduleScrollToEnd\(true, false\)/,
  "Restoring a viewport saved at the end must explicitly restore follow ownership.",
);
assert.match(workspace, /readonly property real distanceFromEnd:/);
assert.match(
  workspace,
  /historyPrefetchDistance:\s*Math\.max\(\s*48,\s*transcript\.height \* 0\.06/,
  "History prefetch must stay almost at the real beginning of the transcript.",
);
assert.match(
  read(policyPath),
  /^\.pragma library/m,
  "The shared QML policy must be loaded once without the CMake re-evaluation warning.",
);
assert.equal(
  (workspace.match(/SmoothedAnimation\s*\{\s*id:\s*scrollToEndAnimation/g) ?? []).length,
  1,
  "Jump and live follow must share one smooth contentY animation.",
);
assert.doesNotMatch(
  workspace,
  /positionViewAtEnd\(/,
  "Repeated instant ListView end jumps cause visible jitter and must not return.",
);
assert.doesNotMatch(
  workspace,
  /id:\s*scrollToEndTimer/,
  "The old 16 ms repeated end-position timer must not compete with smooth scrolling.",
);
assert.match(
  workspace,
  /function jumpToLatest\(\)[\s\S]*clearHistoryAnchor\(ChatStore\.loadingOlderMessages\)[\s\S]*scheduleScrollToEnd\(true, true\)/,
  "Jump to latest must override history restoration and request a smooth explicit glide.",
);
assert.match(
  workspace,
  /function startScrollToEndAnimation\(\)[\s\S]*updateScrollToEndTarget\(\)[\s\S]*ChatViewportPolicy\.scrollDuration[\s\S]*scrollToEndAnimation\.start\(\)/,
  "The smooth controller must compute one current end target and one bounded duration.",
);
assert.match(
  workspace,
  /function finishScrollToEnd\(\)[\s\S]*remaining > 0\.75[\s\S]*scrollToEndSettlePass < 3[\s\S]*transcript\.contentY = root\.scrollToEndTargetY/,
  "The animation must settle only tiny late layout changes and finish at the exact end.",
);
assert.match(
  workspace,
  /function takeManualScrollOwnership\(\)[\s\S]*cancelScrollToEnd\(\)[\s\S]*stopRevealFollow\(\)/,
  "Any real user movement must immediately cancel the automatic glide.",
);
assert.doesNotMatch(
  workspace.slice(
    workspace.indexOf("function takeManualScrollOwnership"),
    workspace.indexOf("function updateScrollToEndTarget"),
  ),
  /programmaticScrollWrite/,
  "A stale programmatic-write grace window must never block real manual scrolling.",
);
assert.doesNotMatch(
  workspace,
  /id:\s*liveRunFollowTimer/,
  "Live follow and jump-to-end must share the same smooth controller.",
);
assert.doesNotMatch(
  workspace,
  /runPhase === "run\.started"/,
  "Starting a Run must not forcibly re-enable follow after manual scrolling.",
);

const startAnimationStart = workspace.indexOf(
  "function startScrollToEndAnimation",
);
const scheduleStart = workspace.indexOf(
  "function scheduleScrollToEnd",
  startAnimationStart,
);
const cancelStart = workspace.indexOf("function cancelScrollToEnd", scheduleStart);
const finishStart = workspace.indexOf("function finishScrollToEnd", cancelStart);
const jumpStart = workspace.indexOf("function jumpToLatest", finishStart);
const captureStart = workspace.indexOf("function captureHistoryAnchor");
const restoreHistoryStart = workspace.indexOf(
  "function restoreHistoryAnchor",
  captureStart,
);
const clearHistoryStart = workspace.indexOf(
  "function clearHistoryAnchor",
  restoreHistoryStart,
);
assert.ok(
  startAnimationStart >= 0
    && scheduleStart > startAnimationStart
    && cancelStart > scheduleStart
    && finishStart > cancelStart
    && jumpStart > finishStart,
);
assert.match(
  workspace.slice(scheduleStart, cancelStart),
  /explicitJump === true[\s\S]*jumpToEndPending = true[\s\S]*scrollToEndPending = true[\s\S]*startScrollToEndAnimation\(\)/,
  "Explicit bottom jumps must enter the shared smooth controller independently of normal auto-follow.",
);
assert.ok(
  captureStart >= 0
    && restoreHistoryStart > captureStart
    && clearHistoryStart > restoreHistoryStart,
);
assert.doesNotMatch(
  workspace.slice(captureStart, restoreHistoryStart),
  /cancelFlick\(\)/,
  "History anchoring must never stop an active user flick.",
);
assert.match(
  workspace.slice(restoreHistoryStart, clearHistoryStart),
  /if \(anchorPlaced\)[\s\S]*clearHistoryAnchor\(\)[\s\S]*return/,
  "A successful history-anchor restoration must finish after one placement instead of bouncing through repeated passes.",
);
assert.match(
  workspace,
  /id:\s*historyPrefetchTimer[\s\S]*interval:\s*280/,
  "History prefetch must wait until manual scrolling has clearly settled.",
);

assert.match(message, /ChatViewportPolicy\.contentZone/);
const frameStart = message.indexOf("id: frame");
const columnStart = message.indexOf("id: messageColumn", frameStart);
assert.ok(frameStart >= 0 && columnStart > frameStart);
assert.doesNotMatch(
  message.slice(frameStart, columnStart),
  /Behavior on x/,
  "Message horizontal placement must not animate independently of viewport geometry.",
);
const activityStart = message.indexOf("id: activityCard");
const surfaceStart = message.indexOf("id: surfaceFrame", activityStart);
assert.ok(activityStart >= 0 && surfaceStart > activityStart);
assert.doesNotMatch(
  message.slice(activityStart, surfaceStart),
  /Behavior on height/,
  "The waiting card must not animate the ListView delegate height.",
);

assert.doesNotMatch(
  activityRow,
  /Qt\.callLater\([\s\S]*root\.reveal/,
  "Destroyed or recycled activity delegates must not retain delayed reveal callbacks.",
);
assert.match(
  cmake,
  /qml\/App\/Workbench\/Workspace\/ChatViewportPolicy\.js/,
  "The QML policy module must be packaged into the native application.",
);

console.log("Chat workspace stability test: PASS");
console.log("  full-window centering with obstruction collision avoidance");
console.log("  manual-scroll ownership with immediate animation cancellation");
console.log("  near-top idle history prefetch and one-pass anchor restoration");
console.log("  smooth bounded jump-to-latest with exact final settlement");
console.log("  one shared transcript contentY animation");
console.log("  non-reused dynamic delegates and stable activity-card height");
